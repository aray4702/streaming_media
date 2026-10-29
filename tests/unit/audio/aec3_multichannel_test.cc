// Ports of libwebrtc's AEC3 unit tests for multichannel render (#565, F6.1):
// multi_channel_content_detector_unittest.cc, config_selector_unittest.cc,
// alignment_mixer_unittest.cc and echo_canceller3_unittest.cc's
// DetectionOfProperStereo* and StereoContentDetectionForMonoSignals, with
// their signals and pass conditions. The UMA metrics tests have no
// counterpart (no histograms here), nor do the death tests (no DCHECKs).
// The tests marked "(ours)" check the wiring that libwebrtc's tests leave to
// its call-level tests: the 2 s default hysteresis and 300 s timeout, the
// downmix while stereo isn't yet persistent, and echo removal on a stereo
// render once the AEC runs on both channels.
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <tuple>
#include <vector>

#include "sm/audio/aec3.h"
#include "sm/core/random.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

Frame StereoFrame(float left, float right) {
  return Frame{{std::vector<float>(kFrameSize, left), std::vector<float>(kFrameSize, right)}};
}

}  // namespace

// --- multi_channel_content_detector_unittest.cc ---

SM_TEST(MultiChannelContentDetector, HandlingOfMono) {
  MultiChannelContentDetector mc(true, 1, 0.0f, 0, 0.0f);
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
}

SM_TEST(MultiChannelContentDetector, HandlingOfMonoAndDetectionOff) {
  MultiChannelContentDetector mc(false, 1, 0.0f, 0, 0.0f);
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
}

SM_TEST(MultiChannelContentDetector, HandlingOfDetectionOff) {
  MultiChannelContentDetector mc(false, 2, 0.0f, 0, 0.0f);
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  const Frame frame = StereoFrame(100.0f, 101.0f);
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
}

SM_TEST(MultiChannelContentDetector, InitialDetectionOfStereo) {
  MultiChannelContentDetector mc(true, 2, 0.0f, 0, 0.0f);
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
}

SM_TEST(MultiChannelContentDetector, DetectionWhenFakeStereo) {
  MultiChannelContentDetector mc(true, 2, 0.0f, 0, 0.0f);
  const Frame frame = StereoFrame(100.0f, 100.0f);
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
}

SM_TEST(MultiChannelContentDetector, DetectionWhenStereo) {
  MultiChannelContentDetector mc(true, 2, 0.0f, 0, 0.0f);
  const Frame frame = StereoFrame(100.0f, 101.0f);
  SM_EXPECT_TRUE(mc.UpdateDetection(frame));
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
}

SM_TEST(MultiChannelContentDetector, DetectionWhenStereoAfterAWhile) {
  MultiChannelContentDetector mc(true, 2, 0.0f, 0, 0.0f);
  SM_EXPECT_FALSE(mc.UpdateDetection(StereoFrame(100.0f, 100.0f)));
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(StereoFrame(100.0f, 100.0f)));
  SM_EXPECT_TRUE(mc.UpdateDetection(StereoFrame(100.0f, 101.0f)));
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(StereoFrame(100.0f, 101.0f)));
}

SM_TEST(MultiChannelContentDetector, DetectionWithStereoBelowThreshold) {
  constexpr float kThreshold = 1.0f;
  MultiChannelContentDetector mc(true, 2, kThreshold, 0, 0.0f);
  const Frame frame = StereoFrame(100.0f, 100.0f + kThreshold);
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
}

SM_TEST(MultiChannelContentDetector, DetectionWithStereoAboveThreshold) {
  constexpr float kThreshold = 1.0f;
  MultiChannelContentDetector mc(true, 2, kThreshold, 0, 0.0f);
  const Frame frame = StereoFrame(100.0f, 100.0f + kThreshold + 0.1f);
  SM_EXPECT_TRUE(mc.UpdateDetection(frame));
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.UpdateDetection(frame));
}

// TimeOutBehaviorForNonTrueStereo, for (detect, timeout s) in
// {false, true} x {0, 1, 10}.
SM_TEST(MultiChannelContentDetector, TimeOutBehaviorForNonTrueStereo) {
  constexpr int kNumFramesPerSecond = 100;
  const Frame true_stereo_frame = StereoFrame(100.0f, 101.0f);
  const Frame fake_stereo_frame = StereoFrame(100.0f, 100.0f);
  for (bool detect_stereo_content : {false, true}) {
    for (int timeout_seconds : {0, 1, 10}) {
      const int timeout_frames = timeout_seconds * kNumFramesPerSecond;
      MultiChannelContentDetector mc(detect_stereo_content, 2, 0.0f, timeout_seconds, 0.0f);
      bool ok = true;
      for (int k = 0; k < 10; ++k) {
        ok = ok && !mc.UpdateDetection(fake_stereo_frame);
        ok = ok && mc.IsProperMultiChannelContentDetected() == !detect_stereo_content;
      }
      ok = ok && mc.UpdateDetection(true_stereo_frame) == detect_stereo_content;
      ok = ok && mc.IsProperMultiChannelContentDetected();
      for (int k = 0; k < timeout_frames - 1; ++k) {
        ok = ok && !mc.UpdateDetection(fake_stereo_frame);
        ok = ok && mc.IsProperMultiChannelContentDetected();
      }
      const bool times_out = detect_stereo_content && timeout_frames > 0;
      ok = ok && mc.UpdateDetection(fake_stereo_frame) == times_out;
      ok = ok && mc.IsProperMultiChannelContentDetected() == !times_out;
      for (int k = 0; k < 10; ++k) {
        ok = ok && !mc.UpdateDetection(fake_stereo_frame);
        ok = ok && mc.IsProperMultiChannelContentDetected() == !times_out;
      }
      SM_EXPECT_TRUE(ok);
    }
  }
}

// PeriodBeforeStereoDetectionIsTriggered, for detect in {false, true}. The
// libwebrtc test reads its hysteresis parameter (0, 0.1, 0.2 s) into an int,
// so all three run with no hysteresis; so does this port. The hysteresis
// itself is checked by DefaultHysteresisAndTimeout below.
SM_TEST(MultiChannelContentDetector, PeriodBeforeStereoDetectionIsTriggered) {
  const Frame true_stereo_frame = StereoFrame(100.0f, 101.0f);
  const Frame fake_stereo_frame = StereoFrame(100.0f, 100.0f);
  for (bool detect_stereo_content : {false, true}) {
    for (float hysteresis : {0.0f, 0.1f, 0.2f}) {
      const int stereo_detection_hysteresis_seconds = static_cast<int>(hysteresis);
      MultiChannelContentDetector mc(detect_stereo_content, 2, 0.0f, 0,
                                     static_cast<float>(stereo_detection_hysteresis_seconds));
      bool ok = true;
      for (int k = 0; k < 10; ++k) {
        ok = ok && !mc.UpdateDetection(fake_stereo_frame);
        ok = ok && mc.IsProperMultiChannelContentDetected() == !detect_stereo_content;
        ok = ok && !mc.IsTemporaryMultiChannelContentDetected();
      }
      for (int k = 0; k < 2; ++k) {
        ok = ok && mc.UpdateDetection(true_stereo_frame) == (detect_stereo_content && k == 0);
        ok = ok && mc.IsProperMultiChannelContentDetected();
        ok = ok && !mc.IsTemporaryMultiChannelContentDetected();
      }
      SM_EXPECT_TRUE(ok);
    }
  }
}

// (ours) M157's defaults: persistent stereo after more than 2 s (200 frames)
// of stereo frames in a row, temporary before that; back to mono after 300 s
// without a stereo frame.
SM_TEST(MultiChannelContentDetector, DefaultHysteresisAndTimeout) {
  const Aec3Config config;
  MultiChannelContentDetector mc(config.multi_channel.detect_stereo_content, 2,
                                 config.multi_channel.stereo_detection_threshold,
                                 config.multi_channel.stereo_detection_timeout_threshold_seconds,
                                 config.multi_channel.stereo_detection_hysteresis_seconds);
  const Frame true_stereo_frame = StereoFrame(100.0f, 101.0f);
  const Frame fake_stereo_frame = StereoFrame(100.0f, 100.0f);
  bool ok = true;
  for (int k = 0; k < 200; ++k) {
    ok = ok && !mc.UpdateDetection(true_stereo_frame);
    ok = ok && !mc.IsProperMultiChannelContentDetected() && mc.IsTemporaryMultiChannelContentDetected();
  }
  SM_EXPECT_TRUE(ok);
  SM_EXPECT_TRUE(mc.UpdateDetection(true_stereo_frame));
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_FALSE(mc.IsTemporaryMultiChannelContentDetected());
  // 300 s of upmixed mono end it.
  for (int k = 0; k < 300 * 100 - 1; ++k) ok = ok && !mc.UpdateDetection(fake_stereo_frame);
  SM_EXPECT_TRUE(ok);
  SM_EXPECT_TRUE(mc.IsProperMultiChannelContentDetected());
  SM_EXPECT_TRUE(mc.UpdateDetection(fake_stereo_frame));
  SM_EXPECT_FALSE(mc.IsProperMultiChannelContentDetected());
}

// --- config_selector_unittest.cc ---

SM_TEST(ConfigSelector, MonoConfigIsSelectedWhenNoMultiChannelConfigPresent) {
  for (int num_channels : {1, 2, 8}) {
    for (bool detect_stereo_content : {false, true}) {
      Aec3Config config;
      config.multi_channel.detect_stereo_content = detect_stereo_content;
      std::optional<Aec3Config> multichannel_config;
      config.delay.default_delay = config.delay.default_delay + 1;
      const size_t custom_delay_value_in_config = config.delay.default_delay;
      ConfigSelector cs(config, multichannel_config, num_channels);
      SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_config);
      cs.Update(false);
      SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_config);
      cs.Update(true);
      SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_config);
    }
  }
}

SM_TEST(ConfigSelector, CorrectInitialConfigIsSelected) {
  for (int num_channels : {1, 2, 8}) {
    for (bool detect_stereo_content : {false, true}) {
      Aec3Config config;
      config.multi_channel.detect_stereo_content = detect_stereo_content;
      std::optional<Aec3Config> multichannel_config = config;
      config.delay.default_delay += 1;
      const size_t custom_delay_value_in_config = config.delay.default_delay;
      multichannel_config->delay.default_delay += 2;
      const size_t custom_delay_value_in_multichannel_config = multichannel_config->delay.default_delay;
      ConfigSelector cs(config, multichannel_config, num_channels);
      if (num_channels == 1 || detect_stereo_content) {
        SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_config);
      } else {
        SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_multichannel_config);
      }
    }
  }
}

SM_TEST(ConfigSelector, CorrectConfigUpdateBehavior) {
  for (int num_channels : {1, 2, 8}) {
    Aec3Config config;
    config.multi_channel.detect_stereo_content = true;
    std::optional<Aec3Config> multichannel_config = config;
    config.delay.default_delay += 1;
    const size_t custom_delay_value_in_config = config.delay.default_delay;
    multichannel_config->delay.default_delay += 2;
    const size_t custom_delay_value_in_multichannel_config = multichannel_config->delay.default_delay;
    ConfigSelector cs(config, multichannel_config, num_channels);
    cs.Update(false);
    SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_config);
    if (num_channels == 1) {
      cs.Update(false);
      SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_config);
    } else {
      cs.Update(true);
      SM_EXPECT_EQ(cs.active_config().delay.default_delay, custom_delay_value_in_multichannel_config);
    }
  }
}

// (ours) CreateDefaultMultichannelConfig's values, and that it is compatible
// with the default config as config_selector.cc requires.
SM_TEST(ConfigSelector, DefaultMultichannelConfig) {
  const Aec3Config mc = CreateDefaultMultichannelConfig();
  SM_EXPECT_EQ(mc.filter.coarse.length_blocks, size_t{11});
  SM_EXPECT_NEAR(mc.filter.coarse.rate, 0.95, 1e-7);
  SM_EXPECT_EQ(mc.filter.coarse_initial.length_blocks, size_t{11});
  SM_EXPECT_NEAR(mc.filter.coarse_initial.rate, 0.95, 1e-7);
  SM_EXPECT_NEAR(mc.suppressor.normal_tuning.max_dec_factor_lf, 0.35, 1e-7);
  SM_EXPECT_NEAR(mc.suppressor.normal_tuning.max_inc_factor, 1.5, 1e-7);
  SM_EXPECT_TRUE(CompatibleConfigs(Aec3Config{}, mc));
  Aec3Config other = mc;
  other.fixed_capture_delay_samples = 10;
  SM_EXPECT_FALSE(CompatibleConfigs(Aec3Config{}, other));
}

// --- alignment_mixer_unittest.cc ---

SM_TEST(AlignmentMixer, GeneralAdaptiveMode) {
  constexpr int kChannelOffset = 100;
  constexpr int kMaxChannelsToTest = 8;
  constexpr float kStrongestSignalScaling = kMaxChannelsToTest * kChannelOffset * 100;
  int failures = 0;
  for (bool initial_silence : {false, true}) {
    for (bool huge_activity_threshold : {false, true}) {
      for (bool prefer_first_two_channels : {false, true}) {
        for (int num_channels = 2; num_channels < 8; ++num_channels) {
          for (int strongest_ch = 0; strongest_ch < num_channels; ++strongest_ch) {
            const float excitation_limit = huge_activity_threshold ? 1000000000.f : 0.001f;
            AlignmentMixer am(static_cast<size_t>(num_channels), false, true, excitation_limit,
                              prefer_first_two_channels);
            Block x(1, num_channels);
            if (initial_silence) {
              std::array<float, kBlockSize> y;
              for (int frame = 0; frame < 10 * kNumBlocksPerSecond; ++frame) am.ProduceOutput(x, y);
            }
            for (int frame = 0; frame < 2 * kNumBlocksPerSecond; ++frame) {
              for (int ch = 0; ch < num_channels; ++ch) {
                const float scaling = ch == strongest_ch ? kStrongestSignalScaling : 1.f;
                auto x_ch = x.View(0, ch);
                std::fill(x_ch.begin(), x_ch.end(), static_cast<float>(frame + ch * kChannelOffset) * scaling);
              }
              std::array<float, kBlockSize> y;
              y.fill(-1.f);
              am.ProduceOutput(x, y);
              if (frame > 1 * kNumBlocksPerSecond) {
                if (!prefer_first_two_channels || huge_activity_threshold) {
                  const float want = x.View(0, strongest_ch)[0];
                  if (!std::all_of(y.begin(), y.end(), [&](float v) { return v == want; })) ++failures;
                } else {
                  bool left_or_right_chosen = false;
                  for (int ch = 0; ch < 2 && !left_or_right_chosen; ++ch) {
                    const auto x_ch = x.View(0, ch);
                    left_or_right_chosen = std::equal(y.begin(), y.end(), x_ch.begin());
                  }
                  if (!left_or_right_chosen) ++failures;
                }
              }
            }
          }
        }
      }
    }
  }
  SM_EXPECT_EQ(failures, 0);
}

SM_TEST(AlignmentMixer, DownmixMode) {
  for (int num_channels = 1; num_channels < 8; ++num_channels) {
    AlignmentMixer am(static_cast<size_t>(num_channels), true, false, 1.f, false);
    Block x(1, num_channels);
    for (int frame = 0; frame < 10; ++frame) {
      for (int ch = 0; ch < num_channels; ++ch) {
        auto x_ch = x.View(0, ch);
        std::fill(x_ch.begin(), x_ch.end(), static_cast<float>(frame + ch));
      }
      std::array<float, kBlockSize> y;
      y.fill(-1.f);
      am.ProduceOutput(x, y);
      float expected_mixed_value = 0.f;
      for (int ch = 0; ch < num_channels; ++ch) expected_mixed_value += static_cast<float>(frame + ch);
      expected_mixed_value *= 1.f / static_cast<float>(num_channels);
      SM_EXPECT_TRUE(std::all_of(y.begin(), y.end(), [&](float v) { return v == expected_mixed_value; }));
    }
  }
}

SM_TEST(AlignmentMixer, FixedMode) {
  for (int num_channels = 1; num_channels < 8; ++num_channels) {
    AlignmentMixer am(static_cast<size_t>(num_channels), false, false, 1.f, false);
    Block x(1, num_channels);
    for (int frame = 0; frame < 10; ++frame) {
      for (int ch = 0; ch < num_channels; ++ch) {
        auto x_ch = x.View(0, ch);
        std::fill(x_ch.begin(), x_ch.end(), static_cast<float>(frame + ch));
      }
      std::array<float, kBlockSize> y;
      y.fill(-1.f);
      am.ProduceOutput(x, y);
      const float want = x.View(0, 0)[0];
      SM_EXPECT_TRUE(std::all_of(y.begin(), y.end(), [&](float v) { return v == want; }));
    }
  }
}

// (ours) The defaults: adaptive selection for render and capture.
SM_TEST(AlignmentMixer, DefaultsAreAdaptive) {
  const DelayConfig config;
  SM_EXPECT_TRUE(AlignmentMixer(2, config.render_alignment_mixing).variant() ==
                 AlignmentMixer::MixingVariant::kAdaptive);
  SM_EXPECT_TRUE(AlignmentMixer(2, config.capture_alignment_mixing).variant() ==
                 AlignmentMixer::MixingVariant::kAdaptive);
  SM_EXPECT_TRUE(AlignmentMixer(1, config.render_alignment_mixing).variant() ==
                 AlignmentMixer::MixingVariant::kFixed);
}

// --- echo_canceller3_unittest.cc: stereo detection ---

namespace {

constexpr size_t kNumBlocksForMonoConfig = 1;
constexpr size_t kNumBlocksForSurroundConfig = 2;

void RunAecInStereo(EchoCanceller3& aec3, float channel_0_value, float channel_1_value) {
  Frame frame = StereoFrame(channel_0_value, channel_1_value);
  aec3.AnalyzeRender(frame);
  const std::span<const float> channels[2] = {frame[0][0], frame[0][1]};
  aec3.AnalyzeCapture(channels);
  aec3.ProcessCapture(&frame, false);
}

}  // namespace

SM_TEST(Aec3, EchoCanceller3DetectionOfProperStereo) {
  Aec3Config mono_config;
  mono_config.multi_channel.detect_stereo_content = true;
  mono_config.multi_channel.stereo_detection_threshold = 0.0f;
  mono_config.multi_channel.stereo_detection_hysteresis_seconds = 0.0f;
  std::optional<Aec3Config> multichannel_config = mono_config;
  mono_config.filter.coarse_initial.length_blocks = kNumBlocksForMonoConfig;
  multichannel_config->filter.coarse_initial.length_blocks = kNumBlocksForSurroundConfig;
  EchoCanceller3 aec3(mono_config, multichannel_config, 16000, 2, 2);
  SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
  SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForMonoConfig);
  RunAecInStereo(aec3, 100.0f, 100.0f);
  SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
  SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForMonoConfig);
  RunAecInStereo(aec3, 100.0f, 101.0f);
  SM_EXPECT_TRUE(aec3.StereoRenderProcessingActive());
  SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForSurroundConfig);
  SM_EXPECT_EQ(aec3.num_render_channels_to_aec(), size_t{2});
}

SM_TEST(Aec3, EchoCanceller3DetectionOfProperStereoUsingThreshold) {
  constexpr float kStereoDetectionThreshold = 2.0f;
  Aec3Config mono_config;
  mono_config.multi_channel.detect_stereo_content = true;
  mono_config.multi_channel.stereo_detection_threshold = kStereoDetectionThreshold;
  mono_config.multi_channel.stereo_detection_hysteresis_seconds = 0.0f;
  std::optional<Aec3Config> multichannel_config = mono_config;
  mono_config.filter.coarse_initial.length_blocks = kNumBlocksForMonoConfig;
  multichannel_config->filter.coarse_initial.length_blocks = kNumBlocksForSurroundConfig;
  EchoCanceller3 aec3(mono_config, multichannel_config, 16000, 2, 2);
  SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
  RunAecInStereo(aec3, 100.0f, 100.0f + kStereoDetectionThreshold - 1.0f);
  SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
  SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForMonoConfig);
  RunAecInStereo(aec3, 100.0f, 100.0f + kStereoDetectionThreshold + 10.0f);
  SM_EXPECT_TRUE(aec3.StereoRenderProcessingActive());
  SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForSurroundConfig);
}

SM_TEST(Aec3, EchoCanceller3DetectionOfProperStereoUsingHysteresis) {
  Aec3Config mono_config;
  mono_config.multi_channel.detect_stereo_content = true;
  mono_config.multi_channel.stereo_detection_hysteresis_seconds = 0.5f;
  std::optional<Aec3Config> surround_config = mono_config;
  mono_config.filter.coarse_initial.length_blocks = kNumBlocksForMonoConfig;
  surround_config->filter.coarse_initial.length_blocks = kNumBlocksForSurroundConfig;
  EchoCanceller3 aec3(mono_config, surround_config, 16000, 2, 2);
  SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
  RunAecInStereo(aec3, 100.0f, 100.0f);
  SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
  constexpr int kNumFramesPerSecond = 100;
  bool ok = true;
  for (int k = 0; k < static_cast<int>(kNumFramesPerSecond * mono_config.multi_channel.stereo_detection_hysteresis_seconds);
       ++k) {
    RunAecInStereo(aec3, 100.0f, 101.0f);
    ok = ok && !aec3.StereoRenderProcessingActive() &&
         aec3.active_config().filter.coarse_initial.length_blocks == kNumBlocksForMonoConfig;
  }
  SM_EXPECT_TRUE(ok);
  RunAecInStereo(aec3, 100.0f, 101.0f);
  SM_EXPECT_TRUE(aec3.StereoRenderProcessingActive());
  SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForSurroundConfig);
}

SM_TEST(Aec3, EchoCanceller3StereoContentDetectionForMonoSignals) {
  Aec3Config mono_config;
  std::optional<Aec3Config> multichannel_config;
  for (bool detect_stereo_content : {false, true}) {
    mono_config.multi_channel.detect_stereo_content = detect_stereo_content;
    multichannel_config = mono_config;
    mono_config.filter.coarse_initial.length_blocks = kNumBlocksForMonoConfig;
    multichannel_config->filter.coarse_initial.length_blocks = kNumBlocksForSurroundConfig;
    EchoCanceller3 aec3(mono_config, multichannel_config, 16000, 1, 1);
    SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
    SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForMonoConfig);
    Frame frame{{std::vector<float>(kFrameSize, 100.0f)}};
    aec3.AnalyzeRender(frame);
    const std::span<const float> channels[1] = {frame[0][0]};
    aec3.AnalyzeCapture(channels);
    aec3.ProcessCapture(&frame, false);
    SM_EXPECT_FALSE(aec3.StereoRenderProcessingActive());
    SM_EXPECT_EQ(aec3.active_config().filter.coarse_initial.length_blocks, kNumBlocksForMonoConfig);
  }
}

// (ours) A stereo far end (two independent talkers, one per channel) through
// two echo paths into a mono microphone, at 16, 32 and 48 kHz with APM's
// default multichannel config. After 2 s of stereo the AEC is re-created on
// both render channels and keeps removing the echo (34.4 dB on this signal;
// the mono AEC on the downmix reaches about the same here, since the
// suppressor removes most of it, so this checks the switch-over, not a gain).
SM_TEST(Aec3, EchoCanceller3RemovesStereoRenderEcho) {
  for (int rate : {16000, 32000, 48000}) {
    const size_t num_bands = NumBandsForRate(rate);
    const int fs = 16000;  // the lowest band, where the echo is
    const double seconds = 12.0;
    auto left = sim::SpeechLike(fs, seconds, 3, true);
    auto right = sim::SpeechLike(fs, seconds, 4, true);
    auto conv_l = sim::Convolve(left, sim::RoomImpulseResponse(fs, 40, 100, 5));
    auto conv_r = sim::Convolve(right, sim::RoomImpulseResponse(fs, 40, 100, 6));
    const size_t delay = 640;  // 40 ms
    EchoCanceller3 aec3(Aec3Config{}, CreateDefaultMultichannelConfig(), rate, 2, 1);
    double in_energy = 0, out_energy = 0;
    const size_t frames = left.size() / kFrameSize;
    for (size_t f = 0; f < frames; ++f) {
      Frame render(num_bands, std::vector<std::vector<float>>(2, std::vector<float>(kFrameSize, 0.f)));
      Frame capture(num_bands, std::vector<std::vector<float>>(1, std::vector<float>(kFrameSize, 0.f)));
      for (size_t k = 0; k < kFrameSize; ++k) {
        const size_t i = f * kFrameSize + k;
        render[0][0][k] = 32767.f * left[i];
        render[0][1][k] = 32767.f * right[i];
        capture[0][0][k] = i >= delay ? 0.5f * 32767.f * (conv_l[i - delay] + conv_r[i - delay]) : 0.f;
      }
      aec3.AnalyzeRender(render);
      const std::span<const float> channels[1] = {capture[0][0]};
      aec3.AnalyzeCapture(channels);
      const bool measured = f >= 8 * 100;  // the last 4 s
      if (measured) {
        for (float v : capture[0][0]) in_energy += static_cast<double>(v) * v;
      }
      aec3.ProcessCapture(&capture, false);
      if (measured) {
        for (float v : capture[0][0]) out_energy += static_cast<double>(v) * v;
      }
    }
    SM_EXPECT_TRUE(aec3.StereoRenderProcessingActive());
    SM_EXPECT_EQ(aec3.num_render_channels_to_aec(), size_t{2});
    const double reduction_db = 10.0 * std::log10(in_energy / std::max(out_energy, 1e-9));
    SM_EXPECT_GE(reduction_db, 20.0);
  }
}
