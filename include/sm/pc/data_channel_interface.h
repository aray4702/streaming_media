// The data channel API, after libwebrtc M157's api/data_channel_interface.{h,cc} and pc/data_channel_utils.h
// (compare/webrtc/reference/). C7 part 4 (#1070), built with SM_WITH_DATACHANNEL=ON.
//
// Differences from libwebrtc: no reference counting or proxies (channels are std::shared_ptr and every call runs
// on the caller's thread, as the rest of the library), DataBuffer holds a std::vector, and the deprecated
// `reliable` field of DataChannelInit is left out.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/sctp/sctp_transport_internal.h"

namespace sm::pc {

using sctp::PriorityValue;
using sctp::RtcError;
using sctp::RtcErrorType;

// RTCDataChannelInit (api/data_channel_interface.h).
struct DataChannelInit {
  // Deliver messages in order (true) or as they arrive (false).
  bool ordered = true;
  // maxPacketLifeTime in ms; partial reliability by time. Not together with maxRetransmits.
  std::optional<int> maxRetransmitTime;
  // Retransmissions before a message is abandoned; partial reliability by count.
  std::optional<int> maxRetransmits;
  // Sub-protocol name, sent in DATA_CHANNEL_OPEN.
  std::string protocol;
  // True: the application negotiated the channel out of band with `id`; no DCEP handshake.
  bool negotiated = false;
  // Stream id for a negotiated channel; -1 = allocate by DTLS role.
  int id = -1;
  std::optional<PriorityValue> priority;
};

// A message, text or binary.
struct DataBuffer {
  DataBuffer(std::vector<uint8_t> d, bool is_binary) : data(std::move(d)), binary(is_binary) {}
  explicit DataBuffer(std::string_view text) : data(text.begin(), text.end()), binary(false) {}
  size_t size() const { return data.size(); }
  std::vector<uint8_t> data;
  bool binary;
};

class DataChannelObserver {
 public:
  virtual ~DataChannelObserver() = default;
  // state() changed.
  virtual void OnStateChange() = 0;
  // A message arrived.
  virtual void OnMessage(const DataBuffer& buffer) = 0;
  // buffered_amount() dropped by `sent_data_size` bytes.
  virtual void OnBufferedAmountChange(uint64_t /*sent_data_size*/) {}
  // The transport's max message size is known.
  virtual void OnMaxMessageSize(int /*max_message_size*/) {}
};

enum class DataState { kConnecting, kOpen, kClosing, kClosed };

inline const char* DataStateString(DataState state) {
  switch (state) {
    case DataState::kConnecting:
      return "connecting";
    case DataState::kOpen:
      return "open";
    case DataState::kClosing:
      return "closing";
    case DataState::kClosed:
      return "closed";
  }
  return "";
}

// DataChannelInterface::MaxSendQueueSize(): 16 MiB.
inline constexpr uint64_t kDataChannelMaxSendQueueSize = 16 * 1024 * 1024;

// pc/data_channel_utils.h.
struct DataChannelStats {
  int internal_id = 0;
  int id = -1;
  std::string label;
  std::string protocol;
  DataState state = DataState::kConnecting;
  uint32_t messages_sent = 0;
  uint32_t messages_received = 0;
  uint64_t bytes_sent = 0;
  uint64_t bytes_received = 0;
};

}  // namespace sm::pc
