// Ports of libwebrtc's goog_cc/delay_based_bwe_unittest.cc and its helper
// delay_based_bwe_unittest_helper.{h,cc} (M157, compare/webrtc/reference/) to
// `cc v2`'s DelayBasedBwe (#166, #61 criterion 2). The harness keeps the
// helper's stream generator, its feedback path (acknowledged-bitrate estimator
// and ProbeBitrateEstimator in front of the DelayBasedBwe) and every constant;
// the expected values are libwebrtc's.
//
// The helper's fixture enables the field trial
// `WebRTC-Bwe-RobustThroughputEstimatorSettings/enabled:true/`, which is also
// M157's default (RobustThroughputEstimatorSettings::enabled = true), so the
// acknowledged bitrate comes from RobustThroughputEstimator here.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "sm/cc/aimd_rate_control.h"
#include "sm/cc/delay_based_bwe.h"
#include "sm/cc/probing.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr size_t kMtu = 1200;
constexpr uint32_t kAcceptedBitrateErrorBps = 50000;
constexpr int kNumInitialPackets = 2;
constexpr int kInitialProbingPackets = 5;
constexpr int64_t kSendSideOffsetUs = 1000000;
constexpr int kNumProbesCluster0 = 5;
constexpr int kNumProbesCluster1 = 8;
constexpr float kTargetUtilizationFraction = 0.95f;

// PacedPacketInfo(probe_cluster_id, probe_cluster_min_probes, probe_cluster_min_bytes).
struct PacedPacketInfo {
  int probe_cluster_id = -1;
  int min_probes = 0;
  int min_bytes = 0;
  PacedPacketInfo() = default;
  PacedPacketInfo(int id, int probes, int bytes) : probe_cluster_id(id), min_probes(probes), min_bytes(bytes) {}
};
const PacedPacketInfo kPacingInfo0(0, kNumProbesCluster0, 2000);
const PacedPacketInfo kPacingInfo1(1, kNumProbesCluster1, 4000);

// test::RtpStream.
class RtpStream {
 public:
  RtpStream(int fps, int bitrate_bps) : fps_(fps), bitrate_bps_(bitrate_bps) {}
  int64_t GenerateFrame(int64_t time_now_us, int64_t* next_sequence_number, std::vector<PacketResult>* packets) {
    if (time_now_us < next_rtp_time_) return next_rtp_time_;
    size_t bits_per_frame = static_cast<size_t>((bitrate_bps_ + fps_ / 2) / fps_);
    size_t n_packets = std::max<size_t>((bits_per_frame + 4 * kMtu) / (8 * kMtu), 1u);
    size_t payload_size = (bits_per_frame + 4 * n_packets) / (8 * n_packets);
    for (size_t i = 0; i < n_packets; ++i) {
      PacketResult packet;
      packet.send_time = Timestamp::Micros(time_now_us + kSendSideOffsetUs);
      packet.size = DataSize::Bytes(static_cast<int64_t>(payload_size));
      packet.seq = (*next_sequence_number)++;
      packets->push_back(packet);
    }
    next_rtp_time_ = time_now_us + (1000000 + fps_ / 2) / fps_;
    return next_rtp_time_;
  }
  int64_t next_rtp_time() const { return next_rtp_time_; }
  void set_bitrate_bps(int bitrate_bps) { bitrate_bps_ = bitrate_bps; }
  int bitrate_bps() const { return bitrate_bps_; }

 private:
  int fps_;
  int bitrate_bps_;
  int64_t next_rtp_time_ = 0;
};

// test::StreamGenerator: a bottleneck of `capacity` that serialises the packets.
class StreamGenerator {
 public:
  StreamGenerator(int capacity, int64_t time_now) : capacity_(capacity), prev_arrival_time_us_(time_now) {}
  void AddStream(RtpStream* stream) { streams_.push_back(std::unique_ptr<RtpStream>(stream)); }
  void set_capacity_bps(int capacity_bps) { capacity_ = capacity_bps; }
  void SetBitrateBps(int bitrate_bps) {
    int total_bitrate_before = 0;
    for (const auto& stream : streams_) total_bitrate_before += stream->bitrate_bps();
    int64_t bitrate_before = 0;
    int total_bitrate_after = 0;
    for (const auto& stream : streams_) {
      bitrate_before += stream->bitrate_bps();
      int64_t bitrate_after = (bitrate_before * bitrate_bps + total_bitrate_before / 2) / total_bitrate_before;
      stream->set_bitrate_bps(static_cast<int>(bitrate_after - total_bitrate_after));
      total_bitrate_after += stream->bitrate_bps();
    }
    SM_EXPECT_EQ(bitrate_before, total_bitrate_before);
    SM_EXPECT_EQ(total_bitrate_after, bitrate_bps);
  }
  int64_t GenerateFrame(int64_t time_now_us, int64_t* next_sequence_number, std::vector<PacketResult>* packets) {
    auto next = [&] {
      return std::min_element(streams_.begin(), streams_.end(),
                              [](const auto& l, const auto& r) { return l->next_rtp_time() < r->next_rtp_time(); });
    };
    auto it = next();
    (*it)->GenerateFrame(time_now_us, next_sequence_number, packets);
    for (PacketResult& packet : *packets) {
      int capacity_bpus = capacity_ / 1000;
      int64_t required_network_time_us = (8 * 1000 * packet.size.bytes() + capacity_bpus / 2) / capacity_bpus;
      prev_arrival_time_us_ = std::max(time_now_us + required_network_time_us, prev_arrival_time_us_ + required_network_time_us);
      packet.receive_time = Timestamp::Micros(prev_arrival_time_us_);
    }
    it = next();
    return std::max((*it)->next_rtp_time(), time_now_us);
  }

 private:
  int capacity_;
  int64_t prev_arrival_time_us_;
  std::vector<std::unique_ptr<RtpStream>> streams_;
};

// DelayBasedBweTest: a simulated clock, the stream generator, and the estimators
// as GoogCcNetworkController chains them.
class DelayBasedBweTest {
 public:
  DelayBasedBweTest() : now_us_(100000000), stream_generator_(1000000, 100000000) {
    DelayBasedBwe::Config cfg;
    bwe_ = std::make_unique<DelayBasedBwe>(cfg);
    probe_est_ = ProbeBitrateEstimator();
  }

  Timestamp Now() const { return Timestamp::Micros(now_us_); }
  int64_t TimeInMilliseconds() const { return (now_us_ + 500) / 1000; }  // SimulatedClock rounds.
  void AdvanceTimeMilliseconds(int64_t ms) { now_us_ += ms * 1000; }
  void AdvanceTimeMicroseconds(int64_t us) { now_us_ += us; }

  void AddDefaultStream() { stream_generator_.AddStream(new RtpStream(30, 300000)); }

  // TestBitrateObserver.
  struct Observer {
    uint32_t latest_bitrate = 0;
    bool updated = false;
    void Reset() { updated = false; }
    void OnReceiveBitrateChanged(uint32_t bitrate) {
      latest_bitrate = bitrate;
      updated = true;
    }
  };

  // LatestEstimate(&ssrcs, &bitrate): true once the rate control has an estimate.
  bool LatestEstimate(DataRate* bitrate) const {
    if (!bwe_->rate_control().ValidEstimate()) return false;
    *bitrate = bwe_->rate_control().LatestEstimate();
    return true;
  }

  void IncomingFeedback(int64_t arrival_time_ms, int64_t send_time_ms, size_t payload_size, const PacedPacketInfo& pacing = {}) {
    IncomingFeedbackAt(Timestamp::Millis(arrival_time_ms + arrival_time_offset_ms_), Timestamp::Millis(send_time_ms), payload_size, pacing);
  }

  void IncomingFeedbackAt(Timestamp receive_time, Timestamp send_time, size_t payload_size, const PacedPacketInfo& pacing) {
    PacketResult packet;
    packet.receive_time = receive_time;
    packet.send_time = send_time;
    packet.size = DataSize::Bytes(static_cast<int64_t>(payload_size));
    packet.seq = next_sequence_number_++;
    packet.probe_cluster = pacing.probe_cluster_id;
    if (pacing.probe_cluster_id >= 0) {
      ProbeCluster c;
      c.id = pacing.probe_cluster_id;
      c.min_probes = pacing.min_probes;
      c.min_bytes = DataSize::Bytes(pacing.min_bytes);
      probe_est_.SetClusterInfo(c);
      probe_est_.OnPacketResult(packet);
    }
    std::vector<PacketResult> msg{packet};
    acked_.OnPacketResults(msg);
    bwe_->OnPacketResults(msg, acked_.bitrate(), probe_est_.FetchAndResetLastEstimate(), Now());
    if (bwe_->last_result().updated) observer_.OnReceiveBitrateChanged(static_cast<uint32_t>(bwe_->estimate().bps()));
  }

  // Returns true if an over-use was seen.
  bool GenerateAndProcessFrame(uint32_t bitrate_bps) {
    stream_generator_.SetBitrateBps(static_cast<int>(bitrate_bps));
    std::vector<PacketResult> packets;
    int64_t next_time_us = stream_generator_.GenerateFrame(now_us_, &next_sequence_number_, &packets);
    if (packets.empty()) return false;
    bool overuse = false;
    observer_.Reset();
    now_us_ += packets.back().receive_time.us() - now_us_;
    for (auto& packet : packets) {
      packet.receive_time += TimeDelta::Millis(arrival_time_offset_ms_);
    }
    acked_.OnPacketResults(packets);
    bwe_->OnPacketResults(packets, acked_.bitrate(), probe_est_.FetchAndResetLastEstimate(), Now());
    if (bwe_->last_result().updated) {
      observer_.OnReceiveBitrateChanged(static_cast<uint32_t>(bwe_->estimate().bps()));
      if (!first_update_ && bwe_->estimate().bps() < static_cast<int64_t>(bitrate_bps)) overuse = true;
      first_update_ = false;
    }
    now_us_ += next_time_us - now_us_;
    return overuse;
  }

  uint32_t SteadyStateRun(int max_number_of_frames, uint32_t start_bitrate, uint32_t min_bitrate, uint32_t max_bitrate,
                          uint32_t target_bitrate) {
    uint32_t bitrate_bps = start_bitrate;
    bool bitrate_update_seen = false;
    for (int i = 0; i < max_number_of_frames; ++i) {
      bool overuse = GenerateAndProcessFrame(bitrate_bps);
      if (overuse) {
        SM_EXPECT_LT(observer_.latest_bitrate, max_bitrate);
        SM_EXPECT_GT(observer_.latest_bitrate, min_bitrate);
        bitrate_bps = observer_.latest_bitrate;
        bitrate_update_seen = true;
      } else if (observer_.updated) {
        bitrate_bps = observer_.latest_bitrate;
        observer_.Reset();
      }
      if (bitrate_update_seen && bitrate_bps > target_bitrate) break;
    }
    SM_EXPECT_TRUE(bitrate_update_seen);
    return bitrate_bps;
  }

  void InitialBehaviorTestHelper(uint32_t expected_converge_bitrate) {
    const int kFramerate = 50;
    const int kFrameIntervalMs = 1000 / kFramerate;
    const PacedPacketInfo kPacingInfo(0, 5, 5000);
    DataRate bitrate = DataRate::Zero();
    int64_t send_time_ms = 0;
    SM_EXPECT_FALSE(LatestEstimate(&bitrate));
    AdvanceTimeMilliseconds(1000);
    SM_EXPECT_FALSE(LatestEstimate(&bitrate));
    SM_EXPECT_FALSE(observer_.updated);
    observer_.Reset();
    AdvanceTimeMilliseconds(1000);
    // Inserting packets for 5 seconds to get a valid estimate.
    for (int i = 0; i < 5 * kFramerate + 1 + kNumInitialPackets; ++i) {
      PacedPacketInfo pacing_info = i < kInitialProbingPackets ? kPacingInfo : PacedPacketInfo();
      if (i == kNumInitialPackets) {
        SM_EXPECT_FALSE(LatestEstimate(&bitrate));
        SM_EXPECT_FALSE(observer_.updated);
        observer_.Reset();
      }
      IncomingFeedback(TimeInMilliseconds(), send_time_ms, kMtu, pacing_info);
      AdvanceTimeMilliseconds(1000 / kFramerate);
      send_time_ms += kFrameIntervalMs;
    }
    SM_EXPECT_TRUE(LatestEstimate(&bitrate));
    SM_EXPECT_NEAR(expected_converge_bitrate, bitrate.bps(), kAcceptedBitrateErrorBps);
    SM_EXPECT_TRUE(observer_.updated);
    observer_.Reset();
    SM_EXPECT_EQ(static_cast<int64_t>(observer_.latest_bitrate), bitrate.bps());
  }

  void RateIncreaseReorderingTestHelper(uint32_t expected_bitrate_bps) {
    const int kFramerate = 50;
    const int kFrameIntervalMs = 1000 / kFramerate;
    const PacedPacketInfo kPacingInfo(0, 5, 5000);
    int64_t send_time_ms = 0;
    for (int i = 0; i < 5 * kFramerate + 1 + kNumInitialPackets; ++i) {
      PacedPacketInfo pacing_info = i < kInitialProbingPackets ? kPacingInfo : PacedPacketInfo();
      if (i == kNumInitialPackets) SM_EXPECT_FALSE(observer_.updated);  // No valid estimate.
      IncomingFeedback(TimeInMilliseconds(), send_time_ms, kMtu, pacing_info);
      AdvanceTimeMilliseconds(kFrameIntervalMs);
      send_time_ms += kFrameIntervalMs;
    }
    SM_EXPECT_TRUE(observer_.updated);
    SM_EXPECT_NEAR(expected_bitrate_bps, observer_.latest_bitrate, kAcceptedBitrateErrorBps);
    for (int i = 0; i < 10; ++i) {
      AdvanceTimeMilliseconds(2 * kFrameIntervalMs);
      send_time_ms += 2 * kFrameIntervalMs;
      IncomingFeedback(TimeInMilliseconds(), send_time_ms, 1000);
      IncomingFeedback(TimeInMilliseconds(), send_time_ms - kFrameIntervalMs, 1000);
    }
    SM_EXPECT_TRUE(observer_.updated);
    SM_EXPECT_NEAR(expected_bitrate_bps, observer_.latest_bitrate, kAcceptedBitrateErrorBps);
  }

  void RateIncreaseRtpTimestampsTestHelper(int expected_iterations) {
    uint32_t bitrate_bps = 30000;
    int iterations = 0;
    AddDefaultStream();
    while (bitrate_bps < 500000) {
      bool overuse = GenerateAndProcessFrame(bitrate_bps);
      if (overuse) {
        SM_EXPECT_GT(observer_.latest_bitrate, bitrate_bps);
        bitrate_bps = observer_.latest_bitrate;
        observer_.Reset();
      } else if (observer_.updated) {
        bitrate_bps = observer_.latest_bitrate;
        observer_.Reset();
      }
      ++iterations;
      if (iterations > 10 * expected_iterations) break;  // Not in libwebrtc: bounds a failing run.
    }
    SM_ASSERT_EQ(expected_iterations, iterations);
  }

  void CapacityDropTestHelper(int number_of_streams, uint32_t expected_bitrate_drop_delta, int64_t receiver_clock_offset_change_ms) {
    const int kFramerate = 30;
    const int kStartBitrate = 900000;
    const int kMinExpectedBitrate = 800000;
    const int kMaxExpectedBitrate = 1100000;
    const uint32_t kInitialCapacityBps = 1000000;
    const uint32_t kReducedCapacityBps = 500000;
    int steady_state_time = 0;
    if (number_of_streams <= 1) {
      steady_state_time = 10;
      AddDefaultStream();
    } else {
      steady_state_time = 10 * number_of_streams;
      int bitrate_sum = 0;
      int kBitrateDenom = number_of_streams * (number_of_streams - 1);
      for (int i = 0; i < number_of_streams; i++) {
        int bitrate = kStartBitrate / 2;
        if (i > 0) bitrate = (kStartBitrate * i + kBitrateDenom / 2) / kBitrateDenom;
        stream_generator_.AddStream(new RtpStream(kFramerate, bitrate));
        bitrate_sum += bitrate;
      }
      SM_ASSERT_EQ(bitrate_sum, kStartBitrate);
    }
    // Run in steady state to make the estimator converge.
    stream_generator_.set_capacity_bps(static_cast<int>(kInitialCapacityBps));
    uint32_t bitrate_bps = SteadyStateRun(steady_state_time * kFramerate, kStartBitrate, kMinExpectedBitrate, kMaxExpectedBitrate, kInitialCapacityBps);
    SM_EXPECT_NEAR(kInitialCapacityBps, bitrate_bps, 180000u);
    observer_.Reset();
    // Add an offset to make sure the BWE can handle it.
    arrival_time_offset_ms_ += receiver_clock_offset_change_ms;
    // Reduce the capacity and verify the decrease time.
    stream_generator_.set_capacity_bps(static_cast<int>(kReducedCapacityBps));
    int64_t overuse_start_time = TimeInMilliseconds();
    int64_t bitrate_drop_time = -1;
    for (int i = 0; i < 100 * number_of_streams; ++i) {
      GenerateAndProcessFrame(bitrate_bps);
      if (bitrate_drop_time == -1 && observer_.latest_bitrate <= kReducedCapacityBps) bitrate_drop_time = TimeInMilliseconds();
      if (observer_.updated) bitrate_bps = observer_.latest_bitrate;
    }
    SM_EXPECT_NEAR(expected_bitrate_drop_delta, bitrate_drop_time - overuse_start_time, 33);
  }

  void TestTimestampGroupingTestHelper() {
    const int kFramerate = 50;
    const int kFrameIntervalMs = 1000 / kFramerate;
    int64_t send_time_ms = 0;
    for (int i = 0; i <= 6 * kFramerate; ++i) {
      IncomingFeedback(TimeInMilliseconds(), send_time_ms, 1000);
      AdvanceTimeMilliseconds(kFrameIntervalMs);
      send_time_ms += kFrameIntervalMs;
    }
    SM_EXPECT_TRUE(observer_.updated);
    SM_EXPECT_GE(observer_.latest_bitrate, 400000u);
    const int kTimestampGroupLength = 15;
    for (int i = 0; i < 100; ++i) {
      for (int j = 0; j < kTimestampGroupLength; ++j) {
        IncomingFeedback(TimeInMilliseconds(), send_time_ms, 100);
        AdvanceTimeMilliseconds(kFrameIntervalMs / kTimestampGroupLength);
        send_time_ms += 1;
      }
      AdvanceTimeMilliseconds(10);
      send_time_ms += kFrameIntervalMs - kTimestampGroupLength;
    }
    SM_EXPECT_TRUE(observer_.updated);
    SM_EXPECT_LT(observer_.latest_bitrate, 400000u);
  }

  void TestWrappingHelper(int silence_time_s) {
    const int kFramerate = 100;
    const int kFrameIntervalMs = 1000 / kFramerate;
    int64_t send_time_ms = 0;
    for (size_t i = 0; i < 3000; ++i) {
      IncomingFeedback(TimeInMilliseconds(), send_time_ms, 1000);
      AdvanceTimeMilliseconds(kFrameIntervalMs);
      send_time_ms += kFrameIntervalMs;
    }
    DataRate bitrate_before = DataRate::Zero();
    LatestEstimate(&bitrate_before);
    AdvanceTimeMilliseconds(silence_time_s * 1000);
    send_time_ms += silence_time_s * 1000;
    for (size_t i = 0; i < 24; ++i) {
      IncomingFeedback(TimeInMilliseconds(), send_time_ms, 1000);
      AdvanceTimeMilliseconds(2 * kFrameIntervalMs);
      send_time_ms += kFrameIntervalMs;
    }
    DataRate bitrate_after = DataRate::Zero();
    LatestEstimate(&bitrate_after);
    SM_EXPECT_LT(bitrate_after, bitrate_before);
  }

  int64_t now_us_;
  Observer observer_;
  RobustThroughputEstimator acked_;
  ProbeBitrateEstimator probe_est_;
  std::unique_ptr<DelayBasedBwe> bwe_;
  StreamGenerator stream_generator_;
  int64_t arrival_time_offset_ms_ = 0;
  int64_t next_sequence_number_ = 0;
  bool first_update_ = true;
};

}  // namespace

SM_TEST(DelayBweLibwebrtc, ProbeDetection) {
  DelayBasedBweTest t;
  int64_t now_ms = t.TimeInMilliseconds();
  // First burst sent at 8 * 1000 / 10 = 800 kbps.
  for (int i = 0; i < kNumProbesCluster0; ++i) {
    t.AdvanceTimeMilliseconds(10);
    now_ms = t.TimeInMilliseconds();
    t.IncomingFeedback(now_ms, now_ms, 1000, kPacingInfo0);
  }
  SM_EXPECT_TRUE(t.observer_.updated);
  // Second burst sent at 8 * 1000 / 5 = 1600 kbps.
  for (int i = 0; i < kNumProbesCluster1; ++i) {
    t.AdvanceTimeMilliseconds(5);
    now_ms = t.TimeInMilliseconds();
    t.IncomingFeedback(now_ms, now_ms, 1000, kPacingInfo1);
  }
  SM_EXPECT_TRUE(t.observer_.updated);
  SM_EXPECT_GT(t.observer_.latest_bitrate, 1500000u);
}

SM_TEST(DelayBweLibwebrtc, ProbeDetectionNonPacedPackets) {
  DelayBasedBweTest t;
  int64_t now_ms = t.TimeInMilliseconds();
  // First burst sent at 8 * 1000 / 10 = 800 kbps, but with every other packet
  // not being paced which could mess things up.
  for (int i = 0; i < kNumProbesCluster0; ++i) {
    t.AdvanceTimeMilliseconds(5);
    now_ms = t.TimeInMilliseconds();
    t.IncomingFeedback(now_ms, now_ms, 1000, kPacingInfo0);
    // Non-paced packet, arriving 5 ms after.
    t.AdvanceTimeMilliseconds(5);
    t.IncomingFeedback(now_ms, now_ms, 100, PacedPacketInfo());
  }
  SM_EXPECT_TRUE(t.observer_.updated);
  SM_EXPECT_GT(t.observer_.latest_bitrate, 800000u);
}

SM_TEST(DelayBweLibwebrtc, ProbeDetectionFasterArrival) {
  DelayBasedBweTest t;
  // First burst sent at 8 * 1000 / 10 = 800 kbps. Arriving at 8 * 1000 / 5 = 1600 kbps.
  int64_t send_time_ms = 0;
  for (int i = 0; i < kNumProbesCluster0; ++i) {
    t.AdvanceTimeMilliseconds(1);
    send_time_ms += 10;
    int64_t now_ms = t.TimeInMilliseconds();
    t.IncomingFeedback(now_ms, send_time_ms, 1000, kPacingInfo0);
  }
  SM_EXPECT_FALSE(t.observer_.updated);
}

SM_TEST(DelayBweLibwebrtc, ProbeDetectionSlowerArrival) {
  DelayBasedBweTest t;
  // First burst sent at 8 * 1000 / 5 = 1600 kbps. Arriving at 8 * 1000 / 7 = 1142 kbps.
  // Since the receive rate is significantly below the send rate, we expect to
  // use 95% of the estimated capacity.
  int64_t send_time_ms = 0;
  for (int i = 0; i < kNumProbesCluster1; ++i) {
    t.AdvanceTimeMilliseconds(7);
    send_time_ms += 5;
    int64_t now_ms = t.TimeInMilliseconds();
    t.IncomingFeedback(now_ms, send_time_ms, 1000, kPacingInfo1);
  }
  SM_EXPECT_TRUE(t.observer_.updated);
  SM_EXPECT_NEAR(t.observer_.latest_bitrate, kTargetUtilizationFraction * 1140000u, 10000u);
}

SM_TEST(DelayBweLibwebrtc, ProbeDetectionSlowerArrivalHighBitrate) {
  DelayBasedBweTest t;
  // Burst sent at 8 * 1000 / 1 = 8000 kbps. Arriving at 8 * 1000 / 2 = 4000 kbps.
  int64_t send_time_ms = 0;
  for (int i = 0; i < kNumProbesCluster1; ++i) {
    t.AdvanceTimeMilliseconds(2);
    send_time_ms += 1;
    int64_t now_ms = t.TimeInMilliseconds();
    t.IncomingFeedback(now_ms, send_time_ms, 1000, kPacingInfo1);
  }
  SM_EXPECT_TRUE(t.observer_.updated);
  SM_EXPECT_NEAR(t.observer_.latest_bitrate, kTargetUtilizationFraction * 4000000u, 10000u);
}

SM_TEST(DelayBweLibwebrtc, InitialBehavior) {
  DelayBasedBweTest t;
  t.InitialBehaviorTestHelper(730000);
}

SM_TEST(DelayBweLibwebrtc, InitializeResult) {
  DelayBasedBwe::Result result;
  SM_EXPECT_FALSE(result.updated);
  SM_EXPECT_FALSE(result.probe);
  SM_EXPECT_FALSE(result.recovered_from_overuse);
  DelayBasedBwe::Config cfg;
  DelayBasedBwe bwe(cfg);
  SM_EXPECT_TRUE(bwe.usage() == BandwidthUsage::kNormal);  // delay_detector_state starts at kBwNormal.
}

SM_TEST(DelayBweLibwebrtc, RateIncreaseReordering) {
  DelayBasedBweTest t;
  t.RateIncreaseReorderingTestHelper(730000);
}

SM_TEST(DelayBweLibwebrtc, RateIncreaseRtpTimestamps) {
  DelayBasedBweTest t;
  t.RateIncreaseRtpTimestampsTestHelper(617);
}

SM_TEST(DelayBweLibwebrtc, CapacityDropOneStream) {
  DelayBasedBweTest t;
  t.CapacityDropTestHelper(1, 500, 0);
}

SM_TEST(DelayBweLibwebrtc, CapacityDropPosOffsetChange) {
  DelayBasedBweTest t;
  t.CapacityDropTestHelper(1, 867, 30000);
}

SM_TEST(DelayBweLibwebrtc, CapacityDropNegOffsetChange) {
  DelayBasedBweTest t;
  t.CapacityDropTestHelper(1, 933, -30000);
}

SM_TEST(DelayBweLibwebrtc, CapacityDropOneStreamWrap) {
  DelayBasedBweTest t;
  t.CapacityDropTestHelper(1, 533, 0);  // wrap_time_stamp is unused in libwebrtc's helper too.
}

SM_TEST(DelayBweLibwebrtc, TestTimestampGrouping) {
  DelayBasedBweTest t;
  t.TestTimestampGroupingTestHelper();
}

SM_TEST(DelayBweLibwebrtc, TestShortTimeoutAndWrap) {
  // A client leaves and rejoins after 35 s (abs send time wraps).
  DelayBasedBweTest t;
  t.TestWrappingHelper(35);
}

SM_TEST(DelayBweLibwebrtc, TestLongTimeoutAndWrap) {
  // A zero difference in abs send times due to the wrap, but a big difference in arrival time.
  DelayBasedBweTest t;
  t.TestWrappingHelper(10 * 64);
}

SM_TEST(DelayBweLibwebrtc, TestInitialOveruse) {
  DelayBasedBweTest t;
  const DataRate kStartBitrate = DataRate::KilobitsPerSec(300);
  const DataRate kInitialCapacity = DataRate::KilobitsPerSec(200);
  const int kFps = 90;  // High FPS to ensure that we send a lot of packets in a short time.
  t.stream_generator_.AddStream(new RtpStream(kFps, static_cast<int>(kStartBitrate.bps())));
  t.stream_generator_.set_capacity_bps(static_cast<int>(kInitialCapacity.bps()));
  // Needed to initialize the AimdRateControl.
  t.bwe_->SetStartBitrate(kStartBitrate);
  int64_t bitrate_bps = kStartBitrate.bps();
  bool seen_overuse = false;
  for (int i = 0; i < 40; ++i) {
    bool overuse = t.GenerateAndProcessFrame(static_cast<uint32_t>(bitrate_bps));
    if (overuse) {
      SM_EXPECT_TRUE(t.observer_.updated);
      SM_EXPECT_LE(static_cast<int64_t>(t.observer_.latest_bitrate), kInitialCapacity.bps());
      SM_EXPECT_GT(static_cast<double>(t.observer_.latest_bitrate), 0.8 * static_cast<double>(kInitialCapacity.bps()));
      bitrate_bps = t.observer_.latest_bitrate;
      seen_overuse = true;
      break;
    } else if (t.observer_.updated) {
      bitrate_bps = t.observer_.latest_bitrate;
      t.observer_.Reset();
    }
  }
  SM_EXPECT_TRUE(seen_overuse);
  SM_EXPECT_LE(static_cast<int64_t>(t.observer_.latest_bitrate), kInitialCapacity.bps());
  SM_EXPECT_GT(static_cast<double>(t.observer_.latest_bitrate), 0.8 * static_cast<double>(kInitialCapacity.bps()));
}

SM_TEST(DelayBweLibwebrtc, TestTimestampPrecisionHandling) {
  // Timestamps with higher than millisecond precision must not cause
  // false-positive overuse (webrtc:14023).
  // Send times(ms): 0.000, 9.725, 20.000, 29.725, ...; recv times(ms): 0.500, 10.000, 20.500, 30.000, ...
  // Send deltas 9.750, 10.250, ...; recv deltas 9.500, 10.500, ...: no delay builds up.
  DelayBasedBweTest t;
  uint32_t last_bitrate = t.observer_.latest_bitrate;
  for (int i = 0; i < 1000; ++i) {
    t.AdvanceTimeMicroseconds(500);
    t.IncomingFeedbackAt(t.Now(), t.Now() - TimeDelta::Micros(500), 1000, PacedPacketInfo());
    t.AdvanceTimeMicroseconds(9500);
    t.IncomingFeedbackAt(t.Now(), t.Now() - TimeDelta::Micros(250), 1000, PacedPacketInfo());
    t.AdvanceTimeMicroseconds(10000);
    // The bitrate should never decrease in this test.
    SM_EXPECT_LE(last_bitrate, t.observer_.latest_bitrate);
    last_bitrate = t.observer_.latest_bitrate;
  }
}
