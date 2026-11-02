// STUN messages (RFC 5389 / 8489) and their TURN (RFC 5766) and ICE (RFC 5245)
// attributes: a port of libwebrtc M157's api/transport/stun.{h,cc}
// (compare/webrtc/reference/api/transport/), requirement C2 of milestone 3
// (#944, #986). The names, constants, wire layout, parsing rules, length
// limits, MESSAGE-INTEGRITY, GOOG-MESSAGE-INTEGRITY-32 and FINGERPRINT follow
// that file; M157 has no MESSAGE-INTEGRITY-SHA256.
//
// Crypto is not in this library. HMAC-SHA1 (MESSAGE-INTEGRITY), MD5 (the
// long-term credential key) and the random bytes for transaction ids come from
// functions the application installs once with SetStunCrypto() (the window
// wires BoringSSL's in #984). Without them, signing and checking integrity
// fail (return false / kIntegrityBad), ComputeStunCredentialHash fails, and
// GenerateTransactionId falls back to a fixed-seed generator that is NOT
// unpredictable, which is fine for tests and wrong for a real call.
//
// Differences from libwebrtc, all where libwebrtc would crash or read the
// wrong type:
// - AddAttribute() on a signed message returns false and drops the attribute,
//   and RemoveAttribute() of a non-integrity attribute from a signed message
//   returns nullptr, where libwebrtc's RTC_CHECK aborts.
// - The typed getters (GetUInt32, GetAddress, ...) return nullptr when the
//   attribute of that type holds another value type (an unknown attribute in
//   the designated-expert range is read as a byte string), where libwebrtc's
//   static_cast reads it as the wrong class.
// - The integrity histograms (WebRTC.Stun.Integrity.*) are not kept.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

// ---------------------------------------------------------------- crypto hooks
struct StunCrypto {
  // HMAC-SHA1(key, data) into out[0..20). Returns false on failure.
  bool (*hmac_sha1)(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[20]) = nullptr;
  // MD5(data) into out[0..16). Returns false on failure.
  bool (*md5)(std::span<const uint8_t> data, uint8_t out[16]) = nullptr;
  // Fills `out` with cryptographically random bytes. Returns false on failure.
  bool (*random_bytes)(std::span<uint8_t> out) = nullptr;
};
// Installs the functions; call once before any STUN use (not thread-safe).
void SetStunCrypto(const StunCrypto& crypto);
const StunCrypto& GetStunCrypto();

// CRC-32 (ISO-HDLC, as zlib and rtc_base/crc32: reflected 0xEDB88320, initial
// and final XOR 0xFFFFFFFF), the checksum of STUN's FINGERPRINT.
uint32_t ComputeCrc32(std::span<const uint8_t> data);

// ---------------------------------------------------------------- constants
// These are the types of STUN messages defined in RFC 5389.
enum StunMessageType : uint16_t {
  STUN_INVALID_MESSAGE_TYPE = 0x0000,
  STUN_BINDING_REQUEST = 0x0001,
  STUN_BINDING_INDICATION = 0x0011,
  STUN_BINDING_RESPONSE = 0x0101,
  STUN_BINDING_ERROR_RESPONSE = 0x0111,
  // GOOG-PING (method 0x80): sent instead of a STUN BINDING identical to the one before.
  GOOG_PING_REQUEST = 0x200,
  GOOG_PING_RESPONSE = 0x300,
  GOOG_PING_ERROR_RESPONSE = 0x310,
};

// Known STUN attributes (RFC 5389 and others), with their value class.
enum StunAttributeType {
  STUN_ATTR_MAPPED_ADDRESS = 0x0001,      // Address
  STUN_ATTR_USERNAME = 0x0006,            // ByteString
  STUN_ATTR_MESSAGE_INTEGRITY = 0x0008,   // ByteString, 20 bytes
  STUN_ATTR_ERROR_CODE = 0x0009,          // ErrorCode
  STUN_ATTR_UNKNOWN_ATTRIBUTES = 0x000a,  // UInt16List
  STUN_ATTR_REALM = 0x0014,               // ByteString
  STUN_ATTR_NONCE = 0x0015,               // ByteString
  STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020,  // XorAddress
  STUN_ATTR_SOFTWARE = 0x8022,            // ByteString
  STUN_ATTR_ALTERNATE_SERVER = 0x8023,    // Address
  STUN_ATTR_FINGERPRINT = 0x8028,         // UInt32
  STUN_ATTR_RETRANSMIT_COUNT = 0xFF00     // UInt32
};

// Value classes of the attributes above (not on the wire).
enum StunAttributeValueType {
  STUN_VALUE_UNKNOWN = 0,
  STUN_VALUE_ADDRESS = 1,
  STUN_VALUE_XOR_ADDRESS = 2,
  STUN_VALUE_UINT32 = 3,
  STUN_VALUE_UINT64 = 4,
  STUN_VALUE_BYTE_STRING = 5,
  STUN_VALUE_ERROR_CODE = 6,
  STUN_VALUE_UINT16_LIST = 7
};

enum StunAddressFamily {
  STUN_ADDRESS_UNDEF = 0,  // Not part of the STUN spec.
  STUN_ADDRESS_IPV4 = 1,
  STUN_ADDRESS_IPV6 = 2
};

enum StunErrorCode {
  STUN_ERROR_NOT_AN_ERROR = 0,
  STUN_ERROR_TRY_ALTERNATE = 300,
  STUN_ERROR_BAD_REQUEST = 400,
  STUN_ERROR_UNAUTHORIZED = 401,
  STUN_ERROR_UNKNOWN_ATTRIBUTE = 420,
  STUN_ERROR_STALE_NONCE = 438,
  STUN_ERROR_SERVER_ERROR = 500,
  STUN_ERROR_GLOBAL_FAILURE = 600,
  STUN_ERROR_SERVER_NOT_REACHABLE = 701,
};

extern const char STUN_ERROR_REASON_TRY_ALTERNATE_SERVER[];
extern const char STUN_ERROR_REASON_BAD_REQUEST[];
extern const char STUN_ERROR_REASON_UNAUTHORIZED[];
extern const char STUN_ERROR_REASON_UNKNOWN_ATTRIBUTE[];
extern const char STUN_ERROR_REASON_STALE_NONCE[];
extern const char STUN_ERROR_REASON_SERVER_ERROR[];

inline constexpr uint32_t kStunTypeMask = 0x0110;
inline constexpr size_t kStunAttributeHeaderSize = 4;
inline constexpr size_t kStunHeaderSize = 20;
inline constexpr size_t kStunTransactionIdOffset = 8;
inline constexpr size_t kStunTransactionIdLength = 12;
inline constexpr uint32_t kStunMagicCookie = 0x2112A442;
inline constexpr size_t kStunMagicCookieLength = sizeof(kStunMagicCookie);
inline constexpr size_t kStunLegacyTransactionIdLength = 16;  // RFC 3489.
inline constexpr size_t kStunMessageIntegritySize = 20;
inline constexpr size_t kStunMessageIntegrity32Size = 4;

// TURN (RFC 5766).
enum TurnMessageType : uint16_t {
  STUN_ALLOCATE_REQUEST = 0x0003,
  STUN_ALLOCATE_RESPONSE = 0x0103,
  STUN_ALLOCATE_ERROR_RESPONSE = 0x0113,
  TURN_REFRESH_REQUEST = 0x0004,
  TURN_REFRESH_RESPONSE = 0x0104,
  TURN_REFRESH_ERROR_RESPONSE = 0x0114,
  TURN_SEND_INDICATION = 0x0016,
  TURN_DATA_INDICATION = 0x0017,
  TURN_CREATE_PERMISSION_REQUEST = 0x0008,
  TURN_CREATE_PERMISSION_RESPONSE = 0x0108,
  TURN_CREATE_PERMISSION_ERROR_RESPONSE = 0x0118,
  TURN_CHANNEL_BIND_REQUEST = 0x0009,
  TURN_CHANNEL_BIND_RESPONSE = 0x0109,
  TURN_CHANNEL_BIND_ERROR_RESPONSE = 0x0119,
};

enum TurnAttributeType {
  STUN_ATTR_CHANNEL_NUMBER = 0x000C,       // UInt32
  STUN_ATTR_LIFETIME = 0x000d,             // UInt32
  STUN_ATTR_XOR_PEER_ADDRESS = 0x0012,     // XorAddress
  STUN_ATTR_DATA = 0x0013,                 // ByteString
  STUN_ATTR_XOR_RELAYED_ADDRESS = 0x0016,  // XorAddress
  STUN_ATTR_EVEN_PORT = 0x0018,            // ByteString, 1 byte.
  STUN_ATTR_REQUESTED_TRANSPORT = 0x0019,  // UInt32
  STUN_ATTR_DONT_FRAGMENT = 0x001A,        // No content, Length = 0
  STUN_ATTR_RESERVATION_TOKEN = 0x0022,    // ByteString, 8 bytes.
};

enum TurnErrorType {
  STUN_ERROR_FORBIDDEN = 403,
  STUN_ERROR_ALLOCATION_MISMATCH = 437,
  STUN_ERROR_WRONG_CREDENTIALS = 441,
  STUN_ERROR_UNSUPPORTED_PROTOCOL = 442
};

extern const char STUN_ERROR_REASON_FORBIDDEN[];
extern const char STUN_ERROR_REASON_ALLOCATION_MISMATCH[];
extern const char STUN_ERROR_REASON_WRONG_CREDENTIALS[];
extern const char STUN_ERROR_REASON_UNSUPPORTED_PROTOCOL[];

// ICE (RFC 5245) and libwebrtc's GOOG attributes.
enum IceAttributeType {
  STUN_ATTR_PRIORITY = 0x0024,         // UInt32
  STUN_ATTR_USE_CANDIDATE = 0x0025,    // No content, Length = 0
  STUN_ATTR_ICE_CONTROLLED = 0x8029,   // UInt64
  STUN_ATTR_ICE_CONTROLLING = 0x802A,  // UInt64
  // Comprehension-optional and not registered with IANA; for ICE only.
  STUN_ATTR_NOMINATION = 0xC001,  // UInt32
  // UInt32: network id in the high 16 bits, network cost in the low 16.
  STUN_ATTR_GOOG_NETWORK_INFO = 0xC057,
  STUN_ATTR_GOOG_LAST_ICE_CHECK_RECEIVED = 0xC058,
  STUN_ATTR_GOOG_MISC_INFO = 0xC059,  // UInt16List
  STUN_ATTR_GOOG_OBSOLETE_1 = 0xC05A,
  STUN_ATTR_GOOG_CONNECTION_ID = 0xC05B,
  STUN_ATTR_GOOG_DELTA = 0xC05C,
  STUN_ATTR_GOOG_DELTA_ACK = 0xC05D,
  STUN_ATTR_GOOG_DELTA_SYNC_REQ = 0xC05E,
  STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32 = 0xC060,  // MESSAGE-INTEGRITY truncated to 32 bits.
  STUN_ATTR_META_DTLS_IN_STUN = 0xC070,
  STUN_ATTR_META_DTLS_IN_STUN_ACK = 0xC071,
};

enum class IceGoogMiscInfoBindingRequestAttributeIndex {
  SUPPORT_GOOG_PING_VERSION = 0,
};

enum class IceGoogMiscInfoBindingResponseAttributeIndex {
  SUPPORT_GOOG_PING_VERSION = 0,
};

enum IceErrorCode {
  STUN_ERROR_ROLE_CONFLICT = 487,
};
extern const char STUN_ERROR_REASON_ROLE_CONFLICT[];

class StunAddressAttribute;
class StunAttribute;
class StunByteStringAttribute;
class StunErrorCodeAttribute;
class StunUInt16ListAttribute;
class StunUInt32Attribute;
class StunUInt64Attribute;
class StunXorAddressAttribute;

// ---------------------------------------------------------------- message
// A STUN/TURN message: a type and attributes, each parsed into the class of its
// value type.
class StunMessage {
 public:
  // An invalid type and an empty, legacy (16-byte, RFC 3489) transaction id.
  StunMessage();
  // `type` with a new 12-byte (RFC 5389) transaction id.
  explicit StunMessage(uint16_t type);
  // `transaction_id` must be 12 or 16 bytes.
  StunMessage(uint16_t type, std::string_view transaction_id);
  virtual ~StunMessage();
  StunMessage(const StunMessage&) = delete;
  StunMessage& operator=(const StunMessage&) = delete;

  enum class IntegrityStatus {
    kNotSet = 0,
    kNoIntegrity = 1,   // No MESSAGE-INTEGRITY attribute.
    kIntegrityOk = 2,   // MESSAGE-INTEGRITY checked OK.
    kIntegrityBad = 3,  // MESSAGE-INTEGRITY verification failed.
    kMaxValue = kIntegrityBad,
  };

  int type() const { return type_; }
  size_t length() const { return length_; }
  const std::string& transaction_id() const { return transaction_id_; }
  uint32_t reduced_transaction_id() const { return reduced_transaction_id_; }

  // True for an RFC 3489 message (16-byte transaction id, no magic cookie).
  bool IsLegacy() const;

  // Attribute types in the comprehension-required range that were not recognized.
  std::vector<uint16_t> GetNonComprehendedAttributes() const;

  // The first attribute of `type`, or nullptr. GetAddress(MAPPED_ADDRESS)
  // falls back to XOR-MAPPED-ADDRESS.
  const StunAddressAttribute* GetAddress(int type) const;
  const StunUInt32Attribute* GetUInt32(int type) const;
  const StunUInt64Attribute* GetUInt64(int type) const;
  const StunByteStringAttribute* GetByteString(int type) const;
  const StunUInt16ListAttribute* GetUInt16List(int type) const;
  const StunErrorCodeAttribute* GetErrorCode() const;
  // The ERROR-CODE value, or STUN_ERROR_GLOBAL_FAILURE without one.
  int GetErrorCodeValue() const;
  const StunUInt16ListAttribute* GetUnknownAttributes() const;

  // Takes ownership of `attr` and appends it. Returns false (and drops it) for
  // anything but an integrity or FINGERPRINT attribute once the message is signed.
  bool AddAttribute(std::unique_ptr<StunAttribute> attr);
  // Removes the last attribute of `type`; nullptr if none (or if the message is
  // signed and `type` is not an integrity attribute).
  std::unique_ptr<StunAttribute> RemoveAttribute(int type);
  void ClearAttributes();

  // Checks MESSAGE-INTEGRITY (or GOOG-MESSAGE-INTEGRITY-32) against the bytes
  // Read() parsed. Done once; RevalidateMessageIntegrity() checks again.
  IntegrityStatus ValidateMessageIntegrity(const std::string& password);
  IntegrityStatus RevalidateMessageIntegrity(const std::string& password);
  IntegrityStatus integrity() const { return integrity_; }
  bool IntegrityOk() const { return integrity_ == IntegrityStatus::kIntegrityOk; }
  // The password used to sign or check the message.
  const std::string& password() const { return password_; }

  // Appends a MESSAGE-INTEGRITY / GOOG-MESSAGE-INTEGRITY-32 valid for the message so far.
  bool AddMessageIntegrity(std::string_view password);
  bool AddMessageIntegrity32(std::string_view password);

  // True if `data` has the magic cookie and its method is one of `methods`
  // (FINGERPRINT is not checked).
  static bool IsStunMethod(std::span<const int> methods, std::span<const uint8_t> data);
  // True if `data` is STUN with a correct FINGERPRINT (RFC 5389 7.3, 15.5).
  static bool ValidateFingerprint(std::span<const uint8_t> data);
  // A new 12-byte transaction id from GetStunCrypto().random_bytes.
  static std::string GenerateTransactionId();

  // Appends a FINGERPRINT valid for the message so far.
  bool AddFingerprint();

  // Parses a STUN packet. False if it is not one or is malformed; unknown
  // attributes outside the designated-expert ranges are skipped.
  bool Read(ByteBufferReader* buf);
  bool Write(ByteBufferWriter* buf) const;

  virtual StunMessage* CreateNew() const;

  void SetStunMagicCookie(uint32_t val);
  void SetTransactionIdForTesting(std::string_view transaction_id);

  std::unique_ptr<StunMessage> Clone() const;

  // True if every attribute for which `attribute_type_mask` is true is in both
  // messages with the same encoding.
  bool EqualAttributes(const StunMessage* other, const std::function<bool(int type)>& attribute_type_mask) const;

  static bool ValidateMessageIntegrityForTesting(const std::string& password, std::span<const uint8_t> data);
  static bool ValidateMessageIntegrity32ForTesting(const std::string& password, std::span<const uint8_t> data);

 protected:
  virtual StunAttributeValueType GetAttributeValueType(int type) const;

  std::vector<std::unique_ptr<StunAttribute>> attrs_;

 private:
  StunAttribute* CreateAttribute(int type, size_t length);
  const StunAttribute* GetAttribute(int type) const;
  const StunAttribute* GetAttributeOfValueType(int type, StunAttributeValueType value_type) const;
  static bool IsValidTransactionId(std::string_view transaction_id);
  bool AddMessageIntegrityOfType(int mi_attr_type, size_t mi_attr_size, std::string_view key);
  static bool ValidateMessageIntegrityOfType(int mi_attr_type, size_t mi_attr_size, std::span<const uint8_t> data,
                                             const std::string& password);

  uint16_t type_ = STUN_INVALID_MESSAGE_TYPE;
  uint16_t length_ = 0;
  std::string transaction_id_;
  uint32_t reduced_transaction_id_ = 0;
  uint32_t stun_magic_cookie_ = kStunMagicCookie;
  std::vector<uint8_t> buffer_;  // The bytes Read() parsed, for integrity checks.
  IntegrityStatus integrity_ = IntegrityStatus::kNotSet;
  std::string password_;
};

// ---------------------------------------------------------------- attributes
class StunAttribute {
 public:
  virtual ~StunAttribute() = default;

  int type() const { return type_; }
  size_t length() const { return length_; }
  virtual StunAttributeValueType value_type() const = 0;
  // Only the XOR address attribute uses its owner (for the transaction id).
  virtual void SetOwner(StunMessage* /* owner */) {}
  // Reads / writes the body (not the type or length).
  virtual bool Read(ByteBufferReader* buf) = 0;
  virtual bool Write(ByteBufferWriter* buf) const = 0;

  // An attribute of the given class, type and smallest length.
  static StunAttribute* Create(StunAttributeValueType value_type, uint16_t type, uint16_t length, StunMessage* owner);
  static std::unique_ptr<StunAddressAttribute> CreateAddress(uint16_t type);
  static std::unique_ptr<StunXorAddressAttribute> CreateXorAddress(uint16_t type);
  static std::unique_ptr<StunUInt32Attribute> CreateUInt32(uint16_t type);
  static std::unique_ptr<StunUInt64Attribute> CreateUInt64(uint16_t type);
  static std::unique_ptr<StunByteStringAttribute> CreateByteString(uint16_t type);
  static std::unique_ptr<StunUInt16ListAttribute> CreateUInt16ListAttribute(uint16_t type);
  static std::unique_ptr<StunErrorCodeAttribute> CreateErrorCode();
  static std::unique_ptr<StunUInt16ListAttribute> CreateUnknownAttributes();

 protected:
  StunAttribute(uint16_t type, uint16_t length) : type_(type), length_(length) {}
  void SetLength(uint16_t length) { length_ = length; }
  void WritePadding(ByteBufferWriter* buf) const;
  void ConsumePadding(ByteBufferReader* buf) const;

 private:
  uint16_t type_;
  uint16_t length_;
};

// An Internet address.
class StunAddressAttribute : public StunAttribute {
 public:
  static const uint16_t SIZE_UNDEF = 0;
  static const uint16_t SIZE_IP4 = 8;
  static const uint16_t SIZE_IP6 = 20;
  StunAddressAttribute(uint16_t type, const SocketAddress& addr);
  StunAddressAttribute(uint16_t type, uint16_t length);

  StunAttributeValueType value_type() const override;

  StunAddressFamily family() const {
    switch (address_.family()) {
      case IpFamily::kV4:
        return STUN_ADDRESS_IPV4;
      case IpFamily::kV6:
        return STUN_ADDRESS_IPV6;
      default:
        return STUN_ADDRESS_UNDEF;
    }
  }

  const SocketAddress& GetAddress() const { return address_; }
  const IpAddress& ipaddr() const { return address_.ipaddr(); }
  uint16_t port() const { return address_.port(); }

  void SetAddress(const SocketAddress& addr) {
    address_ = addr;
    EnsureAddressLength();
  }
  void SetIP(const IpAddress& ip) {
    address_.SetIp(ip);
    EnsureAddressLength();
  }
  void SetPort(uint16_t port) { address_.SetPort(port); }

  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  void EnsureAddressLength() {
    switch (family()) {
      case STUN_ADDRESS_IPV4:
        SetLength(SIZE_IP4);
        break;
      case STUN_ADDRESS_IPV6:
        SetLength(SIZE_IP6);
        break;
      default:
        SetLength(SIZE_UNDEF);
        break;
    }
  }
  SocketAddress address_;
};

// An Internet address XORed on the wire with the magic cookie (and, for IPv6,
// the owner message's transaction id).
class StunXorAddressAttribute : public StunAddressAttribute {
 public:
  StunXorAddressAttribute(uint16_t type, const SocketAddress& addr);
  StunXorAddressAttribute(uint16_t type, uint16_t length, StunMessage* owner);

  StunAttributeValueType value_type() const override;
  void SetOwner(StunMessage* owner) override;
  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  IpAddress GetXoredIP() const;
  StunMessage* owner_;
};

class StunUInt32Attribute : public StunAttribute {
 public:
  static const uint16_t SIZE = 4;
  StunUInt32Attribute(uint16_t type, uint32_t value);
  explicit StunUInt32Attribute(uint16_t type);

  StunAttributeValueType value_type() const override;
  uint32_t value() const { return bits_; }
  void SetValue(uint32_t bits) { bits_ = bits; }
  bool GetBit(size_t index) const;
  void SetBit(size_t index, bool value);

  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  uint32_t bits_;
};

class StunUInt64Attribute : public StunAttribute {
 public:
  static const uint16_t SIZE = 8;
  StunUInt64Attribute(uint16_t type, uint64_t value);
  explicit StunUInt64Attribute(uint16_t type);

  StunAttributeValueType value_type() const override;
  uint64_t value() const { return bits_; }
  void SetValue(uint64_t bits) { bits_ = bits; }

  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  uint64_t bits_;
};

// An arbitrary byte string.
class StunByteStringAttribute : public StunAttribute {
 public:
  explicit StunByteStringAttribute(uint16_t type);
  StunByteStringAttribute(uint16_t type, std::string_view str);
  StunByteStringAttribute(uint16_t type, std::span<const uint8_t> bytes);
  StunByteStringAttribute(uint16_t type, const std::vector<uint32_t>& values);
  StunByteStringAttribute(uint16_t type, uint16_t length);

  StunAttributeValueType value_type() const override;

  std::string_view string_view() const {
    return std::string_view(reinterpret_cast<const char*>(bytes_.data()), length());
  }
  std::span<const uint8_t> array_view() const { return std::span<const uint8_t>(bytes_.data(), length()); }
  // The value as big-endian 32-bit words; nullopt if its length is not a multiple of 4.
  std::optional<std::vector<uint32_t>> GetUInt32Vector() const;

  void CopyBytes(std::span<const uint8_t> bytes);
  void CopyBytes(std::string_view bytes);
  uint8_t GetByte(size_t index) const { return bytes_[index]; }
  void SetByte(size_t index, uint8_t value) { bytes_[index] = value; }

  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  std::vector<uint8_t> bytes_;
};

// An ERROR-CODE: class, number and reason phrase.
class StunErrorCodeAttribute : public StunAttribute {
 public:
  static const uint16_t MIN_SIZE;
  StunErrorCodeAttribute(uint16_t type, int code, const std::string& reason);
  StunErrorCodeAttribute(uint16_t type, uint16_t length);

  StunAttributeValueType value_type() const override;

  // Class * 100 + number, e.g. 401.
  int code() const;
  void SetCode(int code);
  int eclass() const { return class_; }
  int number() const { return number_; }
  const std::string& reason() const { return reason_; }
  void SetClass(uint8_t eclass) { class_ = eclass; }
  void SetNumber(uint8_t number) { number_ = number; }
  void SetReason(const std::string& reason);

  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  uint8_t class_ = 0;
  uint8_t number_ = 0;
  std::string reason_;
};

// A list of 16-bit values (UNKNOWN-ATTRIBUTES, GOOG-MISC-INFO).
class StunUInt16ListAttribute : public StunAttribute {
 public:
  StunUInt16ListAttribute(uint16_t type, uint16_t length);

  StunAttributeValueType value_type() const override;

  size_t Size() const { return attr_types_.size(); }
  uint16_t GetType(int index) const { return attr_types_[static_cast<size_t>(index)]; }
  void SetType(int index, uint16_t value) { attr_types_[static_cast<size_t>(index)] = value; }
  void AddType(uint16_t value);
  void AddTypeAtIndex(uint16_t index, uint16_t value);

  bool Read(ByteBufferReader* buf) override;
  bool Write(ByteBufferWriter* buf) const override;

 private:
  std::vector<uint16_t> attr_types_;
};

// "STUN BINDING request" and so on.
std::string StunMethodToString(int msg_type);
// The success / error response type for a request type, or -1.
int GetStunSuccessResponseType(int request_type);
int GetStunErrorResponseType(int request_type);
bool IsStunRequestType(int msg_type);
bool IsStunIndicationType(int msg_type);
bool IsStunSuccessResponseType(int msg_type);
bool IsStunErrorResponseType(int msg_type);

// The long-term credential key MD5(username ":" realm ":" password) (RFC 5389
// 15.4; no SASLprep, as libwebrtc). False without an MD5 function.
bool ComputeStunCredentialHash(const std::string& username, const std::string& realm, const std::string& password,
                               std::string* hash);

// A copy of `attribute` made by writing and reading it back, through
// `tmp_buffer_ptr` if given.
std::unique_ptr<StunAttribute> CopyStunAttribute(const StunAttribute& attribute,
                                                 ByteBufferWriter* tmp_buffer_ptr = nullptr);

// A TURN message: STUN plus the RFC 5766 attributes.
class TurnMessage : public StunMessage {
 public:
  using StunMessage::StunMessage;

 protected:
  StunAttributeValueType GetAttributeValueType(int type) const override;
  StunMessage* CreateNew() const override;
};

// An ICE connectivity check: STUN plus the RFC 5245 attributes.
class IceMessage : public StunMessage {
 public:
  using StunMessage::StunMessage;

 protected:
  StunAttributeValueType GetAttributeValueType(int type) const override;
  StunMessage* CreateNew() const override;
};

}  // namespace sm::p2p
