// FrameCadenceAdapter and QualityConvergence (7.4, issue #878): libwebrtc
// M157's video/frame_cadence_adapter.cc and quality_convergence_*.cc.
#include <vector>

#include "sm/video/frame_cadence_adapter.h"
#include "sm/video/quality_convergence.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::video;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }

struct Sent {
  int64_t at_ms;
  FrameCadenceAdapter::Output out;
};

// Runs the adapter's tasks up to `until_ms`, as the encoder queue would.
void RunUntil(FrameCadenceAdapter& a, int64_t until_ms, std::vector<Sent>& sent) {
  for (;;) {
    Timestamp next = a.NextProcessTime();
    if (!next.IsFinite() || next > Ms(until_ms)) break;
    for (const auto& o : a.Process(next)) sent.push_back({next.ms(), o});
  }
}

std::vector<int64_t> EncodeTimes(const std::vector<Sent>& sent) {
  std::vector<int64_t> t;
  for (const auto& s : sent) {
    if (s.out.kind == FrameCadenceAdapter::Output::Kind::kEncode) t.push_back(s.at_ms);
  }
  return t;
}

std::vector<int64_t> RefreshTimes(const std::vector<Sent>& sent) {
  std::vector<int64_t> t;
  for (const auto& s : sent) {
    if (s.out.kind == FrameCadenceAdapter::Output::Kind::kRequestRefreshFrame) t.push_back(s.at_ms);
  }
  return t;
}

// A screen share at max 10 fps (frame period 100 ms) with one layer, as
// VideoStreamEncoder configures it for kScreen content.
void EnableZeroHertz(FrameCadenceAdapter& a, Timestamp now) {
  a.SetZeroHertzModeEnabled(FrameCadenceAdapter::ZeroHertzModeParams{1}, now);
  a.OnConstraintsChanged({0.0, 10.0}, now);
}
}  // namespace

SM_TEST(FrameCadenceAdapter, PassthroughDeliversEveryFrameAtOnce) {
  FrameCadenceAdapter a;
  a.OnFrame(1, Ms(5), Ms(0));
  auto out = a.Process(Ms(0));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].frame, 1);
  SM_EXPECT_EQ(out[0].post_time.ms(), 0);
  SM_EXPECT_FALSE(out[0].is_repeat);
  SM_EXPECT_FALSE(a.NextProcessTime().IsFinite());  // No repeats.
  SM_EXPECT_FALSE(a.zero_hertz());
  // The input rate is RateStatistics over 2970 ms, read before each update.
  for (int i = 1; i <= 30; ++i) {
    a.OnFrame(i + 1, Ms(5 + i * 100), Ms(i * 100));
    a.Process(Ms(i * 100));
  }
  SM_ASSERT_TRUE(a.GetInputFrameRateFps().has_value());
  SM_EXPECT_EQ(*a.GetInputFrameRateFps(), 10u);
}

SM_TEST(FrameCadenceAdapter, ZeroHertzOnlyForScreenshareWithMinFpsZero) {
  // IsZeroHertzScreenshareEnabled: params set, max fps > 0 and min fps == 0.
  FrameCadenceAdapter a;
  a.SetZeroHertzModeEnabled(FrameCadenceAdapter::ZeroHertzModeParams{1}, Ms(0));
  SM_EXPECT_FALSE(a.zero_hertz());
  a.OnConstraintsChanged({1.0, 10.0}, Ms(0));
  SM_EXPECT_FALSE(a.zero_hertz());
  a.OnConstraintsChanged({std::nullopt, 10.0}, Ms(0));
  SM_EXPECT_FALSE(a.zero_hertz());
  a.OnConstraintsChanged({0.0, 10.0}, Ms(0));
  SM_EXPECT_TRUE(a.zero_hertz());
  SM_EXPECT_EQ(*a.GetInputFrameRateFps(), 10u);  // The max fps in zero-hertz mode.
  a.SetZeroHertzModeEnabled(std::nullopt, Ms(0));  // Camera content.
  SM_EXPECT_FALSE(a.zero_hertz());
  FrameCadenceAdapter b;
  b.OnConstraintsChanged({0.0, 10.0}, Ms(0));
  SM_EXPECT_FALSE(b.zero_hertz());  // Constraints alone don't enable it.
}

SM_TEST(FrameCadenceAdapter, DelaysAFrameOnePeriodThenRepeatsAtTheFrameRateUntilConverged) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  a.OnFrame(7, Ms(1000), Ms(0));
  std::vector<Sent> sent;
  RunUntil(a, 550, sent);
  SM_ASSERT_EQ(EncodeTimes(sent), (std::vector<int64_t>{100, 200, 300, 400, 500}));
  SM_EXPECT_FALSE(sent[0].out.is_repeat);
  SM_EXPECT_EQ(sent[0].out.capture_time.ms(), 1000);
  for (size_t i = 1; i < sent.size(); ++i) {
    SM_EXPECT_TRUE(sent[i].out.is_repeat);
    SM_EXPECT_EQ(sent[i].out.frame, 7);
    // The repeat's timestamp moves on by the time since repeating started.
    SM_EXPECT_EQ(sent[i].out.capture_time.ms(), 1000 + sent[i].at_ms - 100);
  }
}

SM_TEST(FrameCadenceAdapter, RepeatsOncePerSecondAfterQualityConverges) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  a.OnFrame(1, Ms(1), Ms(0));
  std::vector<Sent> sent;
  RunUntil(a, 150, sent);
  a.UpdateLayerQualityConvergence(0, true);
  RunUntil(a, 3300, sent);
  // The repeat already scheduled at 200 is short; the ones after it are on
  // kZeroHertzIdleRepeatRatePeriod.
  SM_EXPECT_EQ(EncodeTimes(sent), (std::vector<int64_t>{100, 200, 1200, 2200, 3200}));
  // A disabled layer counts as converged; with no layers, never converged.
  FrameCadenceAdapter b;
  b.SetZeroHertzModeEnabled(FrameCadenceAdapter::ZeroHertzModeParams{2}, Ms(0));
  b.OnConstraintsChanged({0.0, 10.0}, Ms(0));
  b.OnFrame(1, Ms(1), Ms(0));
  std::vector<Sent> sent_b;
  RunUntil(b, 150, sent_b);
  b.UpdateLayerQualityConvergence(0, true);
  b.UpdateLayerStatus(1, false);
  RunUntil(b, 1250, sent_b);
  SM_EXPECT_EQ(EncodeTimes(sent_b), (std::vector<int64_t>{100, 200, 1200}));
}

SM_TEST(FrameCadenceAdapter, ANewFrameCancelsTheRepeatsAndResetsConvergence) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  a.OnFrame(1, Ms(1), Ms(0));
  std::vector<Sent> sent;
  RunUntil(a, 250, sent);
  a.UpdateLayerQualityConvergence(0, true);
  a.OnFrame(2, Ms(251), Ms(250));
  RunUntil(a, 560, sent);
  // 1 at 100, its repeat at 200; the repeat due at 300 is cancelled; 2 goes
  // out one period after it arrived and repeats at the short period, since a
  // new frame makes every layer unconverged.
  SM_EXPECT_EQ(EncodeTimes(sent), (std::vector<int64_t>{100, 200, 350, 450, 550}));
  SM_EXPECT_EQ(sent[2].out.frame, 2);
  SM_EXPECT_FALSE(sent[2].out.is_repeat);
}

SM_TEST(FrameCadenceAdapter, TwoFramesWithinAPeriodAreBothSentWithoutRepeatingTheFirst) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  a.OnFrame(1, Ms(1), Ms(0));
  a.Process(Ms(0));
  a.OnFrame(2, Ms(41), Ms(40));
  std::vector<Sent> sent;
  RunUntil(a, 245, sent);
  SM_ASSERT_EQ(EncodeTimes(sent), (std::vector<int64_t>{100, 140, 240}));
  SM_EXPECT_EQ(sent[0].out.frame, 1);
  SM_EXPECT_EQ(sent[1].out.frame, 2);
  SM_EXPECT_TRUE(sent[2].out.is_repeat);
  SM_EXPECT_EQ(sent[2].out.frame, 2);
}

SM_TEST(FrameCadenceAdapter, KeyFrameRequestCutsAnIdleRepeatShort) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  a.OnFrame(1, Ms(1), Ms(0));
  std::vector<Sent> sent;
  RunUntil(a, 150, sent);
  a.UpdateLayerQualityConvergence(0, true);
  RunUntil(a, 500, sent);  // Repeat at 200; the next is idle, due at 1200.
  a.ProcessKeyFrameRequest(Ms(500));
  RunUntil(a, 820, sent);
  // A short repeat one period after the request, then short repeats: the
  // keyframe needs refinement frames, so convergence was reset.
  SM_EXPECT_EQ(EncodeTimes(sent), (std::vector<int64_t>{100, 200, 600, 700, 800}));
  // With the idle repeat due within a frame period, nothing changes.
  FrameCadenceAdapter b;
  EnableZeroHertz(b, Ms(0));
  b.OnFrame(1, Ms(1), Ms(0));
  std::vector<Sent> sent_b;
  RunUntil(b, 150, sent_b);
  b.UpdateLayerQualityConvergence(0, true);
  RunUntil(b, 1150, sent_b);
  b.ProcessKeyFrameRequest(Ms(1150));
  RunUntil(b, 1250, sent_b);
  SM_EXPECT_EQ(EncodeTimes(sent_b), (std::vector<int64_t>{100, 200, 1200}));
}

SM_TEST(FrameCadenceAdapter, RequestsRefreshFramesUntilAFrameArrives) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  std::vector<Sent> sent;
  RunUntil(a, 440, sent);
  // From creation: after kOnDiscardedFrameRefreshFramePeriod (3) periods,
  // then every period.
  SM_EXPECT_EQ(RefreshTimes(sent), (std::vector<int64_t>{300, 400}));
  a.OnFrame(1, Ms(1), Ms(450));
  RunUntil(a, 900, sent);
  SM_EXPECT_EQ(RefreshTimes(sent), (std::vector<int64_t>{300, 400}));
  // A discarded frame starts it again.
  sent.clear();
  a.OnDiscardedFrame(Ms(1000));
  RunUntil(a, 1450, sent);
  SM_EXPECT_EQ(RefreshTimes(sent), (std::vector<int64_t>{1300, 1400}));
}

SM_TEST(FrameCadenceAdapter, RestrictedFrameRateLengthensTheShortRepeats) {
  FrameCadenceAdapter a;
  EnableZeroHertz(a, Ms(0));
  a.UpdateVideoSourceRestrictions(5.0);
  a.OnFrame(1, Ms(1), Ms(0));
  std::vector<Sent> sent;
  RunUntil(a, 560, sent);
  // The first frame keeps the 1 / max fps delay; repeats use the longer of
  // the two periods.
  SM_EXPECT_EQ(EncodeTimes(sent), (std::vector<int64_t>{100, 300, 500}));
  // The restriction survives a new zero-hertz adapter (max fps change).
  a.OnConstraintsChanged({0.0, 20.0}, Ms(560));
  a.OnFrame(2, Ms(561), Ms(560));
  sent.clear();
  RunUntil(a, 1000, sent);
  SM_EXPECT_EQ(EncodeTimes(sent), (std::vector<int64_t>{610, 810}));
}

SM_TEST(QualityConvergence, StaticThresholdsAsLibwebrtc) {
  SM_EXPECT_EQ(QualityConvergenceController::DefaultStaticQpThreshold(VideoCodecType::kVP8), 15);
  SM_EXPECT_EQ(QualityConvergenceController::DefaultStaticQpThreshold(VideoCodecType::kVP9), 32);
  SM_EXPECT_EQ(QualityConvergenceController::DefaultStaticQpThreshold(VideoCodecType::kAV1), 60);
  SM_EXPECT_EQ(QualityConvergenceController::DefaultStaticQpThreshold(VideoCodecType::kH264), -1);
  QualityConvergenceController c;
  c.Initialize(1, std::nullopt, VideoCodecType::kVP8);
  SM_EXPECT_TRUE(c.AddSampleAndCheckTargetQuality(0, 15, false));
  SM_EXPECT_FALSE(c.AddSampleAndCheckTargetQuality(0, 16, false));
  SM_EXPECT_FALSE(c.AddSampleAndCheckTargetQuality(1, 0, false));  // No such layer.
  // The encoder's min QP raises the threshold.
  c.Initialize(1, 20, VideoCodecType::kVP8);
  SM_EXPECT_TRUE(c.AddSampleAndCheckTargetQuality(0, 20, false));
  // H.264 has neither a static threshold nor dynamic detection.
  c.Initialize(1, std::nullopt, VideoCodecType::kH264);
  SM_EXPECT_FALSE(c.AddSampleAndCheckTargetQuality(0, 0, true));
  c.Initialize(1, 10, VideoCodecType::kH264);
  SM_EXPECT_TRUE(c.AddSampleAndCheckTargetQuality(0, 10, false));
}

SM_TEST(QualityConvergence, DynamicDetectionOverRefreshFrames) {
  // VP8: dynamic threshold 15 + 0.06 * 127 = 22, windows 6 + 6.
  auto p = QualityConvergenceMonitor::DefaultParameters(15, VideoCodecType::kVP8);
  SM_EXPECT_TRUE(p.dynamic_detection_enabled);
  SM_EXPECT_EQ(p.dynamic_qp_threshold, 22);
  SM_EXPECT_EQ(p.recent_window_length, 6u);
  SM_EXPECT_EQ(p.past_window_length, 6u);
  SM_EXPECT_EQ(QualityConvergenceMonitor::DefaultParameters(32, VideoCodecType::kVP9).dynamic_qp_threshold, 47);
  SM_EXPECT_EQ(QualityConvergenceMonitor::DefaultParameters(60, VideoCodecType::kAV1).dynamic_qp_threshold, 75);
  SM_EXPECT_FALSE(QualityConvergenceMonitor::DefaultParameters(-1, VideoCodecType::kH264).dynamic_detection_enabled);

  QualityConvergenceMonitor m(p);
  // Six refresh samples only fill the recent window.
  for (int i = 0; i < 6; ++i) {
    m.AddSample(20, true);
    SM_EXPECT_FALSE(m.AtTargetQuality());
  }
  m.AddSample(20, true);  // Past mean 20 <= 22 and <= recent mean 20.
  SM_EXPECT_TRUE(m.AtTargetQuality());
  m.AddSample(40, true);  // Converged stays converged over refresh frames.
  SM_EXPECT_TRUE(m.AtTargetQuality());
  m.AddSample(20, false);  // A new frame clears the window.
  SM_EXPECT_FALSE(m.AtTargetQuality());
  // A past mean above the recent mean (QP still falling) is not converged.
  for (int qp : {22, 22, 22, 21, 21, 21, 20}) m.AddSample(qp, true);
  SM_EXPECT_FALSE(m.AtTargetQuality());
  // A past mean above the dynamic threshold is not converged.
  m.AddSample(-1, false);
  for (int i = 0; i < 12; ++i) m.AddSample(30, true);
  SM_EXPECT_FALSE(m.AtTargetQuality());
}
