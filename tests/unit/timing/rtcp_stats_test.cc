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
