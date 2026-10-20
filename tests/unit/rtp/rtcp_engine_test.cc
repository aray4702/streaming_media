#include <algorithm>
#include <vector>

#include "sm/core/random.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {

std::vector<loss::RtcpType> Types(const Bytes& datagram) {
  std::vector<loss::RtcpType> t;
  auto split = loss::SplitCompoundRtcp(datagram);
  if (split) {
    for (const auto& p : *split) t.push_back(p.type);
  }
  return t;
}

// Times (ms) at which Process emitted a report, running 1 ms ticks.
std::vector<int64_t> ReportTimes(RtcpEngine& e, int64_t until_ms) {
  std::vector<int64_t> times;
  for (int64_t t = 0; t <= until_ms; ++t) {
    for (const auto& o : e.Process(Timestamp::Millis(t))) {
      if (o.kind == RtcpOutKind::kReport) times.push_back(t);
    }
  }
  return times;
}

}  // namespace

SM_TEST(RtcpEngine, ReportsStayOnTheConfiguredGrid) {
  // The test bench phases: SR at 500 ms + k s, RR at 1000 ms + k s.
  RtcpConfig sc;
  sc.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine sender(sc);
  sender.SetSenderState({.ssrc = 7});
  auto st = ReportTimes(sender, 5600);
  SM_EXPECT_TRUE((st == std::vector<int64_t>{500, 1500, 2500, 3500, 4500, 5500}));

  RtcpConfig rc;
  rc.first_report_delay = TimeDelta::Millis(1000);
  RtcpEngine receiver(rc);
  auto rt = ReportTimes(receiver, 3100);
  SM_EXPECT_TRUE((rt == std::vector<int64_t>{1000, 2000, 3000}));
}

SM_TEST(RtcpEngine, DefaultIntervalsFollowLibwebrtc) {
  // First report at half the interval, then every interval: 1 s with video, 5 s without.
  RtcpConfig v;
  RtcpEngine video(v);
  SM_EXPECT_TRUE((ReportTimes(video, 3100) == std::vector<int64_t>{500, 1500, 2500}));
  RtcpConfig a;
  a.has_video = false;
  RtcpEngine audio(a);
  SM_EXPECT_TRUE((ReportTimes(audio, 13000) == std::vector<int64_t>{2500, 7500, 12500}));
}

SM_TEST(RtcpEngine, RandomizedIntervalsStayInRangeAndAreDeterministic) {
  auto run = [](uint64_t seed) {
    RtcpConfig c;
    c.randomize_intervals = true;
    c.seed = seed;
    RtcpEngine e(c);
    return ReportTimes(e, 60000);
  };
  auto a = run(5), b = run(5), other = run(6);
  SM_EXPECT_TRUE(a == b);
  SM_EXPECT_TRUE(a != other);
  // 60 s at a mean of 1 s: about 60 reports.
  SM_EXPECT_GT(a.size(), 45u);
  SM_EXPECT_LT(a.size(), 80u);
  SM_ASSERT_GT(a.size(), 2u);
  SM_EXPECT_GE(a[0], 250);  // 0.5 * (0.5 .. 1.5) s
  SM_EXPECT_LE(a[0], 750);
  for (size_t i = 1; i < a.size(); ++i) {
    SM_EXPECT_GE(a[i] - a[i - 1], 500);
    SM_EXPECT_LE(a[i] - a[i - 1], 1501);
  }
}

SM_TEST(RtcpEngine, ReportIsACompoundOfSrOrRrThenSdes) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  stats.OnPacket(2, 3000, Timestamp::Millis(40));
  RtcpConfig c;
  c.local_ssrc = 100;
  c.cname = "user@host";
  c.first_report_delay = TimeDelta::Millis(10);
  RtcpEngine rx(c);
  rx.AddReceiveStatistics(&stats);
  SM_EXPECT_TRUE(rx.Process(Timestamp::Millis(0)).empty());
  auto out = rx.Process(Timestamp::Millis(10));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE((Types(out[0].bytes) ==
                  std::vector<loss::RtcpType>{loss::RtcpType::kReceiverReport, loss::RtcpType::kSdes}));
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  auto rr = timing::ParseReceiverReport((*split)[0].data);
  SM_ASSERT_TRUE(rr.has_value());
  SM_EXPECT_EQ(rr->ssrc, 100u);
  SM_ASSERT_EQ(rr->blocks.size(), 1u);
  SM_EXPECT_EQ(rr->blocks[0].ssrc, 0x1234u);
  auto sdes = ParseSdes((*split)[1].data);
  SM_ASSERT_TRUE(sdes.has_value());
  SM_EXPECT_TRUE((*sdes)[0].items[0].text == "user@host");
  SM_EXPECT_EQ((*sdes)[0].ssrc, 100u);
  SM_EXPECT_EQ(rx.stats().rr_sent, 1u);
}

SM_TEST(RtcpEngine, SenderReportExtrapolatesTheRtpTimestamp) {
  RtcpConfig c;
  c.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine tx(c);
  RtcpSenderState s;
  s.ssrc = 42;
  s.packet_count = 11;
  s.octet_count = 12345;
  s.rtp_timestamp = 1000;
  s.reference_time = Timestamp::Millis(0);
  tx.SetSenderState(s);
  tx.Process(Timestamp::Millis(0));
  auto out = tx.Process(Timestamp::Millis(500));
  SM_ASSERT_EQ(out.size(), 1u);
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  SM_ASSERT_TRUE(split.has_value());
  auto sr = timing::ParseSenderReport((*split)[0].data);
  SM_ASSERT_TRUE(sr.has_value());
  SM_EXPECT_EQ(sr->ssrc, 42u);
  SM_EXPECT_EQ(sr->rtp_timestamp, 1000u + 500u * 90u);
  SM_EXPECT_EQ(sr->packet_count, 11u);
  SM_EXPECT_EQ(sr->octet_count, 12345u);
  SM_EXPECT_EQ(sr->ntp.value, timing::NtpTime::FromTimestamp(Timestamp::Millis(500)).value);
  SM_EXPECT_EQ(tx.stats().sr_sent, 1u);
}

SM_TEST(RtcpEngine, EarlyFeedbackSharesOneCompoundWithALeadingRr) {
  RtcpConfig c;
  c.local_ssrc = 9;
  RtcpEngine e(c);
  e.Process(Timestamp::Millis(0));
  std::vector<uint16_t> seqs = {10, 11, 40};
  e.QueueNack(0xAA, seqs);
  e.QueuePli(0xAA);
  e.QueueFir(0xAA);
  loss::LossNotification l;
  l.last_decoded = 3;
  l.last_received = 5;
  e.QueueLossNotification(l);
  SM_EXPECT_EQ(e.NextProcessTime(Timestamp::Millis(1)), Timestamp::Millis(1));
  auto out = e.Process(Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE(out[0].kind == RtcpOutKind::kFeedback);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kNack, T::kPli, T::kFir,
                                                         T::kLossNotification}));
  SM_EXPECT_EQ(e.stats().nack_seqs_sent, 3u);
  SM_EXPECT_EQ(e.stats().pli_sent, 1u);
  SM_EXPECT_EQ(e.stats().feedback_datagrams, 1u);
  SM_EXPECT_TRUE(e.Process(Timestamp::Millis(2)).empty());  // Queue was drained.
  // The sender SSRC is the engine's.
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  auto nack = loss::ParseNack((*split)[1].data);
  SM_ASSERT_TRUE(nack.has_value());
  SM_EXPECT_EQ(nack->sender_ssrc, 9u);
  SM_EXPECT_EQ(nack->media_ssrc, 0xAAu);
}

// RTCPSender::PrepareReport in compound mode (rtcp_sender.cc): with `rtcp v2`, a
// NACK or PLI goes out with the full RR (report blocks) and SDES, and restarts
// the report timer; transport-cc keeps the empty leading RR (#178).
SM_TEST(RtcpEngine, V2NackCarriesTheFullReportAndRestartsTheTimer) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  stats.OnPacket(3, 3000, Timestamp::Millis(40));
  RtcpConfig c;
  c.local_ssrc = 100;
  c.cname = "user@host";
  c.version = 2;
  c.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine rx(c);
  rx.AddReceiveStatistics(&stats);
  rx.Process(Timestamp::Millis(0));
  const std::vector<uint16_t> seqs = {2};
  rx.QueueNack(0x1234, seqs);
  auto out = rx.Process(Timestamp::Millis(100));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE(out[0].kind == RtcpOutKind::kFeedback);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kSdes, T::kNack}));
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  auto rr = timing::ParseReceiverReport((*split)[0].data);
  SM_ASSERT_TRUE(rr.has_value());
  SM_ASSERT_EQ(rr->blocks.size(), 1u);
  SM_EXPECT_EQ(rr->blocks[0].ssrc, 0x1234u);
  SM_EXPECT_GT(rr->blocks[0].fraction_lost, 0);
  // The report due at 500 ms moved to at least 100 + 0.5 x 1 s.
  SM_EXPECT_GE(rx.NextProcessTime(Timestamp::Millis(101)), Timestamp::Millis(600));

  rx.QueueTransportFeedback(cc::TransportFeedback{});
  out = rx.Process(Timestamp::Millis(200));
  SM_ASSERT_EQ(out.size(), 1u);
  split = loss::SplitCompoundRtcp(out[0].bytes);
  rr = timing::ParseReceiverReport((*split)[0].data);
  SM_ASSERT_TRUE(rr.has_value());
  SM_EXPECT_TRUE(rr->blocks.empty());
}

// Version 1 keeps the empty leading RR on every early feedback packet.
SM_TEST(RtcpEngine, V1NackLeadsWithAnEmptyRr) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  RtcpConfig c;
  c.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine rx(c);
  rx.AddReceiveStatistics(&stats);
  rx.Process(Timestamp::Millis(0));
  const std::vector<uint16_t> seqs = {2};
  rx.QueueNack(0x1234, seqs);
  auto out = rx.Process(Timestamp::Millis(100));
  SM_ASSERT_EQ(out.size(), 1u);
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  auto rr = timing::ParseReceiverReport((*split)[0].data);
  SM_ASSERT_TRUE(rr.has_value());
  SM_EXPECT_TRUE(rr->blocks.empty());
  SM_EXPECT_EQ(rx.NextProcessTime(Timestamp::Millis(101)), Timestamp::Millis(500));
}

SM_TEST(RtcpEngine, ReducedSizeFeedbackHasNoLeadingReport) {
  RtcpConfig c;
  c.reduced_size = true;
  RtcpEngine e(c);
  e.Process(Timestamp::Millis(0));
  e.QueuePli(1);
  auto out = e.Process(Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<loss::RtcpType>{loss::RtcpType::kPli}));
}

SM_TEST(RtcpEngine, FeedbackIsSplitBeforeTheDatagramLimit) {
  RtcpConfig c;
  c.max_datagram_bytes = 100;
  RtcpEngine e(c);
  e.Process(Timestamp::Millis(0));
  for (int i = 0; i < 20; ++i) e.QueuePli(static_cast<uint32_t>(i));  // 12 bytes each.
  auto out = e.Process(Timestamp::Millis(1));
  SM_EXPECT_GT(out.size(), 2u);
  size_t plis = 0;
  for (const auto& o : out) {
    SM_EXPECT_LE(o.bytes.size(), 100u);
    auto t = Types(o.bytes);
    SM_ASSERT_TRUE(!t.empty());
    SM_EXPECT_TRUE(t[0] == loss::RtcpType::kReceiverReport);  // Every datagram is a valid compound.
    plis += static_cast<size_t>(std::count(t.begin(), t.end(), loss::RtcpType::kPli));
  }
  SM_EXPECT_EQ(plis, 20u);
}

SM_TEST(RtcpEngine, TransportFeedbackAndRembSurviveARoundTrip) {
  RtcpConfig c;
  c.local_ssrc = 5;
  RtcpEngine tx(c), rx;
  tx.Process(Timestamp::Millis(0));
  cc::TransportFeedbackBuilder b(0, 77);
  for (int i = 0; i < 6; ++i) b.OnPacket(static_cast<uint16_t>(100 + i), Timestamp::Millis(10 * i));
  auto fbs = b.Build();
  SM_ASSERT_EQ(fbs.size(), 1u);
  tx.QueueTransportFeedback(fbs[0]);
  std::vector<uint32_t> ssrcs = {77};
  tx.QueueRemb(DataRate::KilobitsPerSec(800), ssrcs);
  auto out = tx.Process(Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  auto in = rx.OnRtcp(out[0].bytes, Timestamp::Millis(30));
  SM_EXPECT_TRUE(in.valid);
  SM_ASSERT_EQ(in.transport_feedback.size(), 1u);
  SM_EXPECT_EQ(in.transport_feedback[0].sender_ssrc, 5u);
  SM_EXPECT_EQ(in.transport_feedback[0].packets.size(), 6u);
  SM_ASSERT_EQ(in.remb.size(), 1u);
  SM_EXPECT_NEAR(static_cast<double>(in.remb[0].bitrate.bps()), 800000.0, 8000.0);
  SM_EXPECT_EQ(in.remb[0].ssrcs.size(), 1u);
  SM_EXPECT_EQ(rx.stats().transport_cc_received, 1u);
  SM_EXPECT_EQ(rx.stats().remb_received, 1u);
}

SM_TEST(RtcpEngine, DispatchTableSendsEachTypeToItsOwnList) {
  // One compound with one packet of every type; each lands in exactly one list.
  loss::Nack nack{1, 2, {5, 6}};
  loss::Fir fir{1, {{2, 4}}};
  loss::LossNotification l{1, 2, 3, 4, true};
  cc::TransportFeedback fb;
  fb.media_ssrc = 2;
  fb.packets = {{10, true, 0}, {11, true, 4}};
  timing::SenderReport sr;
  sr.ssrc = 3;
  sr.packet_count = 9;
  SdesChunk chunk{3, {{kSdesCname, "x"}}};
  Bye bye;
  bye.ssrcs = {3};
  std::vector<Bytes> pk = {timing::SerializeReceiverReport({1, {}}),
                           loss::SerializePli({1, 2}),
                           loss::SerializeNack(nack),
                           loss::SerializeFir(fir),
                           loss::SerializeLossNotification(l),
                           cc::SerializeTransportFeedback(fb),
                           cc::SerializeRemb(1, DataRate::KilobitsPerSec(500), {2}),
                           timing::SerializeSenderReport(sr),
                           SerializeSdes(std::span<const SdesChunk>(&chunk, 1)),
                           SerializeBye(bye),
                           Bytes{0x80, 207, 0, 1, 0, 0, 0, 1}};  // XR: valid header, not interpreted.
  Bytes dg = loss::BuildCompoundRtcp(pk);
  SM_ASSERT_TRUE(!dg.empty());
  RtcpEngine e;
  auto in = e.OnRtcp(dg, Timestamp::Millis(0));
  SM_EXPECT_TRUE(in.valid);
  SM_EXPECT_EQ(in.nacks.size(), 1u);
  SM_EXPECT_EQ(in.nacks[0].seqs.size(), 2u);
  SM_EXPECT_EQ(in.keyframe_requests.size(), 2u);  // PLI + FIR
  SM_EXPECT_EQ(in.loss_notifications.size(), 1u);
  SM_EXPECT_EQ(in.transport_feedback.size(), 1u);
  SM_EXPECT_EQ(in.remb.size(), 1u);
  SM_EXPECT_EQ(in.sender_reports.size(), 1u);
  SM_EXPECT_EQ(in.sdes.size(), 1u);
  SM_EXPECT_EQ(in.byes.size(), 1u);
  SM_EXPECT_EQ(in.ignored, 1u);
  SM_EXPECT_EQ(e.stats().malformed, 0u);
}

SM_TEST(RtcpEngine, LossNotificationNeverRequestsAKeyframe) {
  RtcpEngine e;
  Bytes dg = loss::BuildCompoundRtcp(std::vector<Bytes>{timing::SerializeReceiverReport({1, {}}),
                                                       loss::SerializeLossNotification({1, 2, 3, 4, false})});
  auto in = e.OnRtcp(dg, Timestamp::Millis(0));
  SM_EXPECT_TRUE(in.keyframe_requests.empty());
  SM_EXPECT_EQ(in.loss_notifications.size(), 1u);
}

SM_TEST(RtcpEngine, RepeatedFirSequenceNumberIsIgnored) {
  RtcpEngine tx, rx;
  tx.Process(Timestamp::Millis(0));
  auto send = [&](int64_t t) {
    tx.QueueFir(55);
    auto out = tx.Process(Timestamp::Millis(t));
    return out[0].bytes;
  };
  Bytes first = send(1);
  SM_EXPECT_EQ(rx.OnRtcp(first, Timestamp::Millis(2)).keyframe_requests.size(), 1u);
  // The same datagram again is a retransmitted request.
  SM_EXPECT_EQ(rx.OnRtcp(first, Timestamp::Millis(3)).keyframe_requests.size(), 0u);
  SM_EXPECT_EQ(rx.stats().fir_duplicates, 1u);
  // A new request has the next sequence number.
  SM_EXPECT_EQ(rx.OnRtcp(send(4), Timestamp::Millis(5)).keyframe_requests.size(), 1u);
}

SM_TEST(RtcpEngine, RttComesFromReportBlocksOfLocalStreams) {
  // The SR leaves at 500 ms and arrives at 550 ms. The receiver holds it for
  // 90 ms (DLSR) and the report arrives at 650 ms: RTT = 650 - 500 - 90 = 60 ms.
  RtcpConfig sc;
  sc.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine sender(sc);
  sender.SetSenderState({.ssrc = 7});
  sender.AddLocalMediaSsrc(7);
  sender.Process(Timestamp::Millis(0));
  auto sr = sender.Process(Timestamp::Millis(500));
  SM_ASSERT_EQ(sr.size(), 1u);

  timing::ReceiveStatistics stats(7, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  RtcpConfig rc;
  rc.local_ssrc = 8;
  rc.first_report_delay = TimeDelta::Millis(90);
  RtcpEngine receiver(rc);
  receiver.AddReceiveStatistics(&stats);
  auto in_rx = receiver.OnRtcp(sr[0].bytes, Timestamp::Millis(550));
  SM_ASSERT_EQ(in_rx.sender_reports.size(), 1u);
  stats.OnSenderReport(in_rx.sender_reports[0], Timestamp::Millis(550));
  receiver.Process(Timestamp::Millis(550));
  auto rr = receiver.Process(Timestamp::Millis(640));
  SM_ASSERT_EQ(rr.size(), 1u);
  auto in_tx = sender.OnRtcp(rr[0].bytes, Timestamp::Millis(650));
  SM_ASSERT_EQ(in_tx.rtts.size(), 1u);
  SM_EXPECT_NEAR(in_tx.rtts[0].ms_f(), 60.0, 0.1);
  SM_EXPECT_EQ(in_tx.report_blocks.size(), 1u);
}

SM_TEST(RtcpEngine, BlocksForOtherSsrcsGiveNoRtt) {
  RtcpConfig sc;
  sc.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine sender(sc);
  sender.SetSenderState({.ssrc = 7});
  sender.AddLocalMediaSsrc(7);
  timing::ReportBlock rb;
  rb.ssrc = 99;
  rb.last_sr = 1;
  Bytes rr = timing::SerializeReceiverReport({8, {rb}});
  auto in = sender.OnRtcp(rr, Timestamp::Millis(1000));
  SM_EXPECT_EQ(in.report_blocks.size(), 1u);
  SM_EXPECT_TRUE(in.rtts.empty());
}

SM_TEST(RtcpEngine, MalformedDatagramsAreCountedAndHarmless) {
  RtcpEngine e;
  SM_EXPECT_FALSE(e.OnRtcp(Bytes{}, Timestamp::Millis(0)).valid);
  SM_EXPECT_FALSE(e.OnRtcp(Bytes{1, 2, 3}, Timestamp::Millis(0)).valid);
  // A good header with a body that does not parse: the datagram is valid, the packet is dropped.
  Bytes bad_nack = {0x81, 205, 0, 1, 0, 0, 0, 1};  // NACK with no items.
  auto in = e.OnRtcp(bad_nack, Timestamp::Millis(0));
  SM_EXPECT_TRUE(in.nacks.empty());
  SM_EXPECT_EQ(e.stats().malformed, 3u);
  // Random and truncated datagrams: no crash, bounded state.
  Random rng(3);
  Bytes good = loss::BuildCompoundRtcp(std::vector<Bytes>{timing::SerializeReceiverReport({1, {}}),
                                                         loss::SerializePli({1, 2}),
                                                         loss::SerializeNack({1, 2, {1, 2, 3}})});
  for (int i = 0; i < 5000; ++i) {
    Bytes d = good;
    if (i % 3 == 0) d.resize(static_cast<size_t>(rng.UniformInt(0, static_cast<int64_t>(d.size()))));
    for (int k = 0; k < 3 && !d.empty(); ++k) d[static_cast<size_t>(rng.UniformInt(0, static_cast<int64_t>(d.size()) - 1))] = static_cast<uint8_t>(rng.Next());
    e.OnRtcp(d, Timestamp::Millis(i));
  }
  SM_EXPECT_EQ(e.stats().datagrams_received, 5003u);
}

SM_TEST(RtcpEngine, ByeIsSentAsFeedbackCompound) {
  RtcpEngine tx, rx;
  tx.Process(Timestamp::Millis(0));
  std::vector<uint32_t> ssrcs = {1, 2};
  tx.QueueBye(ssrcs, "done");
  auto out = tx.Process(Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  auto in = rx.OnRtcp(out[0].bytes, Timestamp::Millis(2));
  SM_ASSERT_EQ(in.byes.size(), 1u);
  SM_EXPECT_TRUE(in.byes[0].reason == "done");
  SM_EXPECT_EQ(in.byes[0].ssrcs.size(), 2u);
}

SM_TEST(RtcpEngine, ExportsMetrics) {
  struct W : MetricsWriter {
    std::vector<std::string> names;
    void Counter(std::string_view n, uint64_t) override { names.emplace_back(n); }
    void Gauge(std::string_view n, double) override { names.emplace_back(n); }
    void Hist(std::string_view n, const Histogram&) override { names.emplace_back(n); }
  } w;
  RtcpEngine e;
  e.ExportMetrics(w);
  SM_EXPECT_TRUE(std::find(w.names.begin(), w.names.end(), "reports_sent") != w.names.end());
  SM_EXPECT_TRUE(std::find(w.names.begin(), w.names.end(), "rtt_ms") != w.names.end());
}

SM_TEST(RtcpEngine, FirStateIsBoundedAndOnlyTracksLocalStreams) {
  auto fir_datagram = [](uint32_t sender, uint32_t media, uint8_t seq) {
    loss::Fir f{sender, {{media, seq}}};
    return loss::BuildCompoundRtcp(std::vector<Bytes>{timing::SerializeReceiverReport({sender, {}}),
                                                     loss::SerializeFir(f)});
  };
  // With local SSRCs registered, requests for other streams are ignored and not tracked.
  RtcpEngine local;
  local.AddLocalMediaSsrc(10);
  for (uint32_t i = 0; i < 1000; ++i)
    SM_EXPECT_TRUE(local.OnRtcp(fir_datagram(i, 99, 0), Timestamp::Millis(i)).keyframe_requests.empty());
  SM_EXPECT_EQ(local.fir_tracked(), 0u);
  SM_EXPECT_EQ(local.OnRtcp(fir_datagram(1, 10, 3), Timestamp::Millis(0)).keyframe_requests.size(), 1u);
  SM_EXPECT_EQ(local.OnRtcp(fir_datagram(1, 10, 3), Timestamp::Millis(1)).keyframe_requests.size(), 0u);
  // Without registration every SSRC is accepted, but the table stays capped.
  RtcpEngine open;
  for (uint32_t i = 0; i < 1000; ++i) {
    open.OnRtcp(fir_datagram(i, 5000 + i, 0), Timestamp::Millis(i));
    SM_ASSERT_LE(open.fir_tracked(), RtcpEngine::kMaxFirTracked);
  }
  // A repeated seq_nr is still de-duplicated for a tracked pair.
  SM_EXPECT_EQ(open.OnRtcp(fir_datagram(7, 77, 1), Timestamp::Millis(0)).keyframe_requests.size(), 1u);
  SM_EXPECT_EQ(open.OnRtcp(fir_datagram(7, 77, 1), Timestamp::Millis(1)).keyframe_requests.size(), 0u);
}

// RTCPSender::ComputeTimeUntilNextReport (#76): [0.5, 1.5] x the minimum, and
// a video sender above 72 kbit/s reports every 360 / kbit/s seconds.
SM_TEST(RtcpEngine, V2ReportIntervalFollowsTheSendBitrateRule) {
  auto gaps = [](int64_t bytes_per_ms) {
    RtcpConfig c;
    c.version = 2;
    RtcpEngine e(c);
    RtcpSenderState st;
    st.ssrc = 7;
    std::vector<int64_t> times;
    for (int64_t t = 0; t <= 20000; ++t) {
      st.octet_count = static_cast<uint32_t>(t * bytes_per_ms);
      e.SetSenderState(st);
      for (const auto& o : e.Process(Timestamp::Millis(t))) {
        if (o.kind == RtcpOutKind::kReport) times.push_back(t);
      }
    }
    std::vector<int64_t> g;
    for (size_t i = 2; i < times.size(); ++i) g.push_back(times[i] - times[i - 1]);  // Skip start-up.
    return g;
  };
  const auto fast = gaps(125);  // 1 Mbit/s: minimum 360 ms.
  SM_ASSERT_TRUE(fast.size() > 20);
  for (int64_t g : fast) SM_EXPECT_TRUE(g >= 150 && g <= 560);
  const auto slow = gaps(5);  // 40 kbit/s: nominal 1 s.
  SM_ASSERT_TRUE(slow.size() > 8);
  bool varied = false;
  for (int64_t g : slow) {
    SM_EXPECT_TRUE(g >= 450 && g <= 1550);
    varied = varied || g != slow.front();
  }
  SM_EXPECT_TRUE(varied);
}

// RTCPSender::TimeToSendRTCPReport(send_keyframe_before_rtp) (#101): a video
// report due within 100 ms goes out before the keyframe; audio gets no margin.
SM_TEST(RtcpEngine, V2ReportDueWithin100msIsSentBeforeAKeyframe) {
  RtcpConfig c;
  c.version = 2;
  c.first_report_delay = TimeDelta::Millis(1000);
  RtcpEngine e(c);
  e.Process(Timestamp::Millis(0));
  SM_EXPECT_FALSE(e.ReportDue(Timestamp::Millis(800), false));
  SM_EXPECT_FALSE(e.ReportDue(Timestamp::Millis(800), true));   // 200 ms early.
  SM_EXPECT_TRUE(e.ReportDue(Timestamp::Millis(900), true));    // Exactly 100 ms.
  SM_EXPECT_FALSE(e.ReportDue(Timestamp::Millis(900), false));
  auto out = e.SendReport(Timestamp::Millis(900));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE(out[0].kind == RtcpOutKind::kReport);
  // The next one is an interval from now, so the old slot does not report again.
  SM_EXPECT_FALSE(e.ReportDue(Timestamp::Millis(1000), false));
  RtcpConfig a = c;
  a.has_video = false;
  RtcpEngine audio(a);
  audio.Process(Timestamp::Millis(0));
  SM_EXPECT_FALSE(audio.ReportDue(Timestamp::Millis(900), true));
  RtcpConfig v1;
  v1.first_report_delay = TimeDelta::Millis(1000);
  RtcpEngine old(v1);
  old.Process(Timestamp::Millis(0));
  SM_EXPECT_FALSE(old.ReportDue(Timestamp::Millis(900), true));  // v1 has no margin.
}
