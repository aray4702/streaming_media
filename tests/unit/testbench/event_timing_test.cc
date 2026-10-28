// The test bench hands each arriving packet to its session at the arrival
// time, and an `encode_delay` frame to the sender when its encode is done, and
// puts the session's response on the link then (issue #507), as sm_compare's
// libwebrtc side posts each packet to its task queue at the arrival time
// (compare/webrtc/webrtc_call.cc post_at) and sends through LinkTransport at
// sim.Now(). Before, both waited for the next 1 ms tick.
#include <cstdint>
#include <set>
#include <string>

#include "call.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

Report Traced(const std::string& text) {
  auto parsed = ParseScenario(text);
  SM_EXPECT_TRUE(parsed.scenario.has_value());
  if (!parsed.scenario) return {};
  RunOptions opt;
  opt.library_metrics = false;
  opt.link_trace = true;
  return RunCall(*parsed.scenario, opt);
}

std::set<int64_t> Arrivals(const std::vector<sim::LinkTraceRecord>& trace) {
  std::set<int64_t> out;
  for (const auto& r : trace)
    if (r.arrival_us >= 0) out.insert(r.arrival_us);
  return out;
}

}  // namespace

SM_TEST(TestbenchEventTiming, RetransmissionsLeaveAtTheNackArrivalAndNacksAtThePacketArrival) {
  // random_loss_5pct's link with jitter, so arrivals fall off the 1 ms grid.
  const Report rep = Traced("name timing\nduration 10s\nat 0s capacity=2500kbps delay=40ms jitter=5ms loss=5%\n");
  SM_ASSERT_TRUE(!rep.link_trace.empty());
  SM_ASSERT_TRUE(!rep.reverse_trace.empty());
  const std::set<int64_t> fwd_arrivals = Arrivals(rep.link_trace);
  const std::set<int64_t> rev_arrivals = Arrivals(rep.reverse_trace);

  int64_t prev = 0, rev_off_grid = 0, rev_at_arrival = 0;
  for (const auto& r : rep.reverse_trace) {
    SM_EXPECT_GE(r.time_us, prev);  // In the order the link was offered the packets.
    prev = r.time_us;
    if (r.time_us % 1000 != 0) {
      ++rev_off_grid;
      // Off the tick only when a forward packet's arrival triggered it (a NACK, an immediate report).
      rev_at_arrival += fwd_arrivals.count(r.time_us);
    }
  }
  SM_EXPECT_GE(rev_off_grid, 20);
  SM_EXPECT_EQ(rev_at_arrival, rev_off_grid);

  int64_t rtx = 0, rtx_off_grid = 0, rtx_at_nack = 0;
  prev = 0;
  for (const auto& r : rep.link_trace) {
    SM_EXPECT_GE(r.time_us, prev);
    prev = r.time_us;
    if (r.kind != sim::LinkKind::kRtx) continue;
    ++rtx;
    if (r.time_us % 1000 != 0) ++rtx_off_grid;
    rtx_at_nack += rev_arrivals.count(r.time_us);
  }
  SM_ASSERT_GE(rtx, 20);
  // Most retransmissions leave the moment the NACK arrives (libwebrtc: 60 % of its RTX packets off the grid on
  // seed 1 of random_loss_5pct, high_jitter and burst_loss); the rest wait in the pacer.
  SM_EXPECT_GE(rtx_off_grid * 2, rtx);
  SM_EXPECT_GE(rtx_at_nack * 4, rtx * 3);
}

SM_TEST(TestbenchEventTiming, AnEncodeDelayFrameLeavesWhenItsEncodeIsDone) {
  // issue_84's slow encoder: a 720p frame takes about 33 ms, not a whole number of milliseconds.
  const Report rep = Traced(
      "name encode\nduration 3s\nencode_delay on\nencoder_speed 0.3\nat 0s capacity=5000kbps delay=30ms queue=300ms\n");
  int64_t video = 0, off_grid = 0;
  for (const auto& r : rep.link_trace) {
    if (r.kind != sim::LinkKind::kVideo) continue;
    ++video;
    if (r.time_us % 1000 != 0) ++off_grid;
  }
  SM_ASSERT_GE(video, 100);
  // libwebrtc: 13 678 of 13 707 video packets off the grid on seed 1 of issue_84_cpu_overuse_slow_encoder.
  SM_EXPECT_GE(off_grid * 10, video * 9);
}
