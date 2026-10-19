// Ports of libwebrtc's goog_cc/robust_throughput_estimator_unittest.cc (M157,
// compare/webrtc/reference/) to RobustThroughputEstimator (#166). M157 enables
// the estimator by default (RobustThroughputEstimatorSettings::enabled = true),
// so it is the acknowledged-bitrate estimator of GoogCcNetworkController.
// DefaultEnabled / CanDisable test the field-trial parser, which is not ported.
#include <algorithm>
#include <cmath>
#include <vector>

#include "sm/cc/aimd_rate_control.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

DataRate BytesPerSec(int64_t b) { return DataRate::BitsPerSec(b * 8); }
double BytesPerSecOf(DataRate r) { return static_cast<double>(r.bps()) / 8.0; }
TimeDelta SizeOverRate(DataSize s, DataRate r) { return TimeDelta::Micros(s.bits() * 1000000 / r.bps()); }

class FeedbackGenerator {
 public:
  std::vector<PacketResult> CreateFeedbackVector(size_t number_of_packets, DataSize packet_size, DataRate send_rate, DataRate recv_rate) {
    std::vector<PacketResult> v(number_of_packets);
    for (size_t i = 0; i < number_of_packets; i++) {
      v[i].send_time = send_clock_;
      v[i].seq = sequence_number_;
      v[i].size = packet_size;
      send_clock_ = send_clock_ + SizeOverRate(packet_size, send_rate);
      recv_clock_ = recv_clock_ + SizeOverRate(packet_size, recv_rate);
      sequence_number_ += 1;
      v[i].receive_time = recv_clock_;
    }
    return v;
  }
  Timestamp CurrentReceiveClock() { return recv_clock_; }
  void AdvanceReceiveClock(TimeDelta delta) { recv_clock_ = recv_clock_ + delta; }
  void AdvanceSendClock(TimeDelta delta) { send_clock_ = send_clock_ + delta; }

 private:
  Timestamp send_clock_ = Timestamp::Millis(100000);
  Timestamp recv_clock_ = Timestamp::Millis(10000);
  int64_t sequence_number_ = 100;
};

RobustThroughputEstimator::Settings Window500ms() {
  RobustThroughputEstimator::Settings s;
  s.min_window_duration = TimeDelta::Millis(500);
  return s;
}

std::vector<PacketResult> Packets(FeedbackGenerator& g, size_t n, DataRate send, DataRate recv) {
  return g.CreateFeedbackVector(n, DataSize::Bytes(1000), send, recv);
}

}  // namespace

SM_TEST(RobustThroughputEstimator, InitialEstimate) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate send_rate = BytesPerSec(100000), recv_rate = BytesPerSec(100000);
  // No estimate until the estimator has enough data.
  est.OnPacketResults(Packets(g, 9, send_rate, recv_rate));
  SM_EXPECT_FALSE(est.bitrate().has_value());
  // Estimate once `required_packets` packets have been received.
  est.OnPacketResults(Packets(g, 1, send_rate, recv_rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
  // Estimate remains stable when send and receive rates are stable.
  est.OnPacketResults(Packets(g, 15, send_rate, recv_rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
}

SM_TEST(RobustThroughputEstimator, EstimateAdapts) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  // 1 second, 800 kbps, estimate is stable.
  DataRate rate = BytesPerSec(100000);
  for (int i = 0; i < 10; ++i) {
    est.OnPacketResults(Packets(g, 10, rate, rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_EQ(est.bitrate()->bps(), rate.bps());
  }
  // 1 second, 1600 kbps, estimate increases.
  rate = BytesPerSec(200000);
  for (int i = 0; i < 20; ++i) {
    est.OnPacketResults(Packets(g, 10, rate, rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_GE(est.bitrate()->bps(), BytesPerSec(100000).bps());
    SM_EXPECT_LE(est.bitrate()->bps(), rate.bps());
  }
  // 1 second, 1600 kbps, estimate is stable.
  for (int i = 0; i < 20; ++i) {
    est.OnPacketResults(Packets(g, 10, rate, rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_EQ(est.bitrate()->bps(), rate.bps());
  }
  // 1 second, 400 kbps, estimate decreases.
  rate = BytesPerSec(50000);
  for (int i = 0; i < 5; ++i) {
    est.OnPacketResults(Packets(g, 10, rate, rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_LE(est.bitrate()->bps(), BytesPerSec(200000).bps());
    SM_EXPECT_GE(est.bitrate()->bps(), rate.bps());
  }
  // 1 second, 400 kbps, estimate is stable.
  for (int i = 0; i < 5; ++i) {
    est.OnPacketResults(Packets(g, 10, rate, rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_EQ(est.bitrate()->bps(), rate.bps());
  }
}

SM_TEST(RobustThroughputEstimator, CappedByReceiveRate) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate send_rate = BytesPerSec(100000), recv_rate = BytesPerSec(25000);
  est.OnPacketResults(Packets(g, 20, send_rate, recv_rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(recv_rate), 0.05 * BytesPerSecOf(recv_rate));
}

SM_TEST(RobustThroughputEstimator, CappedBySendRate) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate send_rate = BytesPerSec(50000), recv_rate = BytesPerSec(100000);
  est.OnPacketResults(Packets(g, 20, send_rate, recv_rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(send_rate), 0.05 * BytesPerSecOf(send_rate));
}

SM_TEST(RobustThroughputEstimator, DelaySpike) {
  FeedbackGenerator g;
  // A 500 ms window amplifies the effect of a delay spike.
  RobustThroughputEstimator est(Window500ms());
  DataRate send_rate = BytesPerSec(100000), recv_rate = BytesPerSec(100000);
  est.OnPacketResults(Packets(g, 20, send_rate, recv_rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
  // Delay spike. 25 packets sent, but none received.
  g.AdvanceReceiveClock(TimeDelta::Millis(250));
  // Deliver all of them during the next 50 ms (30 packets of 1000 bytes: 600000 bytes per second).
  recv_rate = BytesPerSec(600000);
  for (int i = 0; i < 30; ++i) {  // Estimate should not drop.
    est.OnPacketResults(Packets(g, 1, send_rate, recv_rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(send_rate), 0.05 * BytesPerSecOf(send_rate));
  }
  // Delivery at normal rate. Once the packets received before the gap have left
  // the window, the receive rate is high, but the estimate is capped by the send rate.
  recv_rate = BytesPerSec(100000);
  for (int i = 0; i < 20; ++i) {
    est.OnPacketResults(Packets(g, 5, send_rate, recv_rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(send_rate), 0.05 * BytesPerSecOf(send_rate));
  }
}

SM_TEST(RobustThroughputEstimator, HighLoss) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate send_rate = BytesPerSec(100000), recv_rate = BytesPerSec(100000);
  std::vector<PacketResult> v = Packets(g, 20, send_rate, recv_rate);
  for (size_t i = 0; i < v.size(); i++) {  // 50% loss.
    if (i % 2 == 1) v[i].receive_time = Timestamp::PlusInfinity();
  }
  std::sort(v.begin(), v.end(), [](const PacketResult& a, const PacketResult& b) { return a.receive_time < b.receive_time; });
  est.OnPacketResults(v);
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(send_rate) / 2, 0.05 * BytesPerSecOf(send_rate) / 2);
}

SM_TEST(RobustThroughputEstimator, ReorderedFeedback) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate send_rate = BytesPerSec(100000), recv_rate = BytesPerSec(100000);
  est.OnPacketResults(Packets(g, 20, send_rate, recv_rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
  std::vector<PacketResult> delayed = Packets(g, 10, send_rate, recv_rate);
  std::vector<PacketResult> v = Packets(g, 10, send_rate, recv_rate);
  // Some feedback is missing, so the estimate drops.
  est.OnPacketResults(v);
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_LT(est.bitrate()->bps(), send_rate.bps());
  // But it recovers completely as soon as the feedback arrives.
  est.OnPacketResults(delayed);
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
  // And remains stable, as if the feedbacks had not been reordered.
  for (int i = 0; i < 10; ++i) {
    est.OnPacketResults(Packets(g, 15, send_rate, recv_rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
  }
}

SM_TEST(RobustThroughputEstimator, DeepReordering) {
  FeedbackGenerator g;
  // A 500 ms window amplifies the effect of reordering.
  RobustThroughputEstimator est(Window500ms());
  DataRate send_rate = BytesPerSec(100000), recv_rate = BytesPerSec(100000);
  std::vector<PacketResult> delayed = Packets(g, 1, send_rate, recv_rate);
  for (int i = 0; i < 10; i++) {
    est.OnPacketResults(Packets(g, 10, send_rate, recv_rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_EQ(est.bitrate()->bps(), send_rate.bps());
  }
  // The delayed packet arrives ~1 s late; the send rate must not drop.
  delayed.front().receive_time = g.CurrentReceiveClock();
  est.OnPacketResults(delayed);
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(send_rate), 0.05 * BytesPerSecOf(send_rate));
  // Throughput stays stable.
  for (int i = 0; i < 10; i++) {
    est.OnPacketResults(Packets(g, 10, send_rate, recv_rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), BytesPerSecOf(send_rate), 0.05 * BytesPerSecOf(send_rate));
  }
}

SM_TEST(RobustThroughputEstimator, ResetsIfReceiveClockChangeBackwards) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate rate = BytesPerSec(100000);
  est.OnPacketResults(Packets(g, 20, rate, rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), rate.bps());
  g.AdvanceReceiveClock(TimeDelta::Seconds(-2));
  rate = BytesPerSec(200000);
  est.OnPacketResults(Packets(g, 20, rate, rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_EQ(est.bitrate()->bps(), rate.bps());
}

SM_TEST(RobustThroughputEstimator, StreamPausedAndResumed) {
  FeedbackGenerator g;
  RobustThroughputEstimator est;
  DataRate rate = BytesPerSec(100000);
  est.OnPacketResults(Packets(g, 20, rate, rate));
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_NEAR(BytesPerSecOf(*est.bitrate()), 100000.0, 5000.0);
  // No packets sent or feedback received for 60 s.
  g.AdvanceSendClock(TimeDelta::Seconds(60));
  g.AdvanceReceiveClock(TimeDelta::Seconds(60));
  // Resumes at the same rate; the estimate is invalid for lack of recent data...
  est.OnPacketResults(Packets(g, 5, rate, rate));
  SM_EXPECT_FALSE(est.bitrate().has_value());
  // ...and is back to the normal level once there is enough.
  for (int i = 0; i < 4; ++i) {
    est.OnPacketResults(Packets(g, 5, rate, rate));
    SM_ASSERT_TRUE(est.bitrate().has_value());
    SM_EXPECT_EQ(est.bitrate()->bps(), rate.bps());
  }
}
