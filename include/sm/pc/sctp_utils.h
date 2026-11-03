// DCEP (RFC 8832) DATA_CHANNEL_OPEN and DATA_CHANNEL_ACK, after libwebrtc M157's pc/sctp_utils.{h,cc}
// (compare/webrtc/reference/). C7 part 4 (#1070).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/pc/data_channel_interface.h"

namespace sm::pc {

// RFC 8831: stream ids are 0..65534; 65535 is reserved.
inline constexpr int kSpecMaxSctpSid = 65535;

// Wraps an SCTP stream id (pc/sctp_utils.h's StreamId).
class StreamId {
 public:
  StreamId() = default;
  explicit StreamId(uint16_t id) : id_(id) {}
  int stream_id_int() const { return id_; }
  bool operator==(const StreamId& o) const { return id_ == o.id_; }
  bool operator!=(const StreamId& o) const { return id_ != o.id_; }
  bool operator<(const StreamId& o) const { return id_ < o.id_; }

 private:
  uint16_t id_ = 0;
};

// True if the payload is a DATA_CHANNEL_OPEN message (type 0x03).
bool IsOpenMessage(std::span<const uint8_t> payload);
// Parses DATA_CHANNEL_OPEN into the label and config (ordered, maxRetransmits / maxRetransmitTime, priority,
// protocol). False if it is malformed.
bool ParseDataChannelOpenMessage(std::span<const uint8_t> payload, std::string* label, DataChannelInit* config);
// True if the payload is a DATA_CHANNEL_ACK message (type 0x02).
bool ParseDataChannelOpenAckMessage(std::span<const uint8_t> payload);
bool WriteDataChannelOpenMessage(std::string_view label, std::string_view protocol,
                                 std::optional<PriorityValue> priority, bool ordered,
                                 std::optional<int> max_retransmits, std::optional<int> max_retransmit_time,
                                 std::vector<uint8_t>* payload);
bool WriteDataChannelOpenMessage(std::string_view label, const DataChannelInit& config,
                                 std::vector<uint8_t>* payload);
void WriteDataChannelOpenAckMessage(std::vector<uint8_t>* payload);

}  // namespace sm::pc
