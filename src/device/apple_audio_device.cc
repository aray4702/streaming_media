#include "sm/device/apple_audio_device.h"

#include <cstring>

#include "sm/core/logging.h"

namespace sm::device {

using State = AudioUnitIO::State;

AppleAudioDevice::AppleAudioDevice(const AudioDeviceConfig& config, UnitFactory unit_factory,
                                   std::unique_ptr<AudioSessionControl> session)
    : config_([&] {
        AudioDeviceConfig c = config;
        c.channels = 1;  // the voice-processing unit is mono, as audio_device_ios
        return c;
      }()),
      unit_factory_(unit_factory),
      session_(std::move(session)) {}

AppleAudioDevice::~AppleAudioDevice() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    playing_ = false;
    recording_ = false;
    ShutdownPlayOrRecordLocked();
  }
  // Stop the session's notifications while the rest of the device is alive.
  session_.reset();
}

void AppleAudioDevice::RegisterAudioCallback(AudioTransport* transport) {
  std::lock_guard<std::mutex> lock(mu_);
  transport_ = transport;
  if (fine_buffer_) fine_buffer_->SetTransport(transport);
}

int AppleAudioDevice::SampleRateHz() const {
  std::lock_guard<std::mutex> lock(mu_);
  return sample_rate_hz_ > 0 ? sample_rate_hz_ : config_.sample_rate_hz;
}

bool AppleAudioDevice::interrupted() const {
  std::lock_guard<std::mutex> lock(mu_);
  return interrupted_;
}

AudioUnitIO::State AppleAudioDevice::unit_state() const {
  std::lock_guard<std::mutex> lock(mu_);
  return unit_ ? unit_->state() : State::kInitRequired;
}

int AppleAudioDevice::restarts() const {
  std::lock_guard<std::mutex> lock(mu_);
  return restarts_;
}

bool AppleAudioDevice::InitPlayout() {
  std::lock_guard<std::mutex> lock(mu_);
  return initialized_ || InitPlayOrRecordLocked();
}

bool AppleAudioDevice::InitRecording() {
  std::lock_guard<std::mutex> lock(mu_);
  return initialized_ || InitPlayOrRecordLocked();
}

bool AppleAudioDevice::InitPlayOrRecordLocked() {
  if (session_ && !session_->Activate(config_)) {
    SM_LOG(kWarning, "audio device: the audio session did not activate");
    return false;
  }
  const int session_rate = session_ ? session_->SampleRateHz() : 0;
  sample_rate_hz_ = session_rate > 0 ? session_rate : config_.sample_rate_hz;
  fine_buffer_ = std::make_unique<FineAudioBuffer>(sample_rate_hz_, 1);
  fine_buffer_->SetTransport(transport_);
  if (!CreateAndInitializeUnitLocked()) {
    unit_.reset();
    if (session_) session_->Deactivate();
    return false;
  }
  initialized_ = true;
  return true;
}

bool AppleAudioDevice::CreateAndInitializeUnitLocked() {
  unit_ = unit_factory_ ? unit_factory_() : nullptr;
  if (!unit_ || !unit_->Init(this)) {
    SM_LOG(kWarning, "audio device: could not create the voice-processing unit");
    return false;
  }
  const bool bypass = config_.echo_canceller == EchoCanceller::kLibrary;
  if (!unit_->Initialize(sample_rate_hz_, bypass)) {
    SM_LOG(kWarning, "audio device: could not initialize the voice-processing unit");
    return false;
  }
  return true;
}

void AppleAudioDevice::ShutdownPlayOrRecordLocked() {
  if (unit_) {
    if (unit_->state() == State::kStarted) unit_->Stop();
    if (unit_->state() == State::kInitialized) unit_->Uninitialize();
    unit_.reset();
  }
  if (fine_buffer_) {
    fine_buffer_->ResetPlayout();
    fine_buffer_->ResetRecord();
  }
  if (initialized_ && session_) session_->Deactivate();
  initialized_ = false;
}

bool AppleAudioDevice::StartPlayout() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_) return false;
  if (playing_) return true;
  fine_buffer_->ResetPlayout();
  if (!recording_ && !interrupted_ && unit_->state() == State::kInitialized && !unit_->Start()) return false;
  playing_ = true;
  return true;
}

bool AppleAudioDevice::StartRecording() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_) return false;
  if (recording_) return true;
  fine_buffer_->ResetRecord();
  if (!playing_ && !interrupted_ && unit_->state() == State::kInitialized && !unit_->Start()) return false;
  recording_ = true;
  return true;
}

bool AppleAudioDevice::StopPlayout() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_ || !playing_) return true;
  if (!recording_) ShutdownPlayOrRecordLocked();
  playing_ = false;
  return true;
}

bool AppleAudioDevice::StopRecording() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_ || !recording_) return true;
  if (!playing_) ShutdownPlayOrRecordLocked();
  recording_ = false;
  return true;
}

void AppleAudioDevice::OnInterruptionBegan() {
  std::lock_guard<std::mutex> lock(mu_);
  interrupted_ = true;
  if (unit_ && unit_->state() == State::kStarted) {
    unit_->Stop();
    // PrepareForNewStart: drop what was buffered before the interruption.
    fine_buffer_->ResetPlayout();
    fine_buffer_->ResetRecord();
  }
}

void AppleAudioDevice::OnInterruptionEnded() {
  std::lock_guard<std::mutex> lock(mu_);
  interrupted_ = false;
  if (!unit_ || !(playing_ || recording_)) return;
  if (session_ && !session_->CanPlayOrRecord()) return;
  if (unit_->state() == State::kInitialized) {
    if (!unit_->Start()) RestartUnitLocked(/*recreate=*/true);
  } else if (unit_->state() != State::kStarted) {
    RestartUnitLocked(/*recreate=*/false);
  }
}

void AppleAudioDevice::OnRouteChanged() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!unit_ || !session_) return;
  const int rate = session_->SampleRateHz();
  // HandleSampleRateChange: only a new hardware rate needs a new unit format.
  if (rate <= 0 || rate == sample_rate_hz_) return;
  SM_LOG(kInfo, "audio device: route change, sample rate %d -> %d Hz", sample_rate_hz_, rate);
  RestartUnitLocked(/*recreate=*/false);
}

void AppleAudioDevice::OnMediaServicesReset() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!unit_) return;
  if (session_) session_->Activate(config_);
  RestartUnitLocked(/*recreate=*/true);
}

bool AppleAudioDevice::RestartUnitLocked(bool recreate) {
  if (unit_->state() == State::kStarted) unit_->Stop();
  if (unit_->state() == State::kInitialized) unit_->Uninitialize();
  const int session_rate = session_ ? session_->SampleRateHz() : 0;
  if (session_rate > 0) sample_rate_hz_ = session_rate;
  // The unit is stopped, so the audio thread no longer touches the buffer.
  fine_buffer_ = std::make_unique<FineAudioBuffer>(sample_rate_hz_, 1);
  fine_buffer_->SetTransport(transport_);
  ++restarts_;
  bool ok;
  if (recreate) {
    ok = CreateAndInitializeUnitLocked();
  } else {
    ok = unit_->Initialize(sample_rate_hz_, config_.echo_canceller == EchoCanceller::kLibrary);
  }
  if (!ok) {
    SM_LOG(kWarning, "audio device: could not restart the voice-processing unit");
    return false;
  }
  const bool can_run = !interrupted_ && (!session_ || session_->CanPlayOrRecord());
  if ((playing_ || recording_) && can_run) return unit_->Start();
  return true;
}

void AppleAudioDevice::OnRecordedData(const int16_t* samples, size_t frames) {
  if (!recording_.load(std::memory_order_acquire)) return;
  fine_buffer_->DeliverRecordedData(samples, frames, kFixedRecordDelayEstimateMs);
}

bool AppleAudioDevice::OnPlayoutData(int16_t* out, size_t frames) {
  if (!playing_.load(std::memory_order_acquire)) {
    std::memset(out, 0, frames * sizeof(int16_t));
    return false;
  }
  fine_buffer_->GetPlayoutData(out, frames, kFixedPlayoutDelayEstimateMs);
  return true;
}

}  // namespace sm::device
