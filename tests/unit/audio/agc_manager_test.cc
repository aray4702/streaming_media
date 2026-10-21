// Unit tests: AGC1's adaptive analog controller (`apm v2`, #242), ported from
// libwebrtc M157's agc/agc_manager_direct_unittest.cc. The `MockAgc` there
// becomes `ScriptedAgc` (its RMS errors are scripted), and the
// `MockGainControl` expectations become the list of compression gains the
// manager hands out. Every test runs, as libwebrtc's parametrized suite, with
// the min mic level override unset, 12 and 20, with and without the RMS error
// override. The capture levels adjuster tests follow
// capture_levels_adjuster/capture_levels_adjuster_unittest.cc.
#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "sm/audio/agc_manager.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::agc1;

namespace {

constexpr int kInitialInputVolume = 128;
constexpr int kClippedMin = 165;  // Arbitrary, but different from the default.
constexpr float kAboveClippedThreshold = 0.2f;
constexpr int kMinMicLevel = 12;
constexpr float kHighSpeechProbability = 0.7f;
constexpr float kSpeechLevelDbfs = -25.0f;
constexpr size_t kFrames = 320;  // 10 ms at 32 kHz, as libwebrtc's tests.
constexpr size_t kBandFrames = 160;

// The `MockAgc`: GetRmsErrorDb() returns the scripted errors in order, then
// the repeated one if set, else false.
class ScriptedAgc : public Agc {
 public:
  void Process(std::span<const int16_t>) override {}
  bool GetRmsErrorDb(int* error) override {
    if (!errors.empty()) {
      *error = errors.front();
      errors.pop_front();
      return true;
    }
    if (repeated) {
      *error = *repeated;
      return true;
    }
    return false;
  }
  void Reset() override { ++resets; }
  float voice_probability() const override { return 0.f; }
  std::deque<int> errors;
  std::optional<int> repeated;
  int resets = 0;
};

struct Param {
  std::optional<int> min_mic_level;
  bool rms_error_override;
};

const Param kParams[] = {{std::nullopt, false}, {std::nullopt, true}, {12, false}, {12, true}, {20, false}, {20, true}};

AnalogGainControllerConfig TestConfig(const Param& p) {
  AnalogGainControllerConfig c;
  c.enabled = true;
  c.clipped_level_min = kClippedMin;
  c.enable_digital_adaptive = true;
  c.clipped_level_step = 15;
  c.clipped_ratio_threshold = 0.1f;
  c.clipped_wait_frames = 300;
  c.min_mic_level_override = p.min_mic_level;
  return c;
}

// `AgcManagerDirectTestHelper`.
struct Helper {
  explicit Helper(const Param& p) : param(p), manager(1, TestConfig(p)), audio(kFrames, 0.f) {
    auto a = std::make_unique<ScriptedAgc>();
    agc = a.get();
    manager.channel(0).set_agc(std::move(a));
    manager.Initialize();
  }
  std::optional<float> Override(float v) const { return param.rms_error_override ? std::optional<float>(v) : std::nullopt; }
  void ApplyGain() {
    if (auto g = manager.GetDigitalCompressionGain()) gains.push_back(*g);
  }
  int CallAgcSequence(int applied_input_volume, std::optional<float> p, std::optional<float> l) {
    manager.set_stream_analog_level(applied_input_volume);
    const std::span<const float> ch[1] = {audio};
    manager.AnalyzePreProcess(ch);
    const std::span<const float> band[1] = {std::span<const float>(audio.data(), kBandFrames)};
    manager.Process(band, p, l);
    ApplyGain();
    return manager.recommended_analog_level();
  }
  // Returns the compression gains set during these calls.
  std::vector<int> CallProcess(int num_calls, std::optional<float> p, std::optional<float> l) {
    gains.clear();
    const std::span<const float> band[1] = {std::span<const float>(audio.data(), kBandFrames)};
    for (int i = 0; i < num_calls; ++i) {
      manager.Process(band, p, l);
      ApplyGain();
    }
    return gains;
  }
  // Process() with `error` scripted (the overridden case gets `level`).
  void Step(int error, float level) {
    agc->errors.push_back(error);
    CallProcess(1, Override(kHighSpeechProbability), Override(level));
  }
  void CallPreProc(int num_calls, float clipped_ratio) {
    const size_t clipping = static_cast<size_t>(clipped_ratio * static_cast<float>(kFrames));
    for (size_t i = 0; i < kFrames; ++i) audio[i] = i < clipping ? 32767.0f : 0.0f;
    const std::span<const float> ch[1] = {audio};
    for (int i = 0; i < num_calls; ++i) manager.AnalyzePreProcess(ch);
  }
  void CallPreProcForChangingAudio(int num_calls, float peak_ratio) {
    const float v = peak_ratio * 32767.0f;
    for (size_t i = 0; i < kFrames; ++i) audio[i] = i % 2 ? 0.0f : v;
    const std::span<const float> ch[1] = {audio};
    for (int i = 0; i < num_calls / 2; ++i) manager.AnalyzePreProcess(ch);
    std::fill(audio.begin(), audio.end(), v);
    for (int i = 0; i < num_calls - num_calls / 2; ++i) manager.AnalyzePreProcess(ch);
  }
  int MinMicLevel() const { return param.min_mic_level.value_or(kMinMicLevel); }

  Param param;
  AgcManagerDirect manager;
  ScriptedAgc* agc;
  std::vector<float> audio;
  std::vector<int> gains;
};

const std::vector<int> kNone;
constexpr std::optional<float> kNoOverride = std::nullopt;

}  // namespace

SM_TEST(Agc1Manager, MicVolumeResponseToRmsError) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.Step(5, -23);  // Compressor default; no residual error.
    h.Step(10, -28);  // Inside the compressor's window; no change of volume.
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), kInitialInputVolume);
    h.Step(11, -29);  // Above the window: volume up.
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 130);
    h.Step(20, -38);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 168);
    h.Step(5, -23);
    h.Step(0, -18);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 168);
    h.Step(-1, -17);  // Below the window: volume down.
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 167);
    h.Step(-1, -17);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 163);
    h.Step(-9, -9);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 129);
  }
}

SM_TEST(Agc1Manager, MicVolumeIsLimited) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    const int expected_up[] = {183, 243, 255};  // The upward step is limited, and so is the maximum.
    for (int e : expected_up) {
      h.Step(30, -48);
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), e);
    }
    h.Step(-1, -17);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 254);
    const int expected_down[] = {194, 137, 88, 54, 33};
    for (int e : expected_down) {
      h.Step(-40, 22);
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), e);
    }
    h.Step(-40, 22);  // Not below the minimum.
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), std::max(18, h.MinMicLevel()));
    h.Step(-40, 22);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), std::max(12, h.MinMicLevel()));
  }
}

SM_TEST(Agc1Manager, CompressorStepsTowardsTarget) {
  for (const Param& p : kParams) {
    Helper h(p);
    const auto prob = h.Override(kHighSpeechProbability);
    h.CallAgcSequence(kInitialInputVolume, prob, h.Override(kSpeechLevelDbfs));
    // Compressor default; no new gain.
    h.agc->errors = {5};
    SM_EXPECT_TRUE(h.CallProcess(1, prob, h.Override(-23)) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(19, kNoOverride, kNoOverride) == kNone);
    // Moves slowly upwards.
    h.agc->errors = {9};
    SM_EXPECT_TRUE(h.CallProcess(1, prob, h.Override(-27)) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(18, kNoOverride, kNoOverride) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{8});
    SM_EXPECT_TRUE(h.CallProcess(19, kNoOverride, kNoOverride) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{9});
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == kNone);
    // Moves slowly downward, then reverses before reaching the original target.
    h.agc->errors = {5};
    SM_EXPECT_TRUE(h.CallProcess(1, prob, h.Override(-23)) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(18, kNoOverride, kNoOverride) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{8});
    h.agc->errors = {9};
    SM_EXPECT_TRUE(h.CallProcess(1, prob, h.Override(-27)) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(18, kNoOverride, kNoOverride) == kNone);
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{9});
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == kNone);
  }
}

SM_TEST(Agc1Manager, CompressorErrorIsDeemphasized) {
  for (const Param& p : kParams) {
    Helper h(p);
    const auto prob = h.Override(kHighSpeechProbability);
    h.CallAgcSequence(kInitialInputVolume, prob, h.Override(kSpeechLevelDbfs));
    h.agc->errors = {10};
    h.CallProcess(1, prob, h.Override(-28));
    h.CallProcess(18, kNoOverride, kNoOverride);
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == std::vector<int>{8});
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{9});
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == kNone);
    h.agc->errors = {0};
    h.CallProcess(1, prob, h.Override(-18));
    h.CallProcess(18, kNoOverride, kNoOverride);
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == std::vector<int>{8});
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == std::vector<int>{7});
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{6});
    SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == kNone);
  }
}

SM_TEST(Agc1Manager, CompressorReachesMaximumAndMinimum) {
  for (const Param& p : kParams) {
    for (bool up : {true, false}) {
      Helper h(p);
      const auto prob = h.Override(kHighSpeechProbability);
      h.CallAgcSequence(kInitialInputVolume, prob, h.Override(kSpeechLevelDbfs));
      h.agc->errors = up ? std::deque<int>{10, 10, 10, 10} : std::deque<int>{0, 0, 0, 0};
      h.CallProcess(4, prob, h.Override(up ? -28.0f : -18.0f));
      h.CallProcess(15, kNoOverride, kNoOverride);
      for (int g = 0; g < 4; ++g) {
        SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == std::vector<int>{up ? 8 + g : 6 - g});
      }
      SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{up ? 12 : 2});
    }
  }
}

SM_TEST(Agc1Manager, NoActionWhileMuted) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.manager.HandleCaptureOutputUsedChange(false);
    h.agc->repeated = 30;
    h.CallProcess(10, h.Override(kHighSpeechProbability), h.Override(-48));
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), kInitialInputVolume);
    SM_EXPECT_TRUE(h.gains.empty());
  }
}

SM_TEST(Agc1Manager, UnmutingChecksVolumeAndRaisesTooLowVolume) {
  for (const Param& p : kParams) {
    for (int volume : {127, 11}) {
      Helper h(p);
      h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
      h.manager.HandleCaptureOutputUsedChange(false);
      h.manager.HandleCaptureOutputUsedChange(true);
      h.manager.set_stream_analog_level(volume);
      const int resets = h.agc->resets;
      h.CallProcess(1, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
      SM_EXPECT_GT(h.agc->resets, resets);
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), volume == 127 ? 127 : h.MinMicLevel());
    }
  }
}

SM_TEST(Agc1Manager, ManualLevelChangeResultsInNoVolumeChange) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    // A volume outside the quantization slack is a manual change.
    h.manager.set_stream_analog_level(154);
    int resets = h.agc->resets;
    h.Step(11, -29);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 154);
    SM_EXPECT_GT(h.agc->resets, resets);
    h.manager.set_stream_analog_level(100);
    resets = h.agc->resets;
    h.Step(-1, -17);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 100);
    SM_EXPECT_GT(h.agc->resets, resets);
    h.Step(-1, -17);  // Continues without a manual change.
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 99);
  }
}

SM_TEST(Agc1Manager, RecoveryAfterManualLevelChangeFromMax) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    for (int e : {183, 243, 255}) {
      h.Step(30, -48);
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), e);
    }
    h.manager.set_stream_analog_level(50);
    h.Step(-1, -17);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 50);
    h.Step(20, -38);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 69);
  }
}

SM_TEST(Agc1Manager, RecoveryAfterManualLevelChangeBelowMin) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.manager.set_stream_analog_level(1);  // Below the minimum but positive.
    h.Step(-1, -17);
    if (p.min_mic_level) {
      // The override applies the minimum at once.
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), h.MinMicLevel());
      continue;
    }
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 1);
    h.Step(11, -29);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 2);
    h.Step(30, -48);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 11);
    h.Step(20, -38);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 18);
  }
}

SM_TEST(Agc1Manager, ClippingBelowThresholdHasNoImpact) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.CallPreProc(100, 0);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 128);
    h.CallPreProc(1, 0.099f);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 128);
  }
}

SM_TEST(Agc1Manager, ClippingLowersVolumeWithWaitingPeriod) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(255, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    int resets = h.agc->resets;
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 240);
    SM_EXPECT_GT(h.agc->resets, resets);
    resets = h.agc->resets;
    h.CallPreProc(300, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 240);
    SM_EXPECT_EQ(h.agc->resets, resets);
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 225);
    SM_EXPECT_EQ(h.manager.stats().clipping_events, 2u);
  }
}

SM_TEST(Agc1Manager, ClippingLoweringIsLimited) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(180, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), kClippedMin);
    const int resets = h.agc->resets;
    h.CallPreProc(1000, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), kClippedMin);
    SM_EXPECT_EQ(h.agc->resets, resets);
  }
}

SM_TEST(Agc1Manager, ClippingMaxIsRespected) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(255, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 240);
    h.agc->repeated = 30;
    h.CallProcess(10, h.Override(kHighSpeechProbability), h.Override(-48));
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 240);

    Helper g(p);
    g.CallAgcSequence(200, g.Override(kHighSpeechProbability), g.Override(kSpeechLevelDbfs));
    g.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(g.manager.recommended_analog_level(), 185);
    g.agc->repeated = 40;
    g.CallProcess(1, g.Override(kHighSpeechProbability), g.Override(-58));
    SM_EXPECT_EQ(g.manager.recommended_analog_level(), 240);
    g.CallProcess(10, g.Override(kHighSpeechProbability), g.Override(-58));
    SM_EXPECT_EQ(g.manager.recommended_analog_level(), 240);
  }
}

SM_TEST(Agc1Manager, MaxCompressionIsIncreasedAfterClipping) {
  for (const Param& p : kParams) {
    Helper h(p);
    const auto prob = h.Override(kHighSpeechProbability);
    h.CallAgcSequence(210, prob, h.Override(kSpeechLevelDbfs));
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 195);
    h.agc->errors = {11, 11, 11, 11, 11};
    h.CallProcess(5, prob, h.Override(-29));
    h.CallProcess(14, kNoOverride, kNoOverride);
    for (int g = 8; g <= 12; ++g) SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == std::vector<int>{g});
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{13});
    // Clip until the maximum surplus compression.
    h.CallPreProc(300, kAboveClippedThreshold);
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 180);
    h.CallPreProc(300, kAboveClippedThreshold);
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), kClippedMin);
    // The level is at the minimum, but the maximum level still goes down.
    for (int i = 0; i < 3; ++i) {
      h.CallPreProc(300, kAboveClippedThreshold);
      h.CallPreProc(1, kAboveClippedThreshold);
    }
    h.agc->errors = {16, 16, 16, 16};
    h.CallProcess(4, prob, h.Override(-34));
    h.CallProcess(15, kNoOverride, kNoOverride);
    for (int g = 14; g <= 17; ++g) SM_EXPECT_TRUE(h.CallProcess(20, kNoOverride, kNoOverride) == std::vector<int>{g});
    SM_EXPECT_TRUE(h.CallProcess(1, kNoOverride, kNoOverride) == std::vector<int>{18});
  }
}

SM_TEST(Agc1Manager, UserCanRaiseVolumeAfterClipping) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(225, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 210);
    h.manager.set_stream_analog_level(250);  // The user changed the volume.
    h.Step(14, -32);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 250);
    h.Step(-10, -8);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 210);
    h.Step(40, -58);  // Back up to the user's new maximum...
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 250);
    h.Step(30, -48);  // ...and not above it.
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 250);
  }
}

SM_TEST(Agc1Manager, ClippingDoesNotPullLowVolumeBackUp) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(80, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    const int resets = h.agc->resets;
    const int initial = h.manager.recommended_analog_level();
    h.CallPreProc(1, kAboveClippedThreshold);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), initial);
    SM_EXPECT_EQ(h.agc->resets, resets);
  }
}

SM_TEST(Agc1Manager, TakesNoActionOnZeroMicVolume) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(kInitialInputVolume, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    h.agc->repeated = 30;
    h.manager.set_stream_analog_level(0);
    h.CallProcess(10, h.Override(kHighSpeechProbability), h.Override(-48));
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 0);
  }
}

SM_TEST(Agc1Manager, ClippingDetectionLowersVolume) {
  for (const Param& p : kParams) {
    Helper h(p);
    h.CallAgcSequence(255, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 255);
    h.CallPreProcForChangingAudio(100, 0.99f);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 255);
    h.CallPreProcForChangingAudio(100, 1.0f);
    SM_EXPECT_EQ(h.manager.recommended_analog_level(), 240);
  }
}

SM_TEST(Agc1Manager, DisableDigitalDisablesDigital) {
  AnalogGainControllerConfig c;
  c.enable_digital_adaptive = false;
  AgcManagerDirect m(1, c);
  m.Initialize();
  SM_EXPECT_EQ(m.digital_target_level_dbfs(), 0);
  SM_EXPECT_EQ(m.digital_compression_gain_db(), 0);
  SM_EXPECT_FALSE(m.digital_limiter_enabled());
  AgcManagerDirect d(1);
  SM_EXPECT_EQ(d.digital_target_level_dbfs(), 2);
  SM_EXPECT_EQ(d.digital_compression_gain_db(), 7);
  SM_EXPECT_TRUE(d.digital_limiter_enabled());
}

SM_TEST(Agc1Manager, MinMicLevelOverrideOutOfRangeIsIgnored) {
  for (int v : {-1, 256}) {
    AnalogGainControllerConfig c;
    c.min_mic_level_override = v;
    AgcManagerDirect m(1, c);
    SM_EXPECT_EQ(m.channel(0).min_mic_level(), kMinMicLevel);
  }
  AnalogGainControllerConfig c;
  c.min_mic_level_override = 50;
  AgcManagerDirect m(1, c);
  SM_EXPECT_EQ(m.channel(0).min_mic_level(), 50);
}

// The clipping predictor (agc2/clipping_predictor.h) in the manager:
// agc_manager_direct_unittest.cc's *ClippingPredictor* tests.
namespace {

// `CallPreProcessAudioBuffer`: half the calls on frames whose every other
// sample is zero, the rest on frames with every sample at `peak_ratio`.
void CallPreProcessAudioBuffer(int num_calls, float peak_ratio, AgcManagerDirect& m) {
  std::vector<float> audio(kFrames, 0.f);
  const std::span<const float> ch[1] = {audio};
  for (size_t i = 0; i < kFrames; i += 2) audio[i] = peak_ratio * 32767.0f;
  for (int n = 0; n < num_calls / 2; ++n) m.AnalyzePreProcess(ch);
  std::fill(audio.begin(), audio.end(), peak_ratio * 32767.0f);
  for (int n = 0; n < num_calls - num_calls / 2; ++n) m.AnalyzePreProcess(ch);
}

// A manager on the test config with a silent `MockAgc`, after one Process()
// at `level`.
std::unique_ptr<AgcManagerDirect> PredictorManager(const Param& p, bool enabled, bool use_step, int level) {
  AnalogGainControllerConfig c = TestConfig(p);
  c.clipping_predictor.enabled = enabled;
  c.clipping_predictor.use_predicted_step = use_step;
  auto m = std::make_unique<AgcManagerDirect>(1, c);
  m->channel(0).set_agc(std::make_unique<ScriptedAgc>());
  m->Initialize();
  m->set_stream_analog_level(level);
  const std::vector<float> zeros(kBandFrames, 0.f);
  const std::span<const float> band[1] = {zeros};
  const std::optional<float> prob = p.rms_error_override ? std::optional<float>(kHighSpeechProbability) : std::nullopt;
  const std::optional<float> lvl = p.rms_error_override ? std::optional<float>(kSpeechLevelDbfs) : std::nullopt;
  m->Process(band, prob, lvl);
  return m;
}

constexpr int kClippedLevelStep = 15;
constexpr int kClippedWaitFrames = 300;

}  // namespace

SM_TEST(Agc1Manager, ClippingPredictorEnabledOnlyWhenConfigured) {
  AgcManagerDirect off(1);
  SM_EXPECT_FALSE(off.clipping_predictor_enabled());
  SM_EXPECT_FALSE(off.use_clipping_predictor_step());
  AnalogGainControllerConfig c;
  c.clipping_predictor.enabled = true;
  c.clipping_predictor.use_predicted_step = true;
  AgcManagerDirect on(1, c);
  SM_EXPECT_TRUE(on.clipping_predictor_enabled());
  SM_EXPECT_TRUE(on.use_clipping_predictor_step());
}

SM_TEST(Agc1Manager, DisabledClippingPredictorDoesNotLowerVolume) {
  for (const Param& p : kParams) {
    {
      Helper h(p);
      h.CallAgcSequence(255, h.Override(kHighSpeechProbability), h.Override(kSpeechLevelDbfs));
      SM_EXPECT_FALSE(h.manager.clipping_predictor_enabled());
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), 255);
      h.CallPreProcForChangingAudio(100, 0.99f);
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), 255);
      h.CallPreProcForChangingAudio(100, 0.99f);
      SM_EXPECT_EQ(h.manager.recommended_analog_level(), 255);
    }
    auto m = PredictorManager(p, false, true, 255);
    SM_EXPECT_FALSE(m->use_clipping_predictor_step());
    SM_EXPECT_EQ(m->recommended_analog_level(), 255);
    CallPreProcessAudioBuffer(10, 0.99f, *m);
    SM_EXPECT_EQ(m->recommended_analog_level(), 255);
    CallPreProcessAudioBuffer(300, 0.99f, *m);
    SM_EXPECT_EQ(m->recommended_analog_level(), 255);
    CallPreProcessAudioBuffer(10, 0.99f, *m);
    SM_EXPECT_EQ(m->recommended_analog_level(), 255);
  }
}

SM_TEST(Agc1Manager, UsedClippingPredictionsProduceLowerAnalogLevels) {
  constexpr int kInitialLevel = 255;
  for (const Param& p : kParams) {
    auto with = PredictorManager(p, true, true, kInitialLevel);
    auto without = PredictorManager(p, false, true, kInitialLevel);
    SM_EXPECT_TRUE(with->clipping_predictor_enabled());
    SM_EXPECT_FALSE(without->clipping_predictor_enabled());
    SM_EXPECT_TRUE(with->use_clipping_predictor_step());
    SM_EXPECT_EQ(with->recommended_analog_level(), kInitialLevel);
    SM_EXPECT_EQ(without->recommended_analog_level(), kInitialLevel);
    // {calls, peak ratio, level with prediction, level without}.
    struct Step {
      int calls;
      float peak;
      int with, without;
    };
    const Step steps[] = {
        {10, 0.99f, kInitialLevel - kClippedLevelStep, kInitialLevel},               // Predicted.
        {kClippedWaitFrames, 0.99f, kInitialLevel - kClippedLevelStep, kInitialLevel},  // Waiting.
        {10, 0.99f, kInitialLevel - 2 * kClippedLevelStep, kInitialLevel},           // Predicted.
        {2 * kClippedWaitFrames, 0.0f, kInitialLevel - 2 * kClippedLevelStep, kInitialLevel},
        {1, 1.0f, kInitialLevel - 3 * kClippedLevelStep, kInitialLevel - kClippedLevelStep},  // Clipping.
        {kClippedWaitFrames, 1.0f, kInitialLevel - 3 * kClippedLevelStep, kInitialLevel - kClippedLevelStep},
        {1, 1.0f, kInitialLevel - 4 * kClippedLevelStep, kInitialLevel - 2 * kClippedLevelStep},
    };
    for (const Step& s : steps) {
      CallPreProcessAudioBuffer(s.calls, s.peak, *with);
      CallPreProcessAudioBuffer(s.calls, s.peak, *without);
      SM_EXPECT_EQ(with->recommended_analog_level(), s.with);
      SM_EXPECT_EQ(without->recommended_analog_level(), s.without);
    }
    SM_EXPECT_GT(with->stats().clipping_predictions, 0u);
    SM_EXPECT_EQ(without->stats().clipping_predictions, 0u);
  }
}

SM_TEST(Agc1Manager, UnusedClippingPredictionsProduceEqualAnalogLevels) {
  constexpr int kInitialLevel = 255;
  for (const Param& p : kParams) {
    auto with = PredictorManager(p, true, false, kInitialLevel);
    auto without = PredictorManager(p, false, true, kInitialLevel);
    SM_EXPECT_TRUE(with->clipping_predictor_enabled());
    SM_EXPECT_FALSE(without->clipping_predictor_enabled());
    SM_EXPECT_FALSE(with->use_clipping_predictor_step());
    SM_EXPECT_EQ(with->recommended_analog_level(), kInitialLevel);
    const std::pair<int, float> steps[] = {{10, 0.99f}, {kClippedWaitFrames, 0.99f}, {10, 0.99f},
                                           {2 * kClippedWaitFrames, 0.0f}, {1, 1.0f}, {kClippedWaitFrames, 1.0f},
                                           {1, 1.0f}};
    for (const auto& [calls, peak] : steps) {
      CallPreProcessAudioBuffer(calls, peak, *with);
      CallPreProcessAudioBuffer(calls, peak, *without);
      SM_EXPECT_EQ(with->recommended_analog_level(), without->recommended_analog_level());
    }
    SM_EXPECT_GT(with->stats().clipping_predictions, 0u);
  }
}

// capture_levels_adjuster_unittest.cc: the emulated volume scales the
// capture by volume / 255; a change ramps over one frame; the result is
// saturated to S16.
SM_TEST(Agc1CaptureLevels, AnalogMicGainEmulation) {
  CaptureLevelsConfig c;
  c.analog_mic_gain_emulation = true;
  c.initial_level = 255;
  CaptureLevels adj(c);
  std::vector<float> x(160, 1000.f);
  std::span<float> ch[1] = {x};
  adj.ApplyPreLevelAdjustment(ch);
  SM_EXPECT_EQ(x[0], 1000.f);  // Unity gain: untouched.
  adj.SetAnalogMicGainLevel(51);
  SM_EXPECT_NEAR(adj.GetPreAdjustmentGain(), 0.2f, 1e-6);
  std::fill(x.begin(), x.end(), 1000.f);
  adj.ApplyPreLevelAdjustment(ch);
  SM_EXPECT_LT(x[0], 1000.f);  // Ramps down over the frame...
  SM_EXPECT_GT(x[0], 990.f);
  SM_EXPECT_NEAR(x[159], 200.f, 1e-3);  // ...to the new gain at its end.
  std::fill(x.begin(), x.end(), 1000.f);
  adj.ApplyPreLevelAdjustment(ch);
  SM_EXPECT_NEAR(x[0], 200.f, 1e-3);
  adj.SetAnalogMicGainLevel(300);  // Clamped to 255.
  SM_EXPECT_EQ(adj.GetAnalogMicGainLevel(), 255);
  CaptureLevelsConfig post;
  post.post_gain = 100.f;
  CaptureLevels sat(post);
  std::fill(x.begin(), x.end(), 1000.f);
  sat.ApplyPostLevelAdjustment(ch);
  SM_EXPECT_EQ(x[0], 32767.f);
  CaptureLevels off;  // Emulation off: the volume does not scale.
  off.SetAnalogMicGainLevel(10);
  SM_EXPECT_EQ(off.GetPreAdjustmentGain(), 1.f);
}
