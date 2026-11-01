// libwebrtc M157 audio/nack_tracker_unittest.cc, every case (#971).
#include <algorithm>
#include <vector>

#include "sm/loss/audio_nack_tracker.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {

constexpr int kSampleRateHz = 16000;
constexpr uint32_t kTimestampIncrement = 480;  // 30 ms.
constexpr TimeDelta kShortRoundTripTime = TimeDelta::Zero();
constexpr size_t kDefaultNackListSize = 200;

bool IsNackListCorrect(const std::vector<uint16_t>& nack_list, const uint16_t* lost, size_t num_lost) {
  if (nack_list.size() != num_lost) return false;
  for (uint16_t s : nack_list) {
    if (std::find(lost, lost + num_lost, s) == lost + num_lost) return false;
  }
  return true;
}

}  // namespace

SM_TEST(AudioNackTracker, EmptyListWhenNoPacketLoss) {
  AudioNackTracker nack(kDefaultNackListSize);
  nack.UpdateSampleRate(kSampleRateHz);
  uint16_t seq_num = 1;
  uint32_t timestamp = 0;
  for (int n = 0; n < 100; n++) {
    nack.UpdateLastReceivedPacket(seq_num, timestamp);
    SM_EXPECT_TRUE(nack.GetNackList(kShortRoundTripTime).empty());
    seq_num++;
    timestamp += kTimestampIncrement;
    SM_EXPECT_TRUE(nack.GetNackList(kShortRoundTripTime).empty());
  }
}

SM_TEST(AudioNackTracker, LatePacketsMovedToNackThenNackListDoesNotChange) {
  const uint16_t kLost[] = {2, 3, 4, 5, 6, 7, 8, 9};
  constexpr int kNumAllLost = 8;
  for (int k = 0; k < 2; k++) {  // Without and with wrap-around.
    AudioNackTracker nack(kDefaultNackListSize);
    nack.UpdateSampleRate(kSampleRateHz);
    uint16_t lost[kNumAllLost];
    for (int n = 0; n < kNumAllLost; n++) lost[n] = static_cast<uint16_t>(kLost[n] + k * 65531);
    uint16_t seq_num = static_cast<uint16_t>(lost[0] - 1);
    uint32_t timestamp = 0;
    nack.UpdateLastReceivedPacket(seq_num, timestamp);
    SM_EXPECT_TRUE(nack.GetNackList(kShortRoundTripTime).empty());
    seq_num = static_cast<uint16_t>(lost[kNumAllLost - 1] + 1);
    timestamp += kTimestampIncrement * (kNumAllLost + 1);
    nack.UpdateLastReceivedPacket(seq_num, timestamp);
    SM_EXPECT_TRUE(IsNackListCorrect(nack.GetNackList(kShortRoundTripTime), lost, kNumAllLost));
    seq_num++;
    timestamp += kTimestampIncrement;
    for (int n = 0; n < 20; ++n) {
      nack.UpdateLastReceivedPacket(seq_num, timestamp);
      SM_EXPECT_TRUE(IsNackListCorrect(nack.GetNackList(kShortRoundTripTime), lost, kNumAllLost));
      seq_num++;
      timestamp += kTimestampIncrement;
    }
  }
}

SM_TEST(AudioNackTracker, ArrivedPacketsAreRemovedFromNackList) {
  const uint16_t kLost[] = {2, 3, 4, 5, 6, 7, 8, 9};
  constexpr int kNumAllLost = 8;
  for (int k = 0; k < 2; ++k) {
    AudioNackTracker nack(kDefaultNackListSize);
    nack.UpdateSampleRate(kSampleRateHz);
    uint16_t lost[kNumAllLost];
    for (int n = 0; n < kNumAllLost; ++n) lost[n] = static_cast<uint16_t>(kLost[n] + k * 65531);
    uint16_t seq_num = static_cast<uint16_t>(lost[0] - 1);
    uint32_t timestamp = 0;
    nack.UpdateLastReceivedPacket(seq_num, timestamp);
    std::vector<uint16_t> nack_list = nack.GetNackList(kShortRoundTripTime);
    SM_EXPECT_TRUE(nack_list.empty());
    size_t index_retransmitted = 0;
    uint32_t timestamp_retransmitted = timestamp + kTimestampIncrement;
    seq_num = static_cast<uint16_t>(lost[kNumAllLost - 1] + 1);
    timestamp += kTimestampIncrement * (kNumAllLost + 1);
    size_t num_lost = kNumAllLost;
    for (int n = 0; n < kNumAllLost; ++n) {
      if (n >= 1) num_lost--;
      nack.UpdateLastReceivedPacket(seq_num, timestamp);
      nack_list = nack.GetNackList(kShortRoundTripTime);
      SM_EXPECT_TRUE(IsNackListCorrect(nack_list, &lost[index_retransmitted], num_lost));
      seq_num++;
      timestamp += kTimestampIncrement;
      nack.UpdateLastReceivedPacket(lost[index_retransmitted], timestamp_retransmitted);  // The resend.
      index_retransmitted++;
      timestamp_retransmitted += kTimestampIncrement;
      nack_list = nack.GetNackList(kShortRoundTripTime);
      SM_EXPECT_TRUE(IsNackListCorrect(nack_list, &lost[index_retransmitted], num_lost - 1));
    }
    SM_ASSERT_TRUE(nack_list.empty());
  }
}

SM_TEST(AudioNackTracker, Reset) {
  AudioNackTracker nack(kDefaultNackListSize);
  nack.UpdateSampleRate(kSampleRateHz);
  uint16_t seq_num = 0;
  nack.UpdateLastReceivedPacket(seq_num, seq_num * kTimestampIncrement);
  seq_num++;
  nack.UpdateLastReceivedPacket(seq_num, seq_num * kTimestampIncrement);
  constexpr int kNumLostPackets = 10;
  seq_num += kNumLostPackets + 1;
  nack.UpdateLastReceivedPacket(seq_num, seq_num * kTimestampIncrement);
  SM_EXPECT_EQ(nack.GetNackList(kShortRoundTripTime).size(), static_cast<size_t>(kNumLostPackets));
  nack.Reset();
  SM_EXPECT_TRUE(nack.GetNackList(kShortRoundTripTime).empty());
}

SM_TEST(AudioNackTracker, ListSizeAppliedFromBeginning) {
  constexpr size_t kNackListSize = 10;
  for (int m = 0; m < 2; ++m) {
    const uint16_t seq_num_offset = (m == 0) ? 0 : 65525;  // Wrap-around for m == 1.
    AudioNackTracker nack(kNackListSize);
    nack.UpdateSampleRate(kSampleRateHz);
    uint16_t seq_num = seq_num_offset;
    uint32_t timestamp = 0x12345678;
    nack.UpdateLastReceivedPacket(seq_num, timestamp);
    const uint16_t num_lost_packets = kNackListSize + 5;
    seq_num = static_cast<uint16_t>(seq_num + num_lost_packets + 1);
    timestamp += (num_lost_packets + 1) * kTimestampIncrement;
    nack.UpdateLastReceivedPacket(seq_num, timestamp);
    SM_EXPECT_EQ(nack.GetNackList(kShortRoundTripTime).size(), kNackListSize);
  }
}

SM_TEST(AudioNackTracker, RoundTripTimeIsApplied) {
  AudioNackTracker nack(200);
  nack.UpdateSampleRate(kSampleRateHz);
  uint16_t seq_num = 0;
  uint32_t timestamp = 0x87654321;
  nack.UpdateLastReceivedPacket(seq_num, timestamp);
  constexpr uint16_t kNumLostPackets = 5;
  seq_num += (1 + kNumLostPackets);
  timestamp += (1 + kNumLostPackets) * kTimestampIncrement;
  nack.UpdateLastReceivedPacket(seq_num, timestamp);
  // Times since the packets (ms): 150, 120, 90, 60, 30 for 1-5; + 930 ms under 1 s for 4 and 5.
  const std::vector<uint16_t> nack_list = nack.GetNackList(TimeDelta::Millis(930));
  SM_ASSERT_EQ(nack_list.size(), 2u);
  SM_EXPECT_EQ(nack_list[0], 4);
  SM_EXPECT_EQ(nack_list[1], 5);
}

SM_TEST(AudioNackTracker, DoNotNackAfterDtx) {
  AudioNackTracker nack(200);
  nack.UpdateSampleRate(kSampleRateHz);
  const uint16_t seq_num = 0;
  const uint32_t timestamp = 0x87654321;
  nack.UpdateLastReceivedPacket(seq_num, timestamp);
  SM_EXPECT_TRUE(nack.GetNackList(std::nullopt).empty());
  constexpr int kDtxPeriod = 400;
  nack.UpdateLastReceivedPacket(seq_num + 2, timestamp + kDtxPeriod * kSampleRateHz / 1000);
  SM_EXPECT_TRUE(nack.GetNackList(std::nullopt).empty());
}

SM_TEST(AudioNackTracker, FixedDelayMode) {
  AudioNackTracker nack(200);
  nack.UpdateSampleRate(kSampleRateHz);
  const uint16_t seq_num = 0;
  const uint32_t timestamp = 0x87654321;
  nack.UpdateLastReceivedPacket(seq_num, timestamp);
  nack.UpdateLastReceivedPacket(seq_num + 2, timestamp + 2 * kTimestampIncrement);
  std::vector<uint16_t> l = nack.GetNackList(kShortRoundTripTime);
  SM_ASSERT_EQ(l.size(), 1u);
  SM_EXPECT_EQ(l[0], seq_num + 1);
  SM_EXPECT_TRUE(nack.GetNackList(TimeDelta::Seconds(1)).empty());  // RTT above the 1 s delay.
  nack.UpdateLastReceivedPacket(seq_num + 3, timestamp + 2 * kTimestampIncrement + kSampleRateHz);
  SM_EXPECT_TRUE(nack.GetNackList(kShortRoundTripTime).empty());  // Older than 1 s.
}

SM_TEST(AudioNackTracker, SampleRateChangeResets) {
  AudioNackTracker nack(200);
  nack.UpdateSampleRate(48000);
  nack.UpdateLastReceivedPacket(0, 0);
  nack.UpdateLastReceivedPacket(2, 1920);
  SM_EXPECT_EQ(nack.size(), 1u);
  nack.UpdateSampleRate(16000);
  SM_EXPECT_EQ(nack.size(), 0u);
}
