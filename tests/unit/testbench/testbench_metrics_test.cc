// The parity metrics of the test bench (issue #60): the video delay
// breakdown, the largest target cut and the send rate over the target.
#include <cmath>

#include "call.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

Sample At(double t_s, double target, double send) {
  Sample s;
  s.t_s = t_s;
  s.target_kbps = target;
  s.send_kbps = send;
  return s;
}

Report WithSeries(const std::string& scenario, const std::vector<Sample>& series) {
  Report rep;
  auto parsed = ParseScenario(scenario);
  SM_EXPECT_TRUE(parsed.scenario.has_value());
  if (parsed.scenario) rep.scenario = *parsed.scenario;
  rep.timeseries = series;
  return rep;
}

VideoFrameTimes Frame(int64_t capture, int64_t submitted, int64_t sent, int64_t complete, int64_t rendered) {
  VideoFrameTimes f;
  f.capture = Timestamp::Millis(capture);
  f.submitted = Timestamp::Millis(submitted);
  f.last_sent = Timestamp::Millis(sent);
  f.complete = Timestamp::Millis(complete);
  f.rendered = Timestamp::Millis(rendered);
  return f;
}

}  // namespace

SM_TEST(TestbenchMetrics, TargetMaxDropIsTheDeepestCutBelowThePreviousPeak) {
  std::vector<Sample> s;
  for (int i = 1; i <= 300; ++i) {
    double t = i / 10.0;
    s.push_back(At(t, t < 15.0 ? 1000 : 600, 500));  // 40% cut at 15 s on an unchanged link.
  }
  Report rep = WithSeries("duration 30s\nat 0s capacity=2000kbps\n", s);
  AddCcMetrics(rep.scenario, 2000, rep);
  SM_EXPECT_NEAR(rep.metrics["cc.target_max_drop_pct"], 40.0, 1e-9);
}

SM_TEST(TestbenchMetrics, TargetMaxDropIgnoresStartupAndLinkChanges) {
  std::vector<Sample> s;
  for (int i = 1; i <= 300; ++i) {
    double t = i / 10.0;
    s.push_back(At(t, t < 14.0 ? 1000 : 600, 500));
  }
  // The cut coincides with a capacity change at 14 s; the 5 s after it are excluded.
  Report rep = WithSeries("duration 30s\nat 0s capacity=2000kbps\nat 14s capacity=700kbps\n", s);
  AddCcMetrics(rep.scenario, 2000, rep);
  SM_EXPECT_NEAR(rep.metrics["cc.target_max_drop_pct"], 0.0, 1e-9);
}

SM_TEST(TestbenchMetrics, SendOverTargetP95IsTheTopPercentileOfTheExcess) {
  std::vector<Sample> s;
  // 180 windows after the first 2 s; the last 20 (11%) send at 2x the target.
  for (int i = 1; i <= 200; ++i) s.push_back(At(i / 10.0, 1000, i > 180 ? 2000 : 900));
  Report rep = WithSeries("duration 20s\n", s);
  AddCcMetrics(rep.scenario, 2000, rep);
  SM_EXPECT_NEAR(rep.metrics["cc.send_over_target_p95_pct"], 100.0, 1e-9);
  SM_EXPECT_NEAR(rep.metrics["cc.send_over_target_ms"], 2000.0, 1e-9);  // The existing metric is unchanged.
}

SM_TEST(TestbenchMetrics, SendBelowTargetIsNotAnExcess) {
  std::vector<Sample> s;
  for (int i = 1; i <= 100; ++i) s.push_back(At(i / 10.0, 1000, 800));
  Report rep = WithSeries("duration 10s\n", s);
  AddCcMetrics(rep.scenario, 2000, rep);
  SM_EXPECT_EQ(rep.metrics["cc.send_over_target_p95_pct"], 0.0);
}

SM_TEST(TestbenchMetrics, VideoDelayBreakdownSplitsEachFrame) {
  // Every frame: 4 ms of encoding, 10 ms in the pacer, 30 ms on the
  // network, 20 ms in the jitter buffer.
  std::vector<VideoFrameTimes> frames;
  for (int i = 0; i < 50; ++i) frames.push_back(Frame(i * 33, i * 33 + 4, i * 33 + 14, i * 33 + 44, i * 33 + 64));
  Report rep;
  AddVideoDelayMetrics(frames, rep);
  SM_EXPECT_NEAR(rep.metrics["video.delay_encode_ms"], 4.0, 1e-9);
  SM_EXPECT_NEAR(rep.metrics["video.delay_pacer_p95_ms"], 10.0, 1e-9);
  SM_EXPECT_NEAR(rep.metrics["video.delay_network_p95_ms"], 30.0, 1e-9);
  SM_EXPECT_NEAR(rep.metrics["video.delay_jitter_buffer_p95_ms"], 20.0, 1e-9);
}

SM_TEST(TestbenchMetrics, VideoDelayBreakdownSkipsFramesThatWereNotRendered) {
  std::vector<VideoFrameTimes> frames;
  frames.push_back(Frame(0, 0, 10, 40, 60));
  VideoFrameTimes lost = Frame(33, 33, 43, 73, 93);
  lost.rendered = Timestamp::MinusInfinity();
  frames.push_back(lost);
  VideoFrameTimes never_complete = Frame(66, 66, 76, 106, 126);
  never_complete.complete = Timestamp::MinusInfinity();
  frames.push_back(never_complete);
  Report rep;
  AddVideoDelayMetrics(frames, rep);
  SM_EXPECT_NEAR(rep.metrics["video.delay_pacer_p95_ms"], 10.0, 1e-9);
  SM_EXPECT_NEAR(rep.metrics["video.delay_network_p95_ms"], 30.0, 1e-9);
}

SM_TEST(TestbenchMetrics, NoRenderedFrameAddsNoDelayMetric) {
  Report rep;
  AddVideoDelayMetrics({}, rep);
  SM_EXPECT_TRUE(rep.metrics.empty());
}

SM_TEST(TestbenchMetrics, ACallReportsTheNewMetricsAndTheBreakdownAddsUp) {
  auto parsed = ParseScenario("duration 12s\nat 0s capacity=2500kbps delay=25ms queue=300ms\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  RunOptions opt;
  opt.library_metrics = false;
  const Report rep = RunCall(*parsed.scenario, opt);
  for (const char* k : {"video.delay_encode_ms", "video.delay_pacer_p95_ms", "video.delay_network_p95_ms",
                        "video.delay_jitter_buffer_p95_ms", "cc.send_over_target_p95_pct", "cc.target_max_drop_pct"}) {
    SM_EXPECT_TRUE(rep.metrics.count(k) == 1);
  }
  const auto& m = rep.metrics;
  SM_EXPECT_EQ(m.at("video.delay_encode_ms"), 0.0);
  // The one-way propagation delay is a floor for the network part.
  SM_EXPECT_GE(m.at("video.delay_network_p95_ms"), 25.0);
  // No part exceeds the total, and the parts' p95s add up to at least the total's.
  const double e2e = m.at("video.e2e_delay_p95_ms");
  SM_EXPECT_LE(m.at("video.delay_pacer_p95_ms"), e2e);
  SM_EXPECT_LE(m.at("video.delay_network_p95_ms"), e2e);
  SM_EXPECT_LE(m.at("video.delay_jitter_buffer_p95_ms"), e2e);
  SM_EXPECT_LE(e2e, m.at("video.delay_pacer_p95_ms") + m.at("video.delay_network_p95_ms") +
                        m.at("video.delay_jitter_buffer_p95_ms") + 1.0);
}

// The forward-link trace (issue #125): one record per packet offered to the link.
SM_TEST(TestbenchMetrics, LinkTraceCsvWritesOneLinePerPacket) {
  std::vector<sim::LinkTraceRecord> trace;
  trace.push_back({0, 213, sim::LinkKind::kAudio, 7, 0, sim::LinkOutcome::kDelivered});
  trace.push_back({20000, 1200, sim::LinkKind::kRtx, 65535, 450, sim::LinkOutcome::kQueueDrop});
  trace.push_back({20500, 60, sim::LinkKind::kOther, -1, 0, sim::LinkOutcome::kLost});
  SM_EXPECT_EQ(LinkTraceCsv(trace),
               "time_us,size,kind,seq,queue_bytes,outcome\n"
               "0,213,audio,7,0,delivered\n"
               "20000,1200,rtx,65535,450,queue_drop\n"
               "20500,60,other,-1,0,lost\n");
  SM_EXPECT_EQ(LinkTraceCsv({}), "time_us,size,kind,seq,queue_bytes,outcome\n");
}

SM_TEST(TestbenchMetrics, ACallRecordsTheLinkTraceOnRequestAndItAgreesWithTheCounters) {
  auto parsed = ParseScenario("name trace\nduration 4s\nat 0s capacity=300kbps delay=30ms queue=200ms\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  RunOptions opt;
  opt.library_metrics = false;
  SM_EXPECT_TRUE(RunCall(*parsed.scenario, opt).link_trace.empty());
  opt.link_trace = true;
  const Report rep = RunCall(*parsed.scenario, opt);
  SM_ASSERT_TRUE(!rep.link_trace.empty());
  uint64_t audio = 0, audio_drops = 0, video_bytes = 0;
  int64_t prev = 0;
  for (const auto& r : rep.link_trace) {
    SM_EXPECT_GE(r.time_us, prev);  // In the order the link was offered the packets.
    prev = r.time_us;
    if (r.kind == sim::LinkKind::kAudio) {
      ++audio;
      audio_drops += r.outcome == sim::LinkOutcome::kQueueDrop;
      SM_EXPECT_GE(r.seq, 0);
    }
    if (r.kind == sim::LinkKind::kVideo) video_bytes += static_cast<uint64_t>(r.size);
  }
  SM_EXPECT_TRUE(audio > 0);
  // The trace and the per-kind metrics describe the same packets.
  SM_EXPECT_EQ(static_cast<double>(audio_drops), rep.metrics.at("net.audio_queue_drops"));
  const double video_kbps = rep.metrics.at("net.video_kbps");
  SM_EXPECT_TRUE(std::abs(static_cast<double>(video_bytes) * 8 / 1000.0 / 4.0 - video_kbps) < 0.01);
}
