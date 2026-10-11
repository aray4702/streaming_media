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
