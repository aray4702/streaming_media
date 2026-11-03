// AppleAudioDevice's state machine (#1105) with a fake voice-processing unit
// and a fake audio session: one unit for both directions, the echo canceller
// switch, interruptions, route changes and a media services reset, as
// libwebrtc's audio_device_ios handles them.
#include <memory>
#include <vector>

#include "sm/device/apple_audio_device.h"
#include "sm_test.h"

using namespace sm::device;
using State = AudioUnitIO::State;

namespace {

struct UnitLog {
  int created = 0;
  int alive = 0;
  int starts = 0;
  int stops = 0;
  int initializes = 0;
  int last_rate = 0;
  bool last_bypass = false;
  bool fail_start = false;
};
UnitLog g_log;

class FakeUnit;
FakeUnit* g_unit = nullptr;  // the newest unit

class FakeUnit : public AudioUnitIO {
 public:
  FakeUnit() {
    ++g_log.created;
    ++g_log.alive;
    g_unit = this;
  }
  ~FakeUnit() override {
    --g_log.alive;
    if (g_unit == this) g_unit = nullptr;
  }
  bool Init(Observer* o) override {
    if (state_ != State::kInitRequired) return false;
    observer_ = o;
    state_ = State::kUninitialized;
    return true;
  }
  bool Initialize(int rate, bool bypass) override {
    if (state_ != State::kUninitialized) return false;
    ++g_log.initializes;
    g_log.last_rate = rate;
    g_log.last_bypass = bypass;
    state_ = State::kInitialized;
    return true;
  }
  bool Start() override {
    if (state_ != State::kInitialized || g_log.fail_start) return false;
    ++g_log.starts;
    state_ = State::kStarted;
    return true;
  }
  bool Stop() override {
    if (state_ != State::kStarted) return false;
    ++g_log.stops;
    state_ = State::kInitialized;
    return true;
  }
  bool Uninitialize() override {
    if (state_ != State::kInitialized) return false;
    state_ = State::kUninitialized;
    return true;
  }
  State state() const override { return state_; }

  // One I/O cycle of `frames`, as the unit's two callbacks run it.
  bool Cycle(size_t frames, int16_t mic_value = 1) {
    std::vector<int16_t> mic(frames, mic_value);
    observer_->OnRecordedData(mic.data(), frames);
    std::vector<int16_t> out(frames, -1);
    const bool audible = observer_->OnPlayoutData(out.data(), frames);
    last_out = out;
    return audible;
  }
  std::vector<int16_t> last_out;

 private:
  Observer* observer_ = nullptr;
  State state_ = State::kInitRequired;
};

std::unique_ptr<AudioUnitIO> MakeFakeUnit() { return std::make_unique<FakeUnit>(); }

struct SessionState {
  int rate = 48000;
  bool can_play = true;
  int activations = 0;
  int deactivations = 0;
};

class FakeSession : public AudioSessionControl {
 public:
  explicit FakeSession(SessionState* s) : s_(s) {}
  bool Activate(const AudioDeviceConfig&) override {
    ++s_->activations;
    return true;
  }
  void Deactivate() override { ++s_->deactivations; }
  int SampleRateHz() const override { return s_->rate; }
  bool CanPlayOrRecord() const override { return s_->can_play; }

 private:
  SessionState* s_;
};

class Transport : public AudioTransport {
 public:
  void RecordedDataIsAvailable(const int16_t*, size_t n, int, int rate, int delay) override {
    rec_frames.push_back(n);
    rec_rate = rate;
    rec_delay = delay;
  }
  size_t NeedMorePlayData(int16_t* out, size_t n, int, int rate) override {
    play_rate = rate;
    for (size_t i = 0; i < n; ++i) out[i] = 1000;
    ++play_requests;
    return n;
  }
  std::vector<size_t> rec_frames;
  int rec_rate = 0;
  int rec_delay = 0;
  int play_rate = 0;
  int play_requests = 0;
};

struct Fixture {
  explicit Fixture(AudioDeviceConfig config = {}) {
    g_log = UnitLog{};
    device = std::make_unique<AppleAudioDevice>(config, &MakeFakeUnit, std::make_unique<FakeSession>(&session));
    device->RegisterAudioCallback(&transport);
  }
  SessionState session;
  Transport transport;
  std::unique_ptr<AppleAudioDevice> device;
};

}  // namespace

SM_TEST(DeviceAppleAudio, OneUnitForBothDirections) {
  Fixture f;
  SM_EXPECT_FALSE(f.device->StartPlayout());  // not initialized
  SM_ASSERT_TRUE(f.device->InitPlayout());
  SM_ASSERT_TRUE(f.device->InitRecording());
  SM_EXPECT_EQ(g_log.created, 1);
  SM_EXPECT_EQ(f.session.activations, 1);
  SM_EXPECT_TRUE(f.device->StartPlayout());
  SM_EXPECT_TRUE(f.device->StartRecording());
  SM_EXPECT_EQ(g_log.starts, 1);
  SM_EXPECT_TRUE(f.device->Playing() && f.device->Recording());
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
  // Stopping one direction keeps the unit running for the other.
  SM_EXPECT_TRUE(f.device->StopPlayout());
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
  SM_EXPECT_EQ(g_log.alive, 1);
  SM_EXPECT_TRUE(f.device->StopRecording());
  SM_EXPECT_EQ(g_log.alive, 0);
  SM_EXPECT_EQ(f.session.deactivations, 1);
  SM_EXPECT_FALSE(f.device->Playing() || f.device->Recording());
}

SM_TEST(DeviceAppleAudio, TenMillisecondFramesWithDelayEstimate) {
  Fixture f;
  f.device->InitRecording();
  f.device->StartRecording();
  f.device->StartPlayout();
  for (int i = 0; i < 100; ++i) SM_ASSERT_TRUE(g_unit->Cycle(512));
  // 100 x 512 frames at 48 kHz = 106 full 10 ms frames.
  SM_EXPECT_EQ(f.transport.rec_frames.size(), size_t{106});
  for (size_t n : f.transport.rec_frames) SM_ASSERT_EQ(n, size_t{480});
  SM_EXPECT_EQ(f.transport.rec_rate, 48000);
  SM_EXPECT_EQ(f.transport.rec_delay,
               AppleAudioDevice::kFixedPlayoutDelayEstimateMs + AppleAudioDevice::kFixedRecordDelayEstimateMs);
  SM_EXPECT_EQ(g_unit->last_out[0], int16_t{1000});
}

SM_TEST(DeviceAppleAudio, SilenceWhenNotPlaying) {
  Fixture f;
  f.device->InitRecording();
  f.device->StartRecording();
  SM_EXPECT_FALSE(g_unit->Cycle(480));  // output flagged silent
  for (int16_t s : g_unit->last_out) SM_ASSERT_EQ(s, int16_t{0});
  SM_EXPECT_EQ(f.transport.play_requests, 0);
  SM_EXPECT_EQ(f.transport.rec_frames.size(), size_t{1});
}

SM_TEST(DeviceAppleAudio, EchoCancellerSwitch) {
  {
    Fixture f;  // default: the library's AEC3, the unit's processing bypassed
    f.device->InitPlayout();
    SM_EXPECT_TRUE(g_log.last_bypass);
    SM_EXPECT_FALSE(f.device->BuiltInAecEnabled());
  }
  {
    AudioDeviceConfig c;
    c.echo_canceller = EchoCanceller::kSystem;
    Fixture f(c);
    f.device->InitPlayout();
    SM_EXPECT_FALSE(g_log.last_bypass);
    SM_EXPECT_TRUE(f.device->BuiltInAecEnabled());
  }
}

SM_TEST(DeviceAppleAudio, RunsAtTheSessionRate) {
  Fixture f;
  f.session.rate = 44100;
  f.device->InitRecording();
  f.device->StartRecording();
  SM_EXPECT_EQ(g_log.last_rate, 44100);
  SM_EXPECT_EQ(f.device->SampleRateHz(), 44100);
  g_unit->Cycle(1000);
  SM_EXPECT_EQ(f.transport.rec_frames.size(), size_t{2});
  SM_EXPECT_EQ(f.transport.rec_frames[0], size_t{441});
}

SM_TEST(DeviceAppleAudio, InterruptionStopsAndRestarts) {
  Fixture f;
  f.device->InitPlayout();
  f.device->StartPlayout();
  f.device->StartRecording();
  g_unit->Cycle(200);  // leaves partial 10 ms chunks buffered
  f.device->OnInterruptionBegan();
  SM_EXPECT_TRUE(f.device->interrupted());
  SM_EXPECT_TRUE(f.device->unit_state() == State::kInitialized);
  SM_EXPECT_EQ(g_log.stops, 1);
  // Still "playing" for the app: the unit restarts when the interruption ends.
  SM_EXPECT_TRUE(f.device->Playing());
  f.device->OnInterruptionEnded();
  SM_EXPECT_FALSE(f.device->interrupted());
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
  SM_EXPECT_EQ(g_log.starts, 2);
  SM_EXPECT_EQ(g_log.created, 1);
  // The 200 frames from before were dropped: 280 more don't complete a frame.
  const size_t before = f.transport.rec_frames.size();
  g_unit->Cycle(280);
  SM_EXPECT_EQ(f.transport.rec_frames.size(), before);
}

SM_TEST(DeviceAppleAudio, InterruptionEndWhileSessionUnavailableStaysStopped) {
  Fixture f;
  f.device->InitPlayout();
  f.device->StartPlayout();
  f.device->OnInterruptionBegan();
  f.session.can_play = false;
  f.device->OnInterruptionEnded();
  SM_EXPECT_TRUE(f.device->unit_state() == State::kInitialized);
  f.session.can_play = true;
  f.device->OnInterruptionEnded();
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
}

SM_TEST(DeviceAppleAudio, StartDuringInterruptionWaitsForTheEnd) {
  Fixture f;
  f.device->InitPlayout();
  f.device->OnInterruptionBegan();
  SM_EXPECT_TRUE(f.device->StartPlayout());
  SM_EXPECT_TRUE(f.device->unit_state() == State::kInitialized);
  f.device->OnInterruptionEnded();
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
}

SM_TEST(DeviceAppleAudio, RouteChangeToANewRateReinitializes) {
  Fixture f;
  f.device->InitPlayout();
  f.device->StartPlayout();
  f.device->StartRecording();
  f.device->OnRouteChanged();  // same rate: nothing to do
  SM_EXPECT_EQ(f.device->restarts(), 0);
  SM_EXPECT_EQ(g_log.initializes, 1);
  f.session.rate = 16000;  // e.g. a Bluetooth HFP headset
  f.device->OnRouteChanged();
  SM_EXPECT_EQ(f.device->restarts(), 1);
  SM_EXPECT_EQ(g_log.initializes, 2);
  SM_EXPECT_EQ(g_log.last_rate, 16000);
  SM_EXPECT_EQ(g_log.created, 1);  // the same unit, a new format
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
  SM_EXPECT_EQ(f.device->SampleRateHz(), 16000);
  g_unit->Cycle(160);
  SM_ASSERT_EQ(f.transport.rec_frames.size(), size_t{1});
  SM_EXPECT_EQ(f.transport.rec_frames[0], size_t{160});
  SM_EXPECT_EQ(f.transport.rec_rate, 16000);
  SM_EXPECT_EQ(f.transport.play_rate, 16000);
}

SM_TEST(DeviceAppleAudio, MediaServicesResetRecreatesTheUnit) {
  Fixture f;
  f.device->InitRecording();
  f.device->StartRecording();
  FakeUnit* old_unit = g_unit;
  f.device->OnMediaServicesReset();
  SM_EXPECT_EQ(g_log.created, 2);
  SM_EXPECT_EQ(g_log.alive, 1);
  SM_EXPECT_TRUE(g_unit != old_unit);
  SM_EXPECT_TRUE(f.device->unit_state() == State::kStarted);
  SM_EXPECT_EQ(f.session.activations, 2);
  g_unit->Cycle(480);
  SM_EXPECT_EQ(f.transport.rec_frames.size(), size_t{1});
}

SM_TEST(DeviceAppleAudio, FailedStartLeavesTheDeviceStopped) {
  Fixture f;
  f.device->InitPlayout();
  g_log.fail_start = true;
  SM_EXPECT_FALSE(f.device->StartPlayout());
  SM_EXPECT_FALSE(f.device->Playing());
  g_log.fail_start = false;
  SM_EXPECT_TRUE(f.device->StartPlayout());
  SM_EXPECT_TRUE(f.device->Playing());
}

SM_TEST(DeviceAppleAudio, DestructionShutsDown) {
  SessionState* s;
  {
    Fixture f;
    s = &f.session;
    f.device->InitPlayout();
    f.device->StartPlayout();
    f.device.reset();
    SM_EXPECT_EQ(g_log.alive, 0);
    SM_EXPECT_EQ(s->deactivations, 1);
  }
}
