// 3.1 Receiver-side audio NACK: libwebrtc M157's NackTracker (audio/nack_tracker.{h,cc}, #971),
// which ChannelReceive::OnRtpPacket feeds with every received packet and asks for the NACK list
// right after (channel_receive.cc:731-739).
//
// A packet newer than the last received one puts every sequence number between them on the list,
// with its playout timestamp estimated from the samples per packet of the two received packets
// (none if that is 0 or more than 120 ms, kMaxPacketDuration: a DTX pause is not a loss). A
// packet that arrives leaves the list. The list holds at most `max_nack_list_size` numbers behind
// the last received one (ChannelReceive::SetNACKStatus gives the NACK history / 20 ms:
// webrtc_voice_engine.cc's kNackRtpHistoryMs of 5000 ms is 250). GetNackList returns the numbers
// still worth asking for: time since the packet (last received timestamp less its estimate) plus
// the RTT under 1 s (kMaxNackDelay), with 100 ms (kDefaultRtt) when the RTT is unknown.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::loss {

class AudioNackTracker {
 public:
  explicit AudioNackTracker(size_t max_nack_list_size);

  // Resets the state if the sample rate changes.
  void UpdateSampleRate(int sample_rate_hz);
  // Every received packet (NetEq insert order).
  void UpdateLastReceivedPacket(uint16_t sequence_number, uint32_t timestamp);
  // Missing packets whose time since the packet plus `round_trip_time` is under 1 s. Late packets
  // (older than the last received) are not added, only removed when they arrive.
  std::vector<uint16_t> GetNackList(std::optional<TimeDelta> round_trip_time) const;
  void Reset();

  size_t size() const { return nack_list_.size(); }

 private:
  struct OldestFirst {  // NackListCompare: oldest first in iteration.
    bool operator()(uint16_t older, uint16_t newer) const { return AheadOf<uint16_t>(newer, older); }
  };
  std::optional<int> GetSamplesPerPacket(uint16_t seq, uint32_t timestamp) const;
  void UpdateList(uint16_t seq, uint32_t timestamp);
  void LimitNackListSize();
  uint32_t EstimateTimestamp(uint16_t seq, int samples_per_packet) const;
  bool Nack(uint32_t timestamp, TimeDelta round_trip_time) const;

  const size_t max_nack_list_size_;
  std::optional<uint16_t> sequence_num_last_received_rtp_;
  std::optional<uint32_t> timestamp_last_received_rtp_;
  int sample_rate_hz_;
  std::map<uint16_t, uint32_t, OldestFirst> nack_list_;  // Sequence number -> estimated timestamp.
};

}  // namespace sm::loss
