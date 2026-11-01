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

SM_TEST(RtcpEngine, RandomizedIntervalsStayInRangeAndAreDeterministic) {
  auto run = [](uint64_t seed) {
    RtcpConfig c;
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
  SM_EXPECT_EQ(a[0], 500);  // half the nominal 1 s, no random draw
  for (size_t i = 1; i < a.size(); ++i) {
    SM_EXPECT_GE(a[i] - a[i - 1], 500);
    SM_EXPECT_LE(a[i] - a[i - 1], 1501);
  }
}

// libwebrtc's RTCPSender draws each interval with webrtc::Random seeded from the clock in microseconds
// (rtcp_sender.cc:130, :693); sm_compare's clock starts at 100000 s on seed 1, so its draws are these (#517).
SM_TEST(RtcpEngine, IntervalsAreLibwebrtcsDrawsFromTheSameSeed) {
  RtcpConfig c;
  c.seed = 100000ull * 1000000;
  RtcpEngine e(c);
  const std::vector<int64_t> times = ReportTimes(e, 5000);
  // 500 ms with no draw, then Rand(500, 1500): 703, 527, 1276, 1330.
  SM_EXPECT_TRUE(times == (std::vector<int64_t>{500, 1203, 1730, 3006, 4336}));
}

SM_TEST(RtcpEngine, XorshiftRandomMatchesWebrtcRandom) {
  // rtc_base/random.cc: xorshift64*, Rand(t) = (low 32 bits * (t + 1)) >> 32.
  XorshiftRandom r(100000ull * 1000000);
  SM_EXPECT_EQ(r.Rand(500, 1500), 703);
  SM_EXPECT_EQ(r.Rand(500, 1500), 527);
  SM_EXPECT_EQ(r.Rand(500, 1500), 1276);
  XorshiftRandom a(100000ull * 1000000);
  SM_EXPECT_EQ(a.Rand(2500u, 7500u), 3517u);
  SM_EXPECT_EQ(a.Rand(-10, 10), -10);
  SM_EXPECT_EQ(a.Rand(0u, 0u), 0u);
}

// RTCPSender's constructor schedules the first report at report_interval_ / 2
// with no random draw (rtcp_sender.cc:171): 0.5 s with video and 2.5 s for an
// audio-only sender, on every seed (#331).
SM_TEST(RtcpEngine, FirstReportAtHalfTheNominalIntervalOnEverySeed) {
  for (uint64_t seed = 1; seed <= 8; ++seed) {
    RtcpConfig video;
    video.seed = seed;
    RtcpEngine v(video);
    auto vt = ReportTimes(v, 600);
    SM_ASSERT_GE(vt.size(), 1u);
    SM_EXPECT_EQ(vt[0], 500);

    RtcpConfig audio;
    audio.seed = seed;
    audio.has_video = false;
    RtcpEngine a(audio);
    auto at = ReportTimes(a, 2600);
    SM_ASSERT_EQ(at.size(), 1u);
    SM_EXPECT_EQ(at[0], 2500);
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

// RTCPSender::BuildSR (rtcp_sender.cc:362-366): the last frame's RTP timestamp plus
// (report time rounded to the ms - the frame's time in ms) * the rate in kHz, so
// sub-millisecond times don't count (issue #895).
SM_TEST(RtcpEngine, SenderReportExtrapolatesInWholeMilliseconds) {
  RtcpConfig c;
  c.first_report_delay = TimeDelta::Micros(500600);
  RtcpEngine tx(c);
  RtcpSenderState s;
  s.ssrc = 42;
  s.rtp_timestamp = 1000;
  s.reference_time = Timestamp::Micros(10400);  // Rounds to 10 ms.
  s.clock_rate = 8000;                          // kBogusRtpRateForAudioRtcp.
  tx.SetSenderState(s);
  tx.Process(Timestamp::Millis(0));
  auto out = tx.Process(Timestamp::Micros(500600));  // Rounds to 501 ms.
  SM_ASSERT_EQ(out.size(), 1u);
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  SM_ASSERT_TRUE(split.has_value());
  auto sr = timing::ParseSenderReport((*split)[0].data);
  SM_ASSERT_TRUE(sr.has_value());
  // (501 - 10) ms at 8 kHz; the microsecond span would give 3921.
  SM_EXPECT_EQ(sr->rtp_timestamp, 1000u + 491u * 8u);
}

// Issue #893: each NACK, PLI and FIR is one RTCPSender::SendRTCP call (RtcpFeedbackBuffer::
// SendBufferedRtcpFeedback sends the key frame request and the NACK apart), so each is its own compound with a
// leading RR; a buffered loss notification goes in the next compound.
SM_TEST(RtcpEngine, EachEarlyRequestIsItsOwnCompoundWithALeadingRr) {
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
  SM_ASSERT_EQ(out.size(), 3u);
  SM_EXPECT_TRUE(out[0].kind == RtcpOutKind::kFeedback);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kNack}));
  SM_EXPECT_TRUE((Types(out[1].bytes) == std::vector<T>{T::kReceiverReport, T::kPli}));
  SM_EXPECT_TRUE((Types(out[2].bytes) == std::vector<T>{T::kReceiverReport, T::kFir, T::kLossNotification}));
  SM_EXPECT_EQ(e.stats().nack_seqs_sent, 3u);
  SM_EXPECT_EQ(e.stats().pli_sent, 1u);
  SM_EXPECT_EQ(e.stats().feedback_datagrams, 3u);
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
SM_TEST(RtcpEngine, NackCarriesTheFullReportAndRestartsTheTimer) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  stats.OnPacket(3, 3000, Timestamp::Millis(40));
  RtcpConfig c;
  c.local_ssrc = 100;
  c.cname = "user@host";
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

// Issue #576: RTCPSender's max_packet_size_ is 1460 by default
// (rtp_rtcp_impl2.cc:146-150, 463), so 1300 bytes of feedback go in one datagram.
SM_TEST(RtcpEngine, FeedbackUpTo1460BytesIsOneDatagram) {
  SM_EXPECT_EQ(RtcpConfig{}.max_datagram_bytes, 1460u);
  RtcpEngine e;
  e.Process(Timestamp::Millis(0));
  // One NACK of 300 sequence numbers 17 apart: one FCI each, 1200 bytes in one SendRTCP call.
  std::vector<uint16_t> seqs;
  for (int i = 0; i < 300; ++i) seqs.push_back(static_cast<uint16_t>(i * 17));
  e.QueueNack(0xAA, seqs);
  auto out = e.Process(Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_GT(out[0].bytes.size(), 1200u);
  SM_EXPECT_LE(out[0].bytes.size(), 1460u);
}

// Issue #860: RTCPSender sends transport-cc and REMB with SendCombinedRtcpPacket, each call a
// datagram of its own, and NACK, PLI, FIR and loss notification with SendRTCP, a compound with the
// report. Feedback queued at one time keeps its order, one datagram per call.
SM_TEST(RtcpEngine, TransportFeedbackNeverSharesADatagramWithACompound) {
  RtcpEngine e;
  e.Process(Timestamp::Millis(0));
  e.QueueTransportFeedback(cc::TransportFeedback{});
  e.QueueNack(0x1234, std::vector<uint16_t>{7});
  e.QueuePli(0x1234);
  e.QueueTransportFeedback(cc::TransportFeedback{});
  e.QueueRemb(DataRate::KilobitsPerSec(300), std::vector<uint32_t>{0x1234});
  auto out = e.Process(Timestamp::Millis(1));
  using T = loss::RtcpType;
  SM_ASSERT_EQ(out.size(), 5u);
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kTransportCc}));
  SM_EXPECT_TRUE((Types(out[1].bytes) == std::vector<T>{T::kReceiverReport, T::kNack}));
  SM_EXPECT_TRUE((Types(out[2].bytes) == std::vector<T>{T::kReceiverReport, T::kPli}));
  SM_EXPECT_TRUE((Types(out[3].bytes) == std::vector<T>{T::kReceiverReport, T::kTransportCc}));
  SM_EXPECT_TRUE((Types(out[4].bytes) == std::vector<T>{T::kReceiverReport, T::kRemb}));
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
  // Two SendCombinedRtcpPacket calls, two datagrams (issue #860).
  SM_ASSERT_EQ(out.size(), 2u);
  auto in = rx.OnRtcp(out[0].bytes, Timestamp::Millis(30));
  SM_EXPECT_TRUE(in.valid);
  SM_EXPECT_TRUE(in.remb.empty());
  auto in_remb = rx.OnRtcp(out[1].bytes, Timestamp::Millis(30));
  SM_EXPECT_TRUE(in_remb.valid);
  SM_EXPECT_TRUE(in_remb.transport_feedback.empty());
  in.remb = in_remb.remb;
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
                           timing::SerializeExtendedReports({.sender_ssrc = 1, .rrtr = timing::NtpTime{5}, .dlrr = {}}),
                           Bytes{0x80, 204, 0, 2, 0, 0, 0, 1, 'n', 'a', 'm', 'e'}};  // APP: valid header, not interpreted.
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
  SM_EXPECT_EQ(e.stats().rrtr_received, 1u);
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

// RTCPSender::BuildFIR (rtcp_sender.cc:405-414) increments sequence_number_fir_ before it writes the
// request: the first FIR carries 1, the next 2 (issue #818).
SM_TEST(RtcpEngine, FirSequenceNumbersStartAtOne) {
  RtcpEngine tx;
  tx.Process(Timestamp::Millis(0));
  for (uint8_t want : {1, 2}) {
    tx.QueueFir(55);
    auto out = tx.Process(Timestamp::Millis(want));
    SM_ASSERT_EQ(out.size(), 1u);
    auto views = loss::SplitCompoundRtcp(out[0].bytes);
    SM_ASSERT_TRUE(views.has_value());
    std::optional<loss::Fir> fir;
    for (const auto& v : *views)
      if (v.type == loss::RtcpType::kFir) fir = loss::ParseFir(v.data);
    SM_ASSERT_TRUE(fir.has_value());
    SM_ASSERT_EQ(fir->entries.size(), 1u);
    SM_EXPECT_EQ(fir->entries[0].ssrc, 55u);
    SM_EXPECT_EQ(fir->entries[0].seq_nr, want);
  }
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

// A BYE goes out as RTCPSender's SendRTCP(kRtcpBye): PrepareReport adds the
// full report and SDES, and ComputeCompoundRTCPPacket appends the BYE after
// every other packet, the XR included (rtcp_sender.cc:631-650; issue #820).
SM_TEST(RtcpEngine, ByeGoesLastInACompoundWithTheFullReport) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  stats.OnPacket(3, 3000, Timestamp::Millis(40));
  RtcpConfig c;
  c.local_ssrc = 100;
  c.cname = "user@host";
  c.xr_rrtr = true;
  c.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine tx(c), rx;
  tx.AddReceiveStatistics(&stats);
  tx.Process(Timestamp::Millis(0));
  std::vector<uint32_t> ssrcs = {100};
  tx.QueueBye(ssrcs);
  const std::vector<uint16_t> seqs = {2};
  tx.QueueNack(0x1234, seqs);
  auto out = tx.Process(Timestamp::Millis(100));
  SM_ASSERT_EQ(out.size(), 1u);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kSdes, T::kNack, T::kXr, T::kBye}));
  auto in = rx.OnRtcp(out[0].bytes, Timestamp::Millis(101));
  SM_ASSERT_EQ(in.byes.size(), 1u);
  SM_EXPECT_TRUE(in.byes[0].ssrcs == ssrcs);
  SM_EXPECT_TRUE(in.byes[0].reason.empty());
  SM_ASSERT_EQ(in.report_blocks.size(), 1u);

  // Alone, it still opens with the report and SDES.
  RtcpEngine alone(c);
  alone.AddReceiveStatistics(&stats);
  alone.Process(Timestamp::Millis(0));
  alone.QueueBye(ssrcs, "done");
  out = alone.Process(Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kSdes, T::kXr, T::kBye}));
  in = rx.OnRtcp(out[0].bytes, Timestamp::Millis(2));
  SM_ASSERT_EQ(in.byes.size(), 1u);
  SM_EXPECT_TRUE(in.byes[0].reason == "done");
}

// RTCPSender::PrepareReport (rtcp_sender.cc:713): SDES goes with every SR,
// with an empty CNAME when none was set (issue #820).
SM_TEST(RtcpEngine, SenderReportCarriesSdesEvenWithoutACname) {
  RtcpConfig c;
  c.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine tx(c);
  tx.SetSenderState({.ssrc = 0x3333, .packet_count = 1, .octet_count = 100, .rtp_timestamp = 0});
  tx.Process(Timestamp::Millis(0));
  auto out = tx.Process(Timestamp::Millis(500));
  SM_ASSERT_EQ(out.size(), 1u);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kSenderReport, T::kSdes}));
  auto split = loss::SplitCompoundRtcp(out[0].bytes);
  auto sdes = ParseSdes((*split)[1].data);
  SM_ASSERT_TRUE(sdes.has_value());
  SM_ASSERT_EQ(sdes->size(), 1u);
  SM_EXPECT_EQ((*sdes)[0].ssrc, 0x3333u);
  SM_ASSERT_EQ((*sdes)[0].items.size(), 1u);
  SM_EXPECT_EQ((*sdes)[0].items[0].type, kSdesCname);
  SM_EXPECT_TRUE((*sdes)[0].items[0].text.empty());
}

// An RR carries SDES only when the module has a CNAME (rtcp_sender.cc:713).
// libwebrtc's video receive module has none (CreateRtpRtcpModule), which
// cname_in_receiver_report = false stands for (issue #820).
SM_TEST(RtcpEngine, ReceiverReportCarriesSdesOnlyWithACname) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  stats.OnPacket(3, 3000, Timestamp::Millis(40));
  using T = loss::RtcpType;
  for (int variant = 0; variant < 2; ++variant) {
    RtcpConfig c;
    c.local_ssrc = 100;
    if (variant == 0) {
      c.cname = "user@host";
      c.cname_in_receiver_report = false;
    }
    c.first_report_delay = TimeDelta::Millis(500);
    RtcpEngine rx(c);
    rx.AddReceiveStatistics(&stats);
    rx.Process(Timestamp::Millis(0));
    const std::vector<uint16_t> seqs = {2};
    rx.QueueNack(0x1234, seqs);
    auto out = rx.Process(Timestamp::Millis(100));
    SM_ASSERT_EQ(out.size(), 1u);
    SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kNack}));
    std::vector<RtcpOutgoing> report;
    for (int64_t t = 101; t <= 2000 && report.empty(); ++t) report = rx.Process(Timestamp::Millis(t));
    SM_ASSERT_EQ(report.size(), 1u);
    SM_EXPECT_TRUE((Types(report[0].bytes) == std::vector<T>{T::kReceiverReport}));
  }
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
SM_TEST(RtcpEngine, ReportIntervalFollowsTheSendBitrateRule) {
  auto gaps = [](int64_t bytes_per_ms) {
    RtcpConfig c;
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
SM_TEST(RtcpEngine, ReportDueWithin100msIsSentBeforeAKeyframe) {
  RtcpConfig c;
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
}

// A receive-only endpoint measures the RTT over XR (issue #356): its report
// carries an RRTR, the sender echoes it in a DLRR on its next report, and
// RTT = arrival - LRR - DLRR (RTCPReceiver::HandleXrDlrrReportBlock).
SM_TEST(RtcpEngine, ReceiveOnlyRttFromRrtrAndDlrr) {
  RtcpConfig rc;
  rc.local_ssrc = 8;
  rc.xr_rrtr = true;
  rc.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine receiver(rc);
  RtcpConfig sc;
  sc.local_ssrc = 1;
  sc.first_report_delay = TimeDelta::Millis(600);
  RtcpEngine sender(sc);
  sender.SetSenderState({.ssrc = 7});
  sender.AddLocalMediaSsrc(7);
  receiver.Process(Timestamp::Millis(0));
  sender.Process(Timestamp::Millis(0));

  // The receiver's report at 500 ms: RR, then XR with the RRTR.
  auto rr = receiver.Process(Timestamp::Millis(500));
  SM_ASSERT_EQ(rr.size(), 1u);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(rr[0].bytes) == std::vector<T>{T::kReceiverReport, T::kXr}));
  auto in_tx = sender.OnRtcp(rr[0].bytes, Timestamp::Millis(520));
  SM_EXPECT_TRUE(in_tx.xr_rtts.empty());

  // The sender's report at 600 ms echoes it, 80 ms after it arrived.
  auto sr = sender.Process(Timestamp::Millis(600));
  SM_ASSERT_EQ(sr.size(), 1u);
  // Every SR carries SDES, with an empty CNAME here (rtcp_sender.cc:713, issue #820).
  SM_EXPECT_TRUE((Types(sr[0].bytes) == std::vector<T>{T::kSenderReport, T::kSdes, T::kXr}));
  auto split = loss::SplitCompoundRtcp(sr[0].bytes);
  auto xr = timing::ParseExtendedReports((*split)[2].data);
  SM_ASSERT_TRUE(xr.has_value());
  SM_EXPECT_FALSE(xr->rrtr.has_value());  // A media sender sends no RRTR.
  SM_ASSERT_EQ(xr->dlrr.size(), 1u);
  SM_EXPECT_EQ(xr->dlrr[0].ssrc, 8u);
  SM_EXPECT_NEAR(timing::CompactNtpToDelta(xr->dlrr[0].delay_since_last_rr).ms_f(), 80.0, 0.1);

  // Arriving at 640 ms: 640 - 500 - 80 = 60 ms.
  auto in_rx = receiver.OnRtcp(sr[0].bytes, Timestamp::Millis(640));
  SM_ASSERT_EQ(in_rx.xr_rtts.size(), 1u);
  SM_EXPECT_NEAR(in_rx.xr_rtts[0].ms_f(), 60.0, 0.1);
  SM_EXPECT_TRUE(in_rx.rtts.empty());

  // Each RRTR is echoed once.
  std::vector<RtcpOutgoing> next;
  for (int64_t t = 601; t <= 3000 && next.empty(); ++t) next = sender.Process(Timestamp::Millis(t));
  SM_ASSERT_EQ(next.size(), 1u);
  SM_EXPECT_TRUE((Types(next[0].bytes) == std::vector<T>{T::kSenderReport, T::kSdes}));
  SM_EXPECT_EQ(sender.stats().dlrr_items_sent, 1u);
  SM_EXPECT_EQ(receiver.stats().rrtr_sent, 1u);
}

// Without the measurement the receiver sends no RRTR and ignores a DLRR, and
// an echo of another endpoint's RRTR gives no RTT.
SM_TEST(RtcpEngine, DlrrCountsOnlyWithRrtrOnAndForOurSsrc) {
  RtcpConfig rc;
  rc.local_ssrc = 8;
  rc.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine off(rc);
  off.Process(Timestamp::Millis(0));
  auto rr = off.Process(Timestamp::Millis(500));
  SM_ASSERT_EQ(rr.size(), 1u);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(rr[0].bytes) == std::vector<T>{T::kReceiverReport}));

  timing::ExtendedReports xr;
  xr.sender_ssrc = 1;
  xr.dlrr = {{8, timing::NtpTime::FromTimestamp(Timestamp::Millis(500)).Compact(), 0}};
  std::vector<std::vector<uint8_t>> packets = {timing::SerializeReceiverReport({.ssrc = 1}),
                                               timing::SerializeExtendedReports(xr)};
  const auto datagram = loss::BuildCompoundRtcp(packets);
  SM_EXPECT_TRUE(off.OnRtcp(datagram, Timestamp::Millis(600)).xr_rtts.empty());

  rc.xr_rrtr = true;
  RtcpEngine on(rc);
  auto in = on.OnRtcp(datagram, Timestamp::Millis(600));
  SM_ASSERT_EQ(in.xr_rtts.size(), 1u);
  SM_EXPECT_NEAR(in.xr_rtts[0].ms_f(), 100.0, 0.1);
  xr.dlrr[0].ssrc = 9;
  packets[1] = timing::SerializeExtendedReports(xr);
  SM_EXPECT_TRUE(on.OnRtcp(loss::BuildCompoundRtcp(packets), Timestamp::Millis(600)).xr_rtts.empty());
  // LRR zero: the peer had no RRTR to echo.
  xr.dlrr[0] = {8, 0, 0};
  packets[1] = timing::SerializeExtendedReports(xr);
  SM_EXPECT_TRUE(on.OnRtcp(loss::BuildCompoundRtcp(packets), Timestamp::Millis(600)).xr_rtts.empty());
  SM_EXPECT_EQ(on.stats().xr_received, 3u);
}

// A NACK compound carries the full report and so the RRTR too, after the
// feedback, as RTCPSender orders its report flags.
SM_TEST(RtcpEngine, NackCompoundCarriesTheRrtrLast) {
  timing::ReceiveStatistics stats(0x1234, 90000);
  stats.OnPacket(1, 0, Timestamp::Millis(0));
  stats.OnPacket(3, 3000, Timestamp::Millis(40));
  RtcpConfig c;
  c.local_ssrc = 100;
  c.cname = "user@host";
  c.xr_rrtr = true;
  c.first_report_delay = TimeDelta::Millis(500);
  RtcpEngine rx(c);
  rx.AddReceiveStatistics(&stats);
  rx.Process(Timestamp::Millis(0));
  const std::vector<uint16_t> seqs = {2};
  rx.QueueNack(0x1234, seqs);
  auto out = rx.Process(Timestamp::Millis(100));
  SM_ASSERT_EQ(out.size(), 1u);
  using T = loss::RtcpType;
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kSdes, T::kNack, T::kXr}));
  // Transport-cc goes without a report, so without an RRTR.
  rx.QueueTransportFeedback(cc::TransportFeedback{});
  out = rx.Process(Timestamp::Millis(200));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE((Types(out[0].bytes) == std::vector<T>{T::kReceiverReport, T::kTransportCc}));
}
