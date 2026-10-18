#include "sm/cc/transport_feedback.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

SM_TEST(TransportFeedback, WireRoundTripMixedChunks) {
  TransportFeedback fb;
  fb.sender_ssrc = 1;
  fb.media_ssrc = 2;
  fb.base_seq = 65530;
  fb.feedback_count = 7;
  fb.reference_time = 1234;
  // 20 received small, 3 lost, a large and a negative delta, 30 lost (run).
  for (int i = 0; i < 20; ++i) fb.packets.push_back({0, true, 4});
  for (int i = 0; i < 3; ++i) fb.packets.push_back({0, false, 0});
  fb.packets.push_back({0, true, 1000});
  fb.packets.push_back({0, true, -20});
  for (int i = 0; i < 30; ++i) fb.packets.push_back({0, false, 0});
  fb.packets.push_back({0, true, 1});
  for (size_t i = 0; i < fb.packets.size(); ++i) fb.packets[i].seq = static_cast<uint16_t>(fb.base_seq + i);
  auto bytes = SerializeTransportFeedback(fb);
  SM_EXPECT_EQ(bytes.size() % 4, 0u);
  auto back = ParseTransportFeedback(bytes);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->base_seq, 65530);
  SM_EXPECT_EQ(back->reference_time, 1234);
  SM_EXPECT_EQ(back->feedback_count, 7);
  SM_ASSERT_EQ(back->packets.size(), fb.packets.size());
  for (size_t i = 0; i < fb.packets.size(); ++i) {
    SM_EXPECT_EQ(back->packets[i].received, fb.packets[i].received);
    if (fb.packets[i].received) SM_EXPECT_EQ(back->packets[i].delta_ticks, fb.packets[i].delta_ticks);
  }
}

SM_TEST(TransportFeedback, BuilderAndHistoryJoin) {
  TransportFeedbackBuilder b;
  SendHistory h;
  for (uint16_t s = 0; s < 50; ++s) h.OnPacketSent(s, Timestamp::Millis(1000 + s), DataSize::Bytes(1000));
  SM_EXPECT_EQ(h.InFlight().bytes(), 50000);
  for (uint16_t s = 0; s < 50; ++s) {
    if (s % 10 != 5) b.OnPacket(s, Timestamp::Micros(1040000 + s * 1000 + 123));
  }
  auto fbs = b.Build();
  SM_ASSERT_EQ(fbs.size(), 1u);
  auto parsed = ParseTransportFeedback(SerializeTransportFeedback(fbs[0]));
  SM_ASSERT_TRUE(parsed.has_value());
  auto res = h.OnFeedback(*parsed, Timestamp::Millis(1100));
  SM_ASSERT_EQ(res.size(), 50u);
  int lost = 0;
  for (const auto& r : res) {
    if (!r.received()) {
      ++lost;
      continue;
    }
    // 250 us quantization.
    double expect_us = 1040000.0 + static_cast<double>(r.seq) * 1000 + 123;
    SM_EXPECT_NEAR(static_cast<double>(r.receive_time.us()), expect_us, 250);
    SM_EXPECT_EQ(r.send_time.ms(), 1000 + r.seq);
  }
  SM_EXPECT_EQ(lost, 5);
  SM_EXPECT_EQ(h.InFlight().bytes(), 0);
  SM_EXPECT_TRUE(b.Build().empty());
}

SM_TEST(TransportFeedback, LateArrivalAfterReportIsCounted) {
  TransportFeedbackBuilder b;
  b.OnPacket(1, Timestamp::Millis(10));
  b.OnPacket(3, Timestamp::Millis(12));
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
  b.OnPacket(2, Timestamp::Millis(30));
  SM_EXPECT_EQ(b.stats().late_after_report, 1u);
}

SM_TEST(TransportFeedback, SplitsWhenDeltaOverflows) {
  TransportFeedbackBuilder b;
  b.OnPacket(1, Timestamp::Millis(0));
  b.OnPacket(2, Timestamp::Millis(20000));  // 80000 ticks > int16.
  auto f = b.Build();
  SM_EXPECT_EQ(f.size(), 2u);
}

SM_TEST(TransportFeedback, RembRoundTrip) {
  auto bytes = SerializeRemb(5, DataRate::BitsPerSec(1234567), {11, 22});
  auto r = ParseRemb(bytes);
  SM_ASSERT_TRUE(r.has_value());
  // Mantissa/exponent: 18-bit mantissa keeps ~4 significant digits.
  SM_EXPECT_NEAR(static_cast<double>(r->first.bps()), 1234567, 1234567 * 0.0001);
  SM_EXPECT_EQ(r->second.size(), 2u);
  SM_EXPECT_EQ(r->second[1], 22u);
}

// --- Version 2: libwebrtc's TransportSequenceNumberFeedbackGenenerator ---------
// Ported from modules/remote_bitrate_estimator/transport_sequence_number_feedback_generator_unittest.cc.

namespace {
constexpr uint16_t kBaseSeq = 10;
const Timestamp kBaseTime = Timestamp::Millis(123);

TransportFeedbackBuilder V2() { return TransportFeedbackBuilder(1, 456, FeedbackBuilderConfig{2}); }

std::vector<int64_t> Seqs(const TransportFeedback& fb) {
  std::vector<int64_t> v;
  for (const auto& a : fb.ArrivalTimes()) v.push_back(a.first);
  return v;
}
// Arrival times are quantised to 250 us ticks; the tests use whole ms.
std::vector<int64_t> TimesMs(const TransportFeedback& fb) {
  std::vector<int64_t> v;
  for (const auto& a : fb.ArrivalTimes()) v.push_back(a.second.ms());
  return v;
}
}  // namespace

SM_TEST(TransportFeedbackV2, SinglePacketAndDuplicate) {
  auto b = V2();
  b.OnPacket(kBaseSeq, kBaseTime);
  b.OnPacket(kBaseSeq, kBaseTime + TimeDelta::Seconds(1));  // Only the first arrival counts.
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].base_seq, kBaseSeq);
  SM_EXPECT_EQ(f[0].media_ssrc, 456u);
  SM_EXPECT_EQ(Seqs(f[0]).size(), 1u);
  SM_EXPECT_EQ(TimesMs(f[0])[0], 123);
}

SM_TEST(TransportFeedbackV2, MissingStartAndNothingNewIsNotReported) {
  auto b = V2();
  b.OnPacket(kBaseSeq, kBaseTime);
  b.OnPacket(kBaseSeq + 1, kBaseTime + TimeDelta::Seconds(1));
  SM_EXPECT_EQ(b.Build().size(), 1u);
  SM_EXPECT_TRUE(b.Build().empty());
  b.OnPacket(kBaseSeq + 3, kBaseTime + TimeDelta::Seconds(3));  // kBaseSeq + 2 is missing.
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].base_seq, kBaseSeq + 2);
  SM_ASSERT_EQ(Seqs(f[0]).size(), 1u);
  SM_EXPECT_EQ(Seqs(f[0])[0], kBaseSeq + 3);
  SM_EXPECT_EQ(f[0].packets.size(), 2u);  // The missing one and the received one.
  SM_EXPECT_EQ(TimesMs(f[0])[0], 3123);
}

SM_TEST(TransportFeedbackV2, FragmentsWhenDeltaIsTooLarge) {
  auto b = V2();
  b.OnPacket(kBaseSeq, kBaseTime);
  b.OnPacket(kBaseSeq + 1, kBaseTime + TimeDelta::Micros(250 * (1 << 16)));
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 2u);
  SM_EXPECT_EQ(f[0].base_seq, kBaseSeq);
  SM_EXPECT_EQ(Seqs(f[0]).size(), 1u);
  SM_EXPECT_EQ(f[1].base_seq, kBaseSeq + 1);
  SM_EXPECT_EQ(Seqs(f[1]).size(), 1u);
}

SM_TEST(TransportFeedbackV2, ReorderingAcrossTheWrap) {
  auto b = V2();
  b.OnPacket(kBaseSeq, kBaseTime);
  b.OnPacket(62762, kBaseTime + TimeDelta::Seconds(1));  // Unwraps to before kBaseSeq.
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].base_seq, 62762);
  SM_EXPECT_EQ(TimesMs(f[0])[0], 1123);
  SM_EXPECT_EQ(TimesMs(f[0])[1], 123);
}

SM_TEST(TransportFeedbackV2, MalformedJumpsReportOnlyTheLastPackets) {
  auto b = V2();
  for (int i = 0; i < 10; ++i) {
    b.OnPacket(static_cast<uint16_t>(kBaseSeq + i), kBaseTime + TimeDelta::Seconds(3 * i));
    b.OnPacket(static_cast<uint16_t>(kBaseSeq + 20000 + i), kBaseTime + TimeDelta::Seconds(3 * i + 1));
    b.OnPacket(static_cast<uint16_t>(kBaseSeq + 40000 + i), kBaseTime + TimeDelta::Seconds(3 * i + 2));
  }
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_ASSERT_EQ(Seqs(f[0]).size(), 2u);
  SM_EXPECT_EQ(static_cast<uint16_t>(Seqs(f[0])[0]), kBaseSeq + 20009);
  SM_EXPECT_EQ(static_cast<uint16_t>(Seqs(f[0])[1]), kBaseSeq + 40009);
  SM_EXPECT_EQ(TimesMs(f[0])[0], 123 + 28000);
  SM_EXPECT_EQ(TimesMs(f[0])[1], 123 + 29000);
}

SM_TEST(TransportFeedbackV2, LateArrivalIsReportedAgainWithTheBackWindow) {
  auto b = V2();
  b.OnPacket(1, Timestamp::Millis(10));
  b.OnPacket(3, Timestamp::Millis(12));
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);  // 1 received, 2 missing, 3 received.
  // 2 arrives after the report: the window start moves back, and the next
  // message covers 2 and 3 (3 is reported again, as libwebrtc does).
  b.OnPacket(2, Timestamp::Millis(30));
  f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].base_seq, 2);
  SM_EXPECT_EQ(Seqs(f[0]).size(), 2u);
  SM_EXPECT_EQ(b.stats().re_reported, 2u);  // 2 (late, not reported before) and 3 are below the previous report's end.
  SM_EXPECT_TRUE(b.Build().empty());
}

SM_TEST(TransportFeedbackV2, OldArrivalsAreCulledAfterTheBackWindow) {
  auto b = V2();
  b.OnPacket(1, Timestamp::Millis(1000));
  SM_EXPECT_EQ(b.Build().size(), 1u);
  // 600 ms later the window start has caught up, so 1 is dropped when 2 arrives;
  // a packet reordered before 2 and older than the window is then a fresh report.
  b.OnPacket(2, Timestamp::Millis(1600));
  auto f = b.Build();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].base_seq, 2);
  SM_EXPECT_EQ(Seqs(f[0]).size(), 1u);
}

SM_TEST(TransportFeedbackV2, IntervalFollowsFivePercentOfTheEstimate) {
  auto b = V2();
  SM_EXPECT_EQ(b.Interval().ms(), 100);  // Unknown estimate.
  b.OnSendBandwidthEstimateChanged(DataRate::BitsPerSec(300000));
  SM_EXPECT_EQ(b.Interval().ms(), 50);
  b.OnSendBandwidthEstimateChanged(DataRate::BitsPerSec(0));
  SM_EXPECT_EQ(b.Interval().ms(), 250);
  b.OnSendBandwidthEstimateChanged(DataRate::BitsPerSec(20000));
  SM_EXPECT_EQ(b.Interval().ms(), 250);
  b.OnSendBandwidthEstimateChanged(DataRate::BitsPerSec(80000));
  SM_EXPECT_EQ(b.Interval().ms(), 136);  // 68 B * 8 / (0.05 * 80 kbit/s).
}

// libwebrtc's TransportFeedbackAdapter erases a packet from its history once
// it is reported received ("packet reported received more than once"), so a
// re-report is ignored and never feeds the estimators a receive time that goes
// backwards.
SM_TEST(TransportFeedback, PacketReportedReceivedTwiceIsIgnored) {
  SendHistory h;
  for (uint16_t s = 0; s < 4; ++s) h.OnPacketSent(s, Timestamp::Millis(1000 + s), DataSize::Bytes(1000));
  TransportFeedbackBuilder b(0, 0, FeedbackBuilderConfig{2});
  b.OnPacket(0, Timestamp::Micros(1040000));
  b.OnPacket(1, Timestamp::Micros(1041000));
  b.OnPacket(3, Timestamp::Micros(1043000));
  auto fbs = b.Build();
  SM_ASSERT_EQ(fbs.size(), 1u);
  auto first = h.OnFeedback(*ParseTransportFeedback(SerializeTransportFeedback(fbs[0])), Timestamp::Millis(1100));
  SM_EXPECT_EQ(first.size(), 4u);  // 0, 1 and 3 received, 2 reported lost.
  // Packet 2 arrives late; the generator reports it again along with 3.
  b.OnPacket(2, Timestamp::Micros(1060000));
  b.OnPacket(3, Timestamp::Micros(1060500));
  auto fbs2 = b.Build();
  SM_ASSERT_EQ(fbs2.size(), 1u);
  auto second = h.OnFeedback(*ParseTransportFeedback(SerializeTransportFeedback(fbs2[0])), Timestamp::Millis(1120));
  int received = 0;
  for (const auto& r : second) {
    SM_EXPECT_TRUE(r.seq == 2 || !r.received());
    if (r.received()) ++received;
  }
  SM_EXPECT_EQ(received, 1);
}
