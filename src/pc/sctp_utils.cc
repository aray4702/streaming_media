// Ported from libwebrtc M157 pc/sctp_utils.cc (compare/webrtc/reference/pc/sctp_utils.cc).
#include "sm/pc/sctp_utils.h"

namespace sm::pc {
namespace {

constexpr uint8_t DATA_CHANNEL_OPEN_MESSAGE_TYPE = 0x03;
constexpr uint8_t DATA_CHANNEL_OPEN_ACK_MESSAGE_TYPE = 0x02;

enum DataChannelOpenMessageChannelType : uint8_t {
  DCOMCT_ORDERED_RELIABLE = 0x00,
  DCOMCT_ORDERED_PARTIAL_RTXS = 0x01,
  DCOMCT_ORDERED_PARTIAL_TIME = 0x02,
  DCOMCT_UNORDERED_RELIABLE = 0x80,
  DCOMCT_UNORDERED_PARTIAL_RTXS = 0x81,
  DCOMCT_UNORDERED_PARTIAL_TIME = 0x82,
};

// Network-order reader over a span (rtc_base/byte_buffer's ByteBufferReader, the parts DCEP uses).
class Reader {
 public:
  explicit Reader(std::span<const uint8_t> d) : d_(d) {}
  bool U8(uint8_t* v) {
    if (pos_ + 1 > d_.size()) return false;
    *v = d_[pos_++];
    return true;
  }
  bool U16(uint16_t* v) {
    if (pos_ + 2 > d_.size()) return false;
    *v = static_cast<uint16_t>((d_[pos_] << 8) | d_[pos_ + 1]);
    pos_ += 2;
    return true;
  }
  bool U32(uint32_t* v) {
    if (pos_ + 4 > d_.size()) return false;
    *v = (uint32_t{d_[pos_]} << 24) | (uint32_t{d_[pos_ + 1]} << 16) | (uint32_t{d_[pos_ + 2]} << 8) |
         uint32_t{d_[pos_ + 3]};
    pos_ += 4;
    return true;
  }
  bool Str(std::string* s, size_t n) {
    if (pos_ + n > d_.size()) return false;
    s->assign(reinterpret_cast<const char*>(d_.data() + pos_), n);
    pos_ += n;
    return true;
  }

 private:
  std::span<const uint8_t> d_;
  size_t pos_ = 0;
};

void Put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}

}  // namespace

bool IsOpenMessage(std::span<const uint8_t> payload) {
  if (payload.empty()) return false;
  return payload[0] == DATA_CHANNEL_OPEN_MESSAGE_TYPE;
}

bool ParseDataChannelOpenMessage(std::span<const uint8_t> payload, std::string* label, DataChannelInit* config) {
  Reader buffer(payload);
  uint8_t message_type;
  if (!buffer.U8(&message_type)) return false;
  if (message_type != DATA_CHANNEL_OPEN_MESSAGE_TYPE) return false;
  uint8_t channel_type;
  if (!buffer.U8(&channel_type)) return false;
  uint16_t priority;
  if (!buffer.U16(&priority)) return false;
  config->priority = PriorityValue(priority);
  uint32_t reliability_param;
  if (!buffer.U32(&reliability_param)) return false;
  uint16_t label_length;
  if (!buffer.U16(&label_length)) return false;
  uint16_t protocol_length;
  if (!buffer.U16(&protocol_length)) return false;
  if (!buffer.Str(label, label_length)) return false;
  if (!buffer.Str(&config->protocol, protocol_length)) return false;

  config->ordered = true;
  switch (channel_type) {
    case DCOMCT_UNORDERED_RELIABLE:
    case DCOMCT_UNORDERED_PARTIAL_RTXS:
    case DCOMCT_UNORDERED_PARTIAL_TIME:
      config->ordered = false;
      break;
    default:
      break;
  }
  config->maxRetransmits = std::nullopt;
  config->maxRetransmitTime = std::nullopt;
  switch (channel_type) {
    case DCOMCT_ORDERED_PARTIAL_RTXS:
    case DCOMCT_UNORDERED_PARTIAL_RTXS:
      // As libwebrtc: the uint32 is stored in an int (a value above INT_MAX wraps).
      config->maxRetransmits = static_cast<int>(reliability_param);
      break;
    case DCOMCT_ORDERED_PARTIAL_TIME:
    case DCOMCT_UNORDERED_PARTIAL_TIME:
      config->maxRetransmitTime = static_cast<int>(reliability_param);
      break;
    default:
      break;
  }
  return true;
}

bool ParseDataChannelOpenAckMessage(std::span<const uint8_t> payload) {
  if (payload.empty()) return false;
  return payload[0] == DATA_CHANNEL_OPEN_ACK_MESSAGE_TYPE;
}

bool WriteDataChannelOpenMessage(std::string_view label, const DataChannelInit& config,
                                 std::vector<uint8_t>* payload) {
  return WriteDataChannelOpenMessage(label, config.protocol, config.priority, config.ordered, config.maxRetransmits,
                                     config.maxRetransmitTime, payload);
}

bool WriteDataChannelOpenMessage(std::string_view label, std::string_view protocol,
                                 std::optional<PriorityValue> opt_priority, bool ordered,
                                 std::optional<int> max_retransmits, std::optional<int> max_retransmit_time,
                                 std::vector<uint8_t>* payload) {
  uint8_t channel_type = 0;
  uint32_t reliability_param = 0;
  const PriorityValue priority = opt_priority.value_or(PriorityValue(PriorityValue::kLow));
  if (ordered) {
    if (max_retransmits) {
      channel_type = DCOMCT_ORDERED_PARTIAL_RTXS;
      reliability_param = static_cast<uint32_t>(*max_retransmits);
    } else if (max_retransmit_time) {
      channel_type = DCOMCT_ORDERED_PARTIAL_TIME;
      reliability_param = static_cast<uint32_t>(*max_retransmit_time);
    } else {
      channel_type = DCOMCT_ORDERED_RELIABLE;
    }
  } else {
    if (max_retransmits) {
      channel_type = DCOMCT_UNORDERED_PARTIAL_RTXS;
      reliability_param = static_cast<uint32_t>(*max_retransmits);
    } else if (max_retransmit_time) {
      channel_type = DCOMCT_UNORDERED_PARTIAL_TIME;
      reliability_param = static_cast<uint32_t>(*max_retransmit_time);
    } else {
      channel_type = DCOMCT_UNORDERED_RELIABLE;
    }
  }
  std::vector<uint8_t>& b = *payload;
  b.clear();
  b.reserve(20 + label.size() + protocol.size());
  b.push_back(DATA_CHANNEL_OPEN_MESSAGE_TYPE);
  b.push_back(channel_type);
  Put16(b, priority.value());
  Put16(b, static_cast<uint16_t>(reliability_param >> 16));
  Put16(b, static_cast<uint16_t>(reliability_param));
  Put16(b, static_cast<uint16_t>(label.size()));
  Put16(b, static_cast<uint16_t>(protocol.size()));
  b.insert(b.end(), label.begin(), label.end());
  b.insert(b.end(), protocol.begin(), protocol.end());
  return true;
}

void WriteDataChannelOpenAckMessage(std::vector<uint8_t>* payload) {
  payload->assign(1, DATA_CHANNEL_OPEN_ACK_MESSAGE_TYPE);
}

}  // namespace sm::pc
