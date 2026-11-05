// C17 (issue #1162): libwebrtc M157's simulcast_unittest.cc (video/config/), the encoder_stream_factory_unittest.cc
// cases that apply (no scale_resolution_down_to, no scalability modes, no NormalizeSimulcastResolution) and
// simulcast_rate_allocator_unittest.cc (modules/video_coding/utility/; not the VP8 frame-buffer-controller mocks),
// with libwebrtc's expected values.
#include <limits>
#include <memory>

#include "sm/video/simulcast.h"
#include "sm/video/simulcast_rate_allocator.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::video;

namespace {
constexpr bool kScreenshare = true;
constexpr int kDefaultTemporalLayers = 3;

std::vector<VideoStream> GetSimulcastBitrates720p() {
  std::vector<VideoStream> s(3);
  s[0].min_bitrate_bps = 30000;
  s[0].target_bitrate_bps = 150000;
  s[0].max_bitrate_bps = 200000;
  s[1].min_bitrate_bps = 150000;
  s[1].target_bitrate_bps = 500000;
  s[1].max_bitrate_bps = 700000;
  s[2].min_bitrate_bps = 600000;
  s[2].target_bitrate_bps = 2500000;
  s[2].max_bitrate_bps = 2500000;
  return s;
}

std::vector<Resolution> CreateResolutions(int max_width, int max_height, int num_streams) {
  std::vector<Resolution> r(static_cast<size_t>(num_streams));
  for (int i = 0; i < num_streams; ++i) {
    r[static_cast<size_t>(i)].width = max_width >> (num_streams - i - 1);
    r[static_cast<size_t>(i)].height = max_height >> (num_streams - i - 1);
  }
  return r;
}

std::vector<Resolution> Resolutions(const std::vector<VideoStream>& streams) {
  std::vector<Resolution> r;
  for (const auto& s : streams) r.push_back({s.width, s.height});
  return r;
}

const SimulcastTrials kTrials;
}  // namespace

// ---- video/config/simulcast_unittest.cc ------------------------------------

SM_TEST(Simulcast, TotalMaxBitrateIsZeroForNoStreams) {
  std::vector<VideoStream> streams;
  SM_EXPECT_EQ(GetTotalMaxBitrate(streams).bps(), 0);
}

SM_TEST(Simulcast, GetTotalMaxBitrateForSingleStream) {
  std::vector<VideoStream> streams(1);
  streams[0].max_bitrate_bps = 100000;
  SM_EXPECT_EQ(GetTotalMaxBitrate(streams).bps(), 100000);
}

SM_TEST(Simulcast, GetTotalMaxBitrateForMultipleStreams) {
  std::vector<VideoStream> streams(3);
  streams[0].target_bitrate_bps = 100000;
  streams[1].target_bitrate_bps = 200000;
  streams[2].max_bitrate_bps = 400000;
  SM_EXPECT_EQ(GetTotalMaxBitrate(streams).bps(), 700000);
}

SM_TEST(Simulcast, BandwidthAboveTotalMaxBitrateGivenToHighestStream) {
  std::vector<VideoStream> streams(3);
  streams[0].target_bitrate_bps = 100000;
  streams[1].target_bitrate_bps = 200000;
  streams[2].max_bitrate_bps = 400000;
  const DataRate one_bps = DataRate::BitsPerSec(1);
  // No bitrate above the total max to give to the highest stream.
  const DataRate max_total = GetTotalMaxBitrate(streams);
  BoostMaxSimulcastLayer(max_total, &streams);
  SM_EXPECT_EQ(streams[2].max_bitrate_bps, 400000);
  SM_EXPECT_TRUE(GetTotalMaxBitrate(streams) == max_total);
  // The bitrate above the total max goes to the highest stream.
  BoostMaxSimulcastLayer(max_total + one_bps, &streams);
  SM_EXPECT_EQ(streams[2].max_bitrate_bps, 400001);
  SM_EXPECT_TRUE(GetTotalMaxBitrate(streams) == max_total + one_bps);
}

SM_TEST(Simulcast, GetConfig) {
  const auto expected = GetSimulcastBitrates720p();
  const auto res = CreateResolutions(1280, 720, 3);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[0].width, 320);
  SM_EXPECT_EQ(streams[0].height, 180);
  SM_EXPECT_EQ(streams[1].width, 640);
  SM_EXPECT_EQ(streams[1].height, 360);
  SM_EXPECT_EQ(streams[2].width, 1280);
  SM_EXPECT_EQ(streams[2].height, 720);
  for (size_t i = 0; i < streams.size(); ++i) {
    SM_EXPECT_EQ(streams[i].num_temporal_layers.value_or(-1), kDefaultTemporalLayers);
    SM_EXPECT_EQ(streams[i].max_framerate, kDefaultVideoMaxFramerate);
    SM_EXPECT_EQ(streams[i].max_qp, -1);
    SM_EXPECT_EQ(streams[i].min_bitrate_bps, expected[i].min_bitrate_bps);
    SM_EXPECT_EQ(streams[i].target_bitrate_bps, expected[i].target_bitrate_bps);
    SM_EXPECT_EQ(streams[i].max_bitrate_bps, expected[i].max_bitrate_bps);
    SM_EXPECT_TRUE(streams[i].active);
  }
}

SM_TEST(Simulcast, GetConfigH265) {
  const int kMin[] = {30000, 193000, 481000}, kTarget[] = {142000, 420000, 1524000}, kMax[] = {142000, 420000, 1524000};
  const auto res = CreateResolutions(1280, 720, 3);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, VideoCodecType::kH265);
  SM_ASSERT_EQ(streams.size(), 3u);
  for (size_t i = 0; i < streams.size(); ++i) {
    SM_EXPECT_EQ(streams[i].num_temporal_layers.value_or(-1), 1);  // kDefaultH265TemporalLayers.
    SM_EXPECT_EQ(streams[i].min_bitrate_bps, kMin[i]);
    SM_EXPECT_EQ(streams[i].target_bitrate_bps, kTarget[i]);
    SM_EXPECT_EQ(streams[i].max_bitrate_bps, kMax[i]);
  }
}

SM_TEST(Simulcast, GetConfigWithBaseHeavyVP8TL3RateAllocation) {
  SimulcastTrials trials;
  trials.base_heavy_tl3_rate_allocation = true;
  const auto expected = GetSimulcastBitrates720p();
  const auto res = CreateResolutions(1280, 720, 3);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, trials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[0].min_bitrate_bps, expected[0].min_bitrate_bps);
  SM_EXPECT_EQ(streams[0].target_bitrate_bps, static_cast<int>(0.4 * expected[0].target_bitrate_bps / 0.6));
  SM_EXPECT_EQ(streams[0].max_bitrate_bps, static_cast<int>(0.4 * expected[0].max_bitrate_bps / 0.6));
  for (size_t i = 1; i < streams.size(); ++i) {
    SM_EXPECT_EQ(streams[i].min_bitrate_bps, expected[i].min_bitrate_bps);
    SM_EXPECT_EQ(streams[i].target_bitrate_bps, expected[i].target_bitrate_bps);
    SM_EXPECT_EQ(streams[i].max_bitrate_bps, expected[i].max_bitrate_bps);
  }
}

SM_TEST(Simulcast, GetConfigWithLimitedMaxLayers) {
  const auto res = CreateResolutions(1280, 720, 2);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 2u);
  SM_EXPECT_EQ(streams[0].width, 640);
  SM_EXPECT_EQ(streams[0].height, 360);
  SM_EXPECT_EQ(streams[1].width, 1280);
  SM_EXPECT_EQ(streams[1].height, 720);
}

SM_TEST(Simulcast, GetConfigForScreenshareSimulcast) {
  const std::vector<Resolution> res{{1400, 800}, {1400, 800}, {1400, 800}};
  const auto streams = GetSimulcastConfig(res, kScreenshare, true, kTrials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 2u);
  for (const auto& s : streams) {
    SM_EXPECT_EQ(s.width, 1400);  // Screen content is never scaled.
    SM_EXPECT_EQ(s.height, 800);
    SM_EXPECT_EQ(s.max_qp, -1);
    SM_EXPECT_TRUE(s.active);
    SM_EXPECT_GT(s.num_temporal_layers.value_or(0), 1);
    SM_EXPECT_GT(s.max_framerate, 0);
    SM_EXPECT_GT(s.min_bitrate_bps, 0);
    SM_EXPECT_GT(s.target_bitrate_bps, s.min_bitrate_bps);
    SM_EXPECT_GE(s.max_bitrate_bps, s.target_bitrate_bps);
  }
}

SM_TEST(Simulcast, GetConfigForScreenshareSimulcastWithLimitedMaxLayers) {
  const std::vector<Resolution> res{{1400, 800}};
  SM_EXPECT_EQ(GetSimulcastConfig(res, kScreenshare, true, kTrials, VideoCodecType::kVP8).size(), 1u);
}

SM_TEST(Simulcast, AveragesBitratesForNonStandardResolution) {
  const std::vector<Resolution> res{{900, 800}};
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 1u);
  SM_EXPECT_EQ(streams[0].width, 900);
  SM_EXPECT_EQ(streams[0].height, 800);
  SM_EXPECT_EQ(streams[0].max_bitrate_bps, 1850000);
  SM_EXPECT_EQ(streams[0].target_bitrate_bps, 1850000);
  SM_EXPECT_EQ(streams[0].min_bitrate_bps, 475000);
}

SM_TEST(Simulcast, BitratesForCloseToStandardResolution) {
  const auto expected = GetSimulcastBitrates720p();
  const auto res = CreateResolutions(1280, 716, 3);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[2].width, 1280);
  SM_EXPECT_EQ(streams[2].height, 716);
  for (size_t i = 0; i < streams.size(); ++i) {
    SM_EXPECT_NEAR(streams[i].max_bitrate_bps, expected[i].max_bitrate_bps, 20000);
    SM_EXPECT_NEAR(streams[i].target_bitrate_bps, expected[i].target_bitrate_bps, 20000);
    SM_EXPECT_NEAR(streams[i].min_bitrate_bps, expected[i].min_bitrate_bps, 20000);
  }
}

SM_TEST(Simulcast, MaxLayersWithRoundUpDisabled) {
  SimulcastTrials t;
  t.layer_limit_max_ratio = 0.0;
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 960, 540, t, VideoCodecType::kVP8), 3u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 960, 539, t, VideoCodecType::kVP8), 2u);  // <960x540: 2 layers.
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 270, t, VideoCodecType::kVP8), 2u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 269, t, VideoCodecType::kVP8), 1u);  // <480x270: 1 layer.
}

SM_TEST(Simulcast, MaxLayersWithDefaultRoundUpRatio) {
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 960, 540, kTrials, VideoCodecType::kVP8), 3u);
  // Lowest cropped height where the layer count of the higher resolution is used.
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 960, 512, kTrials, VideoCodecType::kVP8), 3u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 960, 508, kTrials, VideoCodecType::kVP8), 2u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 270, kTrials, VideoCodecType::kVP8), 2u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 256, kTrials, VideoCodecType::kVP8), 2u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 254, kTrials, VideoCodecType::kVP8), 1u);
}

SM_TEST(Simulcast, MaxLayersWithRoundUpRatio) {
  SimulcastTrials t;
  t.layer_limit_max_ratio = 0.13;
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 270, t, VideoCodecType::kVP8), 2u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 252, t, VideoCodecType::kVP8), 2u);
  SM_EXPECT_EQ(LimitSimulcastLayerCount(1, 3, 480, 250, t, VideoCodecType::kVP8), 1u);
}

SM_TEST(Simulcast, BitratesInterpolatedForResBelow180p) {
  SimulcastTrials t;
  t.lowres_bitrate_interpolation = true;
  const auto res = CreateResolutions(960, 540, 3);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, t, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[0].width, 240);
  SM_EXPECT_EQ(streams[0].height, 135);
  SM_EXPECT_EQ(streams[0].max_bitrate_bps, 112500);
  SM_EXPECT_EQ(streams[0].target_bitrate_bps, 84375);
  SM_EXPECT_EQ(streams[0].min_bitrate_bps, 30000);
}

SM_TEST(Simulcast, BitratesConsistentForVerySmallRes) {
  SimulcastTrials t;
  t.lowres_bitrate_interpolation = true;
  const std::vector<Resolution> res{{1, 1}};
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, t, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 1u);
  SM_EXPECT_EQ(streams[0].max_bitrate_bps, 30000);
  SM_EXPECT_EQ(streams[0].target_bitrate_bps, 30000);
  SM_EXPECT_EQ(streams[0].min_bitrate_bps, 30000);
}

SM_TEST(Simulcast, BitratesNotInterpolatedForResBelow180pWhenDisabledTrialSet) {
  const auto res = CreateResolutions(960, 540, 3);
  const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, VideoCodecType::kVP8);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[0].width, 240);
  SM_EXPECT_EQ(streams[0].height, 135);
  SM_EXPECT_EQ(streams[0].max_bitrate_bps, 200000);
  SM_EXPECT_EQ(streams[0].target_bitrate_bps, 150000);
  SM_EXPECT_EQ(streams[0].min_bitrate_bps, 30000);
}

// BitrateLimitsTest (Vp8H264 and Av1Vp9H265).
SM_TEST(Simulcast, VerifyBitrateLimits) {
  struct Case {
    VideoCodecType codec;
    int width, height;
    int min_kbps[3], max_kbps[3];
  };
  const Case cases[] = {
      {VideoCodecType::kVP8, 1920, 1080, {150, 350, 800}, {450, 1200, 5000}},
      {VideoCodecType::kVP8, 1280, 720, {30, 150, 600}, {200, 700, 2500}},
      {VideoCodecType::kVP8, 960, 540, {30, 150, 350}, {200, 450, 1200}},
      {VideoCodecType::kH264, 1920, 1080, {150, 350, 800}, {450, 1200, 5000}},
      {VideoCodecType::kH264, 1280, 720, {30, 150, 600}, {200, 700, 2500}},
      {VideoCodecType::kH264, 960, 540, {30, 150, 350}, {200, 450, 1200}},
      {VideoCodecType::kH265, 1920, 1080, {121, 337, 769}, {257, 879, 3367}},
      {VideoCodecType::kAV1, 1280, 720, {30, 193, 481}, {142, 420, 1524}},
      {VideoCodecType::kVP9, 960, 540, {30, 121, 337}, {101, 257, 879}},
  };
  for (const Case& c : cases) {
    const auto res = CreateResolutions(c.width, c.height, 3);
    const auto streams = GetSimulcastConfig(res, !kScreenshare, true, kTrials, c.codec);
    SM_ASSERT_EQ(streams.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
      SM_EXPECT_EQ(streams[i].min_bitrate_bps / 1000, c.min_kbps[i]);
      SM_EXPECT_EQ(streams[i].max_bitrate_bps / 1000, c.max_kbps[i]);
    }
  }
}

// ---- video/config/encoder_stream_factory_unittest.cc -----------------------

namespace {
VideoEncoderConfig Config(size_t n, VideoCodecType codec = VideoCodecType::kGeneric) {
  VideoEncoderConfig c;
  c.codec_type = codec;
  c.number_of_streams = n;
  c.simulcast_layers.resize(n);
  return c;
}
}  // namespace

SM_TEST(EncoderStreamFactory, ScaleResolutionDownBy) {
  auto c = Config(1);
  c.simulcast_layers[0].scale_resolution_down_by = 2;
  const auto streams = CreateEncoderStreams(kTrials, 1280, 720, c);
  SM_EXPECT_EQ(streams[0].scale_resolution_down_by, 2.0);
  SM_EXPECT_TRUE(Resolutions(streams) == (std::vector<Resolution>{{640, 360}}));
}

SM_TEST(EncoderStreamFactory, ScaleResolutionDownByWithZeroOrNegativeScaleFactor) {
  for (double f : {0.0, -2.0}) {
    auto c = Config(1);
    c.simulcast_layers[0].scale_resolution_down_by = f;
    SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 1280, 720, c)) == (std::vector<Resolution>{{1280, 720}}));
  }
}

SM_TEST(EncoderStreamFactory, ScaleResolutionDownByWithPartiallySetScaleFactors) {
  auto c = Config(2);
  c.simulcast_layers[0].scale_resolution_down_by = 2;
  c.simulcast_layers[1].scale_resolution_down_by = -1;
  SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 1280, 720, c)) ==
                 (std::vector<Resolution>{{640, 360}, {1280, 720}}));
}

SM_TEST(EncoderStreamFactory, ScaleDownByWithUnalignedResolution) {
  {
    auto c = Config(2);
    c.simulcast_layers[0].scale_resolution_down_by = 1;
    c.simulcast_layers[1].scale_resolution_down_by = 2;
    SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 513, 1025, c)) ==
                   (std::vector<Resolution>{{512, 1024}, {256, 512}}));
  }
  {
    // Kept unchanged if a factor is not a power of two.
    auto c = Config(2);
    c.simulcast_layers[0].scale_resolution_down_by = 1;
    c.simulcast_layers[1].scale_resolution_down_by = 3;
    SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 513, 1025, c)) ==
                   (std::vector<Resolution>{{513, 1025}, {171, 342}}));
  }
  {
    auto c = Config(1);
    c.simulcast_layers[0].scale_resolution_down_by = 1024;
    SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 513, 1025, c)) == (std::vector<Resolution>{{16, 32}}));
  }
  {
    auto c = Config(2);
    c.simulcast_layers[0].scale_resolution_down_by = 1;
    c.simulcast_layers[1].scale_resolution_down_by = 1;
    SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 513, 1025, c)) ==
                   (std::vector<Resolution>{{513, 1025}, {513, 1025}}));
  }
}

SM_TEST(EncoderStreamFactory, ScaleDownByNormalizesResolutionUpToMaxScaleFactor) {
  auto c = Config(3);
  c.simulcast_layers[0].scale_resolution_down_by = 1;
  c.simulcast_layers[1].scale_resolution_down_by = 1;
  c.simulcast_layers[2].scale_resolution_down_by = 2;
  SM_EXPECT_TRUE(Resolutions(CreateEncoderStreams(kTrials, 511, 1027, c)) ==
                 (std::vector<Resolution>{{510, 1026}, {510, 1026}, {255, 513}}));
}

SM_TEST(EncoderStreamFactory, BitratePriority) {
  auto c = Config(2);
  c.bitrate_priority = 0.123;
  const auto streams = CreateEncoderStreams(kTrials, 640, 360, c);
  SM_ASSERT_EQ(streams.size(), 2u);
  SM_EXPECT_EQ(streams[0].bitrate_priority.value_or(-1), 0.123);
  SM_EXPECT_FALSE(streams[1].bitrate_priority.has_value());
}

SM_TEST(EncoderStreamFactory, SetsMinBitrateToDefaultOrExperimentalValue) {
  const auto c = Config(2);
  SM_EXPECT_EQ(CreateEncoderStreams(kTrials, 1920, 1080, c)[0].min_bitrate_bps, kDefaultMinVideoBitrateBps);
  SimulcastTrials t;
  t.experimental_min_bitrate = DataRate::KilobitsPerSec(1);  // WebRTC-Video-MinVideoBitrate/Enabled,br:1kbps/
  SM_EXPECT_EQ(CreateEncoderStreams(t, 1920, 1080, c)[0].min_bitrate_bps, 1000);
}

namespace {
std::vector<Resolution> StreamResolutions(size_t n, Resolution r, size_t first_active = 0, bool screencast = false,
                                          SimulcastTrials t = {}) {
  auto c = Config(n, VideoCodecType::kVP8);
  for (size_t i = 0; i < n; ++i) c.simulcast_layers[i].active = i >= first_active;
  if (screencast) {
    c.content_type = VideoEncoderConfig::ContentType::kScreen;
    c.legacy_conference_mode = true;
  }
  return Resolutions(CreateEncoderStreams(t, r.width, r.height, c));
}
}  // namespace

SM_TEST(EncoderStreamFactory, ResolutionAlignment) {
  SM_EXPECT_TRUE(StreamResolutions(2, {516, 526}) == (std::vector<Resolution>{{258, 263}, {516, 526}}));
  // The top stream is rounded down to a multiple of 2^(n-1) so the smallest one has whole dimensions.
  SM_EXPECT_TRUE(StreamResolutions(2, {515, 517}) == (std::vector<Resolution>{{257, 258}, {514, 516}}));
}

SM_TEST(EncoderStreamFactory, StreamCountByResolution) {
  SM_EXPECT_EQ(StreamResolutions(3, {1000, 1000}).size(), 3u);
  SM_EXPECT_EQ(StreamResolutions(3, {100, 100}).size(), 1u);
  SM_EXPECT_EQ(StreamResolutions(3, {100, 100}, 1).size(), 2u);  // Down to the first active stream.
  SM_EXPECT_EQ(StreamResolutions(3, {100, 100}, 0, true).size(), 2u);  // Legacy screencast keeps 2.
  SimulcastTrials no_limit;
  no_limit.legacy_layer_limit = false;
  SM_EXPECT_EQ(StreamResolutions(3, {100, 100}, 0, false, no_limit).size(), 3u);
  SM_EXPECT_TRUE(StreamResolutions(3, {640, 360}) == (std::vector<Resolution>{{320, 180}, {640, 360}}));
}

SM_TEST(EncoderStreamFactory, SimulcastMaxBitrateZeroOnMiddleLayer) {
  auto c = Config(3, VideoCodecType::kVP8);
  c.simulcast_layers[1].max_bitrate_bps = 0;
  const auto streams = CreateEncoderStreams(kTrials, 1280, 720, c);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[1].max_bitrate_bps, 0);
  SM_EXPECT_EQ(streams[1].min_bitrate_bps, 0);
  SM_EXPECT_EQ(streams[1].target_bitrate_bps, 0);
}

SM_TEST(EncoderStreamFactory, SimulcastMaxBitrateZeroOnHighestLayerIsNotBoosted) {
  auto c = Config(3, VideoCodecType::kVP8);
  c.max_bitrate_bps = 5000000;
  c.simulcast_layers[2].max_bitrate_bps = 0;
  const auto streams = CreateEncoderStreams(kTrials, 1280, 720, c);
  SM_ASSERT_EQ(streams.size(), 3u);
  SM_EXPECT_EQ(streams[2].max_bitrate_bps, 0);
}

SM_TEST(EncoderStreamFactory, SimulcastBoostsTopLayerToStreamMax) {
  auto c = Config(3, VideoCodecType::kH264);
  c.max_bitrate_bps = 5000000;
  const auto streams = CreateEncoderStreams(kTrials, 1280, 720, c);
  SM_ASSERT_EQ(streams.size(), 3u);
  // 150 + 500 + 2500 = 3150 kbps; the 1850 kbps left go to the top layer.
  SM_EXPECT_EQ(streams[2].max_bitrate_bps, 2500000 + 1850000);
  SM_EXPECT_EQ(streams[0].num_temporal_layers.value_or(-1), 1);  // H.264: no temporal layers.
  SM_EXPECT_EQ(streams[0].max_qp, kDefaultVideoMaxQpH26x);
}

SM_TEST(EncoderStreamFactory, SinglecastMaxBitrate) {
  {
    auto c = Config(1);
    c.simulcast_layers[0].max_bitrate_bps = 0;  // Zero is not unset.
    const auto s = CreateEncoderStreams(kTrials, 1280, 720, c);
    SM_EXPECT_EQ(s[0].max_bitrate_bps, 0);
    SM_EXPECT_EQ(s[0].min_bitrate_bps, 0);
  }
  {
    const auto s = CreateEncoderStreams(kTrials, 1280, 720, Config(1));  // Unset: the default for the size.
    SM_EXPECT_EQ(s[0].max_bitrate_bps, 2500000);
    SM_EXPECT_EQ(s[0].min_bitrate_bps, kDefaultMinVideoBitrateBps);
  }
  {
    auto c = Config(1);
    c.simulcast_layers[0].max_bitrate_bps = 0;
    c.max_bitrate_bps = 1000000;  // The SDP max does not raise a zero.
    SM_EXPECT_EQ(CreateEncoderStreams(kTrials, 1280, 720, c)[0].max_bitrate_bps, 0);
  }
  {
    auto c = Config(1);
    c.simulcast_layers[0].max_bitrate_bps = 0;
    c.simulcast_layers[0].min_bitrate_bps = 100000;  // Nor does the min.
    SM_EXPECT_EQ(CreateEncoderStreams(kTrials, 1280, 720, c)[0].max_bitrate_bps, 0);
  }
}

SM_TEST(EncoderStreamFactory, FirstInactiveLayerLendsItsMinimumToTheFirstActiveOne) {
  auto c = Config(3, VideoCodecType::kH264);
  c.simulcast_layers[0].active = false;
  c.simulcast_layers[1].active = false;
  const auto s = CreateEncoderStreams(kTrials, 1280, 720, c);
  SM_ASSERT_EQ(s.size(), 3u);
  SM_EXPECT_EQ(s[2].min_bitrate_bps, 30000);
}

// ---- modules/video_coding/utility/simulcast_rate_allocator_unittest.cc ------

namespace {
constexpr uint32_t kMinBitrateKbps = 50;
constexpr uint32_t kLegacyScreenshareTargetBitrateKbps = 200;
constexpr uint32_t kLegacyScreenshareMaxBitrateKbps = 1000;
constexpr uint32_t kSimulcastScreenshareMinBitrateKbps = 600;
constexpr uint32_t kSimulcastScreenshareMaxBitrateKbps = 1250;
constexpr double kDefaultHysteresis = 1.2;
constexpr uint32_t kU32Max = std::numeric_limits<uint32_t>::max();

struct AllocatorFixture {
  VideoCodec codec;
  std::unique_ptr<SimulcastRateAllocator> allocator;
  AllocatorFixture() {
    codec.codec_type = VideoCodecType::kVP8;
    codec.min_bitrate_kbps = kMinBitrateKbps;
    codec.max_bitrate_kbps = kLegacyScreenshareMaxBitrateKbps;
    codec.active = true;
    Create();
  }
  void Create(bool base_heavy = false) { allocator = std::make_unique<SimulcastRateAllocator>(codec, base_heavy); }
  void Setup3TL() {
    codec.max_bitrate_kbps = 0;
    codec.codec_temporal_layers = 3;
  }
  void Setup1SL3TL(const std::vector<bool>& a) {
    Setup3TL();
    codec.number_of_simulcast_streams = 2;
    codec.simulcast_stream[0] = {0, 0, 0, 3, 500, 100, 10, 0, a[0]};
  }
  void Setup2SL3TL(const std::vector<bool>& a) {
    Setup1SL3TL(a);
    codec.number_of_simulcast_streams = 2;
    codec.simulcast_stream[1] = {0, 0, 0, 3, 1000, 500, 50, 0, a[1]};
  }
  void Setup3SL3TL(const std::vector<bool>& a) {
    Setup2SL3TL(a);
    codec.number_of_simulcast_streams = 3;
    codec.simulcast_stream[2] = {0, 0, 0, 3, 4000, 3000, 2000, 0, a[2]};
  }
  VideoBitrateAllocation Get(uint32_t kbps) {
    return allocator->Allocate(DataRate::KilobitsPerSec(static_cast<int64_t>(kbps)), 30);
  }
  const SimulcastStream& S(size_t i) const { return codec.simulcast_stream[i]; }
};

// ExpectEqual: each spatial layer's sum in kbps; a zero layer is unused; the sum matches.
void ExpectEqual(const std::vector<uint32_t>& expected, const VideoBitrateAllocation& actual) {
  uint32_t sum = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    const uint32_t layer = actual.GetSpatialLayerSum(i);
    if (layer == 0) SM_EXPECT_FALSE(actual.IsSpatialLayerUsed(i));
    SM_EXPECT_EQ(layer, expected[i] * 1000u);
    sum += layer;
  }
  SM_EXPECT_EQ(actual.get_sum_bps(), sum);
}
}  // namespace

SM_TEST(SimulcastRateAllocator, NoSimulcastBelowMin) {
  AllocatorFixture f;
  for (uint32_t r : {kMinBitrateKbps - 1, 1u, 0u}) ExpectEqual({kMinBitrateKbps}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, NoSimulcastAboveMax) {
  AllocatorFixture f;
  ExpectEqual({f.codec.max_bitrate_kbps}, f.Get(f.codec.max_bitrate_kbps + 1));
  ExpectEqual({f.codec.max_bitrate_kbps}, f.Get(kU32Max));
}

SM_TEST(SimulcastRateAllocator, NoSimulcastNoMax) {
  AllocatorFixture f;
  f.codec.max_bitrate_kbps = 0;
  f.Create();
  const uint32_t kMax = VideoBitrateAllocation::kMaxBitrateBps / 1000;
  ExpectEqual({kMax}, f.Get(kMax));
}

SM_TEST(SimulcastRateAllocator, NoSimulcastWithinLimits) {
  AllocatorFixture f;
  for (uint32_t r = f.codec.min_bitrate_kbps; r <= f.codec.max_bitrate_kbps; ++r) ExpectEqual({r}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, NoSimulcastInactive) {
  AllocatorFixture f;
  f.codec.active = false;
  f.Create();
  for (uint32_t r : {kMinBitrateKbps - 10, kLegacyScreenshareTargetBitrateKbps, kLegacyScreenshareMaxBitrateKbps + 10}) {
    ExpectEqual({0}, f.Get(r));
  }
}

SM_TEST(SimulcastRateAllocator, SingleSimulcastBelowMin) {
  AllocatorFixture f;
  f.codec.number_of_simulcast_streams = 1;
  const uint32_t kMin = f.codec.min_bitrate_kbps - 10;
  f.codec.simulcast_stream[0].min_bitrate_kbps = kMin;
  f.codec.simulcast_stream[0].target_bitrate_kbps = kLegacyScreenshareTargetBitrateKbps;
  f.codec.simulcast_stream[0].active = true;
  f.Create();
  for (uint32_t r : {kMin - 1, 1u, 0u}) ExpectEqual({kMin}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, SignalsBwLimited) {
  AllocatorFixture f;
  f.Setup3SL3TL({true, true, true});
  f.Create();
  SM_EXPECT_TRUE(f.Get(f.S(0).min_bitrate_kbps - 10).is_bw_limited());
  SM_EXPECT_TRUE(f.Get(f.S(0).target_bitrate_kbps).is_bw_limited());
  SM_EXPECT_TRUE(f.Get(f.S(0).target_bitrate_kbps + f.S(1).min_bitrate_kbps).is_bw_limited());
  SM_EXPECT_FALSE(f.Get(f.S(0).target_bitrate_kbps + f.S(1).target_bitrate_kbps +
                        static_cast<uint32_t>(f.S(2).min_bitrate_kbps * kDefaultHysteresis + 0.5))
                      .is_bw_limited());
  SM_EXPECT_FALSE(f.Get(100000).is_bw_limited());
}

SM_TEST(SimulcastRateAllocator, SingleSimulcastAboveMax) {
  AllocatorFixture f;
  f.codec.number_of_simulcast_streams = 1;
  f.codec.simulcast_stream[0].min_bitrate_kbps = kMinBitrateKbps;
  const uint32_t kMax = f.codec.simulcast_stream[0].max_bitrate_kbps + 1000;
  f.codec.simulcast_stream[0].max_bitrate_kbps = kMax;
  f.codec.simulcast_stream[0].active = true;
  f.Create();
  for (uint32_t r : {kMax, kMax + 1, kU32Max}) ExpectEqual({kMax}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, SingleSimulcastWithinLimits) {
  AllocatorFixture f;
  f.codec.number_of_simulcast_streams = 1;
  f.codec.simulcast_stream[0].min_bitrate_kbps = kMinBitrateKbps;
  f.codec.simulcast_stream[0].target_bitrate_kbps = kLegacyScreenshareTargetBitrateKbps;
  f.codec.simulcast_stream[0].max_bitrate_kbps = kLegacyScreenshareMaxBitrateKbps;
  f.codec.simulcast_stream[0].active = true;
  f.Create();
  for (uint32_t r = kMinBitrateKbps; r <= kLegacyScreenshareMaxBitrateKbps; ++r) ExpectEqual({r}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, Regular3TLTemporalRateAllocation) {
  AllocatorFixture f;
  f.Setup3SL3TL({true, true, true});
  f.Create();
  const auto a = f.Get(kMinBitrateKbps);
  // 40/20/40.
  SM_EXPECT_EQ(a.GetBitrate(0, 0) / 1000, static_cast<uint32_t>(0.4 * kMinBitrateKbps));
  SM_EXPECT_EQ(a.GetBitrate(0, 1) / 1000, static_cast<uint32_t>(0.2 * kMinBitrateKbps));
  SM_EXPECT_EQ(a.GetBitrate(0, 2) / 1000, static_cast<uint32_t>(0.4 * kMinBitrateKbps));
}

SM_TEST(SimulcastRateAllocator, BaseHeavy3TLTemporalRateAllocation) {
  AllocatorFixture f;
  f.Setup3SL3TL({true, true, true});
  f.Create(/*base_heavy=*/true);
  const auto a = f.Get(kMinBitrateKbps);
  // 60/20/20.
  SM_EXPECT_EQ(a.GetBitrate(0, 0) / 1000, static_cast<uint32_t>(0.6 * kMinBitrateKbps));
  SM_EXPECT_EQ(a.GetBitrate(0, 1) / 1000, static_cast<uint32_t>(0.2 * kMinBitrateKbps));
  SM_EXPECT_EQ(a.GetBitrate(0, 2) / 1000, static_cast<uint32_t>(0.2 * kMinBitrateKbps));
}

SM_TEST(SimulcastRateAllocator, SingleSimulcastInactive) {
  AllocatorFixture f;
  f.codec.number_of_simulcast_streams = 1;
  f.codec.simulcast_stream[0].min_bitrate_kbps = kMinBitrateKbps;
  f.codec.simulcast_stream[0].target_bitrate_kbps = kLegacyScreenshareTargetBitrateKbps;
  f.codec.simulcast_stream[0].max_bitrate_kbps = kLegacyScreenshareMaxBitrateKbps;
  f.codec.simulcast_stream[0].active = false;
  f.Create();
  for (uint32_t r : {kMinBitrateKbps - 10, kLegacyScreenshareTargetBitrateKbps, kLegacyScreenshareMaxBitrateKbps + 10}) {
    ExpectEqual({0}, f.Get(r));
  }
}

SM_TEST(SimulcastRateAllocator, OneToThreeStreams) {
  AllocatorFixture f;
  f.Setup3SL3TL({true, true, true});
  f.Create();
  ExpectEqual({f.S(0).min_bitrate_kbps, 0, 0}, f.Get(f.S(0).min_bitrate_kbps));  // One stream at its min.
  ExpectEqual({f.S(0).target_bitrate_kbps, 0, 0}, f.Get(f.S(0).target_bitrate_kbps));
  const uint32_t two = f.S(0).target_bitrate_kbps + static_cast<uint32_t>(f.S(1).min_bitrate_kbps * kDefaultHysteresis);
  // Above the first stream's target, below the next one's min.
  ExpectEqual({two - 1, 0, 0}, f.Get(two - 1));
  // Just enough for two streams.
  ExpectEqual({f.S(0).target_bitrate_kbps, two - f.S(0).target_bitrate_kbps, 0}, f.Get(two));
  // Second stream maxed out, not enough for the third.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).max_bitrate_kbps, 0},
              f.Get(f.S(0).target_bitrate_kbps + f.S(1).max_bitrate_kbps));
  const uint32_t three = f.S(0).target_bitrate_kbps + f.S(1).target_bitrate_kbps +
                         static_cast<uint32_t>(f.S(2).min_bitrate_kbps * kDefaultHysteresis);
  // First two maxed out, not enough for the third: nowhere to put the rest.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).max_bitrate_kbps, 0}, f.Get(three - 1));
  // Just enough for all three.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).target_bitrate_kbps,
               static_cast<uint32_t>(f.S(2).min_bitrate_kbps * kDefaultHysteresis)},
              f.Get(three));
  // Third maxed out.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).target_bitrate_kbps, f.S(2).max_bitrate_kbps},
              f.Get(f.S(0).target_bitrate_kbps + f.S(1).target_bitrate_kbps + f.S(2).max_bitrate_kbps));
  // Enough to max out all: the lower streams get their targets.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).target_bitrate_kbps, f.S(2).max_bitrate_kbps},
              f.Get(f.S(0).max_bitrate_kbps + f.S(1).max_bitrate_kbps + f.S(2).max_bitrate_kbps));
}

SM_TEST(SimulcastRateAllocator, ThreeStreamsInactive) {
  AllocatorFixture f;
  f.Setup3SL3TL({false, false, false});
  f.Create();
  const uint32_t min = f.S(0).min_bitrate_kbps + f.S(1).min_bitrate_kbps + f.S(2).min_bitrate_kbps;
  const uint32_t target = f.S(0).target_bitrate_kbps + f.S(1).target_bitrate_kbps + f.S(2).target_bitrate_kbps;
  const uint32_t max = f.S(0).max_bitrate_kbps + f.S(1).max_bitrate_kbps + f.S(2).max_bitrate_kbps;
  for (uint32_t r : {0u, min, target, max}) ExpectEqual({0, 0, 0}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, TwoStreamsLowInactive) {
  AllocatorFixture f;
  f.Setup2SL3TL({false, true});
  f.Create();
  const uint32_t mn = f.S(1).min_bitrate_kbps, tg = f.S(1).target_bitrate_kbps, mx = f.S(1).max_bitrate_kbps;
  for (uint32_t r : {0u, mn - 10, mn}) ExpectEqual({0, mn}, f.Get(r));
  ExpectEqual({0, tg}, f.Get(tg));
  for (uint32_t r : {mx, kU32Max}) ExpectEqual({0, mx}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, TwoStreamsHighInactive) {
  AllocatorFixture f;
  f.Setup2SL3TL({true, false});
  f.Create();
  const uint32_t mn = f.S(0).min_bitrate_kbps, tg = f.S(0).target_bitrate_kbps, mx = f.S(0).max_bitrate_kbps;
  for (uint32_t r : {0u, mn - 10, mn}) ExpectEqual({mn, 0}, f.Get(r));
  ExpectEqual({tg, 0}, f.Get(tg));
  for (uint32_t r : {mx, kU32Max}) ExpectEqual({mx, 0}, f.Get(r));
}

SM_TEST(SimulcastRateAllocator, ThreeStreamsMiddleInactive) {
  AllocatorFixture f;
  f.Setup3SL3TL({true, false, true});
  f.Create();
  const uint32_t low_min = f.S(0).min_bitrate_kbps;
  for (uint32_t r : {0u, low_min - 10, low_min}) ExpectEqual({low_min, 0, 0}, f.Get(r));
  ExpectEqual({f.S(0).target_bitrate_kbps, 0, 0}, f.Get(f.S(0).target_bitrate_kbps));
  ExpectEqual({f.S(0).max_bitrate_kbps, 0, 0}, f.Get(f.S(0).target_bitrate_kbps + f.S(2).min_bitrate_kbps - 1));
  ExpectEqual({f.S(0).target_bitrate_kbps, 0, f.S(2).target_bitrate_kbps},
              f.Get(f.S(0).target_bitrate_kbps + f.S(2).target_bitrate_kbps));
  const uint32_t r = f.S(0).target_bitrate_kbps + f.S(2).max_bitrate_kbps;
  for (uint32_t x : {r, r + 10, kU32Max}) ExpectEqual({f.S(0).target_bitrate_kbps, 0, f.S(2).max_bitrate_kbps}, f.Get(x));
}

SM_TEST(SimulcastRateAllocator, NonConferenceModeScreenshare) {
  AllocatorFixture f;
  f.codec.mode = VideoCodecMode::kScreensharing;
  f.Setup3SL3TL({true, true, true});
  f.Create();
  const auto a = f.Get(f.S(0).max_bitrate_kbps + f.S(1).max_bitrate_kbps + f.S(2).max_bitrate_kbps);
  for (size_t i = 0; i < 3; ++i) SM_EXPECT_EQ(a.GetTemporalLayerAllocation(i).size(), 3u);
}

SM_TEST(SimulcastRateAllocator, TemporalLayerForSinglecast) {
  AllocatorFixture f;
  f.Setup3TL();
  f.codec.number_of_simulcast_streams = 0;
  f.Create();
  // A single stream uses the codec's temporal layer count.
  SM_EXPECT_EQ(f.Get(5000).GetTemporalLayerAllocation(0).size(), 3u);
}

namespace {
void SetupConferenceScreenshare(AllocatorFixture& f, bool use_simulcast, bool active = true) {
  f.codec.mode = VideoCodecMode::kScreensharing;
  f.codec.min_bitrate_kbps = kMinBitrateKbps;
  f.codec.max_bitrate_kbps = kLegacyScreenshareMaxBitrateKbps + kSimulcastScreenshareMaxBitrateKbps;
  if (use_simulcast) {
    f.codec.number_of_simulcast_streams = 2;
    f.codec.simulcast_stream[0] = {0, 0, 0, 2, kLegacyScreenshareMaxBitrateKbps, kLegacyScreenshareTargetBitrateKbps,
                                   kMinBitrateKbps, 0, active};
    f.codec.simulcast_stream[1] = {0, 0, 0, 2, kSimulcastScreenshareMaxBitrateKbps,
                                   kSimulcastScreenshareMaxBitrateKbps, kSimulcastScreenshareMinBitrateKbps, 0, active};
  } else {
    f.codec.number_of_simulcast_streams = 0;
    f.codec.codec_temporal_layers = 2;
    f.codec.active = active;
  }
}
}  // namespace

SM_TEST(ScreenshareRateAllocation, ConferenceBitrateBelowAndAboveTl0) {
  for (bool simulcast : {false, true}) {
    AllocatorFixture f;
    SetupConferenceScreenshare(f, simulcast);
    f.Create();
    f.allocator->SetLegacyConferenceMode(true);
    auto a = f.allocator->Allocate(DataRate::KilobitsPerSec(kLegacyScreenshareTargetBitrateKbps), 5);
    // All in TL0.
    SM_EXPECT_EQ(a.get_sum_kbps(), kLegacyScreenshareTargetBitrateKbps);
    SM_EXPECT_EQ(a.GetBitrate(0, 0) / 1000, kLegacyScreenshareTargetBitrateKbps);
    SM_EXPECT_EQ(a.is_bw_limited(), simulcast);
    const uint32_t target = (kLegacyScreenshareTargetBitrateKbps + kLegacyScreenshareMaxBitrateKbps) / 2;
    a = f.allocator->Allocate(DataRate::KilobitsPerSec(target), 5);
    // TL0 filled, the rest in TL1.
    SM_EXPECT_EQ(a.get_sum_kbps(), target);
    SM_EXPECT_EQ(a.GetBitrate(0, 0) / 1000, kLegacyScreenshareTargetBitrateKbps);
    SM_EXPECT_EQ(a.GetBitrate(0, 1) / 1000, target - kLegacyScreenshareTargetBitrateKbps);
    SM_EXPECT_EQ(a.is_bw_limited(), simulcast);
  }
}

SM_TEST(ScreenshareRateAllocation, ConferenceBitrateAboveTl1) {
  AllocatorFixture f;
  SetupConferenceScreenshare(f, false);
  f.Create();
  f.allocator->SetLegacyConferenceMode(true);
  const auto a = f.allocator->Allocate(DataRate::KilobitsPerSec(kLegacyScreenshareMaxBitrateKbps * 2), 5);
  // TL0 and TL1 filled, no more.
  SM_EXPECT_EQ(a.get_sum_kbps(), kLegacyScreenshareMaxBitrateKbps);
  SM_EXPECT_EQ(a.GetBitrate(0, 0) / 1000, kLegacyScreenshareTargetBitrateKbps);
  SM_EXPECT_EQ(a.GetBitrate(0, 1) / 1000, kLegacyScreenshareMaxBitrateKbps - kLegacyScreenshareTargetBitrateKbps);
  SM_EXPECT_FALSE(a.is_bw_limited());
}

SM_TEST(ScreenshareRateAllocation, InactiveScreenshare) {
  for (bool simulcast : {false, true}) {
    AllocatorFixture f;
    SetupConferenceScreenshare(f, simulcast, false);
    f.Create();
    const uint32_t target = (kLegacyScreenshareTargetBitrateKbps + kLegacyScreenshareMaxBitrateKbps) / 2;
    SM_EXPECT_EQ(f.allocator->Allocate(DataRate::KilobitsPerSec(target), 5).get_sum_kbps(), 0u);
  }
}

SM_TEST(ScreenshareRateAllocation, Hysteresis) {
  AllocatorFixture f;
  SetupConferenceScreenshare(f, true);
  f.Create();
  const uint32_t enable = f.S(0).target_bitrate_kbps + f.S(1).min_bitrate_kbps;
  const uint32_t enable_with_hysteresis = (enable * 135) / 100;
  // The first allocation of a new allocator has no hysteresis.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).min_bitrate_kbps}, f.Get(enable));
  // Below what two streams need.
  ExpectEqual({enable - 1, 0}, f.Get(enable - 1));
  // Not enabled again until above the hysteresis threshold.
  ExpectEqual({enable, 0}, f.Get(enable));
  ExpectEqual({f.S(0).target_bitrate_kbps, enable_with_hysteresis - f.S(0).target_bitrate_kbps},
              f.Get(enable_with_hysteresis));
  // Down again: the second stream stays.
  ExpectEqual({f.S(0).target_bitrate_kbps, f.S(1).min_bitrate_kbps}, f.Get(enable));
  // Below the enable rate: off again, and the hysteresis holds it off.
  ExpectEqual({enable - 1, 0}, f.Get(enable - 1));
  ExpectEqual({enable, 0}, f.Get(enable));
}

// VideoCodecInitializer::SetupCodec for the default 720p H.264 simulcast: per-stream limits, the codec max the sum.
SM_TEST(SimulcastRateAllocator, SetupCodecFor720pSimulcast) {
  VideoEncoderConfig c;
  c.codec_type = VideoCodecType::kH264;
  c.number_of_streams = 3;
  c.simulcast_layers.resize(3);
  const auto streams = CreateEncoderStreams(kTrials, 1280, 720, c);
  const VideoCodec codec = SetupCodec(c, streams, DataRate::KilobitsPerSec(300));
  SM_EXPECT_EQ(codec.number_of_simulcast_streams, 3u);
  SM_EXPECT_EQ(codec.width, 1280);
  SM_EXPECT_EQ(codec.height, 720);
  SM_EXPECT_EQ(codec.min_bitrate_kbps, 30u);
  SM_EXPECT_EQ(codec.max_bitrate_kbps, 200u + 700u + 2500u);
  SM_EXPECT_EQ(codec.start_bitrate_kbps, 300u);
  SM_EXPECT_TRUE(simulcast_utility::ValidSimulcastParameters(codec, 3));
  SimulcastRateAllocator a(codec);
  // The first allocation has no hysteresis: 150 + 500 + 600 turns on all three.
  ExpectEqual({150, 500, 719}, a.Allocate(DataRate::KilobitsPerSec(1369)));
  // Below 1250 the top layer is off and the middle one takes the rest up to its max.
  ExpectEqual({150, 700, 0}, a.Allocate(DataRate::KilobitsPerSec(1249)));
  // Back on only at 150 + 500 + 1.2 x 600 = 1370 kbps.
  ExpectEqual({150, 700, 0}, a.Allocate(DataRate::KilobitsPerSec(1369)));
  ExpectEqual({150, 500, 720}, a.Allocate(DataRate::KilobitsPerSec(1370)));
}
