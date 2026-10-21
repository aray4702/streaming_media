// The encoder's QP and encode-time model both call implementations share
// (testbench/encoder_model.h, issue #84).
#include "encoder_model.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {
constexpr int64_t BytesPerFrame(int kbps, int fps = 30) { return int64_t{kbps} * 1000 / 8 / fps; }
}  // namespace

SM_TEST(EncoderModel, ReferenceRatesSitBetweenTheH264Thresholds) {
  // libwebrtc's start rates per resolution (bitrate_constraint_unittest.cc,
  // resolution_bitrate_limits): 720p at 1000 kbps, 360p at 500 kbps.
  SM_EXPECT_EQ(EncoderModel::Qp(BytesPerFrame(1000), false, 1280, 720), 30);
  SM_EXPECT_EQ(EncoderModel::Qp(BytesPerFrame(500), false, 640, 360), 30);
}

SM_TEST(EncoderModel, HalfTheBitsIsSixMoreQp) {
  const int at_1000 = EncoderModel::Qp(BytesPerFrame(1000), false, 1280, 720);
  SM_EXPECT_EQ(EncoderModel::Qp(BytesPerFrame(500), false, 1280, 720), at_1000 + 6);
  SM_EXPECT_EQ(EncoderModel::Qp(BytesPerFrame(2000), false, 1280, 720), at_1000 - 6);
}

SM_TEST(EncoderModel, LowRateAt720pIsAboveTheHighThresholdAndFineAt360p) {
  // 300 kbps: libwebrtc's QualityScaler (H.264 high threshold 37) adapts 720p down, not 360p.
  SM_EXPECT_GT(EncoderModel::Qp(BytesPerFrame(300), false, 1280, 720), 37);
  SM_EXPECT_LE(EncoderModel::Qp(BytesPerFrame(300), false, 640, 360), 37);
  SM_EXPECT_GE(EncoderModel::Qp(BytesPerFrame(300), false, 640, 360), 24);
}

SM_TEST(EncoderModel, KeyframeHasTheQpOfADeltaFrameAtTheSameRate) {
  const int64_t delta = BytesPerFrame(800);
  SM_EXPECT_EQ(EncoderModel::Qp(delta * 4, true, 1280, 720), EncoderModel::Qp(delta, false, 1280, 720));
}

SM_TEST(EncoderModel, ComplexContentNeedsMoreBits) {
  const int64_t b = BytesPerFrame(1000);
  SM_EXPECT_EQ(EncoderModel::Qp(b, false, 1280, 720, 2.0), 36);
  SM_EXPECT_EQ(EncoderModel::Qp(b, false, 1280, 720, 0.5), 24);
}

SM_TEST(EncoderModel, QpIsClampedToH264sRange) {
  SM_EXPECT_EQ(EncoderModel::Qp(1, false, 1920, 1080), 51);
  SM_EXPECT_EQ(EncoderModel::Qp(1000000, false, 160, 90), 0);
  SM_EXPECT_EQ(EncoderModel::Qp(0, false, 0, 0), EncoderModel::Qp(1, false, 1, 1));  // No division by zero.
}

SM_TEST(EncoderModel, EncodeTimeIsLinearInPixelsOverSpeed) {
  SM_EXPECT_EQ(EncoderModel::EncodeTimeUs(1280, 720), 10000);
  SM_EXPECT_EQ(EncoderModel::EncodeTimeUs(640, 360), 2500);
  SM_EXPECT_EQ(EncoderModel::EncodeTimeUs(1280, 720, 0.5), 20000);
  SM_EXPECT_EQ(EncoderModel::EncodeTimeUs(1280, 720, 0.3), 33333);  // A full 30 fps frame interval.
}

SM_TEST(EncoderModel, ScenarioKeysRoundTrip) {
  auto p = ParseScenario("video_size 640x360\nvideo_complexity 1.5\nencoder_speed 0.3\n");
  SM_ASSERT_TRUE(p.scenario.has_value());
  SM_EXPECT_EQ(p.scenario->video_width, 640);
  SM_EXPECT_EQ(p.scenario->video_height, 360);
  SM_EXPECT_NEAR(p.scenario->video_complexity, 1.5, 1e-9);
  SM_EXPECT_NEAR(p.scenario->encoder_speed, 0.3, 1e-9);
  auto again = ParseScenario(FormatScenario(*p.scenario));
  SM_ASSERT_TRUE(again.scenario.has_value());
  SM_EXPECT_EQ(again.scenario->video_width, 640);
  SM_EXPECT_NEAR(again.scenario->encoder_speed, 0.3, 1e-9);
  // Defaults: 720p, as sm_compare's camera was before this key.
  auto d = ParseScenario("duration 1s\n");
  SM_ASSERT_TRUE(d.scenario.has_value());
  SM_EXPECT_EQ(d.scenario->video_width, 1280);
  SM_EXPECT_EQ(d.scenario->video_height, 720);
  SM_EXPECT_TRUE(FormatScenario(*d.scenario).find("video_size") == std::string::npos);
  SM_EXPECT_FALSE(ParseScenario("video_size 641x360\n").scenario.has_value());
  SM_EXPECT_FALSE(ParseScenario("video_size 640\n").scenario.has_value());
  SM_EXPECT_FALSE(ParseScenario("encoder_speed 0\n").scenario.has_value());
  SM_EXPECT_FALSE(ParseScenario("video_complexity x\n").scenario.has_value());
}
