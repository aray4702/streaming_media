// The parity metrics of the test bench (issue #60): the video delay
// breakdown, the largest target cut and the send rate over the target.
#include <cmath>
#include <map>

#include "call.h"
#include "call_common.h"
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

// libwebrtc's AudioSendStream registers 32000 + 48 B / 20 ms with BitrateAllocator
// (issue #196: 51200 in sm_compare once the route arrives after Start, PR #198).
SM_TEST(TestbenchMetrics, AudioAllocationRateIsLibwebrtcs) {
  Scenario s;
  SM_EXPECT_EQ(AudioAllocationRate(s).bps(), static_cast<int64_t>(51200));
  s.audio = false;
  SM_EXPECT_EQ(AudioAllocationRate(s).bps(), static_cast<int64_t>(0));
}

// The per-second video send trace (issue #131).
SM_TEST(TestbenchMetrics, VideoTraceCsvWritesOneLinePerSecond) {
  std::vector<VideoTraceRow> trace(2);
  trace[0].t_ms = 1000;
  trace[0].encoder_target_bps = 50000;
  trace[0].alloc_target_bps = 60000;
  trace[0].captured = 30;
  trace[0].encoded = 24;
  trace[0].dropped_rate_limiter = 6;
  trace[0].encoded_bytes = 9000;
  trace[0].pacer_queue_ms = 12.54;
  trace[1].t_ms = 2000;
  trace[1].dropped_congestion_window = 2;
  trace[1].dropped_encoder_queue = 1;
  trace[1].paused = 1;
  SM_EXPECT_EQ(VideoTraceCsv(trace),
               "t_s,encoder_target_bps,alloc_target_bps,captured,encoded,dropped_capturer,dropped_encoder_queue,"
               "dropped_rate_limiter,dropped_congestion_window,dropped_encoder,paused,encoded_bytes,pacer_queue_ms\n"
               "1,50000,60000,30,24,0,0,6,0,0,0,9000,12.5\n"
               "2,0,0,0,0,0,1,0,2,0,1,0,0.0\n");
}

SM_TEST(TestbenchMetrics, ACallRecordsTheVideoTraceOnRequestAndItAgreesWithTheCounters) {
  auto parsed = ParseScenario("name vtrace\nduration 6s\nat 0s capacity=150kbps delay=30ms queue=200ms\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  RunOptions opt;
  opt.library_metrics = false;
  SM_EXPECT_TRUE(RunCall(*parsed.scenario, opt).video_trace.empty());
  opt.video_trace = true;
  const Report rep = RunCall(*parsed.scenario, opt);
  SM_ASSERT_EQ(rep.video_trace.size(), static_cast<size_t>(6));
  int64_t dropped = 0, bytes = 0;
  for (size_t i = 0; i < rep.video_trace.size(); ++i) {
    const VideoTraceRow& r = rep.video_trace[i];
    SM_EXPECT_EQ(r.t_ms, static_cast<int64_t>((i + 1) * 1000));
    // Every captured frame is encoded or dropped for exactly one reason.
    SM_EXPECT_EQ(r.captured, r.encoded + r.dropped_capturer + r.dropped_encoder_queue + r.dropped_rate_limiter +
                                 r.dropped_congestion_window + r.dropped_encoder);
    dropped += r.captured - r.encoded;
    bytes += r.encoded_bytes;
  }
  SM_EXPECT_EQ(static_cast<double>(dropped), rep.metrics.at("video.encoder_dropped"));
  SM_EXPECT_TRUE(std::abs(static_cast<double>(bytes) * 8 / 1000.0 / 6.0 - rep.metrics.at("video.avg_kbps")) < 0.01);
}

SM_TEST(TestbenchMetrics, CompareSeedsIsParsedForSmCompareAndRangeChecked) {
  auto parsed = ParseScenario("name seeds\ncompare_seeds 40\nduration 2s\n");
  SM_ASSERT_TRUE(parsed.scenario.has_value());
  SM_EXPECT_EQ(parsed.scenario->compare_seeds, 40);
  SM_EXPECT_EQ(ParseScenario("duration 2s\n").scenario->compare_seeds, 0);
  SM_EXPECT_TRUE(!ParseScenario("compare_seeds 0\n").scenario.has_value());
  SM_EXPECT_TRUE(!ParseScenario("compare_seeds 201\n").scenario.has_value());
  // sm_testbench runs the scenario the same with or without the directive.
  RunOptions opt;
  opt.library_metrics = false;
  const Report with = RunCall(*parsed.scenario, opt);
  auto plain = ParseScenario("name seeds\nduration 2s\n");
  SM_ASSERT_TRUE(plain.scenario.has_value());
  SM_EXPECT_EQ(with.metrics.at("video.avg_kbps"), RunCall(*plain.scenario, opt).metrics.at("video.avg_kbps"));
}

// The GoogCC estimate trace (issue #149).
SM_TEST(TestbenchMetrics, BweTraceCsvWritesOneLinePerEventAndLeavesMissingColumnsEmpty) {
  using Kind = cc::BweTraceRow::Kind;
  std::vector<cc::BweTraceRow> trace(4);
  trace[0].time_us = 150000;
  trace[0].kind = Kind::kDelayBased;
  trace[0].bitrate_bps = 924666;
  trace[0].detector = 2;
  trace[1].time_us = 150000;
  trace[1].kind = Kind::kLossBased;
  trace[1].bitrate_bps = 900000;
  trace[1].fraction_loss = 13;
  trace[1].packets = 40;
  trace[1].loss_state = 2;
  trace[1].loss_limit_bps = 880000;
  trace[1].instant_ub_bps = 5000000;
  trace[2].time_us = 200000;
  trace[2].bitrate_bps = 900000;
  trace[3].time_us = 200000;  // Issue #152.
  trace[3].kind = Kind::kDetector;
  trace[3].detector = 0;
  trace[3].packets = 91;
  trace[3].modified_trend = -0.25;
  trace[3].threshold = 6.19079;
  trace[3].send_delta_ms = 5;
  trace[3].recv_delta_ms = 7.75;
  trace[3].send_ms = 3592;
  trace[3].arrival_ms = 3659;
  SM_EXPECT_EQ(BweTraceCsv(trace),
               "time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps,"
               "modified_trend,threshold,send_delta_ms,recv_delta_ms,send_ms,arrival_ms\n"
               "150000,delay_based,924666,overusing,,,,,,,,,,,\n"
               "150000,loss_based,900000,,13,40,decreasing,880000,5000000,,,,,,\n"
               "200000,target,900000,,,,,,,,,,,,\n"
               "200000,detector,0,normal,,91,,,,-0.25,6.19079,5,7.75,3592,3659\n");
}

SM_TEST(TestbenchMetrics, ACallRecordsTheBweTraceOnRequestWithCcV2Only) {
  const std::string link = "duration 6s\nat 0s capacity=800kbps delay=30ms queue=200ms\nat 3s loss=3%\n";
  RunOptions opt;
  opt.library_metrics = false;
  for (const bool v2 : {false, true}) {
    auto parsed = ParseScenario(std::string("name bwetrace\n") + (v2 ? "cc v2\n" : "cc v1\n") + link);
    SM_ASSERT_TRUE(parsed.scenario.has_value());
    opt.bwe_trace = false;
    SM_EXPECT_TRUE(RunCall(*parsed.scenario, opt).bwe_trace.empty());
    opt.bwe_trace = true;
    const Report rep = RunCall(*parsed.scenario, opt);
    if (!v2) {
      SM_EXPECT_TRUE(rep.bwe_trace.empty());
      continue;
    }
    SM_ASSERT_TRUE(!rep.bwe_trace.empty());
    int delay = 0, loss = 0, target = 0, detector = 0;
    int64_t prev = 0;
    for (const auto& r : rep.bwe_trace) {
      SM_EXPECT_GE(r.time_us, prev);
      prev = r.time_us;
      switch (r.kind) {
        case cc::BweTraceRow::Kind::kDelayBased: ++delay; SM_EXPECT_TRUE(r.detector >= 0 && r.detector <= 2); break;
        case cc::BweTraceRow::Kind::kLossBased: ++loss; SM_EXPECT_TRUE(r.fraction_loss >= 0 && r.loss_state >= 0); break;
        case cc::BweTraceRow::Kind::kTarget: ++target; break;
        case cc::BweTraceRow::Kind::kDetector:
          ++detector;
          SM_EXPECT_TRUE(r.detector >= 0 && r.detector <= 2 && r.packets > 0);
          SM_EXPECT_TRUE(std::isfinite(r.modified_trend) && r.threshold > 0 && std::isfinite(r.send_delta_ms));
          break;
      }
    }
    SM_EXPECT_TRUE(delay > 0 && loss > 0 && target > 0 && detector > 0);
  }
}

// The A/V sync offset (issue #63), as libwebrtc's GetStreamSyncOffsetInMs:
// the audio playout point moved on to the render time, minus the frame's capture.
SM_TEST(TestbenchMetrics, AvSyncOffset) {
  AvSyncMeter meter;
  // A frame before audio playout starts is not counted.
  meter.OnVideoRendered(Timestamp::Millis(50), Timestamp::Millis(0));
  std::map<std::string, double> none;
  meter.AddMetrics(none);
  SM_EXPECT_TRUE(none.empty());
  // Audio captured at 40 ms plays at 100 ms (60 ms behind).
  meter.OnAudioPlayout(Timestamp::Millis(100), Timestamp::Millis(40));
  // Rendered 5 ms later, 105 ms after capture: the audio playing then was
  // captured at 45 ms, so the video is 45 ms late.
  meter.OnVideoRendered(Timestamp::Millis(105), Timestamp::Millis(0));
  // Rendered 10 ms after capture: 20 ms ahead of the audio.
  meter.OnAudioPlayout(Timestamp::Millis(110), Timestamp::Millis(50));
  meter.OnVideoRendered(Timestamp::Millis(110), Timestamp::Millis(70));
  std::map<std::string, double> m;
  meter.AddMetrics(m);
  SM_EXPECT_NEAR(m["sync.av_offset_p50_ms"], 12.5, 1e-9);
  SM_EXPECT_NEAR(m["sync.av_offset_p95_ms"], 43.75, 1e-9);
  SM_EXPECT_NEAR(m["sync.av_offset_max_ms"], 45, 1e-9);
}
