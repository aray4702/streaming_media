// Congestion-window pushback over sim::NetworkLink (issue #53): a sender
// session whose encoder keeps producing at the old rate after the bottleneck
// drops 5x. With pushback the encoder drops frames and the data in flight
// (on average after the drop) and what the bottleneck has to drop stay
// smaller than without it.
#include <algorithm>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/rtp/session.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
constexpr uint32_t kVideoSsrc = 2, kRtxSsrc = 3;

SessionConfig MakeConfig(uint32_t ssrc, const char* cname, bool pushback) {
  SessionConfig c;
  c.local_ssrc = ssrc;
  c.cname = cname;
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.extensions.Register(2, sync::uri::kAbsSendTime);
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  c.send_side.cwnd.enabled = pushback;
  return c;
}

struct Result {
  uint64_t queue_dropped = 0, frames_encoded = 0;  // queue_dropped: from 9 s on (see the test).
  int64_t max_in_flight = 0;
  double mean_in_flight = 0;  // Over the 6 s after the drop.
  double min_ratio = 1.0;
  int64_t in_flight_end = 0;
  int64_t window_end = 0;  // The congestion window (target x (min feedback RTT + 350 ms)) at the end.
  double sent_kbps_after_drop = 0;
};

Result Run(bool pushback) {
  const test::TestVideoParser parser;
  Session a(MakeConfig(10, "alice", pushback)), b(MakeConfig(20, "bob", pushback));
  VideoStreamConfig v;
  v.ssrc = kVideoSsrc;
  v.rtx_ssrc = kRtxSsrc;
  v.parser = &parser;
  auto send = a.AddVideoSend(v);
  auto recv = b.AddVideoReceive(v);
  (void)b.AddVideoSend(v);

  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(30);
  lc.capacity = DataRate::KilobitsPerSec(2500);
  lc.queue_limit = DataSize::Bytes(25000);
  sim::NetworkLink<std::vector<uint8_t>> ab(lc, 1);
  sim::NetworkLink<std::vector<uint8_t>> ba(lc, 2);

  Result res;
  bool key_needed = true;
  uint64_t drops_at_9s = 0;
  int64_t sent_after = 0;
  const int64_t end_ms = 14000;
  for (int64_t t = 0; t < end_ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t == 8000) {
      lc.capacity = DataRate::KilobitsPerSec(500);
      ab.SetConfig(lc);
    }
    if (t % 33 == 0) {
      if (!a.send_side().DropCapturedFrame()) {
        const bool key = key_needed;
        key_needed = false;
        const double per_frame = static_cast<double>(a.rates().video_target.bps()) / 8 / 30;
        auto chunks = test::MakeTestFrameChunks(key, static_cast<size_t>(key ? per_frame * 4 : per_frame), 1188);
        a.SendVideoChunks(send, chunks, static_cast<uint32_t>(t * 90), now);
        ++res.frames_encoded;
      }
    }
    a.Process(now);
    b.Process(now);
    for (auto& p : a.TakeOutgoing()) {
      if (t >= 8500) sent_after += p.wire_size.bytes();
      ab.Send(now, std::move(p.bytes), p.wire_size);
    }
    for (auto& p : b.TakeOutgoing()) ba.Send(now, std::move(p.bytes), p.wire_size);
    ab.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { b.OnPacket(bytes, at); });
    ba.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { a.OnPacket(bytes, at); });
    b.TakeVideoFrames(recv);
    for (const auto& e : a.TakeEvents()) {
      if (e.type == SessionEventType::kKeyframeRequested) key_needed = true;
    }
    b.TakeEvents();
    if (t == 9000) drops_at_9s = ab.stats().queue_dropped;
    if (t >= 8000) {
      const int64_t in_flight = a.send_side().gcc().history().InFlight().bytes();
      res.max_in_flight = std::max(res.max_in_flight, in_flight);
      res.mean_in_flight += static_cast<double>(in_flight) / static_cast<double>(end_ms - 8000);
      res.min_ratio = std::min(res.min_ratio, a.send_side().congestion_window().encoding_rate_ratio());
    }
  }
  res.queue_dropped = ab.stats().queue_dropped - drops_at_9s;
  res.in_flight_end = a.send_side().gcc().history().InFlight().bytes();
  if (const auto w = a.send_side().congestion_window().data_window()) res.window_end = w->bytes();
  res.sent_kbps_after_drop = static_cast<double>(sent_after) * 8 / 5500.0;
  return res;
}
}  // namespace

SM_TEST(E2E_CongestionWindowPushback, EncoderBacksOffWhenTheBottleneckFills) {
  const Result with = Run(true);
  const Result without = Run(false);
  SM_RECORD("with_queue_dropped", static_cast<double>(with.queue_dropped));
  SM_RECORD("without_queue_dropped", static_cast<double>(without.queue_dropped));
  SM_RECORD("with_max_in_flight", static_cast<double>(with.max_in_flight));
  SM_RECORD("without_max_in_flight", static_cast<double>(without.max_in_flight));
  SM_RECORD("with_mean_in_flight", with.mean_in_flight);
  SM_RECORD("without_mean_in_flight", without.mean_in_flight);
  SM_RECORD("with_frames_encoded", static_cast<double>(with.frames_encoded));
  SM_RECORD("without_frames_encoded", static_cast<double>(without.frames_encoded));
  SM_RECORD("with_min_ratio", with.min_ratio);
  SM_RECORD("with_sent_kbps", with.sent_kbps_after_drop);
  SM_RECORD("without_sent_kbps", without.sent_kbps_after_drop);
  SM_RECORD("with_in_flight_end", static_cast<double>(with.in_flight_end));
  SM_RECORD("with_window_end", static_cast<double>(with.window_end));
  SM_EXPECT_LT(with.min_ratio, 1.0);
  SM_EXPECT_EQ(without.min_ratio, 1.0);
  SM_EXPECT_LT(with.frames_encoded, without.frames_encoded);
  // Pushback cuts the data in flight and the send rate after the drop, the
  // bottleneck drops fewer packets, and what is still in flight at the end
  // fits the congestion window (the target times the RTT plus libwebrtc's
  // 350 ms QueueSize). The first two were ratios to the run without pushback
  // (2x and 1.5x) when the encoder took no share off for repair traffic;
  // libwebrtc's measured repair share (FecControllerDefault) makes the run
  // without pushback back off on its own, so the ratios no longer hold (issue
  // #21, program.md 0.2 guard rails). The data in flight is compared as its
  // mean over the 6 s after the drop: its peak comes at the drop itself,
  // before any feedback says the link has changed, and so before pushback can
  // act (libwebrtc has no counterpart to this test; Owner on #315, issue #217).
  SM_EXPECT_LT(with.mean_in_flight, without.mean_in_flight);
  // The bottleneck's drops are counted from 9 s, once the first feedback has
  // reported the drop at 8 s: in the second before that, pushback cannot act,
  // and how much the queue drops then depends on where each run's estimate
  // stood at 8 s. The two runs' GoogCC trajectories differ before the drop
  // (pushback's frame drops during the ramp change the send pattern). With
  // libwebrtc's 1 ms feedback receive times (#365), the run with pushback met
  // the drop at 2906 kbps after an estimate step at 7.9 s, against 2285 kbps
  // without, and dropped 172 packets in 8-9 s against 120; over the whole call
  // that read 293 with pushback vs 274 without. From 9 s on it is 19 vs 28
  // (main before #365: 2 vs 76; whole call 232 vs 336). Owner on #381.
  SM_EXPECT_LT(with.queue_dropped, without.queue_dropped);
  SM_EXPECT_LT(with.sent_kbps_after_drop, without.sent_kbps_after_drop);
  SM_EXPECT_LE(with.in_flight_end, with.window_end);
}
