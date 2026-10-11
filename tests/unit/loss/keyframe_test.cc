#include "sm/core/event_log.h"
#include "sm/loss/keyframe_request.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
FrameInfo Key(int64_t id) { return FrameInfo{id, true, {}}; }
FrameInfo Delta(int64_t id, int64_t ref) { return FrameInfo{id, false, {ref}}; }
size_t CountType(const std::vector<FeedbackMessage>& v, FeedbackType t) {
  size_t n = 0;
  for (auto& m : v) n += m.type == t;
  return n;
}
}  // namespace

SM_TEST(KeyframeRequest, InOrderFramesAreDecodable) {
  KeyframeRequester k;
  k.OnFrame(Key(0), Ms(0));
  k.OnFrame(Delta(1, 0), Ms(33));
  k.OnFrame(Delta(2, 1), Ms(66));
  auto d = k.TakeDecodableFrames();
  SM_EXPECT_EQ(d.size(), 3u);
  SM_EXPECT_TRUE(k.Process(Ms(100)).empty());
}

SM_TEST(KeyframeRequest, LateReferenceUnblocksWithoutPli) {
  KeyframeRequester k;
  k.OnFrame(Key(0), Ms(0));
  k.OnFrame(Delta(2, 1), Ms(66));  // 1 is late (being NACKed).
  auto fb = k.Process(Ms(70));
  SM_EXPECT_EQ(CountType(fb, FeedbackType::kLntf), 1u);
  SM_EXPECT_EQ(CountType(fb, FeedbackType::kPli), 0u);
  k.OnFrame(Delta(1, 0), Ms(120));
  auto d = k.TakeDecodableFrames();
  SM_ASSERT_EQ(d.size(), 3u);
  SM_EXPECT_EQ(d[1], 1);
  SM_EXPECT_EQ(d[2], 2);
  SM_EXPECT_EQ(CountType(k.Process(Ms(500)), FeedbackType::kPli), 0u);
}

SM_TEST(KeyframeRequest, PliAfterRepairWindowThenThrottled) {
  KeyframeRequestConfig cfg;
  cfg.repair_window = TimeDelta::Millis(200);
  cfg.min_request_interval = TimeDelta::Millis(300);
  KeyframeRequester k(cfg);
  k.UpdateRtt(TimeDelta::Millis(50));
  k.OnFrame(Key(0), Ms(0));
  k.OnFrame(Delta(2, 1), Ms(66));
  SM_EXPECT_EQ(CountType(k.Process(Ms(200)), FeedbackType::kPli), 0u);
  SM_EXPECT_EQ(CountType(k.Process(Ms(266)), FeedbackType::kPli), 1u);
  SM_EXPECT_TRUE(k.waiting_for_keyframe());
  SM_EXPECT_EQ(CountType(k.Process(Ms(300)), FeedbackType::kPli), 0u);  // Throttled.
  // No keyframe: retry after 2x throttle.
  SM_EXPECT_EQ(CountType(k.Process(Ms(866)), FeedbackType::kPli), 1u);
  k.OnFrame(Key(10), Ms(900));
  SM_EXPECT_FALSE(k.waiting_for_keyframe());
  SM_EXPECT_EQ(k.stats().frames_dropped, 1u);
  SM_EXPECT_NEAR(k.stats().time_to_keyframe_ms.mean(), 634, 70);
}

SM_TEST(KeyframeRequest, DecoderErrorAndStreamStart) {
  EventLog log;
  KeyframeRequestConfig cfg;
  cfg.events = &log;
  KeyframeRequester k(cfg);
  k.OnStreamStart(Ms(0));
  auto fb = k.Process(Ms(0));
  SM_ASSERT_EQ(fb.size(), 1u);
  SM_EXPECT_TRUE(fb[0].type == FeedbackType::kFir);
  SM_EXPECT_EQ(fb[0].fir_seq, 1);
  k.OnFrame(Key(0), Ms(50));
  k.OnDecoderError(Ms(1000));
  fb = k.Process(Ms(1000));
  SM_EXPECT_EQ(CountType(fb, FeedbackType::kPli), 1u);
  SM_EXPECT_EQ(log.Count("keyframe", "pli"), 1u);
  SM_EXPECT_EQ(log.Count("keyframe", "fir"), 1u);
}
