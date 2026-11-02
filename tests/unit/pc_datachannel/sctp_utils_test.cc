// Every case of libwebrtc M157's pc/sctp_utils_unittest.cc (compare/webrtc/reference/pc/sctp_utils_unittest.cc).
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "sm/pc/sctp_utils.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;

namespace {

struct Reader {
  const std::vector<uint8_t>& d;
  size_t pos = 0;
  bool U8(uint8_t* v) {
    if (pos + 1 > d.size()) return false;
    *v = d[pos++];
    return true;
  }
  bool U16(uint16_t* v) {
    if (pos + 2 > d.size()) return false;
    *v = static_cast<uint16_t>(d[pos] << 8 | d[pos + 1]);
    pos += 2;
    return true;
  }
  bool U32(uint32_t* v) {
    uint16_t hi, lo;
    if (!U16(&hi) || !U16(&lo)) return false;
    *v = uint32_t{hi} << 16 | lo;
    return true;
  }
  bool Str(std::string* s, size_t n) {
    if (pos + n > d.size()) return false;
    s->assign(d.begin() + static_cast<long>(pos), d.begin() + static_cast<long>(pos + n));
    pos += n;
    return true;
  }
};

// SctpUtilsTest::VerifyOpenMessageFormat.
void VerifyOpenMessageFormat(const std::vector<uint8_t>& packet, const std::string& label,
                             const DataChannelInit& config) {
  uint8_t message_type, channel_type;
  uint32_t reliability;
  uint16_t priority, label_length, protocol_length;
  Reader buffer{packet};
  SM_ASSERT_TRUE(buffer.U8(&message_type));
  SM_EXPECT_EQ(0x03, message_type);
  SM_ASSERT_TRUE(buffer.U8(&channel_type));
  if (config.ordered) {
    SM_EXPECT_EQ(config.maxRetransmits ? 0x01 : (config.maxRetransmitTime ? 0x02 : 0), channel_type);
  } else {
    SM_EXPECT_EQ(config.maxRetransmits ? 0x81 : (config.maxRetransmitTime ? 0x82 : 0x80), channel_type);
  }
  SM_ASSERT_TRUE(buffer.U16(&priority));
  if (config.priority) {
    SM_EXPECT_EQ(priority, config.priority->value());
  } else {
    SM_EXPECT_EQ(priority, PriorityValue(PriorityValue::kLow).value());
  }
  SM_ASSERT_TRUE(buffer.U32(&reliability));
  if (config.maxRetransmits || config.maxRetransmitTime) {
    SM_EXPECT_EQ(config.maxRetransmits ? *config.maxRetransmits : *config.maxRetransmitTime,
                 static_cast<int>(reliability));
  }
  SM_ASSERT_TRUE(buffer.U16(&label_length));
  SM_ASSERT_TRUE(buffer.U16(&protocol_length));
  SM_EXPECT_EQ(label.size(), size_t{label_length});
  SM_EXPECT_EQ(config.protocol.size(), size_t{protocol_length});
  std::string label_output, protocol_output;
  SM_ASSERT_TRUE(buffer.Str(&label_output, label_length));
  SM_EXPECT_EQ(label, label_output);
  SM_ASSERT_TRUE(buffer.Str(&protocol_output, protocol_length));
  SM_EXPECT_EQ(config.protocol, protocol_output);
}

}  // namespace

SM_TEST(PcSctpUtils, WriteParseOpenMessageWithOrderedReliable) {
  DataChannelInit config;
  std::string label = "abc";
  config.protocol = "y";
  std::vector<uint8_t> packet;
  SM_ASSERT_TRUE(WriteDataChannelOpenMessage(label, config, &packet));
  VerifyOpenMessageFormat(packet, label, config);
  std::string output_label;
  DataChannelInit output_config;
  SM_ASSERT_TRUE(ParseDataChannelOpenMessage(packet, &output_label, &output_config));
  SM_EXPECT_EQ(label, output_label);
  SM_EXPECT_EQ(config.protocol, output_config.protocol);
  SM_EXPECT_EQ(config.ordered, output_config.ordered);
  SM_EXPECT_TRUE(config.maxRetransmitTime == output_config.maxRetransmitTime);
  SM_EXPECT_TRUE(config.maxRetransmits == output_config.maxRetransmits);
}

SM_TEST(PcSctpUtils, WriteParseOpenMessageWithMaxRetransmitTime) {
  DataChannelInit config;
  std::string label = "abc";
  config.ordered = false;
  config.maxRetransmitTime = 10;
  config.protocol = "y";
  std::vector<uint8_t> packet;
  SM_ASSERT_TRUE(WriteDataChannelOpenMessage(label, config, &packet));
  VerifyOpenMessageFormat(packet, label, config);
  std::string output_label;
  DataChannelInit output_config;
  SM_ASSERT_TRUE(ParseDataChannelOpenMessage(packet, &output_label, &output_config));
  SM_EXPECT_EQ(label, output_label);
  SM_EXPECT_EQ(config.protocol, output_config.protocol);
  SM_EXPECT_EQ(config.ordered, output_config.ordered);
  SM_ASSERT_TRUE(output_config.maxRetransmitTime.has_value());
  SM_EXPECT_EQ(*config.maxRetransmitTime, *output_config.maxRetransmitTime);
  SM_EXPECT_FALSE(output_config.maxRetransmits.has_value());
}

SM_TEST(PcSctpUtils, WriteParseOpenMessageWithMaxRetransmits) {
  DataChannelInit config;
  std::string label = "abc";
  config.maxRetransmits = 10;
  config.protocol = "y";
  std::vector<uint8_t> packet;
  SM_ASSERT_TRUE(WriteDataChannelOpenMessage(label, config, &packet));
  VerifyOpenMessageFormat(packet, label, config);
  std::string output_label;
  DataChannelInit output_config;
  SM_ASSERT_TRUE(ParseDataChannelOpenMessage(packet, &output_label, &output_config));
  SM_EXPECT_EQ(label, output_label);
  SM_EXPECT_EQ(config.protocol, output_config.protocol);
  SM_EXPECT_EQ(config.ordered, output_config.ordered);
  SM_EXPECT_TRUE(config.maxRetransmits == output_config.maxRetransmits);
  SM_EXPECT_FALSE(output_config.maxRetransmitTime.has_value());
}

SM_TEST(PcSctpUtils, WriteParseOpenMessageWithPriority) {
  DataChannelInit config;
  std::string label = "abc";
  config.protocol = "y";
  config.priority = PriorityValue(PriorityValue::kVeryLow);
  std::vector<uint8_t> packet;
  SM_ASSERT_TRUE(WriteDataChannelOpenMessage(label, config, &packet));
  VerifyOpenMessageFormat(packet, label, config);
  std::string output_label;
  DataChannelInit output_config;
  SM_ASSERT_TRUE(ParseDataChannelOpenMessage(packet, &output_label, &output_config));
  SM_EXPECT_EQ(label, output_label);
  SM_ASSERT_TRUE(output_config.priority.has_value());
  SM_EXPECT_EQ(config.priority->value(), output_config.priority->value());
}

SM_TEST(PcSctpUtils, WriteParseAckMessage) {
  std::vector<uint8_t> packet;
  WriteDataChannelOpenAckMessage(&packet);
  uint8_t message_type;
  Reader buffer{packet};
  SM_ASSERT_TRUE(buffer.U8(&message_type));
  SM_EXPECT_EQ(0x02, message_type);
  SM_EXPECT_TRUE(ParseDataChannelOpenAckMessage(packet));
}

SM_TEST(PcSctpUtils, TestIsOpenMessage) {
  std::vector<uint8_t> open{0x03};
  SM_EXPECT_TRUE(IsOpenMessage(open));
  std::vector<uint8_t> openAck{0x02};
  SM_EXPECT_FALSE(IsOpenMessage(openAck));
  std::vector<uint8_t> invalid{0x01};
  SM_EXPECT_FALSE(IsOpenMessage(invalid));
  std::vector<uint8_t> empty;
  SM_EXPECT_FALSE(IsOpenMessage(empty));
}

// SctpSidTest.Basics.
SM_TEST(PcSctpUtils, SidBasics) {
  static_assert(sctp::kMinSctpSid == 0, "Min stream id should be 0");
  static_assert(sctp::kMaxSctpSid <= kSpecMaxSctpSid, "");
  static_assert(kSpecMaxSctpSid == std::numeric_limits<uint16_t>::max(),
                "Max legal sctp stream value should be 0xffff");
  SM_EXPECT_TRUE(true);
}

// Not in libwebrtc's tests: a truncated OPEN is rejected at every length.
SM_TEST(PcSctpUtils, TruncatedOpenRejected) {
  DataChannelInit config;
  config.protocol = "proto";
  std::vector<uint8_t> packet;
  SM_ASSERT_TRUE(WriteDataChannelOpenMessage("label", config, &packet));
  for (size_t n = 0; n < packet.size(); ++n) {
    std::vector<uint8_t> cut(packet.begin(), packet.begin() + static_cast<long>(n));
    std::string label;
    DataChannelInit out;
    SM_EXPECT_FALSE(ParseDataChannelOpenMessage(cut, &label, &out));
  }
}
