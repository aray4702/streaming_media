#include "sm/timing/rtcp_stats.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

SM_TEST(RtcpStats, NtpRoundTrip) {
  Timestamp t = Timestamp::Micros(1700000000123456);
  NtpTime n = NtpTime::FromTimestamp(t);
  SM_EXPECT_NEAR(static_cast<double>(n.ToTimestamp().us()), static_cast<double>(t.us()), 1);
  SM_EXPECT_EQ(n.value >> 32, 1700000000ull + 2208988800ull);
  SM_EXPECT_NEAR(CompactNtpToDelta(0x00010000).us(), 1000000, 1);
  SM_EXPECT_NEAR(CompactNtpToDelta(0x00008000).us(), 500000, 1);
}

SM_TEST(RtcpStats, SenderAndReceiverReportWireFormat) {
  SenderReport sr;
  sr.ssrc = 0x11223344;
  sr.ntp.value = 0x0102030405060708ull;
  sr.rtp_timestamp = 0xAABBCCDD;
  sr.packet_count = 10;
  sr.octet_count = 1000;
  ReportBlock rb;
  rb.ssrc = 7;
  rb.fraction_lost = 25;
  rb.cumulative_lost = -3;
  rb.extended_highest_seq = 70000;
  rb.jitter = 123;
  rb.last_sr = 0x12345678;
  rb.delay_since_last_sr = 0x00010000;
  sr.blocks.push_back(rb);
  auto bytes = SerializeSenderReport(sr);
  SM_EXPECT_EQ(bytes.size(), 52u);
  SM_EXPECT_EQ(bytes[1], 200);
  SM_EXPECT_EQ(bytes[3], 12);  // Length in words minus one.
  auto back = ParseSenderReport(bytes);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->ntp.value, sr.ntp.value);
  SM_ASSERT_EQ(back->blocks.size(), 1u);
  SM_EXPECT_EQ(back->blocks[0].cumulative_lost, -3);
  SM_EXPECT_EQ(back->blocks[0].extended_highest_seq, 70000u);
  ReceiverReport rr{9, {rb}};
  auto rbytes = SerializeReceiverReport(rr);
  SM_EXPECT_EQ(rbytes.size(), 32u);
  auto rback = ParseReceiverReport(rbytes);
  SM_ASSERT_TRUE(rback.has_value());
  SM_EXPECT_EQ(rback->blocks[0].jitter, 123u);
  SM_EXPECT_FALSE(ParseReceiverReport(bytes).has_value());  // SR is not an RR.
  bytes.resize(20);
  SM_EXPECT_FALSE(ParseSenderReport(bytes).has_value());
}

SM_TEST(RtcpStats, FractionAndCumulativeLoss) {
  ReceiveStatistics rs(1, 90000);
  for (uint16_t s = 0; s < 100; ++s) {
    if (s % 10 != 3) rs.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33));
  }
  auto rb = rs.MakeReportBlock(Timestamp::Millis(3300));
  SM_EXPECT_EQ(rb.cumulative_lost, 10);
  SM_EXPECT_NEAR(rb.fraction_lost / 256.0, 0.10, 0.005);
  SM_EXPECT_EQ(rb.extended_highest_seq, 99u);
  // Next interval without loss.
  for (uint16_t s = 100; s < 150; ++s) rs.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33));
  auto rb2 = rs.MakeReportBlock(Timestamp::Millis(5000));
  SM_EXPECT_EQ(rb2.fraction_lost, 0);
  SM_EXPECT_EQ(rb2.cumulative_lost, 10);
}

SM_TEST(RtcpStats, InterarrivalJitterRfc3550) {
  ReceiveStatistics steady(1, 8000);
  for (uint16_t s = 0; s < 200; ++s) steady.OnPacket(s, s * 160u, Timestamp::Millis(s * 20));
  SM_EXPECT_NEAR(steady.jitter_ms(), 0, 0.2);
  // Transit alternates 0 / +10 ms: |D| = 10 ms each packet -> J -> 10 ms.
  ReceiveStatistics alt(1, 8000);
  for (uint16_t s = 0; s < 400; ++s) alt.OnPacket(s, s * 160u, Timestamp::Millis(s * 20 + (s % 2) * 10));
  SM_EXPECT_NEAR(alt.jitter_ms(), 10, 0.5);
}

SM_TEST(RtcpStats, RttFromLsrDlsr) {
  // Sender sends SR at t=1000 ms; receiver holds it 200 ms; one-way delay 40 ms.
  Timestamp sr_sent = Timestamp::Millis(1000);
  SenderReport sr;
  sr.ntp = NtpTime::FromTimestamp(sr_sent);
  ReceiveStatistics rs(1, 90000);
  rs.OnPacket(0, 0, Timestamp::Millis(1000));
  rs.OnSenderReport(sr, sr_sent + TimeDelta::Millis(40));
  ReportBlock rb = rs.MakeReportBlock(sr_sent + TimeDelta::Millis(240));
  RttEstimator rtt;
  auto r = rtt.OnReportBlock(rb, sr_sent + TimeDelta::Millis(280));
  SM_ASSERT_TRUE(r.has_value());
  SM_EXPECT_NEAR(r->ms_f(), 80, 0.1);
  SM_EXPECT_FALSE(rtt.OnReportBlock(ReportBlock{}, Timestamp::Millis(0)).has_value());
}

// RTCPSender::CreateReportBlocks (rtcp_sender.cc:739-748): DLSR is CompactNtp(now) - CompactNtp(last_rr),
// the difference of two truncated compact NTP times, not the interval truncated once (issue #942). An SR
// that arrives at 1040.007 ms and a report 200 ms later: 13108 units, where the truncated interval is 13107.
SM_TEST(RtcpStats, DelaySinceLastSrIsTheDifferenceOfCompactNtpTimes) {
  const Timestamp sr_arrival = Timestamp::Micros(1040007);
  const Timestamp now = sr_arrival + TimeDelta::Millis(200);
  SenderReport sr;
  sr.ntp = NtpTime::FromTimestamp(Timestamp::Millis(1000));
  ReceiveStatistics rs(1, 90000);
  rs.OnPacket(0, 0, Timestamp::Millis(1000));
  rs.OnSenderReport(sr, sr_arrival);
  const ReportBlock rb = rs.MakeReportBlock(now);
  SM_EXPECT_EQ(rb.delay_since_last_sr, NtpTime::FromTimestamp(now).Compact() - NtpTime::FromTimestamp(sr_arrival).Compact());
  SM_EXPECT_EQ(rb.delay_since_last_sr, 13108u);
  // At every arrival phase within a millisecond the two agree with libwebrtc's formula.
  for (int us = 0; us < 1000; ++us) {
    const Timestamp at = Timestamp::Micros(2000000 + us);
    ReceiveStatistics r(1, 90000);
    r.OnPacket(0, 0, Timestamp::Millis(1000));
    r.OnSenderReport(sr, at);
    const Timestamp later = at + TimeDelta::Micros(123457);
    SM_EXPECT_EQ(r.MakeReportBlock(later).delay_since_last_sr,
                 NtpTime::FromTimestamp(later).Compact() - NtpTime::FromTimestamp(at).Compact());
  }
}

// CompactNtpRttToTimeDelta (ntp_time_util.cc): 1 ms floor, a negative compact
// interval counts as 1 ms.
SM_TEST(RtcpStats, RttFloorsAtOneMillisecond) {
  Timestamp sr_sent = Timestamp::Millis(5000);
  SenderReport sr;
  sr.ntp = NtpTime::FromTimestamp(sr_sent);
  ReceiveStatistics rs(1, 90000);
  rs.OnPacket(0, 0, Timestamp::Millis(5000));
  rs.OnSenderReport(sr, sr_sent);
  ReportBlock rb = rs.MakeReportBlock(sr_sent + TimeDelta::Millis(100));
  RttEstimator zero;
  auto r = zero.OnReportBlock(rb, sr_sent + TimeDelta::Millis(100));  // 0 ms measured.
  SM_ASSERT_TRUE(r.has_value());
  SM_EXPECT_NEAR(r->ms_f(), 1.0, 0.01);
  RttEstimator negative;
  auto n = negative.OnReportBlock(rb, sr_sent + TimeDelta::Millis(50));  // Arrival before the delay: negative.
  SM_ASSERT_TRUE(n.has_value());
  SM_EXPECT_NEAR(n->ms_f(), 1.0, 0.01);
}

// receive_statistics_impl.cc UpdateJitter: in-order packets with a new
// timestamp only, and a jump of more than 5 s of video clock is skipped.
SM_TEST(RtcpStats, JitterIgnoresOutOfOrderSameTimestampAndJumps) {
  ReceiveStatistics steady(1, 90000);
  for (uint16_t s = 0; s < 50; ++s) steady.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33 + (s % 2) * 8));
  const double base = steady.jitter_ms();
  SM_EXPECT_TRUE(base > 0);
  // More packets of an old frame (out of order) and of the same timestamp leave it alone.
  steady.OnPacket(10, 10 * 3000u, Timestamp::Millis(50 * 33 + 40));
  steady.OnPacket(50, 50 * 3000u, Timestamp::Millis(50 * 33));
  const double after_frame = steady.jitter_ms();
  steady.OnPacket(51, 50 * 3000u, Timestamp::Millis(50 * 33 + 500));  // Same timestamp: skipped.
  SM_EXPECT_NEAR(steady.jitter_ms(), after_frame, 1e-9);
  steady.OnPacket(52, 50 * 3000u + 6 * 90000u, Timestamp::Millis(50 * 33 + 510));  // Jump of 6 s: skipped.
  SM_EXPECT_NEAR(steady.jitter_ms(), after_frame, 1e-9);
}

// StreamStatisticianImpl::ReviseFrequencyAndJitter (#101): the jitter is a
// count of samples, so a new payload frequency rescales it.
SM_TEST(RtcpStats, JitterIsRescaledWhenThePayloadFrequencyChanges) {
  ReceiveStatistics rs(1, 48000);
  for (uint16_t s = 0; s < 60; ++s) rs.OnPacket(s, s * 960u, Timestamp::Millis(s * 20 + (s % 2) * 6), 48000);
  const double before = rs.jitter_ms();
  SM_EXPECT_TRUE(before > 1.0);
  // Same timing on a 16 kHz payload: the stored sample count shrinks to a third, so the
  // jitter in milliseconds stays close rather than jumping to 3x.
  rs.OnPacket(60, 60 * 960u, Timestamp::Millis(60 * 20), 16000);
  rs.OnPacket(61, 60 * 960u + 320u, Timestamp::Millis(60 * 20 + 20 + 6), 16000);
  SM_EXPECT_EQ(rs.jitter_frequency(), 16000);
  SM_EXPECT_TRUE(rs.jitter_ms() < before * 1.5 && rs.jitter_ms() > before * 0.3);
  // An unchanged frequency leaves it alone.
  ReceiveStatistics same(1, 90000);
  same.OnPacket(0, 0, Timestamp::Millis(0), 90000);
  same.OnPacket(1, 3000, Timestamp::Millis(40), 90000);
  SM_EXPECT_EQ(same.jitter_frequency(), 90000);
}

// RFC 3611 extended reports, as libwebrtc's rtcp_packet::ExtendedReports
// lays them out: RRTR first, then one DLRR block with every item (issue #356).
SM_TEST(RtcpStats, ExtendedReportsRoundTrip) {
  ExtendedReports xr;
  xr.sender_ssrc = 0x11223344;
  xr.rrtr = NtpTime{0x0102030405060708ULL};
  xr.dlrr = {{7, 0xAABBCCDD, 0x00010000}, {9, 0x12345678, 0x8000}};
  const auto b = SerializeExtendedReports(xr);
  SM_ASSERT_EQ(b.size(), 8u + 12u + 4u + 24u);
  SM_EXPECT_EQ(b[1], 207);
  SM_EXPECT_EQ((b[2] << 8) | b[3], 11);  // Length in words minus one.
  SM_EXPECT_EQ(b[8], 4);                  // RRTR block type,
  SM_EXPECT_EQ((b[10] << 8) | b[11], 2);  // two words.
  SM_EXPECT_EQ(b[20], 5);                 // DLRR block type,
  SM_EXPECT_EQ((b[22] << 8) | b[23], 6);  // three words per item.
  auto p = ParseExtendedReports(b);
  SM_ASSERT_TRUE(p.has_value());
  SM_EXPECT_EQ(p->sender_ssrc, 0x11223344u);
  SM_ASSERT_TRUE(p->rrtr.has_value());
  SM_EXPECT_EQ(p->rrtr->value, 0x0102030405060708ULL);
  SM_ASSERT_EQ(p->dlrr.size(), 2u);
  SM_EXPECT_EQ(p->dlrr[1].ssrc, 9u);
  SM_EXPECT_EQ(p->dlrr[1].last_rr, 0x12345678u);
  SM_EXPECT_EQ(p->dlrr[1].delay_since_last_rr, 0x8000u);

  // Only an RRTR: no DLRR block at all.
  ExtendedReports only;
  only.rrtr = NtpTime{1};
  SM_EXPECT_EQ(SerializeExtendedReports(only).size(), 20u);
  // At most kMaxNumberOfDlrrItems items.
  ExtendedReports many;
  many.dlrr.resize(kMaxNumberOfDlrrItems + 5);
  SM_EXPECT_EQ(SerializeExtendedReports(many).size(), 8u + 4u + 12u * kMaxNumberOfDlrrItems);
}

SM_TEST(RtcpStats, ExtendedReportsSkipUnknownAndMisSizedBlocks) {
  // Header (5 words after it), sender SSRC, an unknown block of one word, an
  // RRTR claiming one word (ignored), and a DLRR whose length is not a
  // multiple of three (ignored).
  std::vector<uint8_t> b = {0x80, 207, 0, 6, 0, 0, 0, 1,
                            42, 0, 0, 1, 1, 2, 3, 4,
                            4, 0, 0, 1, 9, 9, 9, 9,
                            5, 0, 0, 1, 7, 7, 7, 7};
  b[3] = static_cast<uint8_t>(b.size() / 4 - 1);
  auto p = ParseExtendedReports(b);
  SM_ASSERT_TRUE(p.has_value());
  SM_EXPECT_FALSE(p->rrtr.has_value());
  SM_EXPECT_TRUE(p->dlrr.empty());
  // A block that runs past the packet makes it malformed.
  std::vector<uint8_t> bad = {0x80, 207, 0, 3, 0, 0, 0, 1, 4, 0, 0, 2, 1, 2, 3, 4};
  SM_EXPECT_FALSE(ParseExtendedReports(bad).has_value());
  SM_EXPECT_FALSE(ParseExtendedReports(std::vector<uint8_t>{0x80, 207, 0}).has_value());
}

// StreamStatisticianImpl::MaybeAppendReportBlockAndReset (receive_statistics_impl.cc:241-250):
// no report block before the stream's first packet, nor 8 s (kStatisticsTimeout) after its last.
SM_TEST(RtcpStats, ReportBlockDueOnceReceivedUntilEightSecondsOfSilence) {
  ReceiveStatistics s(0x300, 90000);
  SM_EXPECT_FALSE(s.ReportBlockDue(Timestamp::Millis(0)));
  s.OnPacket(10, 1000, Timestamp::Millis(100));
  SM_EXPECT_TRUE(s.ReportBlockDue(Timestamp::Millis(100)));
  SM_EXPECT_TRUE(s.ReportBlockDue(Timestamp::Millis(8099)));
  SM_EXPECT_FALSE(s.ReportBlockDue(Timestamp::Millis(8100)));
  s.OnPacket(11, 2000, Timestamp::Millis(9000));
  SM_EXPECT_TRUE(s.ReportBlockDue(Timestamp::Millis(9000)));
}

// StreamStatisticianImpl::UpdateOutOfOrder (receive_statistics_impl.cc:71-115): a
// sequence number more than the max reordering threshold away is held; if the next
// packet follows it, the stream restarted and the gap is no loss
// (ReceiveStatisticsTest.StreamRestartDoesntCountAsLoss).
SM_TEST(RtcpStats, StreamRestartDoesntCountAsLoss) {
  ReceiveStatistics rs(1, 90000, true);  // kDefaultMaxReorderingThreshold, 50.
  SM_EXPECT_EQ(rs.max_reordering_threshold(), kDefaultMaxReorderingThreshold);
  SM_EXPECT_EQ(kDefaultMaxReorderingThreshold, 50);
  for (uint16_t s = 0; s < 10; ++s) rs.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33));
  rs.OnPacket(1000, 30000, Timestamp::Millis(330));
  // Held until the next packet: not yet received, the maximum doesn't move.
  SM_EXPECT_EQ(rs.Snapshot().cumulative_lost, 0);
  ReportBlock held = rs.MakeReportBlock(Timestamp::Millis(340));
  SM_EXPECT_EQ(held.extended_highest_seq, 9u);
  SM_EXPECT_EQ(held.cumulative_lost, 0);
  rs.OnPacket(1001, 33000, Timestamp::Millis(363));
  rs.OnPacket(1002, 36000, Timestamp::Millis(396));
  ReportBlock rb = rs.MakeReportBlock(Timestamp::Millis(400));
  SM_EXPECT_EQ(rb.cumulative_lost, 0);
  SM_EXPECT_EQ(rb.fraction_lost, 0);
  SM_EXPECT_EQ(rb.extended_highest_seq, 1002u);
  // Loss after the restart counts as usual.
  rs.OnPacket(1004, 42000, Timestamp::Millis(462));
  SM_EXPECT_EQ(rs.MakeReportBlock(Timestamp::Millis(470)).cumulative_lost, 1);
}

// A jump that the next packet doesn't follow is an old out-of-order packet: it
// counts as received and doesn't move the maximum (ReceiveStatisticsTest.
// StreamRestartNeedsTwoConsecutivePackets).
SM_TEST(RtcpStats, StreamRestartNeedsTwoConsecutivePackets) {
  ReceiveStatistics rs(1, 90000, true);
  for (uint16_t s = 0; s < 10; ++s) rs.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33));
  rs.OnPacket(400, 30000, Timestamp::Millis(330));
  rs.OnPacket(10, 30000, Timestamp::Millis(363));
  rs.OnPacket(11, 33000, Timestamp::Millis(396));
  ReportBlock rb = rs.MakeReportBlock(Timestamp::Millis(400));
  SM_EXPECT_EQ(rb.extended_highest_seq, 11u);
  // 13 packets received for 12 expected: libwebrtc clamps the negative loss to 0.
  SM_EXPECT_EQ(rb.cumulative_lost, 0);
  SM_EXPECT_EQ(rb.fraction_lost, 0);
  SM_EXPECT_EQ(rs.Snapshot().packets_received, 13u);
}

// Within the threshold a jump is loss. RtpVideoStreamReceiver2 sets 450
// (kMaxPacketAgeToNack) with NACK on (rtp_video_stream_receiver2.cc:106, :365-367).
SM_TEST(RtcpStats, JumpWithinMaxReorderingThresholdIsLoss) {
  ReceiveStatistics rs(1, 90000, true, kMaxPacketAgeToNack);
  SM_EXPECT_EQ(rs.max_reordering_threshold(), 450);
  for (uint16_t s = 0; s < 10; ++s) rs.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33));
  rs.OnPacket(309, 30000, Timestamp::Millis(330));
  rs.OnPacket(310, 33000, Timestamp::Millis(363));
  ReportBlock rb = rs.MakeReportBlock(Timestamp::Millis(400));
  SM_EXPECT_EQ(rb.cumulative_lost, 299);
  SM_EXPECT_EQ(rb.extended_highest_seq, 310u);
  // MaybeAppendReportBlockAndReset: 255 * lost / expected (receive_statistics_impl.cc:263-266).
  SM_EXPECT_EQ(rb.fraction_lost, 255 * 299 / 311);
  // The same jump with the default threshold is a restart.
  ReceiveStatistics def(1, 90000, true);
  for (uint16_t s = 0; s < 10; ++s) def.OnPacket(s, s * 3000u, Timestamp::Millis(s * 33));
  def.OnPacket(309, 30000, Timestamp::Millis(330));
  def.OnPacket(310, 33000, Timestamp::Millis(363));
  SM_EXPECT_EQ(def.MakeReportBlock(Timestamp::Millis(400)).cumulative_lost, 0);
}

// Fraction lost scales by 255, not 256 (receive_statistics_impl.cc:263-266), and a
// negative cumulative loss (duplicates) reports 0 (:268-274).
SM_TEST(RtcpStats, FractionLostScalesBy255AndNegativeLossClampsToZero) {
  ReceiveStatistics rs(1, 90000, true);
  rs.OnPacket(0, 0, Timestamp::Millis(0));
  SM_EXPECT_EQ(rs.MakeReportBlock(Timestamp::Millis(10)).fraction_lost, 0);
  rs.OnPacket(2, 3000, Timestamp::Millis(33));
  // One of two lost: 127, where 256 * 1 / 2 would be 128.
  ReportBlock rb = rs.MakeReportBlock(Timestamp::Millis(40));
  SM_EXPECT_EQ(rb.fraction_lost, 127);
  SM_EXPECT_EQ(rb.cumulative_lost, 1);
  for (int i = 0; i < 5; ++i) rs.OnPacket(2, 3000, Timestamp::Millis(120 + i));
  ReportBlock dup = rs.MakeReportBlock(Timestamp::Millis(200));
  SM_EXPECT_EQ(dup.cumulative_lost, 0);
  SM_EXPECT_EQ(dup.fraction_lost, 0);
}

// v1 (receive_statistics_v2 off, the default until the flip): no threshold, so a jump
// is loss; fraction lost 256 * lost / expected capped at 255; the loss as counted.
SM_TEST(RtcpStats, V1KeepsJumpsAsLossAnd256Scale) {
  ReceiveStatistics rs(1, 90000);
  SM_EXPECT_FALSE(rs.v2());
  rs.OnPacket(0, 0, Timestamp::Millis(0));
  rs.MakeReportBlock(Timestamp::Millis(10));
  rs.OnPacket(2, 3000, Timestamp::Millis(33));
  SM_EXPECT_EQ(rs.MakeReportBlock(Timestamp::Millis(40)).fraction_lost, 128);
  rs.OnPacket(1000, 6000, Timestamp::Millis(66));
  rs.OnPacket(1001, 9000, Timestamp::Millis(99));
  ReportBlock rb = rs.MakeReportBlock(Timestamp::Millis(100));
  SM_EXPECT_EQ(rb.cumulative_lost, 998);
  SM_EXPECT_EQ(rb.extended_highest_seq, 1001u);
  for (int i = 0; i < 5; ++i) rs.OnPacket(10, 9000, Timestamp::Millis(120 + i));
  SM_EXPECT_EQ(rs.MakeReportBlock(Timestamp::Millis(200)).cumulative_lost, 993);
}
