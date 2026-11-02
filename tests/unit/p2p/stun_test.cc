// Port of libwebrtc M157 api/transport/stun_unittest.cc
// (compare/webrtc/reference/api/transport/stun_unittest.cc) for sm/p2p/stun.h
// (#986). The sample packets and RFC 5769 test vectors are copied from it
// unchanged. Every TEST_F there has a case here with the same name; where the
// port differs (death tests, histograms) the case says so.
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/stun.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm::p2p;

namespace {

// Sample STUN packets with various attributes, gathered by wiresharking
// pjproject's pjnath test programs (www.pjsip.org).
// clang-format off
constexpr uint8_t kStunMessageWithIPv6MappedAddress[] = {
  0x00, 0x01, 0x00, 0x18,  // message header
  0x21, 0x12, 0xa4, 0x42,  // transaction id
  0x29, 0x1f, 0xcd, 0x7c,
  0xba, 0x58, 0xab, 0xd7,
  0xf2, 0x41, 0x01, 0x00,
  0x00, 0x01, 0x00, 0x14,  // Address type (mapped), length
  0x00, 0x02, 0xb8, 0x81,  // family (IPv6), port
  0x24, 0x01, 0xfa, 0x00,  // an IPv6 address
  0x00, 0x04, 0x10, 0x00,
  0xbe, 0x30, 0x5b, 0xff,
  0xfe, 0xe5, 0x00, 0xc3
};

constexpr uint8_t kStunMessageWithIPv4MappedAddress[] = {
  0x01, 0x01, 0x00, 0x0c,   // binding response, length 12
  0x21, 0x12, 0xa4, 0x42,   // magic cookie
  0x29, 0x1f, 0xcd, 0x7c,   // transaction ID
  0xba, 0x58, 0xab, 0xd7,
  0xf2, 0x41, 0x01, 0x00,
  0x00, 0x01, 0x00, 0x08,  // Mapped, 8 byte length
  0x00, 0x01, 0x9d, 0xfc,  // AF_INET, unxor-ed port
  0xac, 0x17, 0x44, 0xe6   // IPv4 address
};

// Test XOR-mapped IP addresses:
constexpr uint8_t kStunMessageWithIPv6XorMappedAddress[] = {
  0x01, 0x01, 0x00, 0x18,  // message header (binding response)
  0x21, 0x12, 0xa4, 0x42,  // magic cookie (rfc5389)
  0xe3, 0xa9, 0x46, 0xe1,  // transaction ID
  0x7c, 0x00, 0xc2, 0x62,
  0x54, 0x08, 0x01, 0x00,
  0x00, 0x20, 0x00, 0x14,  // Address Type (XOR), length
  0x00, 0x02, 0xcb, 0x5b,  // family, XOR-ed port
  0x05, 0x13, 0x5e, 0x42,  // XOR-ed IPv6 address
  0xe3, 0xad, 0x56, 0xe1,
  0xc2, 0x30, 0x99, 0x9d,
  0xaa, 0xed, 0x01, 0xc3
};

constexpr uint8_t kStunMessageWithIPv4XorMappedAddress[] = {
  0x01, 0x01, 0x00, 0x0c,  // message header (binding response)
  0x21, 0x12, 0xa4, 0x42,  // magic cookie
  0x29, 0x1f, 0xcd, 0x7c,  // transaction ID
  0xba, 0x58, 0xab, 0xd7,
  0xf2, 0x41, 0x01, 0x00,
  0x00, 0x20, 0x00, 0x08,  // address type (xor), length
  0x00, 0x01, 0xfc, 0xb5,  // family (AF_INET), XOR-ed port
  0x8d, 0x05, 0xe0, 0xa4   // IPv4 address
};

// ByteString Attribute (username)
constexpr uint8_t kStunMessageWithByteStringAttribute[] = {
  0x00, 0x01, 0x00, 0x0c,
  0x21, 0x12, 0xa4, 0x42,
  0xe3, 0xa9, 0x46, 0xe1,
  0x7c, 0x00, 0xc2, 0x62,
  0x54, 0x08, 0x01, 0x00,
  0x00, 0x06, 0x00, 0x08,  // username attribute (length 8)
  0x61, 0x62, 0x63, 0x64,  // abcdefgh
  0x65, 0x66, 0x67, 0x68
};

// Message with an unknown but comprehensible optional attribute.
// Parsing should succeed despite this unknown attribute.
constexpr uint8_t kStunMessageWithUnknownAttribute[] = {
  0x00, 0x01, 0x00, 0x14,
  0x21, 0x12, 0xa4, 0x42,
  0xe3, 0xa9, 0x46, 0xe1,
  0x7c, 0x00, 0xc2, 0x62,
  0x54, 0x08, 0x01, 0x00,
  0x00, 0xaa, 0x00, 0x07,  // Unknown attribute, length 7 (needs padding!)
  0x61, 0x62, 0x63, 0x64,  // abcdefg + padding
  0x65, 0x66, 0x67, 0x00,
  0x00, 0x06, 0x00, 0x03,  // Followed by a known attribute we can
  0x61, 0x62, 0x63, 0x00   // check for (username of length 3)
};

// ByteString Attribute (username) with padding byte
constexpr uint8_t kStunMessageWithPaddedByteStringAttribute[] = {
  0x00, 0x01, 0x00, 0x08,
  0x21, 0x12, 0xa4, 0x42,
  0xe3, 0xa9, 0x46, 0xe1,
  0x7c, 0x00, 0xc2, 0x62,
  0x54, 0x08, 0x01, 0x00,
  0x00, 0x06, 0x00, 0x03,  // username attribute (length 3)
  0x61, 0x62, 0x63, 0xcc   // abc
};

// Message with an Unknown Attributes (uint16_t list) attribute.
constexpr uint8_t kStunMessageWithUInt16ListAttribute[] = {
  0x00, 0x01, 0x00, 0x0c,
  0x21, 0x12, 0xa4, 0x42,
  0xe3, 0xa9, 0x46, 0xe1,
  0x7c, 0x00, 0xc2, 0x62,
  0x54, 0x08, 0x01, 0x00,
  0x00, 0x0a, 0x00, 0x06,  // username attribute (length 6)
  0x00, 0x01, 0x10, 0x00,  // three attributes plus padding
  0xAB, 0xCU, 0xBE, 0xEF
};

// Error response message (unauthorized)
constexpr uint8_t kStunMessageWithErrorAttribute[] = {
  0x01, 0x11, 0x00, 0x14,
  0x21, 0x12, 0xa4, 0x42,
  0x29, 0x1f, 0xcd, 0x7c,
  0xba, 0x58, 0xab, 0xd7,
  0xf2, 0x41, 0x01, 0x00,
  0x00, 0x09, 0x00, 0x10,
  0x00, 0x00, 0x04, 0x01,
  0x55, 0x6e, 0x61, 0x75,
  0x74, 0x68, 0x6f, 0x72,
  0x69, 0x7a, 0x65, 0x64
};

// Sample messages with an invalid length Field

// The actual length in bytes of the invalid messages (including STUN header)

constexpr uint8_t kStunMessageWithZeroLength[] = {
  0x00, 0x01, 0x00, 0x00,  // length of 0 (last 2 bytes)
  0x21, 0x12, 0xA4, 0x42,  // magic cookie
  '0', '1', '2', '3',      // transaction id
  '4', '5', '6', '7',
  '8', '9', 'a', 'b',
  0x00, 0x20, 0x00, 0x08,  // xor mapped address
  0x00, 0x01, 0x21, 0x1F,
  0x21, 0x12, 0xA4, 0x53,
};

constexpr uint8_t kStunMessageWithExcessLength[] = {
  0x00, 0x01, 0x00, 0x55,  // length of 85
  0x21, 0x12, 0xA4, 0x42,  // magic cookie
  '0', '1', '2', '3',      // transaction id
  '4', '5', '6', '7',
  '8', '9', 'a', 'b',
  0x00, 0x20, 0x00, 0x08,  // xor mapped address
  0x00, 0x01, 0x21, 0x1F,
  0x21, 0x12, 0xA4, 0x53,
};

constexpr uint8_t kStunMessageWithSmallLength[] = {
  0x00, 0x01, 0x00, 0x03,  // length of 3
  0x21, 0x12, 0xA4, 0x42,  // magic cookie
  '0', '1', '2', '3',      // transaction id
  '4', '5', '6', '7',
  '8', '9', 'a', 'b',
  0x00, 0x20, 0x00, 0x08,  // xor mapped address
  0x00, 0x01, 0x21, 0x1F,
  0x21, 0x12, 0xA4, 0x53,
};

constexpr uint8_t kStunMessageWithBadHmacAtEnd[] = {
  0x00, 0x01, 0x00, 0x14,  // message length exactly 20
  0x21, 0x12, 0xA4, 0x42,  // magic cookie
  '0', '1', '2', '3',      // transaction ID
  '4', '5', '6', '7',
  '8', '9', 'a', 'b',
  0x00, 0x08, 0x00, 0x14,  // type=STUN_ATTR_MESSAGE_INTEGRITY, length=20
  '0', '0', '0', '0',      // We lied, there are only 16 bytes of HMAC.
  '0', '0', '0', '0',
  '0', '0', '0', '0',
  '0', '0', '0', '0',
};

// RTCP packet, for testing we correctly ignore non stun packet types.
// V=2, P=false, RC=0, Type=200, Len=6, Sender-SSRC=85, etc
constexpr uint8_t kRtcpPacket[] = {
  0x80, 0xc8, 0x00, 0x06, 0x00, 0x00, 0x00, 0x55,
  0xce, 0xa5, 0x18, 0x3a, 0x39, 0xcc, 0x7d, 0x09,
  0x23, 0xed, 0x19, 0x07, 0x00, 0x00, 0x01, 0x56,
  0x00, 0x03, 0x73, 0x50,
};
constexpr uint8_t kRfc5769SampleRequest[] = {
  0x00, 0x01, 0x00, 0x58,   //    Request type and message length
  0x21, 0x12, 0xa4, 0x42,   //    Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,   // }
  0xbc, 0x34, 0xd6, 0x86,   // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,   // }
  0x80, 0x22, 0x00, 0x10,   //    SOFTWARE attribute header
  0x53, 0x54, 0x55, 0x4e,   // }
  0x20, 0x74, 0x65, 0x73,   // }  User-agent...
  0x74, 0x20, 0x63, 0x6c,   // }  ...name
  0x69, 0x65, 0x6e, 0x74,   // }
  0x00, 0x24, 0x00, 0x04,   //    PRIORITY attribute header
  0x6e, 0x00, 0x01, 0xff,   //    ICE priority value
  0x80, 0x29, 0x00, 0x08,   //    ICE-CONTROLLED attribute header
  0x93, 0x2f, 0xf9, 0xb1,   // }  Pseudo-random tie breaker...
  0x51, 0x26, 0x3b, 0x36,   // }   ...for ICE control
  0x00, 0x06, 0x00, 0x09,   //    USERNAME attribute header
  0x65, 0x76, 0x74, 0x6a,   // }
  0x3a, 0x68, 0x36, 0x76,   // }  Username (9 bytes) and padding (3 bytes)
  0x59, 0x20, 0x20, 0x20,   // }
  0x00, 0x08, 0x00, 0x14,   //    MESSAGE-INTEGRITY attribute header
  0x9a, 0xea, 0xa7, 0x0c,   // }
  0xbf, 0xd8, 0xcb, 0x56,   // }
  0x78, 0x1e, 0xf2, 0xb5,   // }  HMAC-SHA1 fingerprint
  0xb2, 0xd3, 0xf2, 0x49,   // }
  0xc1, 0xb5, 0x71, 0xa2,   // }
  0x80, 0x28, 0x00, 0x04,   //    FINGERPRINT attribute header
  0xe5, 0x7a, 0x3b, 0xcf    //    CRC32 fingerprint
};

// 2.1.  Sample Request
constexpr uint8_t kSampleRequestMI32[] = {
  0x00, 0x01, 0x00, 0x48,   //    Request type and message length
  0x21, 0x12, 0xa4, 0x42,   //    Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,   // }
  0xbc, 0x34, 0xd6, 0x86,   // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,   // }
  0x80, 0x22, 0x00, 0x10,   //    SOFTWARE attribute header
  0x53, 0x54, 0x55, 0x4e,   // }
  0x20, 0x74, 0x65, 0x73,   // }  User-agent...
  0x74, 0x20, 0x63, 0x6c,   // }  ...name
  0x69, 0x65, 0x6e, 0x74,   // }
  0x00, 0x24, 0x00, 0x04,   //    PRIORITY attribute header
  0x6e, 0x00, 0x01, 0xff,   //    ICE priority value
  0x80, 0x29, 0x00, 0x08,   //    ICE-CONTROLLED attribute header
  0x93, 0x2f, 0xf9, 0xb1,   // }  Pseudo-random tie breaker...
  0x51, 0x26, 0x3b, 0x36,   // }   ...for ICE control
  0x00, 0x06, 0x00, 0x09,   //    USERNAME attribute header
  0x65, 0x76, 0x74, 0x6a,   // }
  0x3a, 0x68, 0x36, 0x76,   // }  Username (9 bytes) and padding (3 bytes)
  0x59, 0x20, 0x20, 0x20,   // }
  0xC0, 0x60, 0x00, 0x04,   //    MESSAGE-INTEGRITY-32 attribute header
  0x45, 0x45, 0xce, 0x7c,   // }  HMAC-SHA1 fingerprint (first 32 bit)
  0x80, 0x28, 0x00, 0x04,   //    FINGERPRINT attribute header
  0xe5, 0x7a, 0x3b, 0xcf    //    CRC32 fingerprint
};

// 2.2.  Sample IPv4 Response
constexpr uint8_t kRfc5769SampleResponse[] = {
  0x01, 0x01, 0x00, 0x3c,  //     Response type and message length
  0x21, 0x12, 0xa4, 0x42,  //     Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,  // }
  0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,  // }
  0x80, 0x22, 0x00, 0x0b,  //    SOFTWARE attribute header
  0x74, 0x65, 0x73, 0x74,  // }
  0x20, 0x76, 0x65, 0x63,  // }  UTF-8 server name
  0x74, 0x6f, 0x72, 0x20,  // }
  0x00, 0x20, 0x00, 0x08,  //    XOR-MAPPED-ADDRESS attribute header
  0x00, 0x01, 0xa1, 0x47,  //    Address family (IPv4) and xor'd mapped port
  0xe1, 0x12, 0xa6, 0x43,  //    Xor'd mapped IPv4 address
  0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
  0x2b, 0x91, 0xf5, 0x99,  // }
  0xfd, 0x9e, 0x90, 0xc3,  // }
  0x8c, 0x74, 0x89, 0xf9,  // }  HMAC-SHA1 fingerprint
  0x2a, 0xf9, 0xba, 0x53,  // }
  0xf0, 0x6b, 0xe7, 0xd7,  // }
  0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
  0xc0, 0x7d, 0x4c, 0x96   //    CRC32 fingerprint
};

// 2.3.  Sample IPv6 Response
constexpr uint8_t kRfc5769SampleResponseIPv6[] = {
  0x01, 0x01, 0x00, 0x48,  //    Response type and message length
  0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,  // }
  0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,  // }
  0x80, 0x22, 0x00, 0x0b,  //    SOFTWARE attribute header
  0x74, 0x65, 0x73, 0x74,  // }
  0x20, 0x76, 0x65, 0x63,  // }  UTF-8 server name
  0x74, 0x6f, 0x72, 0x20,  // }
  0x00, 0x20, 0x00, 0x14,  //    XOR-MAPPED-ADDRESS attribute header
  0x00, 0x02, 0xa1, 0x47,  //    Address family (IPv6) and xor'd mapped port.
  0x01, 0x13, 0xa9, 0xfa,  // }
  0xa5, 0xd3, 0xf1, 0x79,  // }  Xor'd mapped IPv6 address
  0xbc, 0x25, 0xf4, 0xb5,  // }
  0xbe, 0xd2, 0xb9, 0xd9,  // }
  0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
  0xa3, 0x82, 0x95, 0x4e,  // }
  0x4b, 0xe6, 0x7b, 0xf1,  // }
  0x17, 0x84, 0xc9, 0x7c,  // }  HMAC-SHA1 fingerprint
  0x82, 0x92, 0xc2, 0x75,  // }
  0xbf, 0xe3, 0xed, 0x41,  // }
  0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
  0xc8, 0xfb, 0x0b, 0x4c   //    CRC32 fingerprint
};

// 2.4.  Sample Request with Long-Term Authentication
constexpr uint8_t kRfc5769SampleRequestLongTermAuth[] = {
  0x00, 0x01, 0x00, 0x60,  //    Request type and message length
  0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
  0x78, 0xad, 0x34, 0x33,  // }
  0xc6, 0xad, 0x72, 0xc0,  // }  Transaction ID
  0x29, 0xda, 0x41, 0x2e,  // }
  0x00, 0x06, 0x00, 0x12,  //    USERNAME attribute header
  0xe3, 0x83, 0x9e, 0xe3,  // }
  0x83, 0x88, 0xe3, 0x83,  // }
  0xaa, 0xe3, 0x83, 0x83,  // }  Username value (18 bytes) and padding (2 bytes)
  0xe3, 0x82, 0xaf, 0xe3,  // }
  0x82, 0xb9, 0x00, 0x00,  // }
  0x00, 0x15, 0x00, 0x1c,  //    NONCE attribute header
  0x66, 0x2f, 0x2f, 0x34,  // }
  0x39, 0x39, 0x6b, 0x39,  // }
  0x35, 0x34, 0x64, 0x36,  // }
  0x4f, 0x4c, 0x33, 0x34,  // }  Nonce value
  0x6f, 0x4c, 0x39, 0x46,  // }
  0x53, 0x54, 0x76, 0x79,  // }
  0x36, 0x34, 0x73, 0x41,  // }
  0x00, 0x14, 0x00, 0x0b,  //    REALM attribute header
  0x65, 0x78, 0x61, 0x6d,  // }
  0x70, 0x6c, 0x65, 0x2e,  // }  Realm value (11 bytes) and padding (1 byte)
  0x6f, 0x72, 0x67, 0x00,  // }
  0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
  0xf6, 0x70, 0x24, 0x65,  // }
  0x6d, 0xd6, 0x4a, 0x3e,  // }
  0x02, 0xb8, 0xe0, 0x71,  // }  HMAC-SHA1 fingerprint
  0x2e, 0x85, 0xc9, 0xa2,  // }
  0x8c, 0xa8, 0x96, 0x66   // }
};

// Length parameter is changed to 0x38 from 0x58.
// AddMessageIntegrity will add MI information and update the length param
// accordingly.
constexpr uint8_t kRfc5769SampleRequestWithoutMI[] = {
  0x00, 0x01, 0x00, 0x38,  //    Request type and message length
  0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,  // }
  0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,  // }
  0x80, 0x22, 0x00, 0x10,  //    SOFTWARE attribute header
  0x53, 0x54, 0x55, 0x4e,  // }
  0x20, 0x74, 0x65, 0x73,  // }  User-agent...
  0x74, 0x20, 0x63, 0x6c,  // }  ...name
  0x69, 0x65, 0x6e, 0x74,  // }
  0x00, 0x24, 0x00, 0x04,  //    PRIORITY attribute header
  0x6e, 0x00, 0x01, 0xff,  //    ICE priority value
  0x80, 0x29, 0x00, 0x08,  //    ICE-CONTROLLED attribute header
  0x93, 0x2f, 0xf9, 0xb1,  // }  Pseudo-random tie breaker...
  0x51, 0x26, 0x3b, 0x36,  // }   ...for ICE control
  0x00, 0x06, 0x00, 0x09,  //    USERNAME attribute header
  0x65, 0x76, 0x74, 0x6a,  // }
  0x3a, 0x68, 0x36, 0x76,  // }  Username (9 bytes) and padding (3 bytes)
  0x59, 0x20, 0x20, 0x20   // }
};

// This HMAC differs from the RFC 5769 SampleRequest message. This differs
// because spec uses 0x20 for the padding where as our implementation uses 0.
constexpr uint8_t kCalculatedHmac1[] = {
  0x79, 0x07, 0xc2, 0xd2,  // }
  0xed, 0xbf, 0xea, 0x48,  // }
  0x0e, 0x4c, 0x76, 0xd8,  // }  HMAC-SHA1 fingerprint
  0x29, 0x62, 0xd5, 0xc3,  // }
  0x74, 0x2a, 0xf9, 0xe3   // }
};

// This truncated HMAC differs from kCalculatedHmac1
// above since the sum is computed including header
// and the header is different since the message is shorter
// than when MESSAGE-INTEGRITY is used.
constexpr uint8_t kCalculatedHmac1_32[] = {
  0xda, 0x39, 0xde, 0x5d,  // }
};

// Length parameter is changed to 0x1c from 0x3c.
// AddMessageIntegrity will add MI information and update the length param
// accordingly.
constexpr uint8_t kRfc5769SampleResponseWithoutMI[] = {
  0x01, 0x01, 0x00, 0x1c,  //    Response type and message length
  0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
  0xb7, 0xe7, 0xa7, 0x01,  // }
  0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
  0xfa, 0x87, 0xdf, 0xae,  // }
  0x80, 0x22, 0x00, 0x0b,  //    SOFTWARE attribute header
  0x74, 0x65, 0x73, 0x74,  // }
  0x20, 0x76, 0x65, 0x63,  // }  UTF-8 server name
  0x74, 0x6f, 0x72, 0x20,  // }
  0x00, 0x20, 0x00, 0x08,  //    XOR-MAPPED-ADDRESS attribute header
  0x00, 0x01, 0xa1, 0x47,  //    Address family (IPv4) and xor'd mapped port
  0xe1, 0x12, 0xa6, 0x43   //    Xor'd mapped IPv4 address
};

// This HMAC differs from the RFC 5769 SampleResponse message. This differs
// because spec uses 0x20 for the padding where as our implementation uses 0.
constexpr uint8_t kCalculatedHmac2[] = {
  0x5d, 0x6b, 0x58, 0xbe,  // }
  0xad, 0x94, 0xe0, 0x7e,  // }
  0xef, 0x0d, 0xfc, 0x12,  // }  HMAC-SHA1 fingerprint
  0x82, 0xa2, 0xbd, 0x08,  // }
  0x43, 0x14, 0x10, 0x28   // }
};

// This truncated HMAC differs from kCalculatedHmac2
// above since the sum is computed including header
// and the header is different since the message is shorter
// than when MESSAGE-INTEGRITY is used.
constexpr uint8_t kCalculatedHmac2_32[] = {
  0xe7, 0x5c, 0xd3, 0x16,  // }
};

// clang-format on

// A transaction ID without the 'magic cookie' portion

// clang-format on

// The actual length in bytes of the invalid-length messages (including the header).
constexpr int kRealLengthOfInvalidLengthTestCases = 32;

const std::span<const uint8_t> kZeroLenView(kStunMessageWithZeroLength);
const std::span<const uint8_t> kExcessLenView(kStunMessageWithExcessLength);
const std::span<const uint8_t> kSmallLenView(kStunMessageWithSmallLength);

// A parsed address (the test texts are valid).
SocketAddress Addr(std::string_view ip, uint16_t port) { return SocketAddress(*IpAddress::FromString(ip), port); }

// RFC 5769 test vectors.
constexpr std::array<uint8_t, 12> kRfc5769SampleMsgTransactionId = {0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34,
                                                                    0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae};
constexpr char kRfc5769SampleMsgClientSoftware[] = "STUN test client";
constexpr char kRfc5769SampleMsgServerSoftware[] = "test vector";
constexpr char kRfc5769SampleMsgUsername[] = "evtj:h6vY";
constexpr char kRfc5769SampleMsgPassword[] = "VOkJxbRl1RmTxUk/WvJxBt";
const SocketAddress kRfc5769SampleMsgMappedAddress = Addr("192.0.2.1", 32853);
const SocketAddress kRfc5769SampleMsgIPv6MappedAddress = Addr("2001:db8:1234:5678:11:2233:4455:6677", 32853);

constexpr std::array<uint8_t, 12> kRfc5769SampleMsgWithAuthTransactionId = {0x78, 0xad, 0x34, 0x33, 0xc6, 0xad,
                                                                            0x72, 0xc0, 0x29, 0xda, 0x41, 0x2e};
constexpr char kRfc5769SampleMsgWithAuthUsername[] =
    "\xe3\x83\x9e\xe3\x83\x88\xe3\x83\xaa\xe3\x83\x83\xe3\x82\xaf\xe3\x82\xb9";
constexpr char kRfc5769SampleMsgWithAuthPassword[] = "TheMatrIX";
constexpr char kRfc5769SampleMsgWithAuthNonce[] = "f//499k954d6OL34oL9FSTvy64sA";
constexpr char kRfc5769SampleMsgWithAuthRealm[] = "example.org";

// A transaction ID without the magic cookie; pjnat's test programs use it a lot.
constexpr std::array<uint8_t, 12> kTestTransactionId1 = {0x29, 0x1f, 0xcd, 0x7c, 0xba, 0x58,
                                                         0xab, 0xd7, 0xf2, 0x41, 0x01, 0x00};
// And this one sometimes.
constexpr std::array<uint8_t, 12> kTestTransactionId2 = {0xe3, 0xa9, 0x46, 0xe1, 0x7c, 0x00,
                                                         0xc2, 0x62, 0x54, 0x08, 0x01, 0x00};

const std::array<uint8_t, 16> kIPv6TestAddress1Bytes = {0x24, 0x01, 0xfa, 0x00, 0x00, 0x04, 0x10, 0x00,
                                            0xbe, 0x30, 0x5b, 0xff, 0xfe, 0xe5, 0x00, 0xc3};
const std::array<uint8_t, 16> kIPv6TestAddress2Bytes = {0x24, 0x01, 0xfa, 0x00, 0x00, 0x04, 0x10, 0x12,
                                            0x06, 0x0c, 0xce, 0xff, 0xfe, 0x1f, 0x61, 0xa4};
const IpAddress kIPv6TestAddress1 = IpAddress::V6(kIPv6TestAddress1Bytes);
const IpAddress kIPv6TestAddress2 = IpAddress::V6(kIPv6TestAddress2Bytes);
// libwebrtc's in_addr {0xe64417ac} on POSIX: bytes ac 17 44 e6.
const IpAddress kIPv4TestAddress1(0xac1744e6u);

constexpr char kTestUserName1[] = "abcdefgh";
constexpr char kTestUserName2[] = "abc";
constexpr char kTestErrorReason[] = "Unauthorized";
constexpr int kTestErrorClass = 4;
constexpr int kTestErrorNumber = 1;
constexpr int kTestErrorCode = 401;

constexpr int kTestMessagePort1 = 59977;
constexpr int kTestMessagePort2 = 47233;
constexpr int kTestMessagePort3 = 56743;
constexpr int kTestMessagePort4 = 40444;

std::string_view AsStringView(std::span<const uint8_t> b) {
  return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
}

std::span<const uint8_t> AsUint8Span(std::string_view s) {
  return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

uint16_t GetBE16(std::span<const uint8_t> d) { return static_cast<uint16_t>((d[0] << 8) | d[1]); }

// The StunTest fixture's helpers.
bool CheckStunHeader(const StunMessage& msg, StunMessageType expected_type, size_t expected_length) {
  return msg.type() == expected_type && msg.length() == expected_length;
}

bool CheckStunTransactionID(const StunMessage& msg, std::span<const uint8_t> expected) {
  return msg.transaction_id().size() == expected.size() &&
         (expected.size() == kStunTransactionIdLength + 4) == msg.IsLegacy() &&
         std::equal(expected.begin(), expected.end(), reinterpret_cast<const uint8_t*>(msg.transaction_id().data()));
}

bool CheckStunAddressAttribute(const StunAddressAttribute* addr, StunAddressFamily expected_family,
                               int expected_port, const IpAddress& expected_address) {
  return addr != nullptr && addr->family() == expected_family && addr->port() == expected_port &&
         (expected_family == STUN_ADDRESS_IPV4 || expected_family == STUN_ADDRESS_IPV6) &&
         addr->ipaddr() == expected_address;
}

// The size the message should report (data size minus the header), or 0 if it did not parse.
size_t ReadStunMessage(StunMessage* msg, std::span<const uint8_t> data) {
  ByteBufferReader buf(data);
  return msg->Read(&buf) ? data.size() - 20 : 0;
}

bool BytesEqual(const ByteBufferWriter& out, std::span<const uint8_t> expected) {
  return out.Length() == expected.size() && std::memcmp(out.Data(), expected.data(), expected.size()) == 0;
}

void CheckFailureToRead(const uint8_t* testcase, size_t length) {
  StunMessage msg;
  ByteBufferReader buf(std::span<const uint8_t>(testcase, length));
  SM_EXPECT_FALSE(msg.Read(&buf));
}

struct CryptoInstaller {
  CryptoInstaller() { sm::p2p::testing::InstallTestCrypto(); }
} g_install_crypto;

}  // namespace

// Test that the GetStun*Type and IsStun*Type methods work as expected.
SM_TEST(Stun, MessageTypes) {
  SM_EXPECT_EQ(int{STUN_BINDING_RESPONSE}, GetStunSuccessResponseType(STUN_BINDING_REQUEST));
  SM_EXPECT_EQ(int{STUN_BINDING_ERROR_RESPONSE}, GetStunErrorResponseType(STUN_BINDING_REQUEST));
  SM_EXPECT_EQ(-1, GetStunSuccessResponseType(STUN_BINDING_INDICATION));
  SM_EXPECT_EQ(-1, GetStunSuccessResponseType(STUN_BINDING_RESPONSE));
  SM_EXPECT_EQ(-1, GetStunSuccessResponseType(STUN_BINDING_ERROR_RESPONSE));
  SM_EXPECT_EQ(-1, GetStunErrorResponseType(STUN_BINDING_INDICATION));
  SM_EXPECT_EQ(-1, GetStunErrorResponseType(STUN_BINDING_RESPONSE));
  SM_EXPECT_EQ(-1, GetStunErrorResponseType(STUN_BINDING_ERROR_RESPONSE));

  int types[] = {STUN_BINDING_REQUEST, STUN_BINDING_INDICATION, STUN_BINDING_RESPONSE, STUN_BINDING_ERROR_RESPONSE};
  for (size_t i = 0; i < std::size(types); ++i) {
    SM_EXPECT_EQ(i == 0U, IsStunRequestType(types[i]));
    SM_EXPECT_EQ(i == 1U, IsStunIndicationType(types[i]));
    SM_EXPECT_EQ(i == 2U, IsStunSuccessResponseType(types[i]));
    SM_EXPECT_EQ(i == 3U, IsStunErrorResponseType(types[i]));
    SM_EXPECT_EQ(1, types[i] & 0xFEEF);
  }
}

SM_TEST(Stun, ReadMessageWithIPv4AddressAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv4MappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_MAPPED_ADDRESS);
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV4, kTestMessagePort4, kIPv4TestAddress1));
}

SM_TEST(Stun, ReadMessageWithIPv4XorAddressAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv4XorMappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV4, kTestMessagePort3, kIPv4TestAddress1));
}

SM_TEST(Stun, ReadMessageWithIPv6AddressAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv6MappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_MAPPED_ADDRESS);
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV6, kTestMessagePort2, kIPv6TestAddress1));
}

SM_TEST(Stun, ReadMessageWithInvalidAddressAttribute) {
  // As libwebrtc's case of this name (which reads the valid IPv6 packet).
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv6MappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_MAPPED_ADDRESS);
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV6, kTestMessagePort2, kIPv6TestAddress1));
  // And an address attribute with an unknown family fails the message.
  auto bad = std::to_array(kStunMessageWithIPv4MappedAddress);
  bad[25] = 0x03;
  StunMessage msg2;
  SM_EXPECT_EQ(ReadStunMessage(&msg2, bad), 0u);
}

SM_TEST(Stun, ReadMessageWithIPv6XorAddressAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv6XorMappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId2));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV6, kTestMessagePort1, kIPv6TestAddress1));
}

// The RFC 5389 fields of the RFC 5769 sample request.
SM_TEST(Stun, ReadRfc5769RequestMessage) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kRfc5769SampleRequest);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kRfc5769SampleMsgTransactionId));
  const StunByteStringAttribute* software = msg.GetByteString(STUN_ATTR_SOFTWARE);
  SM_ASSERT_TRUE(software != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgClientSoftware), software->string_view());
  const StunByteStringAttribute* username = msg.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgUsername), username->string_view());
  SM_ASSERT_TRUE(msg.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  const StunUInt32Attribute* fingerprint = msg.GetUInt32(STUN_ATTR_FINGERPRINT);
  SM_ASSERT_TRUE(fingerprint != nullptr);
  SM_EXPECT_EQ(0xe57a3bcfu, fingerprint->value());
}

SM_TEST(Stun, ReadRfc5769ResponseMessage) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kRfc5769SampleResponse);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kRfc5769SampleMsgTransactionId));
  const StunByteStringAttribute* software = msg.GetByteString(STUN_ATTR_SOFTWARE);
  SM_ASSERT_TRUE(software != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgServerSoftware), software->string_view());
  const StunAddressAttribute* mapped_address = msg.GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_ASSERT_TRUE(mapped_address != nullptr);
  SM_EXPECT_TRUE(kRfc5769SampleMsgMappedAddress == mapped_address->GetAddress());
  SM_ASSERT_TRUE(msg.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  SM_ASSERT_TRUE(msg.GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
}

SM_TEST(Stun, ReadRfc5769ResponseMessageIPv6) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kRfc5769SampleResponseIPv6);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kRfc5769SampleMsgTransactionId));
  const StunByteStringAttribute* software = msg.GetByteString(STUN_ATTR_SOFTWARE);
  SM_ASSERT_TRUE(software != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgServerSoftware), software->string_view());
  const StunAddressAttribute* mapped_address = msg.GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_ASSERT_TRUE(mapped_address != nullptr);
  SM_EXPECT_TRUE(kRfc5769SampleMsgIPv6MappedAddress == mapped_address->GetAddress());
  SM_ASSERT_TRUE(msg.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  SM_ASSERT_TRUE(msg.GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
}

SM_TEST(Stun, ReadRfc5769RequestMessageLongTermAuth) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kRfc5769SampleRequestLongTermAuth);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kRfc5769SampleMsgWithAuthTransactionId));
  const StunByteStringAttribute* username = msg.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgWithAuthUsername), username->string_view());
  const StunByteStringAttribute* nonce = msg.GetByteString(STUN_ATTR_NONCE);
  SM_ASSERT_TRUE(nonce != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgWithAuthNonce), nonce->string_view());
  const StunByteStringAttribute* realm = msg.GetByteString(STUN_ATTR_REALM);
  SM_ASSERT_TRUE(realm != nullptr);
  SM_EXPECT_EQ(std::string_view(kRfc5769SampleMsgWithAuthRealm), realm->string_view());
  // No fingerprint.
  SM_ASSERT_TRUE(msg.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  SM_ASSERT_TRUE(msg.GetUInt32(STUN_ATTR_FINGERPRINT) == nullptr);
}

// kStunMessageWithIPv4MappedAddress with something else where the magic cookie was (RFC 3489).
SM_TEST(Stun, ReadLegacyMessage) {
  auto rfc3489_packet = std::to_array(kStunMessageWithIPv4MappedAddress);
  std::memcpy(&rfc3489_packet[4], "ABCD", 4);
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, rfc3489_packet);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, std::span<const uint8_t>(rfc3489_packet).subspan(4, kStunTransactionIdLength + 4)));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_MAPPED_ADDRESS);
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV4, kTestMessagePort4, kIPv4TestAddress1));
}

SM_TEST(Stun, SetIPv6XorAddressAttributeOwner) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv6XorMappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId2));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_ASSERT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV6, kTestMessagePort1, kIPv6TestAddress1));

  // An owner with a different transaction ID.
  StunMessage msg2(STUN_INVALID_MESSAGE_TYPE, "ABCDABCDABCD");
  StunXorAddressAttribute addr2(STUN_ATTR_XOR_MAPPED_ADDRESS, 20, nullptr);
  addr2.SetIP(addr->ipaddr());
  addr2.SetPort(addr->port());
  addr2.SetOwner(&msg2);
  // The internal IP address doesn't change.
  SM_ASSERT_TRUE(addr2.ipaddr() == addr->ipaddr());

  ByteBufferWriter correct_buf;
  ByteBufferWriter wrong_buf;
  SM_EXPECT_TRUE(addr->Write(&correct_buf));
  SM_EXPECT_TRUE(addr2.Write(&wrong_buf));
  // But the buffers written differ.
  SM_ASSERT_TRUE(std::memcmp(correct_buf.Data(), wrong_buf.Data(), wrong_buf.Length()) != 0);
  // And reading a known good value gives the wrong address.
  ByteBufferReader read_buf(correct_buf);
  addr2.Read(&read_buf);
  SM_ASSERT_TRUE(addr->ipaddr() != addr2.ipaddr());
  addr2.SetIP(addr->ipaddr());
  addr2.SetPort(addr->port());
  // Writing with no owner fails and writes nothing.
  addr2.SetOwner(nullptr);
  SM_ASSERT_TRUE(addr2.ipaddr() == addr->ipaddr());
  wrong_buf.Clear();
  SM_EXPECT_FALSE(addr2.Write(&wrong_buf));
  SM_ASSERT_EQ(0U, wrong_buf.Length());
}

SM_TEST(Stun, SetIPv4XorAddressAttributeOwner) {
  // IPv4 XOR addresses use only the fixed magic cookie, so the owner doesn't matter.
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithIPv4XorMappedAddress);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  const StunAddressAttribute* addr = msg.GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  SM_ASSERT_TRUE(CheckStunAddressAttribute(addr, STUN_ADDRESS_IPV4, kTestMessagePort3, kIPv4TestAddress1));

  StunMessage msg2(STUN_INVALID_MESSAGE_TYPE, "ABCDABCDABCD");
  StunXorAddressAttribute addr2(STUN_ATTR_XOR_MAPPED_ADDRESS, 20, nullptr);
  addr2.SetIP(addr->ipaddr());
  addr2.SetPort(addr->port());
  addr2.SetOwner(&msg2);
  SM_ASSERT_TRUE(addr2.ipaddr() == addr->ipaddr());

  ByteBufferWriter correct_buf;
  ByteBufferWriter wrong_buf;
  SM_EXPECT_TRUE(addr->Write(&correct_buf));
  SM_EXPECT_TRUE(addr2.Write(&wrong_buf));
  // The same address data is written.
  SM_ASSERT_TRUE(std::memcmp(correct_buf.Data(), wrong_buf.Data(), wrong_buf.Length()) == 0);
  // And it un-XORs an address of a message with another transaction ID.
  ByteBufferReader read_buf(correct_buf);
  SM_EXPECT_TRUE(addr2.Read(&read_buf));
  SM_ASSERT_TRUE(addr->ipaddr() == addr2.ipaddr());
  // No owner is still an error.
  addr2.SetOwner(nullptr);
  SM_ASSERT_TRUE(addr2.ipaddr() == addr->ipaddr());
  wrong_buf.Clear();
  SM_EXPECT_FALSE(addr2.Write(&wrong_buf));
}

SM_TEST(Stun, CreateIPv6AddressAttribute) {
  auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr->SetAddress(SocketAddress(kIPv6TestAddress2, kTestMessagePort2));
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr.get(), STUN_ADDRESS_IPV6, kTestMessagePort2, kIPv6TestAddress2));
}

SM_TEST(Stun, CreateIPv4AddressAttribute) {
  // in_addr s_addr 0xBEB0B0BE: bytes be b0 b0 be.
  IpAddress test_ip(0xBEB0B0BEu);
  auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr->SetAddress(SocketAddress(test_ip, kTestMessagePort2));
  SM_EXPECT_TRUE(CheckStunAddressAttribute(addr.get(), STUN_ADDRESS_IPV4, kTestMessagePort2, test_ip));
}

// The parts of an address can be set in any order.
SM_TEST(Stun, CreateAddressInArbitraryOrder) {
  auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr->SetPort(kTestMessagePort1);
  addr->SetIP(kIPv4TestAddress1);
  SM_ASSERT_EQ(kTestMessagePort1, int{addr->port()});
  SM_ASSERT_TRUE(kIPv4TestAddress1 == addr->ipaddr());

  auto addr2 = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr2->SetIP(kIPv4TestAddress1);
  addr2->SetPort(kTestMessagePort2);
  SM_ASSERT_EQ(kTestMessagePort2, int{addr2->port()});
  SM_ASSERT_TRUE(kIPv4TestAddress1 == addr2->ipaddr());
}

SM_TEST(Stun, WriteMessageWithIPv6AddressAttribute) {
  size_t size = sizeof(kStunMessageWithIPv6MappedAddress);
  StunMessage msg(STUN_BINDING_REQUEST, AsStringView(kTestTransactionId1));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr->SetAddress(SocketAddress(kIPv6TestAddress1, kTestMessagePort2));
  msg.AddAttribute(std::move(addr));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size - 20));
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_EXPECT_TRUE(BytesEqual(out, kStunMessageWithIPv6MappedAddress));
}

SM_TEST(Stun, WriteMessageWithIPv4AddressAttribute) {
  size_t size = sizeof(kStunMessageWithIPv4MappedAddress);
  StunMessage msg(STUN_BINDING_RESPONSE, AsStringView(kTestTransactionId1));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr->SetAddress(SocketAddress(kIPv4TestAddress1, kTestMessagePort4));
  msg.AddAttribute(std::move(addr));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size - 20));
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_EXPECT_TRUE(BytesEqual(out, kStunMessageWithIPv4MappedAddress));
}

SM_TEST(Stun, WriteMessageWithIPv6XorAddressAttribute) {
  size_t size = sizeof(kStunMessageWithIPv6XorMappedAddress);
  StunMessage msg(STUN_BINDING_RESPONSE, AsStringView(kTestTransactionId2));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId2));
  auto addr = StunAttribute::CreateXorAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  addr->SetAddress(SocketAddress(kIPv6TestAddress1, kTestMessagePort1));
  msg.AddAttribute(std::move(addr));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size - 20));
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_EXPECT_TRUE(BytesEqual(out, kStunMessageWithIPv6XorMappedAddress));
}

SM_TEST(Stun, WriteMessageWithIPv4XoreAddressAttribute) {
  size_t size = sizeof(kStunMessageWithIPv4XorMappedAddress);
  StunMessage msg(STUN_BINDING_RESPONSE, AsStringView(kTestTransactionId1));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  auto addr = StunAttribute::CreateXorAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  addr->SetAddress(SocketAddress(kIPv4TestAddress1, kTestMessagePort3));
  msg.AddAttribute(std::move(addr));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_RESPONSE, size - 20));
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_EXPECT_TRUE(BytesEqual(out, kStunMessageWithIPv4XorMappedAddress));
}

SM_TEST(Stun, ReadByteStringAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithByteStringAttribute);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId2));
  const StunByteStringAttribute* username = msg.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_EQ(std::string_view(kTestUserName1), username->string_view());
}

SM_TEST(Stun, ReadPaddedByteStringAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithPaddedByteStringAttribute);
  SM_ASSERT_TRUE(size != 0U);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId2));
  const StunByteStringAttribute* username = msg.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_EQ(std::string_view(kTestUserName2), username->string_view());
}

SM_TEST(Stun, ReadErrorCodeAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithErrorAttribute);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_ERROR_RESPONSE, size));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  const StunErrorCodeAttribute* errorcode = msg.GetErrorCode();
  SM_ASSERT_TRUE(errorcode != nullptr);
  SM_EXPECT_EQ(kTestErrorClass, errorcode->eclass());
  SM_EXPECT_EQ(kTestErrorNumber, errorcode->number());
  SM_EXPECT_EQ(std::string(kTestErrorReason), errorcode->reason());
  SM_EXPECT_EQ(kTestErrorCode, errorcode->code());
  SM_EXPECT_EQ(kTestErrorCode, msg.GetErrorCodeValue());
}

// GetErrorCodeValue is STUN_ERROR_GLOBAL_FAILURE without an error code attribute.
SM_TEST(Stun, GetErrorCodeValueWithNoErrorAttribute) {
  StunMessage msg;
  ReadStunMessage(&msg, kStunMessageWithIPv6MappedAddress);
  SM_EXPECT_EQ(int{STUN_ERROR_GLOBAL_FAILURE}, msg.GetErrorCodeValue());
}

SM_TEST(Stun, ReadMessageWithAUInt16ListAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithUInt16ListAttribute);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  const StunUInt16ListAttribute* types = msg.GetUnknownAttributes();
  SM_ASSERT_TRUE(types != nullptr);
  SM_EXPECT_EQ(3U, types->Size());
  SM_EXPECT_EQ(0x1U, types->GetType(0));
  SM_EXPECT_EQ(0x1000U, types->GetType(1));
  SM_EXPECT_EQ(0xAB0CU, types->GetType(2));
}

SM_TEST(Stun, ReadMessageWithAnUnknownAttribute) {
  StunMessage msg;
  size_t size = ReadStunMessage(&msg, kStunMessageWithUnknownAttribute);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size));
  // Parsing succeeded and there is a USERNAME attribute.
  const StunByteStringAttribute* username = msg.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_EQ(std::string_view(kTestUserName2), username->string_view());
}

SM_TEST(Stun, WriteMessageWithAnErrorCodeAttribute) {
  size_t size = sizeof(kStunMessageWithErrorAttribute);
  StunMessage msg(STUN_BINDING_ERROR_RESPONSE, AsStringView(kTestTransactionId1));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId1));
  auto errorcode = StunAttribute::CreateErrorCode();
  errorcode->SetCode(kTestErrorCode);
  errorcode->SetReason(kTestErrorReason);
  msg.AddAttribute(std::move(errorcode));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_ERROR_RESPONSE, size - 20));
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  // No padding.
  SM_EXPECT_TRUE(BytesEqual(out, kStunMessageWithErrorAttribute));
}

SM_TEST(Stun, WriteMessageWithAUInt16ListAttribute) {
  size_t size = sizeof(kStunMessageWithUInt16ListAttribute);
  StunMessage msg(STUN_BINDING_REQUEST, AsStringView(kTestTransactionId2));
  SM_EXPECT_TRUE(CheckStunTransactionID(msg, kTestTransactionId2));
  auto list = StunAttribute::CreateUnknownAttributes();
  list->AddType(0x1U);
  list->AddType(0x1000U);
  list->AddType(0xAB0CU);
  msg.AddAttribute(std::move(list));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size - 20));
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_ASSERT_EQ(size, out.Length());
  // Everything up to the padding.
  SM_ASSERT_TRUE(std::memcmp(out.Data(), kStunMessageWithUInt16ListAttribute, size - 2) == 0);
}

// Messages with invalid lengths don't parse.
SM_TEST(Stun, FailToReadInvalidMessages) {
  CheckFailureToRead(kStunMessageWithZeroLength, kRealLengthOfInvalidLengthTestCases);
  CheckFailureToRead(kStunMessageWithSmallLength, kRealLengthOfInvalidLengthTestCases);
  CheckFailureToRead(kStunMessageWithExcessLength, kRealLengthOfInvalidLengthTestCases);
}

// A non-STUN message doesn't parse.
SM_TEST(Stun, FailToReadRtcpPacket) { CheckFailureToRead(kRtcpPacket, sizeof(kRtcpPacket)); }

// The validation against the RFC 5769 test messages.
SM_TEST(Stun, ValidateMessageIntegrity) {
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kRfc5769SampleRequest));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting("InvalidPassword", kRfc5769SampleRequest));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kRfc5769SampleResponse));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting("InvalidPassword", kRfc5769SampleResponse));
  SM_EXPECT_TRUE(
      StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kRfc5769SampleResponseIPv6));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting("InvalidPassword", kRfc5769SampleResponseIPv6));

  // The key for the long-term authentication HMAC.
  std::string key;
  SM_EXPECT_TRUE(ComputeStunCredentialHash(kRfc5769SampleMsgWithAuthUsername, kRfc5769SampleMsgWithAuthRealm,
                                           kRfc5769SampleMsgWithAuthPassword, &key));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(key, kRfc5769SampleRequestLongTermAuth));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting("InvalidPassword", kRfc5769SampleRequestLongTermAuth));

  // Edge cases.
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kStunMessageWithZeroLength));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kStunMessageWithExcessLength));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kStunMessageWithSmallLength));

  // Again, with the lengths matching what the headers claim.
  auto GetSubspan = [](std::span<const uint8_t> view) {
    size_t claimed_len = kStunHeaderSize + GetBE16(view.subspan(2, 2));
    return view.subspan(0, std::min(claimed_len, view.size()));
  };
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, GetSubspan(kZeroLenView)));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, GetSubspan(kExcessLenView)));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, GetSubspan(kSmallLenView)));

  // A too-short HMAC doesn't overflow the buffer.
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kStunMessageWithBadHmacAtEnd));

  // Flipping one bit anywhere before the end of the M-I attribute fails the check.
  auto buf = std::to_array(kRfc5769SampleRequest);
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] ^= 0x01;
    if (i > 0) buf[i - 1] ^= 0x01;
    SM_EXPECT_EQ(i >= buf.size() - 8, StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, buf));
  }
}

// MESSAGE-INTEGRITY generation. IceMessage, because the RFC 5769 messages carry ICE attributes.
SM_TEST(Stun, AddMessageIntegrity) {
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_TRUE(msg.AddMessageIntegrity(kRfc5769SampleMsgPassword));
  const StunByteStringAttribute* mi_attr = msg.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
  SM_ASSERT_TRUE(mi_attr != nullptr);
  SM_EXPECT_EQ(20U, mi_attr->length());
  SM_EXPECT_TRUE(std::memcmp(mi_attr->array_view().data(), kCalculatedHmac1, sizeof(kCalculatedHmac1)) == 0);
  ByteBufferWriter buf1;
  SM_EXPECT_TRUE(msg.Write(&buf1));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, buf1.DataView()));

  IceMessage msg2;
  ByteBufferReader buf2(kRfc5769SampleResponseWithoutMI);
  SM_EXPECT_TRUE(msg2.Read(&buf2));
  SM_EXPECT_TRUE(msg2.AddMessageIntegrity(kRfc5769SampleMsgPassword));
  const StunByteStringAttribute* mi_attr2 = msg2.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
  SM_ASSERT_TRUE(mi_attr2 != nullptr);
  SM_EXPECT_EQ(20U, mi_attr2->length());
  SM_EXPECT_TRUE(std::memcmp(mi_attr2->array_view().data(), kCalculatedHmac2, sizeof(kCalculatedHmac2)) == 0);
  ByteBufferWriter buf3;
  SM_EXPECT_TRUE(msg2.Write(&buf3));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, buf3.DataView()));
}

SM_TEST(Stun, ValidateMessageIntegrity32) {
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, kSampleRequestMI32));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrity32ForTesting("InvalidPassword", kSampleRequestMI32));

  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, kStunMessageWithZeroLength));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, kStunMessageWithExcessLength));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, kStunMessageWithSmallLength));

  auto GetSubspan = [](std::span<const uint8_t> view) {
    size_t claimed_len = kStunHeaderSize + GetBE16(view.subspan(2, 2));
    return view.subspan(0, std::min(claimed_len, view.size()));
  };
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, GetSubspan(kZeroLenView)));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, GetSubspan(kExcessLenView)));
  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, GetSubspan(kSmallLenView)));

  SM_EXPECT_FALSE(
      StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, kStunMessageWithBadHmacAtEnd));

  auto buf = std::to_array(kSampleRequestMI32);
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] ^= 0x01;
    if (i > 0) buf[i - 1] ^= 0x01;
    SM_EXPECT_EQ(i >= buf.size() - 8,
                 StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, buf));
  }
}

SM_TEST(Stun, AddMessageIntegrity32) {
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_TRUE(msg.AddMessageIntegrity32(kRfc5769SampleMsgPassword));
  const StunByteStringAttribute* mi_attr = msg.GetByteString(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32);
  SM_ASSERT_TRUE(mi_attr != nullptr);
  SM_EXPECT_EQ(4U, mi_attr->length());
  SM_EXPECT_TRUE(std::memcmp(mi_attr->array_view().data(), kCalculatedHmac1_32, sizeof(kCalculatedHmac1_32)) == 0);
  ByteBufferWriter buf1;
  SM_EXPECT_TRUE(msg.Write(&buf1));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, buf1.DataView()));

  IceMessage msg2;
  ByteBufferReader buf2(kRfc5769SampleResponseWithoutMI);
  SM_EXPECT_TRUE(msg2.Read(&buf2));
  SM_EXPECT_TRUE(msg2.AddMessageIntegrity32(kRfc5769SampleMsgPassword));
  const StunByteStringAttribute* mi_attr2 = msg2.GetByteString(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32);
  SM_ASSERT_TRUE(mi_attr2 != nullptr);
  SM_EXPECT_EQ(4U, mi_attr2->length());
  SM_EXPECT_TRUE(std::memcmp(mi_attr2->array_view().data(), kCalculatedHmac2_32, sizeof(kCalculatedHmac2_32)) == 0);
  ByteBufferWriter buf3;
  SM_EXPECT_TRUE(msg2.Write(&buf3));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrity32ForTesting(kRfc5769SampleMsgPassword, buf3.DataView()));
}

// Both MESSAGE-INTEGRITY-32 and MESSAGE-INTEGRITY in one message validate
// (not expected, but not forbidden).
SM_TEST(Stun, AddMessageIntegrity32AndMessageIntegrity) {
  IceMessage msg;
  auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  attr->CopyBytes(std::span<const uint8_t>(std::to_array<uint8_t>({'k', 'e', 's', 'o', '\0'})));
  msg.AddAttribute(std::move(attr));
  msg.AddMessageIntegrity32("password1");
  msg.AddMessageIntegrity("password2");

  ByteBufferWriter buf1;
  SM_EXPECT_TRUE(msg.Write(&buf1));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrity32ForTesting("password1", buf1.DataView()));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting("password2", buf1.DataView()));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrity32ForTesting("password2", buf1.DataView()));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting("password1", buf1.DataView()));
}

SM_TEST(Stun, ValidateFingerprint) {
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(kRfc5769SampleRequest));
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(kRfc5769SampleResponse));
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(kRfc5769SampleResponseIPv6));

  SM_EXPECT_FALSE(StunMessage::ValidateFingerprint(kStunMessageWithZeroLength));
  SM_EXPECT_FALSE(StunMessage::ValidateFingerprint(kStunMessageWithExcessLength));
  SM_EXPECT_FALSE(StunMessage::ValidateFingerprint(kStunMessageWithSmallLength));

  // Flipping one bit anywhere fails the fingerprint check.
  auto buf = std::to_array(kRfc5769SampleRequest);
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] ^= 0x01;
    if (i > 0) buf[i - 1] ^= 0x01;
    SM_EXPECT_FALSE(StunMessage::ValidateFingerprint(buf));
  }
  // All back to normal: the check passes again.
  buf[buf.size() - 1] ^= 0x01;
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(buf));
}

SM_TEST(Stun, AddFingerprint) {
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_TRUE(msg.AddFingerprint());
  ByteBufferWriter buf1;
  SM_EXPECT_TRUE(msg.Write(&buf1));
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(buf1.DataView()));
}

std::string_view ByteStringOf(const StunAttribute* a) {
  return static_cast<const StunByteStringAttribute*>(a)->string_view();
}

SM_TEST(Stun, RemoveAttribute) {
  StunMessage msg;
  // Removing something that doesn't exist returns nullptr.
  SM_EXPECT_TRUE(msg.RemoveAttribute(STUN_ATTR_USERNAME) == nullptr);
  {
    auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
    attr->CopyBytes(std::string_view("kes"));
    msg.AddAttribute(std::move(attr));
  }
  size_t len = msg.length();
  {
    auto attr = msg.RemoveAttribute(STUN_ATTR_USERNAME);
    SM_ASSERT_TRUE(attr != nullptr);
    SM_EXPECT_EQ(attr->type(), int{STUN_ATTR_USERNAME});
    SM_EXPECT_EQ(std::string_view("kes"), ByteStringOf(attr.get()));
    SM_EXPECT_LT(msg.length(), len);
  }
  // The same attribute type twice.
  {
    auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
    attr->CopyBytes(std::string_view("kes"));
    msg.AddAttribute(std::move(attr));
  }
  {
    auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
    attr->CopyBytes(std::string_view("kenta"));
    msg.AddAttribute(std::move(attr));
  }
  // Remove takes the last one added.
  {
    auto attr = msg.RemoveAttribute(STUN_ATTR_USERNAME);
    SM_ASSERT_TRUE(attr != nullptr);
    SM_EXPECT_EQ(std::string_view("kenta"), ByteStringOf(attr.get()));
  }
  {
    auto attr = msg.RemoveAttribute(STUN_ATTR_USERNAME);
    SM_ASSERT_TRUE(attr != nullptr);
    SM_EXPECT_EQ(std::string_view("kes"), ByteStringOf(attr.get()));
  }
  SM_EXPECT_TRUE(msg.RemoveAttribute(STUN_ATTR_USERNAME) == nullptr);
}

SM_TEST(Stun, ClearAttributes) {
  StunMessage msg;
  auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  attr->CopyBytes(std::string_view("kes"));
  msg.AddAttribute(std::move(attr));
  size_t len = msg.length();
  msg.ClearAttributes();
  SM_EXPECT_EQ(msg.length(), len - /* 3 + 1 byte padding + header */ 8);
  SM_EXPECT_TRUE(msg.GetByteString(STUN_ATTR_USERNAME) == nullptr);
}

SM_TEST(Stun, CopyAttribute) {
  ByteBufferWriter buf;
  ByteBufferWriter* buffer_ptrs[] = {&buf, nullptr};
  // With and without a supplied ByteBufferWriter.
  for (auto buffer_ptr : buffer_ptrs) {
    {
      auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
      attr->CopyBytes(std::string_view("kes"));
      auto copy = CopyStunAttribute(*attr, buffer_ptr);
      SM_ASSERT_TRUE(copy != nullptr);
      SM_ASSERT_EQ(copy->value_type(), STUN_VALUE_BYTE_STRING);
      SM_EXPECT_EQ(std::string_view("kes"), ByteStringOf(copy.get()));
    }
    {
      auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
      addr->SetAddress(SocketAddress(kIPv6TestAddress2, kTestMessagePort2));
      SM_EXPECT_TRUE(CheckStunAddressAttribute(addr.get(), STUN_ADDRESS_IPV6, kTestMessagePort2, kIPv6TestAddress2));
      auto copy = CopyStunAttribute(*addr, buffer_ptr);
      SM_ASSERT_TRUE(copy != nullptr);
      SM_ASSERT_EQ(copy->value_type(), STUN_VALUE_ADDRESS);
      SM_EXPECT_TRUE(CheckStunAddressAttribute(static_cast<StunAddressAttribute*>(copy.get()), STUN_ADDRESS_IPV6,
                                               kTestMessagePort2, kIPv6TestAddress2));
    }
    {
      auto addr = StunAttribute::CreateAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
      addr->SetAddress(SocketAddress(kIPv6TestAddress2, kTestMessagePort2));
      SM_EXPECT_TRUE(CheckStunAddressAttribute(addr.get(), STUN_ADDRESS_IPV6, kTestMessagePort2, kIPv6TestAddress2));
      auto copy = CopyStunAttribute(*addr, buffer_ptr);
      SM_ASSERT_TRUE(copy != nullptr);
      SM_ASSERT_EQ(copy->value_type(), STUN_VALUE_ADDRESS);
      SM_EXPECT_TRUE(CheckStunAddressAttribute(static_cast<StunAddressAttribute*>(copy.get()), STUN_ADDRESS_IPV6,
                                               kTestMessagePort2, kIPv6TestAddress2));
    }
  }
}

void AddCloneTestAttributes(StunMessage* msg) {
  auto errorcode = StunAttribute::CreateErrorCode();
  errorcode->SetCode(kTestErrorCode);
  errorcode->SetReason(kTestErrorReason);
  msg->AddAttribute(std::move(errorcode));
  auto bytes2 = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  bytes2->CopyBytes(std::string_view("abcdefghijkl"));
  msg->AddAttribute(std::move(bytes2));
  auto uval2 = StunAttribute::CreateUInt32(STUN_ATTR_RETRANSMIT_COUNT);
  uval2->SetValue(11);
  msg->AddAttribute(std::move(uval2));
  auto addr = StunAttribute::CreateAddress(STUN_ATTR_MAPPED_ADDRESS);
  addr->SetIP(kIPv6TestAddress1);
  addr->SetPort(kTestMessagePort1);
  msg->AddAttribute(std::move(addr));
}

SM_TEST(Stun, Clone) {
  IceMessage msg(0, "0123456789ab");
  AddCloneTestAttributes(&msg);
  auto copy = msg.Clone();
  SM_ASSERT_TRUE(copy != nullptr);
  ByteBufferWriter out1;
  SM_EXPECT_TRUE(msg.Write(&out1));
  ByteBufferWriter out2;
  SM_EXPECT_TRUE(copy->Write(&out2));
  SM_EXPECT_TRUE(BytesEqual(out1, out2.DataView()));
}

SM_TEST(Stun, EqualAttributes) {
  IceMessage msg;
  AddCloneTestAttributes(&msg);
  auto copy = msg.Clone();
  SM_ASSERT_TRUE(copy != nullptr);
  auto all = [](int) { return true; };
  auto not_nonce = [](int type) { return type != STUN_ATTR_NONCE; };
  SM_EXPECT_TRUE(copy->EqualAttributes(&msg, all));
  {
    auto attr = StunAttribute::CreateByteString(STUN_ATTR_NONCE);
    attr->CopyBytes(std::string_view("keso"));
    msg.AddAttribute(std::move(attr));
    SM_EXPECT_FALSE(copy->EqualAttributes(&msg, all));
    SM_EXPECT_TRUE(copy->EqualAttributes(&msg, not_nonce));
  }
  {
    auto attr = StunAttribute::CreateByteString(STUN_ATTR_NONCE);
    attr->CopyBytes(std::string_view("keso"));
    copy->AddAttribute(std::move(attr));
    SM_EXPECT_TRUE(copy->EqualAttributes(&msg, all));
  }
  {
    copy->RemoveAttribute(STUN_ATTR_NONCE);
    auto attr = StunAttribute::CreateByteString(STUN_ATTR_NONCE);
    attr->CopyBytes(std::string_view("kent"));
    copy->AddAttribute(std::move(attr));
    SM_EXPECT_FALSE(copy->EqualAttributes(&msg, all));
    SM_EXPECT_TRUE(copy->EqualAttributes(&msg, not_nonce));
  }
  {
    msg.RemoveAttribute(STUN_ATTR_NONCE);
    SM_EXPECT_FALSE(copy->EqualAttributes(&msg, all));
    SM_EXPECT_TRUE(copy->EqualAttributes(&msg, not_nonce));
  }
}

SM_TEST(Stun, ReduceTransactionIdIsHostOrderIndependent) {
  const std::string transaction_id = "abcdefghijkl";
  StunMessage message(0, transaction_id);
  SM_EXPECT_EQ(message.reduced_transaction_id(), 1835954016u);
}

SM_TEST(Stun, GoogMiscInfo) {
  StunMessage msg(STUN_BINDING_REQUEST, "ABCDEFGHIJKL");
  const size_t size = /* msg header */ 20 + /* attr header */ 4 + /* 3 * 2 rounded to multiple of 4 */ 8;
  auto list = StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
  list->AddTypeAtIndex(0, 0x1U);
  list->AddTypeAtIndex(3, 0x1000U);
  list->AddTypeAtIndex(2, 0xAB0CU);
  msg.AddAttribute(std::move(list));
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, size - 20));

  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_ASSERT_EQ(size, out.Length());

  size_t read_size = ReadStunMessage(&msg, out.DataView());
  SM_ASSERT_EQ(read_size + 20, size);
  SM_EXPECT_TRUE(CheckStunHeader(msg, STUN_BINDING_REQUEST, read_size));
  const StunUInt16ListAttribute* types = msg.GetUInt16List(STUN_ATTR_GOOG_MISC_INFO);
  SM_ASSERT_TRUE(types != nullptr);
  SM_EXPECT_EQ(4U, types->Size());
  SM_EXPECT_EQ(0x1U, types->GetType(0));
  SM_EXPECT_EQ(0x0U, types->GetType(1));
  SM_EXPECT_EQ(0x1000U, types->GetType(3));
  SM_EXPECT_EQ(0xAB0CU, types->GetType(2));
}

SM_TEST(Stun, IsStunMethod) {
  int methods[] = {STUN_BINDING_REQUEST};
  SM_EXPECT_TRUE(StunMessage::IsStunMethod(methods, kRfc5769SampleRequest));
  int other[] = {STUN_BINDING_RESPONSE};
  SM_EXPECT_FALSE(StunMessage::IsStunMethod(other, kRfc5769SampleRequest));
}

SM_TEST(Stun, SizeRestrictionOnAttributes) {
  StunMessage msg(STUN_BINDING_REQUEST, "ABCDEFGHIJKL");
  auto long_username = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  std::string long_string(509, 'x');
  long_username->CopyBytes(long_string);
  msg.AddAttribute(std::move(long_username));
  ByteBufferWriter out;
  SM_ASSERT_FALSE(msg.Write(&out));
}

// libwebrtc's case also checks the WebRTC.Stun.Integrity.Request histogram,
// which the port does not keep (stun.h); the statuses are the same.
SM_TEST(Stun, ValidateMessageIntegrityWithParser) {
  StunMessage message;
  ByteBufferReader reader(kRfc5769SampleRequest);
  SM_EXPECT_TRUE(message.Read(&reader));
  SM_EXPECT_TRUE(message.ValidateMessageIntegrity(kRfc5769SampleMsgPassword) ==
                 StunMessage::IntegrityStatus::kIntegrityOk);
  SM_EXPECT_TRUE(message.RevalidateMessageIntegrity("Invalid password") ==
                 StunMessage::IntegrityStatus::kIntegrityBad);
  // A message without integrity.
  StunMessage plain;
  ByteBufferReader reader2(kStunMessageWithByteStringAttribute);
  SM_EXPECT_TRUE(plain.Read(&reader2));
  SM_EXPECT_TRUE(plain.ValidateMessageIntegrity("x") == StunMessage::IntegrityStatus::kNoIntegrity);
}

SM_TEST(Stun, ByteStringAsVectorOfUint32) {
  StunMessage message;
  ByteBufferReader reader(kRfc5769SampleRequest);
  SM_EXPECT_TRUE(message.Read(&reader));
  // USERNAME's length is not a multiple of 4.
  const StunByteStringAttribute* username = message.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_FALSE(username->GetUInt32Vector().has_value());
  // MESSAGE-INTEGRITY is 20 bytes.
  const StunByteStringAttribute* integrity = message.GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
  SM_ASSERT_TRUE(integrity != nullptr);
  std::optional<std::vector<uint32_t>> integrity_vector = integrity->GetUInt32Vector();
  SM_ASSERT_TRUE(integrity_vector.has_value());
  // From the RFC 5769 sample request.
  std::vector<uint32_t> expected = {0x9aeaa70c, 0xbfd8cb56, 0x781ef2b5, 0xb2d3f249, 0xc1b571a2};
  SM_EXPECT_TRUE(*integrity_vector == expected);
}

SM_TEST(Stun, RemovingSigningAttributesAllowsAddingNewAttributes) {
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_TRUE(msg.AddMessageIntegrity(kRfc5769SampleMsgPassword));
  SM_ASSERT_TRUE(msg.IntegrityOk());
  // Removing MESSAGE-INTEGRITY makes the message unsigned.
  auto removed_attr = msg.RemoveAttribute(STUN_ATTR_MESSAGE_INTEGRITY);
  SM_ASSERT_TRUE(removed_attr != nullptr);
  SM_EXPECT_FALSE(msg.IntegrityOk());
  // Now removing and adding attributes works.
  msg.RemoveAttribute(STUN_ATTR_USERNAME);
  auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  attr->CopyBytes(std::string_view("newuser"));
  SM_EXPECT_TRUE(msg.AddAttribute(std::move(attr)));
  const StunByteStringAttribute* username = msg.GetByteString(STUN_ATTR_USERNAME);
  SM_ASSERT_TRUE(username != nullptr);
  SM_EXPECT_EQ(std::string_view("newuser"), username->string_view());
}

// libwebrtc's AddingAttributesAfterSigningCrashesDeathTest: the port refuses
// the attribute instead of aborting (stun.h), and the message stays as signed.
SM_TEST(Stun, AddingAttributesAfterSigningFails) {
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_TRUE(msg.AddMessageIntegrity(kRfc5769SampleMsgPassword));
  SM_ASSERT_TRUE(msg.IntegrityOk());
  size_t len = msg.length();
  auto attr = StunAttribute::CreateByteString(STUN_ATTR_USERNAME);
  attr->CopyBytes(std::string_view("keso"));
  SM_EXPECT_FALSE(msg.AddAttribute(std::move(attr)));
  SM_EXPECT_EQ(msg.length(), len);
  // FINGERPRINT may still be added after signing.
  SM_EXPECT_TRUE(msg.AddFingerprint());
  ByteBufferWriter out;
  SM_EXPECT_TRUE(msg.Write(&out));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, out.DataView()));
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(out.DataView()));
}

// libwebrtc's RemovingAttributesAfterSigningCrashesDeathTest, likewise.
SM_TEST(Stun, RemovingAttributesAfterSigningFails) {
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_TRUE(msg.AddMessageIntegrity(kRfc5769SampleMsgPassword));
  SM_ASSERT_TRUE(msg.IntegrityOk());
  SM_EXPECT_TRUE(msg.RemoveAttribute(STUN_ATTR_USERNAME) == nullptr);
  SM_EXPECT_TRUE(msg.GetByteString(STUN_ATTR_USERNAME) != nullptr);
}

// ---- Beyond stun_unittest.cc: the port's own choices (stun.h).

// The ICE attributes of the RFC 5769 request parse as their classes in an
// IceMessage; a plain StunMessage skips PRIORITY and ICE-CONTROLLED (outside
// the designated-expert ranges). An unknown attribute in those ranges is read
// as a byte string, which the typed getters of other classes refuse.
SM_TEST(Stun, IceAttributesAndTypedGetters) {
  IceMessage ice;
  ByteBufferReader r1(kRfc5769SampleRequest);
  SM_ASSERT_TRUE(ice.Read(&r1));
  SM_ASSERT_TRUE(ice.GetUInt32(STUN_ATTR_PRIORITY) != nullptr);
  SM_EXPECT_EQ(0x6e0001ffu, ice.GetUInt32(STUN_ATTR_PRIORITY)->value());
  SM_ASSERT_TRUE(ice.GetUInt64(STUN_ATTR_ICE_CONTROLLED) != nullptr);
  SM_EXPECT_EQ(0x932ff9b151263b36ull, ice.GetUInt64(STUN_ATTR_ICE_CONTROLLED)->value());
  SM_EXPECT_TRUE(ice.GetNonComprehendedAttributes().empty());

  StunMessage stun;
  ByteBufferReader r2(kRfc5769SampleRequest);
  SM_ASSERT_TRUE(stun.Read(&r2));
  SM_EXPECT_TRUE(stun.GetUInt32(STUN_ATTR_PRIORITY) == nullptr);
  SM_EXPECT_TRUE(stun.GetUInt64(STUN_ATTR_ICE_CONTROLLED) == nullptr);
  SM_EXPECT_TRUE(stun.GetByteString(STUN_ATTR_ICE_CONTROLLED) == nullptr);
  // An attribute of another value type is not returned as an address.
  SM_EXPECT_TRUE(stun.GetAddress(STUN_ATTR_SOFTWARE) == nullptr);

  IceMessage with_info(STUN_BINDING_REQUEST, "0123456789ab");
  with_info.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_GOOG_NETWORK_INFO, 0x00010032u));
  ByteBufferWriter out;
  SM_ASSERT_TRUE(with_info.Write(&out));
  StunMessage plain;
  ByteBufferReader r3(out);
  SM_ASSERT_TRUE(plain.Read(&r3));
  SM_EXPECT_TRUE(plain.GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO) == nullptr);
  SM_ASSERT_TRUE(plain.GetByteString(STUN_ATTR_GOOG_NETWORK_INFO) != nullptr);
  SM_EXPECT_EQ(plain.GetByteString(STUN_ATTR_GOOG_NETWORK_INFO)->length(), 4u);
}

// TURN attributes in a TurnMessage.
SM_TEST(Stun, TurnMessageRoundTrip) {
  TurnMessage msg(STUN_ALLOCATE_REQUEST, "0123456789ab");
  msg.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_REQUESTED_TRANSPORT, 17u << 24));
  msg.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_LIFETIME, 600u));
  msg.AddAttribute(
      std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_PEER_ADDRESS, Addr("2001:db8::7", 3478)));
  msg.AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_DATA, std::string_view("payload")));
  SM_ASSERT_TRUE(msg.AddMessageIntegrity("secret"));
  SM_ASSERT_TRUE(msg.AddFingerprint());
  ByteBufferWriter out;
  SM_ASSERT_TRUE(msg.Write(&out));
  SM_EXPECT_EQ(out.Length(), msg.length() + kStunHeaderSize);

  TurnMessage back;
  ByteBufferReader r(out);
  SM_ASSERT_TRUE(back.Read(&r));
  SM_EXPECT_EQ(back.type(), int{STUN_ALLOCATE_REQUEST});
  SM_ASSERT_TRUE(back.GetUInt32(STUN_ATTR_LIFETIME) != nullptr);
  SM_EXPECT_EQ(600u, back.GetUInt32(STUN_ATTR_LIFETIME)->value());
  SM_ASSERT_TRUE(back.GetAddress(STUN_ATTR_XOR_PEER_ADDRESS) != nullptr);
  SM_EXPECT_TRUE(back.GetAddress(STUN_ATTR_XOR_PEER_ADDRESS)->GetAddress() == Addr("2001:db8::7", 3478));
  SM_EXPECT_TRUE(back.ValidateMessageIntegrity("secret") == StunMessage::IntegrityStatus::kIntegrityOk);
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(out.DataView()));
  SM_EXPECT_EQ(StunMethodToString(back.type()), std::string("TURN ALLOCATE request"));
  SM_EXPECT_EQ(StunMethodToString(0x0abc), std::string("UNKNOWN<2748>"));
}

// Generated transaction ids: 12 characters of libwebrtc's base64 table, from
// the installed random source.
SM_TEST(Stun, GeneratedTransactionIds) {
  StunMessage a(STUN_BINDING_REQUEST);
  StunMessage b(STUN_BINDING_REQUEST);
  SM_EXPECT_EQ(a.transaction_id().size(), kStunTransactionIdLength);
  SM_EXPECT_FALSE(a.IsLegacy());
  SM_EXPECT_TRUE(a.transaction_id() != b.transaction_id());
  const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (char c : a.transaction_id()) SM_EXPECT_TRUE(table.find(c) != std::string::npos);
}

// Without crypto installed, signing and checking fail rather than pass.
SM_TEST(Stun, NoCryptoFailsClosed) {
  StunCrypto saved = GetStunCrypto();
  SetStunCrypto(StunCrypto{});
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, kRfc5769SampleRequest));
  IceMessage msg;
  ByteBufferReader buf(kRfc5769SampleRequestWithoutMI);
  SM_EXPECT_TRUE(msg.Read(&buf));
  SM_EXPECT_FALSE(msg.AddMessageIntegrity(kRfc5769SampleMsgPassword));
  SM_EXPECT_FALSE(msg.IntegrityOk());
  std::string key;
  SM_EXPECT_FALSE(ComputeStunCredentialHash("u", "r", "p", &key));
  // A transaction id is still made (from the fixed-seed fallback).
  StunMessage m(STUN_BINDING_REQUEST);
  SM_EXPECT_EQ(m.transaction_id().size(), kStunTransactionIdLength);
  SetStunCrypto(saved);
}

// Truncated packets of every length fail to parse without reading past the end.
SM_TEST(Stun, TruncatedPacketsFail) {
  for (size_t n = 0; n < sizeof(kRfc5769SampleRequest); ++n) {
    std::vector<uint8_t> cut(kRfc5769SampleRequest, kRfc5769SampleRequest + n);
    IceMessage msg;
    ByteBufferReader r(cut);
    SM_EXPECT_FALSE(msg.Read(&r));
    SM_EXPECT_FALSE(StunMessage::ValidateFingerprint(cut));
    SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting(kRfc5769SampleMsgPassword, cut));
  }
}

SM_TEST(Stun, Crc32CheckValue) {
  // The CRC-32/ISO-HDLC check value.
  const char* s = "123456789";
  SM_EXPECT_EQ(ComputeCrc32(AsUint8Span(s)), 0xCBF43926u);
  SM_EXPECT_EQ(ComputeCrc32({}), 0u);
}
