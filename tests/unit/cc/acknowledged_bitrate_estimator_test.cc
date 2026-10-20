// Ports of libwebrtc's goog_cc/acknowledged_bitrate_estimator_unittest.cc (M157,
// compare/webrtc/reference/) to AckedBitrateEstimatorV2 (#166). The libwebrtc
// tests replace BitrateEstimator with a gmock; here a recording subclass takes
// its place and the expected call sequences are checked in order.
#include <string>
#include <vector>

#include "sm/cc/aimd_rate_control.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr int64_t kFirstArrivalTimeMs = 10;
constexpr int64_t kFirstSendTimeMs = 10;
constexpr int64_t kSequenceNumber = 1;
constexpr int64_t kPayloadSize = 10;

struct Call {
  enum Kind { kUpdate, kExpectFastRateChange };
  Call(Kind k, Timestamp t, DataSize s, bool alr) : kind(k), at_time(t), size(s), in_alr(alr) {}
  Kind kind;
  Timestamp at_time;
  DataSize size;
  bool in_alr;
};

class RecordingBitrateEstimator : public BitrateEstimator {
 public:
  void Update(Timestamp at_time, DataSize data_size, bool in_alr) override {
    calls.emplace_back(Call::kUpdate, at_time, data_size, in_alr);
  }
  std::optional<DataRate> bitrate() const override { return returned; }
  void ExpectFastRateChange() override {
    calls.emplace_back(Call::kExpectFastRateChange, Timestamp::Millis(0), DataSize::Zero(), false);
  }
  std::vector<Call> calls;
  std::optional<DataRate> returned;
};

std::vector<PacketResult> CreateFeedbackVector() {
  std::vector<PacketResult> v(2);
  v[0].receive_time = Timestamp::Millis(kFirstArrivalTimeMs);
  v[0].send_time = Timestamp::Millis(kFirstSendTimeMs);
  v[0].seq = kSequenceNumber;
  v[0].size = DataSize::Bytes(kPayloadSize);
  v[1].receive_time = Timestamp::Millis(kFirstArrivalTimeMs + 10);
  v[1].send_time = Timestamp::Millis(kFirstSendTimeMs + 10);
  v[1].seq = kSequenceNumber;
  v[1].size = DataSize::Bytes(kPayloadSize + 10);
  return v;
}

}  // namespace

SM_TEST(AcknowledgedBitrateEstimator, UpdateBandwidth) {
  RecordingBitrateEstimator mock;
  AckedBitrateEstimatorV2 est(&mock);
  auto v = CreateFeedbackVector();
  est.OnPacketResults(v);
  SM_ASSERT_EQ(mock.calls.size(), 2u);
  for (size_t i = 0; i < 2; i++) {
    SM_EXPECT_TRUE(mock.calls[i].kind == Call::kUpdate);
    SM_EXPECT_TRUE(mock.calls[i].at_time == v[i].receive_time);
    SM_EXPECT_TRUE(mock.calls[i].size == v[i].size);
    SM_EXPECT_FALSE(mock.calls[i].in_alr);
  }
}

SM_TEST(AcknowledgedBitrateEstimator, ExpectFastRateChangeWhenLeftAlr) {
  RecordingBitrateEstimator mock;
  AckedBitrateEstimatorV2 est(&mock);
  auto v = CreateFeedbackVector();
  est.SetAlrEndedTime(Timestamp::Millis(kFirstArrivalTimeMs + 1));
  est.OnPacketResults(v);
  // Update(packet 0), ExpectFastRateChange (packet 1 was sent after the ALR
  // ended), Update(packet 1), in this order.
  SM_ASSERT_EQ(mock.calls.size(), 3u);
  SM_EXPECT_TRUE(mock.calls[0].kind == Call::kUpdate);
  SM_EXPECT_TRUE(mock.calls[0].at_time == v[0].receive_time);
  SM_EXPECT_TRUE(mock.calls[0].size == v[0].size);
  SM_EXPECT_TRUE(mock.calls[1].kind == Call::kExpectFastRateChange);
  SM_EXPECT_TRUE(mock.calls[2].kind == Call::kUpdate);
  SM_EXPECT_TRUE(mock.calls[2].at_time == v[1].receive_time);
  SM_EXPECT_TRUE(mock.calls[2].size == v[1].size);
  SM_EXPECT_FALSE(mock.calls[2].in_alr);
}

SM_TEST(AcknowledgedBitrateEstimator, ReturnBitrate) {
  RecordingBitrateEstimator mock;
  AckedBitrateEstimatorV2 est(&mock);
  mock.returned = DataRate::KilobitsPerSec(42);
  SM_ASSERT_TRUE(est.bitrate().has_value());
  SM_EXPECT_TRUE(*est.bitrate() == *mock.returned);
}

// Not in libwebrtc's file: the ALR flag set by SetAlr reaches Update (the
// in-ALR state marks the acked-rate estimator, program.md 5.6).
SM_TEST(AcknowledgedBitrateEstimator, PassesTheAlrFlagToTheEstimator) {
  RecordingBitrateEstimator mock;
  AckedBitrateEstimatorV2 est(&mock);
  est.SetAlr(true);
  est.OnPacketResults(CreateFeedbackVector());
  SM_ASSERT_EQ(mock.calls.size(), 2u);
  SM_EXPECT_TRUE(mock.calls[0].in_alr);
  SM_EXPECT_TRUE(mock.calls[1].in_alr);
}
