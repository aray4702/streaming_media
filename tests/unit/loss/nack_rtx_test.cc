#include <algorithm>

#include "sm/core/event_log.h"
#include "sm/loss/nack_tracker.h"
#include "sm/loss/rtx.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
}  // namespace

SM_TEST(NackTracker, DetectsGapAndRequestsImmediately) {
  NackTracker n;
  n.OnReceivedPacket(10, true, Ms(0));
  n.OnReceivedPacket(13, false, Ms(1));
  SM_EXPECT_EQ(n.pending(), 2u);
  auto batch = n.GetNackBatch(Ms(1));
  SM_ASSERT_EQ(batch.size(), 2u);
  SM_EXPECT_EQ(batch[0], 11);
  SM_EXPECT_EQ(batch[1], 12);
  SM_EXPECT_TRUE(n.GetNackBatch(Ms(2)).empty());  // Not due again yet.
}

SM_TEST(NackTracker, RetriesAfterRttThenRecovers) {
  NackConfig cfg;
  cfg.min_retry_interval = TimeDelta::Millis(10);
  NackTracker n(cfg);
  n.UpdateRtt(TimeDelta::Millis(50));
  n.OnReceivedPacket(1, true, Ms(0));
  n.OnReceivedPacket(3, false, Ms(0));
  SM_EXPECT_EQ(n.GetNackBatch(Ms(0)).size(), 1u);
  SM_EXPECT_TRUE(n.GetNackBatch(Ms(49)).empty());
  SM_EXPECT_EQ(n.GetNackBatch(Ms(50)).size(), 1u);
  n.OnReceivedPacket(2, false, Ms(80));
  SM_EXPECT_EQ(n.pending(), 0u);
  SM_EXPECT_EQ(n.stats().recovered, 1u);
  SM_EXPECT_NEAR(n.stats().recovery_time_ms.mean(), 80, 1);
}

SM_TEST(NackTracker, ReorderWaitSuppressesSpuriousNacks) {
  NackConfig cfg;
  cfg.reorder_wait = TimeDelta::Millis(10);
  NackTracker n(cfg);
  n.OnReceivedPacket(1, true, Ms(0));
  n.OnReceivedPacket(3, false, Ms(0));
  SM_EXPECT_TRUE(n.GetNackBatch(Ms(5)).empty());
  n.OnReceivedPacket(2, false, Ms(6));  // Late, not lost.
  SM_EXPECT_TRUE(n.GetNackBatch(Ms(20)).empty());
  SM_EXPECT_EQ(n.stats().reordered, 1u);
  SM_EXPECT_EQ(n.stats().nack_requests, 0u);
}

SM_TEST(NackTracker, GivesUpAfterMaxRetriesAndRequestsKeyframe) {
  NackConfig cfg;
  cfg.max_retries = 3;
  cfg.min_retry_interval = TimeDelta::Millis(10);
  NackTracker n(cfg);
  n.UpdateRtt(TimeDelta::Millis(10));
  n.OnReceivedPacket(1, true, Ms(0));
  n.OnReceivedPacket(3, false, Ms(0));
  int sent = 0;
  for (int t = 0; t <= 100; t += 10) sent += static_cast<int>(n.GetNackBatch(Ms(t)).size());
  SM_EXPECT_EQ(sent, 3);
  SM_EXPECT_EQ(n.pending(), 0u);
  SM_EXPECT_EQ(n.stats().abandoned, 1u);
  SM_EXPECT_TRUE(n.TakeKeyframeRequest());
  SM_EXPECT_FALSE(n.TakeKeyframeRequest());
}

SM_TEST(NackTracker, HandlesWrapAround) {
  NackTracker n;
  n.OnReceivedPacket(65534, true, Ms(0));
  n.OnReceivedPacket(1, false, Ms(1));
  auto b = n.GetNackBatch(Ms(1));
  SM_ASSERT_EQ(b.size(), 2u);
  SM_EXPECT_EQ(b[0], 65535);
  SM_EXPECT_EQ(b[1], 0);
}

SM_TEST(NackTracker, OverflowDropsToKeyframe) {
  NackConfig cfg;
  cfg.max_nack_list = 10;
  NackTracker n(cfg);
  n.OnReceivedPacket(0, true, Ms(0));
  n.OnReceivedPacket(8, false, Ms(0));     // 7 missing.
  n.OnReceivedPacket(9, true, Ms(0));      // Keyframe at 9.
  n.OnReceivedPacket(15, false, Ms(0));    // +5 missing = 12 > 10.
  SM_EXPECT_EQ(n.pending(), 5u);           // Only gaps after keyframe 9 remain.
  SM_EXPECT_FALSE(n.TakeKeyframeRequest());
  n.OnReceivedPacket(40, false, Ms(0));    // No later keyframe: clear all.
  SM_EXPECT_TRUE(n.TakeKeyframeRequest());
}

SM_TEST(NackTracker, EmitsEventsAndMetrics) {
  EventLog log;
  NackConfig cfg;
  cfg.events = &log;
  NackTracker n(cfg);
  n.OnReceivedPacket(1, true, Ms(0));
  n.OnReceivedPacket(5, false, Ms(0));
  n.GetNackBatch(Ms(0));
  SM_EXPECT_EQ(log.Count("nack", "gap"), 1u);
  SM_EXPECT_EQ(log.Count("nack", "request"), 1u);
  auto m = CollectMetrics(n);
  SM_EXPECT_EQ(m["missing_detected"], 3.0);
  SM_EXPECT_EQ(m["nack_requests"], 3.0);
  SM_EXPECT_EQ(m["pending"], 3.0);
}

SM_TEST(Rtx, EncapsulationRoundTrip) {
  RtpPacket p;
  p.seq = 4242;
  p.timestamp = 90000;
  p.ssrc = 1;
  p.payload_type = 96;
  p.marker = true;
  p.payload = {9, 8, 7};
  RtpPacket r = MakeRtxPacket(p, 2, 97, 7);
  SM_EXPECT_EQ(r.ssrc, 2u);
  SM_EXPECT_EQ(r.seq, 7);
  SM_EXPECT_EQ(r.payload.size(), 5u);
  auto back = RestoreFromRtx(r, 1, 96);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->seq, 4242);
  SM_EXPECT_EQ(back->payload, p.payload);
  SM_EXPECT_TRUE(back->marker);
  RtpPacket pad;
  SM_EXPECT_FALSE(RestoreFromRtx(pad, 1, 96).has_value());
}

SM_TEST(Rtx, HistoryResendThrottleAndEviction) {
  RtxConfig cfg;
  cfg.history_capacity = 8;
  cfg.rtx_ssrc = 99;
  RtxSender s(cfg);
  for (uint16_t i = 0; i < 16; ++i) {
    RtpPacket p;
    p.seq = i;
    p.payload = {static_cast<uint8_t>(i)};
    s.OnPacketSent(p, Ms(i));
  }
  std::vector<uint16_t> req = {2, 10, 15};
  auto out = s.OnNack(req, Ms(20), TimeDelta::Millis(50));
  SM_EXPECT_EQ(out.size(), 2u);  // 2 was evicted.
  SM_EXPECT_EQ(s.stats().not_found, 1u);
  auto again = s.OnNack(std::vector<uint16_t>{10}, Ms(30), TimeDelta::Millis(50));
  SM_EXPECT_TRUE(again.empty());
  SM_EXPECT_EQ(s.stats().throttled, 1u);
  auto later = s.OnNack(std::vector<uint16_t>{10}, Ms(80), TimeDelta::Millis(50));
  SM_EXPECT_EQ(later.size(), 1u);
  SM_EXPECT_EQ(later[0].ssrc, 99u);
  // Too old.
  auto stale = s.OnNack(std::vector<uint16_t>{15}, Ms(5000), TimeDelta::Millis(50));
  SM_EXPECT_TRUE(stale.empty());
}

SM_TEST(NackTracker, V2GivesUpAfter100RetriesWithoutKeyframeRequest) {
  // NackRequester: kMaxNackRetries 100, and the entry is only erased.
  NackConfig cfg;
  cfg.version = 2;
  cfg.min_retry_interval = TimeDelta::Millis(10);
  NackTracker n(cfg);
  n.UpdateRtt(TimeDelta::Millis(10));
  n.OnReceivedPacket(1, true, Ms(0));
  n.OnReceivedPacket(3, false, Ms(0));
  int sent = 0;
  for (int t = 0; t <= 2000; t += 10) sent += static_cast<int>(n.GetNackBatch(Ms(t)).size());
  SM_EXPECT_EQ(sent, 100);
  SM_EXPECT_EQ(n.pending(), 0u);
  SM_EXPECT_EQ(n.stats().abandoned, 1u);
  SM_EXPECT_FALSE(n.TakeKeyframeRequest());
}

SM_TEST(NackTracker, V2ListOverflowClearsEverythingAndRequestsKeyframe) {
  // AddPacketsToNack: no attempt to keep the gaps after a keyframe.
  NackConfig cfg;
  cfg.version = 2;
  cfg.max_nack_list = 10;
  NackTracker n(cfg);
  n.OnReceivedPacket(0, true, Ms(0));
  n.OnReceivedPacket(8, false, Ms(0));   // 7 missing.
  n.OnReceivedPacket(9, true, Ms(0));    // A keyframe at 9 changes nothing.
  SM_EXPECT_EQ(n.pending(), 7u);
  SM_EXPECT_FALSE(n.TakeKeyframeRequest());
  n.OnReceivedPacket(15, false, Ms(0));  // +5 would make 12 > 10: clear.
  SM_EXPECT_EQ(n.pending(), 0u);
  SM_EXPECT_TRUE(n.TakeKeyframeRequest());
  SM_EXPECT_FALSE(n.TakeKeyframeRequest());
}
