#include "voice_processing_audio_unit.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "sm/core/logging.h"

namespace sm::device {

namespace {
constexpr AudioUnitElement kOutputBus = 0;  // playout
constexpr AudioUnitElement kInputBus = 1;   // capture
constexpr UInt32 kBytesPerSample = 2;

AudioStreamBasicDescription Format(int sample_rate_hz) {
  // 16-bit signed, packed, mono (voice_processing_audio_unit.mm, GetFormat).
  AudioStreamBasicDescription f = {};
  f.mSampleRate = sample_rate_hz;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  f.mBytesPerPacket = kBytesPerSample;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = kBytesPerSample;
  f.mChannelsPerFrame = 1;
  f.mBitsPerChannel = 8 * kBytesPerSample;
  return f;
}

bool Check(OSStatus s, const char* what) {
  if (s == noErr) return true;
  SM_LOG(kWarning, "VoiceProcessingIO: %s failed (OSStatus %d)", what, static_cast<int>(s));
  return false;
}
}  // namespace

std::unique_ptr<AudioUnitIO> VoiceProcessingAudioUnit::Create() {
  return std::make_unique<VoiceProcessingAudioUnit>();
}

VoiceProcessingAudioUnit::~VoiceProcessingAudioUnit() {
  if (unit_ == nullptr) return;
  if (state_ == State::kStarted) Stop();
  if (state_ == State::kInitialized) Uninitialize();
  AudioComponentInstanceDispose(unit_);
}

bool VoiceProcessingAudioUnit::Init(Observer* observer) {
  if (state_ != State::kInitRequired) return false;
  observer_ = observer;
  AudioComponentDescription desc = {};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_VoiceProcessingIO;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (comp == nullptr) {
    SM_LOG(kWarning, "VoiceProcessingIO: no audio component");
    return false;
  }
  if (!Check(AudioComponentInstanceNew(comp, &unit_), "AudioComponentInstanceNew")) {
    unit_ = nullptr;
    return false;
  }
  UInt32 one = 1;
  UInt32 zero = 0;
  bool ok = Check(AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, kInputBus,
                                       &one, sizeof(one)),
                  "enable input") &&
            Check(AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, kOutputBus,
                                       &one, sizeof(one)),
                  "enable output") &&
            // The input callback renders into our own buffer.
            Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output,
                                       kInputBus, &zero, sizeof(zero)),
                  "disable input buffer allocation");
  AURenderCallbackStruct render = {&VoiceProcessingAudioUnit::OnRender, this};
  ok = ok && Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, kOutputBus,
                                        &render, sizeof(render)),
                   "render callback");
  AURenderCallbackStruct input = {&VoiceProcessingAudioUnit::OnInput, this};
  ok = ok && Check(AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global,
                                        kInputBus, &input, sizeof(input)),
                   "input callback");
  if (!ok) {
    AudioComponentInstanceDispose(unit_);
    unit_ = nullptr;
    return false;
  }
  state_ = State::kUninitialized;
  return true;
}

bool VoiceProcessingAudioUnit::Initialize(int sample_rate_hz, bool bypass_voice_processing) {
  if (state_ != State::kUninitialized) return false;
  const AudioStreamBasicDescription f = Format(sample_rate_hz);
  if (!Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, kInputBus, &f,
                                  sizeof(f)),
             "input format") ||
      !Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, kOutputBus, &f,
                                  sizeof(f)),
             "output format")) {
    return false;
  }
  // EchoCanceller::kLibrary: the raw microphone for the library's AEC3.
  UInt32 bypass = bypass_voice_processing ? 1 : 0;
  Check(AudioUnitSetProperty(unit_, kAUVoiceIOProperty_BypassVoiceProcessing, kAudioUnitScope_Global, kInputBus,
                             &bypass, sizeof(bypass)),
        "bypass voice processing");
  UInt32 max_frames = 0;
  UInt32 size = sizeof(max_frames);
  if (AudioUnitGetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames,
                           &size) != noErr ||
      max_frames == 0) {
    max_frames = 4096;
  }
  // Callbacks can be a frame over the maximum slice (513 of 512 seen on macOS): keep headroom.
  record_buffer_.assign(std::max<UInt32>(2 * max_frames, 4096), 0);
  // AudioUnitInitialize can fail while the session is still settling (audio_device_ios retries).
  OSStatus s = noErr;
  for (int attempt = 0; attempt < kMaxInitializeAttempts; ++attempt) {
    s = AudioUnitInitialize(unit_);
    if (s == noErr) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!Check(s, "AudioUnitInitialize")) return false;
  if (!bypass_voice_processing) {
    // EchoCanceller::kSystem: the unit's AGC on, as libwebrtc checks it.
    UInt32 agc = 0;
    size = sizeof(agc);
    if (AudioUnitGetProperty(unit_, kAUVoiceIOProperty_VoiceProcessingEnableAGC, kAudioUnitScope_Global, kInputBus,
                             &agc, &size) == noErr &&
        agc == 0) {
      agc = 1;
      Check(AudioUnitSetProperty(unit_, kAUVoiceIOProperty_VoiceProcessingEnableAGC, kAudioUnitScope_Global, kInputBus,
                                 &agc, sizeof(agc)),
            "enable AGC");
    }
  }
  state_ = State::kInitialized;
  return true;
}

bool VoiceProcessingAudioUnit::Start() {
  if (state_ != State::kInitialized) return false;
  if (!Check(AudioOutputUnitStart(unit_), "AudioOutputUnitStart")) return false;
  state_ = State::kStarted;
  return true;
}

bool VoiceProcessingAudioUnit::Stop() {
  if (state_ != State::kStarted) return false;
  const bool ok = Check(AudioOutputUnitStop(unit_), "AudioOutputUnitStop");
  state_ = State::kInitialized;
  return ok;
}

bool VoiceProcessingAudioUnit::Uninitialize() {
  if (state_ != State::kInitialized) return false;
  const bool ok = Check(AudioUnitUninitialize(unit_), "AudioUnitUninitialize");
  state_ = State::kUninitialized;
  return ok;
}

OSStatus VoiceProcessingAudioUnit::OnRender(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*,
                                            UInt32, UInt32 frames, AudioBufferList* io_data) {
  auto* self = static_cast<VoiceProcessingAudioUnit*>(ref);
  if (io_data == nullptr || io_data->mNumberBuffers != 1) return noErr;
  AudioBuffer& b = io_data->mBuffers[0];
  const UInt32 n = std::min<UInt32>(frames, b.mDataByteSize / kBytesPerSample);
  auto* out = static_cast<int16_t*>(b.mData);
  if (!self->observer_->OnPlayoutData(out, n)) *flags |= kAudioUnitRenderAction_OutputIsSilence;
  return noErr;
}

OSStatus VoiceProcessingAudioUnit::OnInput(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts,
                                           UInt32 bus, UInt32 frames, AudioBufferList*) {
  auto* self = static_cast<VoiceProcessingAudioUnit*>(ref);
  if (frames > self->record_buffer_.size()) return noErr;  // larger than the unit's maximum slice
  AudioBufferList list;
  list.mNumberBuffers = 1;
  list.mBuffers[0].mNumberChannels = 1;
  list.mBuffers[0].mDataByteSize = frames * kBytesPerSample;
  list.mBuffers[0].mData = self->record_buffer_.data();
  const OSStatus s = AudioUnitRender(self->unit_, flags, ts, bus, frames, &list);
  if (s != noErr) return s;
  self->observer_->OnRecordedData(self->record_buffer_.data(), frames);
  return noErr;
}

}  // namespace sm::device
