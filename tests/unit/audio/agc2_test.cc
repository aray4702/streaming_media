// Unit tests: AGC2 (`apm v2`, #242), ported from libwebrtc M157's
// modules/audio_processing/agc2/*_unittest.cc with their constants.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "sm/audio/agc2.h"
#include "sm/core/random.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::agc2;

namespace {
constexpr float kMaxSpeechProbability = 1.0f;

std::vector<double> LinSpace(double l, double r, int num_points) {
  std::vector<double> points(static_cast<size_t>(num_points));
  const double step = (r - l) / (num_points - 1.0);
  points[0] = l;
  for (int i = 1; i < num_points - 1; ++i) points[static_cast<size_t>(i)] = l + i * step;
  points[static_cast<size_t>(num_points - 1)] = r;
  return points;
}

// LimiterDbGainCurve (limiter_db_gain_curve.cc) with agc2_testing_common.h's
// parameters (max input +1 dBFS, knee 1 dB, ratio 5): the curve the
// interpolated table approximates.
struct LimiterDbGainCurve {
  static constexpr double kMaxInputDb = 1.0, kKneeDb = 1.0, kRatio = 5.0;
  double knee_start_dbfs = -kKneeDb / 2.0 - kMaxInputDb / (kRatio - 1.0);
  double limiter_start_dbfs = knee_start_dbfs + kKneeDb;
  double a = (1.0 - kRatio) / (2.0 * kKneeDb * kRatio);
  double b = 1.0 - 2.0 * a * knee_start_dbfs;
  double c = a * knee_start_dbfs * knee_start_dbfs;
  static double ToS16(double dbfs) { return 32768.0 * std::pow(10.0, dbfs / 20.0); }
  static double ToDbfs(double v) { return 20.0 * std::log10(v / 32768.0); }
  double knee_start_linear() const { return ToS16(knee_start_dbfs); }
  double limiter_start_linear() const { return ToS16(limiter_start_dbfs); }
  double max_input_level_linear() const { return ToS16(kMaxInputDb); }
  double OutputDbfs(double in) const {
    if (in < knee_start_dbfs) return in;
    if (in < limiter_start_dbfs) return a * in * in + b * in + c;
    return (in - kMaxInputDb) / kRatio;
  }
  double GetGainLinear(double x) const {
    if (x < knee_start_linear()) return 1.0;
    return ToS16(OutputDbfs(ToDbfs(x))) / x;
  }
};
const LimiterDbGainCurve kLimiterCurve;
constexpr double kLevelEpsilon = 1e-2 * kMaxAbsFloatS16Value;

AdaptiveDigitalGainController::FrameInfo GetFrameInfoToNotAdapt(const AdaptiveDigitalConfig& config) {
  AdaptiveDigitalGainController::FrameInfo info;
  info.speech_probability = kMaxSpeechProbability;
  info.speech_level_dbfs = -config.initial_gain_db - config.headroom_db;
  info.speech_level_reliable = true;
  info.noise_rms_dbfs = kMinLevelDbfs;
  info.headroom_db = config.headroom_db;
  info.limiter_envelope_dbfs = -2.0f;
  return info;
}

float MaxGainChangePerFrameDb(float db_per_s) { return db_per_s * kFrameDurationMs / 1000.0f; }
}  // namespace

// interpolated_gain_curve_unittest.cc
SM_TEST(GainController2, InterpolatedGainCurveValidMonotonicAndApproximates) {
  InterpolatedGainCurve igc;
  float prev_gain = igc.LookUpGainToApply(0.0f);
  for (double level : LinSpace(kLevelEpsilon, kLimiterCurve.max_input_level_linear() * 2.0, 500)) {
    const float gain = igc.LookUpGainToApply(static_cast<float>(level));
    SM_EXPECT_LE(0.0f, gain);
    SM_EXPECT_LE(gain, 1.0f);
    SM_EXPECT_GE(prev_gain, gain);
    prev_gain = gain;
  }
  // CheckApproximation: within 1/32768 of the limiter curve.
  float max_err = 0;
  for (double level : LinSpace(kLevelEpsilon, kLimiterCurve.max_input_level_linear() - kLevelEpsilon, 500)) {
    max_err = std::max(max_err, static_cast<float>(std::fabs(kLimiterCurve.GetGainLinear(level) -
                                                             igc.LookUpGainToApply(static_cast<float>(level)))));
  }
  SM_RECORD("max_interpolation_error", max_err);
  SM_EXPECT_LT(max_err, 1.0f / 32768.0f);
}

SM_TEST(GainController2, InterpolatedGainCurveRegionBoundaries) {
  InterpolatedGainCurve igc;
  for (double level : {kLevelEpsilon, kLimiterCurve.knee_start_linear() + kLevelEpsilon,
                       kLimiterCurve.limiter_start_linear() + kLevelEpsilon,
                       kLimiterCurve.max_input_level_linear() + kLevelEpsilon}) {
    igc.LookUpGainToApply(static_cast<float>(level));
  }
  SM_EXPECT_EQ(igc.stats().look_ups_identity_region, 1u);
  SM_EXPECT_EQ(igc.stats().look_ups_knee_region, 1u);
  SM_EXPECT_EQ(igc.stats().look_ups_limiter_region, 1u);
  SM_EXPECT_EQ(igc.stats().look_ups_saturation_region, 1u);
  // CheckIdentityRegion.
  for (double level : LinSpace(kLevelEpsilon, kLimiterCurve.knee_start_linear(), 10)) {
    SM_EXPECT_EQ(igc.LookUpGainToApply(static_cast<float>(level)), 1.0f);
  }
}

// limiter_unittest.cc
SM_TEST(GainController2, LimiterOutputVolumeAboveThreshold) {
  constexpr int kSamples = 480;
  const float input_level = (kMaxAbsFloatS16Value + DbfsToFloatS16(kLimiterMaxInputLevelDbFs)) / 2.0f;
  Limiter limiter(kSamples);
  std::vector<float> buffer(kSamples);
  for (int i = 0; i < 6; ++i) {
    std::fill(buffer.begin(), buffer.end(), input_level);
    limiter.Process(buffer);
  }
  for (float s : buffer) {
    SM_EXPECT_LT(0.9f * kMaxAbsFloatS16Value, s);
    SM_EXPECT_LE(s, kMaxFloatS16Value);
  }
}

// gain_applier_unittest.cc: the gain is ramped within a frame.
SM_TEST(GainController2, GainApplierRampsAndClips) {
  GainApplier applier(/*hard_clip_samples=*/true, 1.0f);
  std::vector<float> frame(160, 1.0f);
  applier.SetGainFactor(2.0f);
  applier.ApplyGain(frame);
  SM_EXPECT_NEAR(frame.front(), 1.0f, 1e-6f);
  SM_EXPECT_NEAR(frame.back(), 2.0f - 1.0f / 160.0f, 1e-5f);
  std::vector<float> loud(160, 30000.0f);
  applier.ApplyGain(loud);  // Constant gain 2, then clipped.
  SM_EXPECT_EQ(loud[0], kMaxFloatS16Value);
}

// adaptive_digital_gain_controller_unittest.cc
SM_TEST(GainController2, AdaptiveDigitalMaxGainApplied) {
  AdaptiveDigitalConfig config;
  config.max_output_noise_level_dbfs = -40.0f;
  const int frames = static_cast<int>(config.max_gain_db / MaxGainChangePerFrameDb(6.0f)) + 10;
  AdaptiveDigitalGainController ctrl(config, kAdjacentSpeechFramesThreshold);
  auto info = GetFrameInfoToNotAdapt(config);
  info.speech_level_dbfs = -60.0f;
  float applied_gain = 0;
  for (int i = 0; i < frames; ++i) {
    std::vector<float> f(80, 1.0f);
    ctrl.Process(info, f);
    applied_gain = f[0];
  }
  SM_EXPECT_NEAR(20.0f * std::log10(applied_gain), config.max_gain_db, 0.1f);
}

SM_TEST(GainController2, AdaptiveDigitalGainDoesNotChangeFast) {
  const AdaptiveDigitalConfig config;
  AdaptiveDigitalGainController ctrl(config, kAdjacentSpeechFramesThreshold);
  const float max_step_db = MaxGainChangePerFrameDb(config.max_gain_change_db_per_second);
  // libwebrtc's version computes its frame count as int(-25 / 0.06) + 10,
  // which is negative, so its loops never run. Here: 25 dB worth of frames
  // each way, starting from the initial gain the first frame applies.
  const int frames = static_cast<int>(25.0f / max_step_db) + 10;
  const float max_change_linear = DbToRatio(max_step_db);
  float last_gain = DbToRatio(config.initial_gain_db);
  float max_change = 0;
  for (float level : {-50.0f, 0.0f}) {
    for (int i = 0; i < frames; ++i) {
      std::vector<float> f(80, 1.0f);
      auto info = GetFrameInfoToNotAdapt(config);
      info.speech_level_dbfs = level;
      ctrl.Process(info, f);
      max_change = std::max(max_change, std::abs(f[0] - last_gain));
      last_gain = f[0];
    }
  }
  SM_EXPECT_LE(max_change, max_change_linear);
}

SM_TEST(GainController2, AdaptiveDigitalNoiseAndLimiterLimitGain) {
  const AdaptiveDigitalConfig config;
  const int initial_frames = static_cast<int>(config.initial_gain_db / MaxGainChangePerFrameDb(6.0f));
  for (int variant = 0; variant < 2; ++variant) {
    AdaptiveDigitalGainController ctrl(config, kAdjacentSpeechFramesThreshold);
    for (int i = 0; i < initial_frames + 50; ++i) {
      std::vector<float> f(480, 1.0f);
      auto info = GetFrameInfoToNotAdapt(config);
      info.speech_level_dbfs = -25.0f;
      if (variant == 0) {
        info.noise_rms_dbfs = -20.0f;  // NoiseLimitsGain.
      } else {
        info.limiter_envelope_dbfs = 1.0f;  // AudioLevelLimitsGain.
        info.speech_level_reliable = false;
      }
      ctrl.Process(info, f);
      if (i > initial_frames) SM_EXPECT_NEAR(*std::max_element(f.begin(), f.end()), 1.0f, 0.001f);
    }
  }
}

SM_TEST(GainController2, AdaptiveDigitalIncreasesOnlyAfterEnoughSpeechFrames) {
  const AdaptiveDigitalConfig config;
  for (int threshold : {1, 7, 31, kAdjacentSpeechFramesThreshold}) {
    AdaptiveDigitalGainController ctrl(config, threshold);
    auto info = GetFrameInfoToNotAdapt(config);
    info.speech_level_dbfs -= 12.0f;
    float prev_gain = 0.0f;
    for (int i = 0; i < threshold; ++i) {
      std::vector<float> f(480, 1.0f);
      ctrl.Process(info, f);
      if (i > 0) SM_EXPECT_EQ(prev_gain, f[0]);  // No increase yet.
      prev_gain = f[0];
    }
    std::vector<float> f(480, 1.0f);
    ctrl.Process(info, f);
    SM_EXPECT_GT(f[0], prev_gain);
  }
}

// saturation_protector_unittest.cc
SM_TEST(GainController2, SaturationProtectorEstimatesCrestAndChangesSlowly) {
  constexpr float kInitialHeadroomDb = 20.0f;
  {
    SaturationProtector sp(kInitialHeadroomDb, 1);
    constexpr float kCrest = kInitialHeadroomDb + 1.0f;
    for (int i = 0; i < 2000; ++i) sp.Analyze(kMaxSpeechProbability, -20.0f, -20.0f - kCrest);
    SM_EXPECT_NEAR(sp.HeadroomDb(), kCrest, 0.5f * std::fabs(kInitialHeadroomDb - kCrest));
    sp.Reset();
    SM_EXPECT_EQ(sp.HeadroomDb(), kInitialHeadroomDb);
  }
  {
    SaturationProtector sp(kInitialHeadroomDb, 1);
    float last = sp.HeadroomDb(), max_diff = 0;
    for (float crest : {kInitialHeadroomDb - 5.0f, kInitialHeadroomDb}) {
      for (int i = 0; i < 1000; ++i) {
        sp.Analyze(kMaxSpeechProbability, -20.0f, -20.0f - crest);
        max_diff = std::max(max_diff, std::fabs(sp.HeadroomDb() - last));
        last = sp.HeadroomDb();
      }
    }
    SM_EXPECT_LE(max_diff, 0.5f / 1000 * kFrameDurationMs);
  }
  for (int threshold : {2, 9, 17}) {
    SaturationProtector short_seg(kInitialHeadroomDb, threshold), long_seg(kInitialHeadroomDb, threshold);
    for (int i = 0; i < threshold - 1; ++i) short_seg.Analyze(kMaxSpeechProbability, 0.0f, -10.0f);
    for (int i = 0; i < threshold + 1; ++i) long_seg.Analyze(kMaxSpeechProbability, 0.0f, -10.0f);
    SM_EXPECT_EQ(short_seg.HeadroomDb(), kInitialHeadroomDb);
    SM_EXPECT_NE(long_seg.HeadroomDb(), kInitialHeadroomDb);
  }
}

// saturation_protector_buffer_unittest.cc
SM_TEST(GainController2, SaturationProtectorBufferFrontIsOldest) {
  SaturationProtectorBuffer b;
  SM_EXPECT_FALSE(b.Front().has_value());
  for (int i = 0; i < b.Capacity() + 2; ++i) b.PushBack(static_cast<float>(i));
  SM_EXPECT_EQ(b.Size(), b.Capacity());
  SM_EXPECT_EQ(*b.Front(), 2.0f);
}

// speech_level_estimator_unittest.cc
SM_TEST(GainController2, SpeechLevelEstimatorConfidenceAndConvergence) {
  const AdaptiveDigitalConfig config;
  constexpr int kFramesToConfidence = static_cast<int>(kLevelEstimatorTimeToConfidenceMs) / kFrameDurationMs;
  SpeechLevelEstimator e(config, 1);
  const float initial = e.GetLevelDbfs();
  SM_EXPECT_EQ(initial, -40.0f);  // -20 - 15 - 5.
  const float rms = initial / 2.0f;
  for (int i = 0; i < kFramesToConfidence / 2; ++i) e.Update(rms, kMaxSpeechProbability);
  SM_EXPECT_FALSE(e.IsConfident());
  for (int i = kFramesToConfidence / 2; i < kFramesToConfidence; ++i) e.Update(rms, kMaxSpeechProbability);
  SM_EXPECT_TRUE(e.IsConfident());
  SM_EXPECT_NEAR(e.GetLevelDbfs(), rms, 0.5f);
  const float level = e.GetLevelDbfs();
  for (int i = 0; i < kFramesToConfidence; ++i) e.Update(0.0f, 0.0f);  // Non-speech is ignored.
  SM_EXPECT_EQ(e.GetLevelDbfs(), level);

  // ConvergenceSpeedAfterConfidence: 7 s.
  SpeechLevelEstimator e2(config, 1);
  for (int i = 0; i < kFramesToConfidence; ++i) e2.Update(initial, kMaxSpeechProbability);
  SM_EXPECT_TRUE(e2.IsConfident());
  for (int i = 0; i < 700; ++i) e2.Update(rms, kMaxSpeechProbability);
  SM_EXPECT_NEAR(e2.GetLevelDbfs(), rms, 0.5f);

  // Short speech segments don't move it.
  for (int threshold : {1, 9, 17}) {
    SpeechLevelEstimator s(config, threshold);
    for (int i = 0; i < threshold - 1; ++i) s.Update(rms, kMaxSpeechProbability);
    s.Update(rms, kVadConfidenceThreshold / 2.0f);
    SM_EXPECT_EQ(s.GetLevelDbfs(), initial);
  }
}

// noise_level_estimator_unittest.cc
SM_TEST(GainController2, NoiseFloorEstimator) {
  for (int fs : {8000, 16000, 32000, 48000}) {
    const int n = fs / 100;
    auto run = [&](auto gen) {
      NoiseFloorEstimator est;
      std::vector<float> f(static_cast<size_t>(n));
      for (int i = 0; i < 200; ++i) {
        for (float& v : f) v = gen();
        est.Analyze(f);
      }
      return est.Analyze(f);
    };
    Random rng(42);
    // Full-scale white noise: about -5.5 dBFS.
    SM_EXPECT_NEAR(run([&] { return static_cast<float>(rng.Uniform(-32768.0, 32767.0)); }), -5.5f, 0.5f);
    // Full-scale 600 Hz sine: about -3 dBFS.
    int k = 0;
    SM_EXPECT_NEAR(run([&] {
                     return 32767.0f *
                            static_cast<float>(std::sin(2 * std::numbers::pi * 600.0 * k++ / fs));
                   }),
                   -3.0f, 0.1f);
    // 20 Hz full-scale pulses over a floor of 10: the floor is found.
    int j = 0;
    const int period = fs / 20;
    SM_EXPECT_NEAR(run([&] { return (j++ % period) == 0 ? 32767.0f : 10.0f; }),
                   20.0f * std::log10(10.0f / 32767.0f), 0.5f);
  }
}

// GainController2: a quiet speech-like signal is lifted by the default
// initial gain at once and the output stays in the S16 range.
SM_TEST(GainController2, ComposedQuietSpeechGetsGainAndNeverClips) {
  GainController2 gc;
  SM_EXPECT_EQ(gc.gain_db(), 15.0f);
  float peak = 0;
  for (int i = 0; i < 1000; ++i) {
    std::vector<float> f(160);
    for (size_t s = 0; s < f.size(); ++s) {
      f[s] = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * 300.0 * (i * 160 + s) / 16000.0));
    }
    gc.Process(f, 1.0f);
    for (float v : f) peak = std::max(peak, std::fabs(v));
  }
  SM_RECORD("gain_db", gc.gain_db());
  SM_EXPECT_LE(peak, 1.0f);
  // -9 dBFS RMS speech, +5 dB headroom guess... the target is below 0 dB:
  // the gain falls to 0 dB at 6 dB/s.
  SM_EXPECT_EQ(gc.gain_db(), 0.0f);
}

// #594: GainController2 runs at APM's processing rates. A rate below APM's
// 8 kHz floor (`ValidateAudioFormat`, audio_processing_impl.cc) or above the
// limiter's 480 samples per 10 ms (`kMaximalNumberOfSamplesPerChannel`,
// limiter.cc) is rejected: valid() is false and Process leaves the audio
// untouched, where 0 Hz used to assert in the VAD's resampler. A frame that
// is not 10 ms at the configured rate is rejected the same way.
SM_TEST(GainController2, RejectsUnsupportedRatesAndFrameLengths) {
  for (int rate : {0, -16000, 4000, 96000}) {
    GainController2Config c;
    c.sample_rate = rate;
    GainController2 gc(c);
    SM_EXPECT_FALSE(gc.valid());
    std::vector<float> f(160, 0.1f);
    for (int i = 0; i < 10; ++i) {
      gc.Process(f);
      gc.Process(f, 1.0f);
    }
    SM_EXPECT_TRUE(std::all_of(f.begin(), f.end(), [](float x) { return x == 0.1f; }));
    SM_EXPECT_EQ(gc.stats().frames, 0u);
  }
  GainController2 gc;
  std::vector<float> f(320, 0.1f);  // 20 ms at 16 kHz.
  gc.Process(f);
  SM_EXPECT_TRUE(std::all_of(f.begin(), f.end(), [](float x) { return x == 0.1f; }));
  SM_EXPECT_EQ(gc.stats().frames, 0u);
}

SM_TEST(GainController2, RunsAtApmRates) {
  for (int rate : {8000, 11025, 16000, 32000, 44100, 48000}) {
    GainController2Config c;
    c.sample_rate = rate;
    GainController2 gc(c);
    SM_EXPECT_TRUE(gc.valid());
    std::vector<float> f(static_cast<size_t>(rate / 100), 0.1f);
    for (int i = 0; i < 10; ++i) gc.Process(f);
    SM_EXPECT_EQ(gc.stats().frames, 10u);
  }
}
