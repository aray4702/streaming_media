// Port of libwebrtc M157 api/transport/stun.cc; see include/sm/p2p/stun.h.
#include "sm/p2p/stun.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <utility>

#include "sm/core/random.h"

namespace sm::p2p {

namespace {

const int k127Utf8CharactersLengthInBytes = 508;
const int kMessageIntegrityAttributeLength = 20;
const int kTheoreticalMaximumAttributeLength = 65535;

StunCrypto g_crypto;

uint16_t GetBE16(std::span<const uint8_t> d) { return static_cast<uint16_t>((d[0] << 8) | d[1]); }
uint32_t GetBE32(std::span<const uint8_t> d) {
  return (uint32_t{d[0]} << 24) | (uint32_t{d[1]} << 16) | (uint32_t{d[2]} << 8) | d[3];
}

std::span<const uint8_t> AsUint8Span(std::string_view s) {
  return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

bool ComputeHmacSha1(std::string_view key, std::span<const uint8_t> data, uint8_t out[20]) {
  return g_crypto.hmac_sha1 != nullptr && g_crypto.hmac_sha1(AsUint8Span(key), data, out);
}

uint32_t ReduceTransactionId(std::string_view transaction_id) {
  ByteBufferReader reader(AsUint8Span(transaction_id));
  uint32_t result = 0;
  uint32_t next;
  while (reader.ReadUInt32(&next)) {
    result ^= next;
  }
  return result;
}

// The maximum length of a BYTE_STRING attribute per specification.
bool LengthValid(int type, int length) {
  // "Less than 509 bytes" is meant as at most 127 UTF-8 characters of up to 4 bytes.
  switch (type) {
    case STUN_ATTR_USERNAME:
      return length <= k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.3
    case STUN_ATTR_MESSAGE_INTEGRITY:
      return length == kMessageIntegrityAttributeLength;  // RFC 8489 section 14.5
    case STUN_ATTR_REALM:
      return length <= k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.9
    case STUN_ATTR_NONCE:
      return length <= k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.10
    case STUN_ATTR_SOFTWARE:
      return length <= k127Utf8CharactersLengthInBytes;  // RFC 8489 section 14.14
    case STUN_ATTR_DATA:
      // The content of a UDP datagram, in theory up to 65535 bytes.
      return length <= kTheoreticalMaximumAttributeLength;
    default:
      return length <= kTheoreticalMaximumAttributeLength;
  }
}

bool DesignatedExpertRange(int attr_type) {
  return (attr_type >= 0x4000 && attr_type <= 0x7FFF) || (attr_type >= 0xC000 && attr_type <= 0xFFFF);
}

const char kBase64[64] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                          'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                          'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                          'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

// rtc_base/crypto_random.cc CreateRandomString(len): each random byte picks
// one of 64 characters.
std::string CreateRandomString(size_t len) {
  std::vector<uint8_t> bytes(len);
  bool ok = g_crypto.random_bytes != nullptr && g_crypto.random_bytes(bytes);
  if (!ok) {
    // No secure source installed: deterministic, for tests only (see stun.h).
    static Random fallback(0x57u);
    for (auto& b : bytes) b = static_cast<uint8_t>(fallback.Next());
  }
  std::string str;
  str.reserve(len);
  for (uint8_t b : bytes) str.push_back(kBase64[b % 64]);
  return str;
}

uint32_t Crc32Table(uint32_t i) {
  uint32_t c = i;
  for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  return c;
}

// The address's bytes in network order (4 or 16; none when unspecified).
std::vector<uint8_t> AddressBytes(const IpAddress& ip) {
  if (ip.family() == IpFamily::kV4) {
    uint32_t v = ip.v4();
    return {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v)};
  }
  if (ip.family() == IpFamily::kV6) return std::vector<uint8_t>(ip.v6().begin(), ip.v6().end());
  return {};
}

}  // namespace

void SetStunCrypto(const StunCrypto& crypto) { g_crypto = crypto; }
const StunCrypto& GetStunCrypto() { return g_crypto; }

uint32_t ComputeCrc32(std::span<const uint8_t> data) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) t[i] = Crc32Table(i);
    return t;
  }();
  uint32_t c = 0xFFFFFFFFu;
  for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

const char STUN_ERROR_REASON_TRY_ALTERNATE_SERVER[] = "Try Alternate Server";
const char STUN_ERROR_REASON_BAD_REQUEST[] = "Bad Request";
const char STUN_ERROR_REASON_UNAUTHORIZED[] = "Unauthorized";
const char STUN_ERROR_REASON_UNKNOWN_ATTRIBUTE[] = "Unknown Attribute";
const char STUN_ERROR_REASON_FORBIDDEN[] = "Forbidden";
const char STUN_ERROR_REASON_ALLOCATION_MISMATCH[] = "Allocation Mismatch";
const char STUN_ERROR_REASON_STALE_NONCE[] = "Stale Nonce";
const char STUN_ERROR_REASON_WRONG_CREDENTIALS[] = "Wrong Credentials";
const char STUN_ERROR_REASON_UNSUPPORTED_PROTOCOL[] = "Unsupported Protocol";
const char STUN_ERROR_REASON_ROLE_CONFLICT[] = "Role Conflict";
const char STUN_ERROR_REASON_SERVER_ERROR[] = "Server Error";

namespace {
const char EMPTY_TRANSACTION_ID[] = "0000000000000000";
const uint32_t STUN_FINGERPRINT_XOR_VALUE = 0x5354554E;
}  // namespace

// ---------------------------------------------------------------- StunMessage

StunMessage::StunMessage() : StunMessage(STUN_INVALID_MESSAGE_TYPE, EMPTY_TRANSACTION_ID) {}

StunMessage::StunMessage(uint16_t type) : StunMessage(type, GenerateTransactionId()) {}

StunMessage::StunMessage(uint16_t type, std::string_view transaction_id)
    : type_(type), transaction_id_(transaction_id), reduced_transaction_id_(ReduceTransactionId(transaction_id_)) {}

StunMessage::~StunMessage() = default;

bool StunMessage::IsLegacy() const { return transaction_id_.size() == kStunLegacyTransactionIdLength; }

bool StunMessage::AddAttribute(std::unique_ptr<StunAttribute> attr) {
  // Once a message is signed only the integrity attributes and FINGERPRINT may
  // be added (libwebrtc RTC_CHECKs this).
  if (attr->type() != STUN_ATTR_MESSAGE_INTEGRITY && attr->type() != STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32 &&
      attr->type() != STUN_ATTR_FINGERPRINT && integrity_ == IntegrityStatus::kIntegrityOk) {
    return false;
  }
  attr->SetOwner(this);
  size_t attr_length = attr->length();
  if (attr_length % 4 != 0) {
    attr_length += (4 - (attr_length % 4));
  }
  length_ = static_cast<uint16_t>(length_ + attr_length + 4);
  attrs_.push_back(std::move(attr));
  return true;
}

std::unique_ptr<StunAttribute> StunMessage::RemoveAttribute(int type) {
  if (type != STUN_ATTR_MESSAGE_INTEGRITY && type != STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32 &&
      integrity_ == IntegrityStatus::kIntegrityOk) {
    // Removing attributes breaks integrity (libwebrtc RTC_CHECKs this).
    return nullptr;
  }
  std::unique_ptr<StunAttribute> attribute;
  for (auto it = attrs_.rbegin(); it != attrs_.rend(); ++it) {
    if ((*it)->type() == type) {
      attribute = std::move(*it);
      attrs_.erase(std::next(it).base());
      break;
    }
  }
  if (attribute) {
    attribute->SetOwner(nullptr);
    size_t attr_length = attribute->length();
    if (attr_length % 4 != 0) {
      attr_length += (4 - (attr_length % 4));
    }
    length_ = static_cast<uint16_t>(length_ - (attr_length + 4));
    if (type == STUN_ATTR_MESSAGE_INTEGRITY || type == STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32) {
      // The message is no longer signed, so attributes may be added again.
      integrity_ = IntegrityStatus::kNotSet;
    }
  }
  return attribute;
}

void StunMessage::ClearAttributes() {
  for (auto it = attrs_.rbegin(); it != attrs_.rend(); ++it) {
    (*it)->SetOwner(nullptr);
  }
  attrs_.clear();
  length_ = 0;
  integrity_ = IntegrityStatus::kNotSet;
}

std::vector<uint16_t> StunMessage::GetNonComprehendedAttributes() const {
  std::vector<uint16_t> unknown_attributes;
  for (auto& attr : attrs_) {
    // The "comprehension-required" range is 0x0000-0x7FFF.
    if (attr->type() >= 0x0000 && attr->type() <= 0x7FFF &&
        GetAttributeValueType(attr->type()) == STUN_VALUE_UNKNOWN) {
      unknown_attributes.push_back(static_cast<uint16_t>(attr->type()));
    }
  }
  return unknown_attributes;
}

const StunAttribute* StunMessage::GetAttributeOfValueType(int type, StunAttributeValueType value_type) const {
  const StunAttribute* a = GetAttribute(type);
  return a != nullptr && a->value_type() == value_type ? a : nullptr;
}

const StunAddressAttribute* StunMessage::GetAddress(int type) const {
  auto as_address = [](const StunAttribute* a) -> const StunAddressAttribute* {
    if (a == nullptr) return nullptr;
    StunAttributeValueType v = a->value_type();
    return v == STUN_VALUE_ADDRESS || v == STUN_VALUE_XOR_ADDRESS ? static_cast<const StunAddressAttribute*>(a)
                                                                  : nullptr;
  };
  if (type == STUN_ATTR_MAPPED_ADDRESS) {
    // XOR-MAPPED-ADDRESS when MAPPED-ADDRESS is missing.
    const StunAttribute* mapped_address = GetAttribute(STUN_ATTR_MAPPED_ADDRESS);
    if (!mapped_address) mapped_address = GetAttribute(STUN_ATTR_XOR_MAPPED_ADDRESS);
    return as_address(mapped_address);
  }
  return as_address(GetAttribute(type));
}

const StunUInt32Attribute* StunMessage::GetUInt32(int type) const {
  return static_cast<const StunUInt32Attribute*>(GetAttributeOfValueType(type, STUN_VALUE_UINT32));
}

const StunUInt64Attribute* StunMessage::GetUInt64(int type) const {
  return static_cast<const StunUInt64Attribute*>(GetAttributeOfValueType(type, STUN_VALUE_UINT64));
}

const StunByteStringAttribute* StunMessage::GetByteString(int type) const {
  return static_cast<const StunByteStringAttribute*>(GetAttributeOfValueType(type, STUN_VALUE_BYTE_STRING));
}

const StunUInt16ListAttribute* StunMessage::GetUInt16List(int type) const {
  return static_cast<const StunUInt16ListAttribute*>(GetAttributeOfValueType(type, STUN_VALUE_UINT16_LIST));
}

const StunErrorCodeAttribute* StunMessage::GetErrorCode() const {
  return static_cast<const StunErrorCodeAttribute*>(GetAttributeOfValueType(STUN_ATTR_ERROR_CODE, STUN_VALUE_ERROR_CODE));
}

int StunMessage::GetErrorCodeValue() const {
  const StunErrorCodeAttribute* error_attribute = GetErrorCode();
  return error_attribute ? error_attribute->code() : STUN_ERROR_GLOBAL_FAILURE;
}

const StunUInt16ListAttribute* StunMessage::GetUnknownAttributes() const {
  return GetUInt16List(STUN_ATTR_UNKNOWN_ATTRIBUTES);
}

StunMessage::IntegrityStatus StunMessage::ValidateMessageIntegrity(const std::string& password) {
  password_ = password;
  if (GetByteString(STUN_ATTR_MESSAGE_INTEGRITY)) {
    integrity_ = ValidateMessageIntegrityOfType(STUN_ATTR_MESSAGE_INTEGRITY, kStunMessageIntegritySize, buffer_,
                                                password)
                     ? IntegrityStatus::kIntegrityOk
                     : IntegrityStatus::kIntegrityBad;
  } else if (GetByteString(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32)) {
    integrity_ = ValidateMessageIntegrityOfType(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32, kStunMessageIntegrity32Size,
                                                buffer_, password)
                     ? IntegrityStatus::kIntegrityOk
                     : IntegrityStatus::kIntegrityBad;
  } else {
    integrity_ = IntegrityStatus::kNoIntegrity;
  }
  return integrity_;
}

StunMessage::IntegrityStatus StunMessage::RevalidateMessageIntegrity(const std::string& password) {
  integrity_ = IntegrityStatus::kNotSet;
  return ValidateMessageIntegrity(password);
}

bool StunMessage::ValidateMessageIntegrityForTesting(const std::string& password, std::span<const uint8_t> data) {
  return ValidateMessageIntegrityOfType(STUN_ATTR_MESSAGE_INTEGRITY, kStunMessageIntegritySize, data, password);
}

bool StunMessage::ValidateMessageIntegrity32ForTesting(const std::string& password, std::span<const uint8_t> data) {
  return ValidateMessageIntegrityOfType(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32, kStunMessageIntegrity32Size, data,
                                        password);
}

// RFC 5389 section 15.4.
bool StunMessage::ValidateMessageIntegrityOfType(int mi_attr_type, size_t mi_attr_size, std::span<const uint8_t> data,
                                                 const std::string& password) {
  // The message size.
  if ((data.size() % 4) != 0 || data.size() < kStunHeaderSize) {
    return false;
  }
  // The message length from the STUN header.
  uint16_t msg_length = GetBE16(data.subspan(2, 2));
  if (data.size() != (msg_length + kStunHeaderSize)) {
    return false;
  }

  // Find the integrity attribute.
  size_t current_pos = kStunHeaderSize;
  bool has_message_integrity_attr = false;
  while (current_pos + 4 <= data.size()) {
    uint16_t attr_type = GetBE16(data.subspan(current_pos, 2));
    uint16_t attr_length = GetBE16(data.subspan(current_pos + 2, 2));
    if (attr_type == mi_attr_type) {
      if (attr_length != mi_attr_size || current_pos + 4 + attr_length > data.size()) {
        return false;
      }
      has_message_integrity_attr = true;
      break;
    }
    // Otherwise skip to the next attribute.
    current_pos += 4 + attr_length;
    if ((attr_length % 4) != 0) {
      current_pos += (4 - (attr_length % 4));
    }
  }
  if (!has_message_integrity_attr) {
    return false;
  }

  // The HMAC covers the message up to the attribute, with the header's length
  // as if the attribute were the last one.
  size_t mi_pos = current_pos;
  std::vector<uint8_t> temp_data(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(current_pos));
  if (data.size() > mi_pos + kStunAttributeHeaderSize + mi_attr_size) {
    size_t extra_offset = data.size() - (mi_pos + kStunAttributeHeaderSize + mi_attr_size);
    size_t new_adjusted_len = data.size() - extra_offset - kStunHeaderSize;
    temp_data[2] = static_cast<uint8_t>(new_adjusted_len >> 8);
    temp_data[3] = static_cast<uint8_t>(new_adjusted_len);
  }

  uint8_t hmac[kStunMessageIntegritySize];
  if (!ComputeHmacSha1(password, temp_data, hmac)) {
    return false;
  }
  return std::equal(hmac, hmac + mi_attr_size, data.begin() + static_cast<std::ptrdiff_t>(current_pos + 4));
}

bool StunMessage::AddMessageIntegrity(std::string_view password) {
  return AddMessageIntegrityOfType(STUN_ATTR_MESSAGE_INTEGRITY, kStunMessageIntegritySize, password);
}

bool StunMessage::AddMessageIntegrity32(std::string_view password) {
  return AddMessageIntegrityOfType(STUN_ATTR_GOOG_MESSAGE_INTEGRITY_32, kStunMessageIntegrity32Size, password);
}

bool StunMessage::AddMessageIntegrityOfType(int attr_type, size_t attr_size, std::string_view key) {
  // Add the attribute with a dummy value; a known attribute, so it can't fail.
  auto msg_integrity_attr_ptr =
      std::make_unique<StunByteStringAttribute>(static_cast<uint16_t>(attr_type), std::string(attr_size, '0'));
  auto* msg_integrity_attr = msg_integrity_attr_ptr.get();
  AddAttribute(std::move(msg_integrity_attr_ptr));

  // The HMAC of the message.
  ByteBufferWriter buf;
  if (!Write(&buf)) return false;
  size_t msg_len_for_hmac = buf.Length() - kStunAttributeHeaderSize - msg_integrity_attr->length();
  uint8_t hmac[kStunMessageIntegritySize];
  if (!ComputeHmacSha1(key, buf.DataView().first(msg_len_for_hmac), hmac)) {
    // As libwebrtc: the attribute keeps its dummy value.
    return false;
  }
  msg_integrity_attr->CopyBytes(std::span<const uint8_t>(hmac, attr_size));
  password_ = std::string(key);
  integrity_ = IntegrityStatus::kIntegrityOk;
  return true;
}

// RFC 5389 section 7.3, with the FINGERPRINT check of section 15.5.
bool StunMessage::ValidateFingerprint(std::span<const uint8_t> data) {
  size_t fingerprint_attr_size = kStunAttributeHeaderSize + StunUInt32Attribute::SIZE;
  if (data.size() % 4 != 0 || data.size() < kStunHeaderSize + fingerprint_attr_size) return false;
  // The magic cookie.
  size_t magic_cookie_offset = kStunTransactionIdOffset - kStunMagicCookieLength;
  if (GetBE32(data.subspan(magic_cookie_offset, 4)) != kStunMagicCookie) return false;
  // The fingerprint's type and length.
  size_t fingerprint_attr_offset = data.size() - fingerprint_attr_size;
  if (GetBE16(data.subspan(fingerprint_attr_offset, 2)) != STUN_ATTR_FINGERPRINT ||
      GetBE16(data.subspan(fingerprint_attr_offset + 2, 2)) != StunUInt32Attribute::SIZE)
    return false;
  // Its value.
  uint32_t fingerprint = GetBE32(data.subspan(fingerprint_attr_offset + kStunAttributeHeaderSize, 4));
  return ((fingerprint ^ STUN_FINGERPRINT_XOR_VALUE) == ComputeCrc32(data.first(data.size() - fingerprint_attr_size)));
}

std::string StunMessage::GenerateTransactionId() { return CreateRandomString(kStunTransactionIdLength); }

bool StunMessage::IsStunMethod(std::span<const int> methods, std::span<const uint8_t> data) {
  if (data.size() % 4 != 0 || data.size() < kStunHeaderSize) return false;
  size_t magic_cookie_offset = kStunTransactionIdOffset - kStunMagicCookieLength;
  if (GetBE32(data.subspan(magic_cookie_offset, 4)) != kStunMagicCookie) return false;
  int method = GetBE16(data);
  for (int m : methods) {
    if (m == method) return true;
  }
  return false;
}

bool StunMessage::AddFingerprint() {
  // Add the attribute with a dummy value; a known attribute, so it can't fail.
  auto fingerprint_attr_ptr = std::make_unique<StunUInt32Attribute>(STUN_ATTR_FINGERPRINT, 0);
  auto* fingerprint_attr = fingerprint_attr_ptr.get();
  AddAttribute(std::move(fingerprint_attr_ptr));

  ByteBufferWriter buf;
  if (!Write(&buf)) return false;
  size_t msg_len_for_crc32 = buf.Length() - kStunAttributeHeaderSize - fingerprint_attr->length();
  uint32_t c = ComputeCrc32(buf.DataView().first(msg_len_for_crc32));
  // The CRC-32 XORed with a constant.
  fingerprint_attr->SetValue(c ^ STUN_FINGERPRINT_XOR_VALUE);
  return true;
}

bool StunMessage::Read(ByteBufferReader* buf) {
  // A copy of the bytes for later integrity checks.
  buffer_.assign(buf->DataView().begin(), buf->DataView().end());

  if (!buf->ReadUInt16(&type_)) return false;
  if (type_ & 0x8000) {
    // RTP and RTCP set the first bit (version 2); not STUN.
    return false;
  }
  if (!buf->ReadUInt16(&length_)) return false;

  std::string_view magic_cookie;
  if (!buf->ReadStringView(&magic_cookie, kStunMagicCookieLength)) return false;

  std::string transaction_id;
  if (!buf->ReadString(&transaction_id, kStunTransactionIdLength)) return false;

  uint32_t magic_cookie_int = GetBE32(AsUint8Span(magic_cookie));
  if (magic_cookie_int != kStunMagicCookie) {
    // No magic cookie: the peer implements RFC 3489, not RFC 5389.
    transaction_id.insert(0, magic_cookie);
  }
  transaction_id_ = transaction_id;
  reduced_transaction_id_ = ReduceTransactionId(transaction_id_);

  if (length_ != buf->Length()) return false;

  attrs_.resize(0);

  size_t rest = buf->Length() - length_;
  while (buf->Length() > rest) {
    uint16_t attr_type, attr_length;
    if (!buf->ReadUInt16(&attr_type)) return false;
    if (!buf->ReadUInt16(&attr_length)) return false;

    std::unique_ptr<StunAttribute> attr(CreateAttribute(attr_type, attr_length));
    if (!attr) {
      // Skip unknown or malformed attributes.
      size_t skip = attr_length;
      if ((skip % 4) != 0) skip += (4 - (skip % 4));
      if (!buf->Consume(skip)) return false;
    } else {
      if (!attr->Read(buf)) return false;
      attrs_.push_back(std::move(attr));
    }
  }
  return true;
}

bool StunMessage::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt16(type_);
  buf->WriteUInt16(length_);
  if (!IsLegacy()) buf->WriteUInt32(stun_magic_cookie_);
  buf->WriteString(transaction_id_);

  for (const auto& attr : attrs_) {
    buf->WriteUInt16(static_cast<uint16_t>(attr->type()));
    buf->WriteUInt16(static_cast<uint16_t>(attr->length()));
    if (!attr->Write(buf)) return false;
  }
  return true;
}

StunMessage* StunMessage::CreateNew() const { return new StunMessage(); }

void StunMessage::SetStunMagicCookie(uint32_t val) { stun_magic_cookie_ = val; }

void StunMessage::SetTransactionIdForTesting(std::string_view transaction_id) {
  transaction_id_ = std::string(transaction_id);
  reduced_transaction_id_ = ReduceTransactionId(transaction_id_);
}

StunAttributeValueType StunMessage::GetAttributeValueType(int type) const {
  switch (type) {
    case STUN_ATTR_MAPPED_ADDRESS:
      return STUN_VALUE_ADDRESS;
    case STUN_ATTR_USERNAME:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_MESSAGE_INTEGRITY:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_ERROR_CODE:
      return STUN_VALUE_ERROR_CODE;
    case STUN_ATTR_UNKNOWN_ATTRIBUTES:
      return STUN_VALUE_UINT16_LIST;
    case STUN_ATTR_REALM:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_NONCE:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_XOR_MAPPED_ADDRESS:
      return STUN_VALUE_XOR_ADDRESS;
    case STUN_ATTR_SOFTWARE:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_ALTERNATE_SERVER:
      return STUN_VALUE_ADDRESS;
    case STUN_ATTR_FINGERPRINT:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_RETRANSMIT_COUNT:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_GOOG_LAST_ICE_CHECK_RECEIVED:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_GOOG_MISC_INFO:
      return STUN_VALUE_UINT16_LIST;
    case STUN_ATTR_GOOG_DELTA:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_GOOG_DELTA_ACK:
      return STUN_VALUE_UINT64;
    default:
      return STUN_VALUE_UNKNOWN;
  }
}

StunAttribute* StunMessage::CreateAttribute(int type, size_t length) {
  StunAttributeValueType value_type = GetAttributeValueType(type);
  if (value_type != STUN_VALUE_UNKNOWN) {
    return StunAttribute::Create(value_type, static_cast<uint16_t>(type), static_cast<uint16_t>(length), this);
  } else if (DesignatedExpertRange(type)) {
    // Unknown attributes in these ranges are read as byte strings.
    return StunAttribute::Create(STUN_VALUE_BYTE_STRING, static_cast<uint16_t>(type), static_cast<uint16_t>(length),
                                 this);
  }
  return nullptr;
}

const StunAttribute* StunMessage::GetAttribute(int type) const {
  for (const auto& attr : attrs_) {
    if (attr->type() == type) return attr.get();
  }
  return nullptr;
}

bool StunMessage::IsValidTransactionId(std::string_view transaction_id) {
  return transaction_id.size() == kStunTransactionIdLength || transaction_id.size() == kStunLegacyTransactionIdLength;
}

bool StunMessage::EqualAttributes(const StunMessage* other,
                                  const std::function<bool(int type)>& attribute_type_mask) const {
  ByteBufferWriter tmp_buffer_ptr1;
  ByteBufferWriter tmp_buffer_ptr2;
  for (const auto& attr : attrs_) {
    if (attribute_type_mask(attr->type())) {
      const StunAttribute* other_attr = other->GetAttribute(attr->type());
      if (other_attr == nullptr) return false;
      tmp_buffer_ptr1.Clear();
      tmp_buffer_ptr2.Clear();
      attr->Write(&tmp_buffer_ptr1);
      other_attr->Write(&tmp_buffer_ptr2);
      if (tmp_buffer_ptr1.Length() != tmp_buffer_ptr2.Length()) return false;
      if (tmp_buffer_ptr1.Length() > 0 &&
          std::memcmp(tmp_buffer_ptr1.Data(), tmp_buffer_ptr2.Data(), tmp_buffer_ptr1.Length()) != 0)
        return false;
    }
  }
  for (const auto& attr : other->attrs_) {
    if (attribute_type_mask(attr->type())) {
      if (GetAttribute(attr->type()) == nullptr) return false;
      // The values were compared above.
    }
  }
  return true;
}

std::unique_ptr<StunMessage> StunMessage::Clone() const {
  std::unique_ptr<StunMessage> copy(CreateNew());
  if (!copy) return nullptr;
  ByteBufferWriter buf;
  if (!Write(&buf)) return nullptr;
  ByteBufferReader reader(buf);
  if (!copy->Read(&reader)) return nullptr;
  return copy;
}

// ---------------------------------------------------------------- attributes

void StunAttribute::ConsumePadding(ByteBufferReader* buf) const {
  int remainder = length_ % 4;
  if (remainder > 0) buf->Consume(static_cast<size_t>(4 - remainder));
}

void StunAttribute::WritePadding(ByteBufferWriter* buf) const {
  int remainder = length_ % 4;
  if (remainder > 0) {
    uint8_t zeroes[4] = {0};
    buf->Write(std::span<const uint8_t>(zeroes, static_cast<size_t>(4 - remainder)));
  }
}

StunAttribute* StunAttribute::Create(StunAttributeValueType value_type, uint16_t type, uint16_t length,
                                     StunMessage* owner) {
  switch (value_type) {
    case STUN_VALUE_ADDRESS:
      return new StunAddressAttribute(type, length);
    case STUN_VALUE_XOR_ADDRESS:
      return new StunXorAddressAttribute(type, length, owner);
    case STUN_VALUE_UINT32:
      return new StunUInt32Attribute(type);
    case STUN_VALUE_UINT64:
      return new StunUInt64Attribute(type);
    case STUN_VALUE_BYTE_STRING:
      return new StunByteStringAttribute(type, length);
    case STUN_VALUE_ERROR_CODE:
      return new StunErrorCodeAttribute(type, length);
    case STUN_VALUE_UINT16_LIST:
      return new StunUInt16ListAttribute(type, length);
    default:
      return nullptr;
  }
}

std::unique_ptr<StunAddressAttribute> StunAttribute::CreateAddress(uint16_t type) {
  return std::make_unique<StunAddressAttribute>(type, uint16_t{0});
}

std::unique_ptr<StunXorAddressAttribute> StunAttribute::CreateXorAddress(uint16_t type) {
  return std::make_unique<StunXorAddressAttribute>(type, uint16_t{0}, nullptr);
}

std::unique_ptr<StunUInt64Attribute> StunAttribute::CreateUInt64(uint16_t type) {
  return std::make_unique<StunUInt64Attribute>(type);
}

std::unique_ptr<StunUInt32Attribute> StunAttribute::CreateUInt32(uint16_t type) {
  return std::make_unique<StunUInt32Attribute>(type);
}

std::unique_ptr<StunByteStringAttribute> StunAttribute::CreateByteString(uint16_t type) {
  return std::make_unique<StunByteStringAttribute>(type, uint16_t{0});
}

std::unique_ptr<StunErrorCodeAttribute> StunAttribute::CreateErrorCode() {
  return std::make_unique<StunErrorCodeAttribute>(STUN_ATTR_ERROR_CODE, StunErrorCodeAttribute::MIN_SIZE);
}

std::unique_ptr<StunUInt16ListAttribute> StunAttribute::CreateUInt16ListAttribute(uint16_t type) {
  return std::make_unique<StunUInt16ListAttribute>(type, uint16_t{0});
}

std::unique_ptr<StunUInt16ListAttribute> StunAttribute::CreateUnknownAttributes() {
  return std::make_unique<StunUInt16ListAttribute>(STUN_ATTR_UNKNOWN_ATTRIBUTES, uint16_t{0});
}

// Address.

StunAddressAttribute::StunAddressAttribute(uint16_t type, const SocketAddress& addr) : StunAttribute(type, 0) {
  SetAddress(addr);
}

StunAddressAttribute::StunAddressAttribute(uint16_t type, uint16_t length) : StunAttribute(type, length) {}

StunAttributeValueType StunAddressAttribute::value_type() const { return STUN_VALUE_ADDRESS; }

bool StunAddressAttribute::Read(ByteBufferReader* buf) {
  uint8_t dummy;
  if (!buf->ReadUInt8(&dummy)) return false;
  uint8_t stun_family;
  if (!buf->ReadUInt8(&stun_family)) return false;
  uint16_t port;
  if (!buf->ReadUInt16(&port)) return false;
  if (stun_family == STUN_ADDRESS_IPV4) {
    uint8_t v4addr[4];
    if (length() != SIZE_IP4) return false;
    if (!buf->ReadBytes(v4addr)) return false;
    SetAddress(SocketAddress(IpAddress::V4(v4addr[0], v4addr[1], v4addr[2], v4addr[3]), port));
  } else if (stun_family == STUN_ADDRESS_IPV6) {
    std::array<uint8_t, 16> v6addr;
    if (length() != SIZE_IP6) return false;
    if (!buf->ReadBytes(v6addr)) return false;
    SetAddress(SocketAddress(IpAddress::V6(v6addr), port));
  } else {
    return false;
  }
  return true;
}

bool StunAddressAttribute::Write(ByteBufferWriter* buf) const {
  StunAddressFamily address_family = family();
  if (address_family == STUN_ADDRESS_UNDEF) return false;
  buf->WriteUInt8(0);
  buf->WriteUInt8(static_cast<uint8_t>(address_family));
  buf->WriteUInt16(address_.port());
  buf->Write(AddressBytes(address_.ipaddr()));
  return true;
}

// XOR address.

StunXorAddressAttribute::StunXorAddressAttribute(uint16_t type, const SocketAddress& addr)
    : StunAddressAttribute(type, addr), owner_(nullptr) {}

StunXorAddressAttribute::StunXorAddressAttribute(uint16_t type, uint16_t length, StunMessage* owner)
    : StunAddressAttribute(type, length), owner_(owner) {}

StunAttributeValueType StunXorAddressAttribute::value_type() const { return STUN_VALUE_XOR_ADDRESS; }

void StunXorAddressAttribute::SetOwner(StunMessage* owner) { owner_ = owner; }

IpAddress StunXorAddressAttribute::GetXoredIP() const {
  if (owner_) {
    const IpAddress& ip = ipaddr();
    const std::vector<uint8_t> bytes = AddressBytes(ip);
    const uint8_t cookie[4] = {static_cast<uint8_t>(kStunMagicCookie >> 24),
                               static_cast<uint8_t>(kStunMagicCookie >> 16),
                               static_cast<uint8_t>(kStunMagicCookie >> 8), static_cast<uint8_t>(kStunMagicCookie)};
    switch (ip.family()) {
      case IpFamily::kV4: {
        uint8_t b[4];
        for (int i = 0; i < 4; ++i) b[i] = bytes[i] ^ cookie[i];
        return IpAddress::V4(b[0], b[1], b[2], b[3]);
      }
      case IpFamily::kV6: {
        const std::string& transaction_id = owner_->transaction_id();
        if (transaction_id.length() == kStunTransactionIdLength) {
          // Magic cookie, then the transaction id, both in network order.
          std::array<uint8_t, 16> b;
          for (size_t i = 0; i < 4; ++i) b[i] = bytes[i] ^ cookie[i];
          for (size_t i = 0; i < 12; ++i) b[4 + i] = bytes[4 + i] ^ static_cast<uint8_t>(transaction_id[i]);
          return IpAddress::V6(b);
        }
        break;
      }
      default:
        break;
    }
  }
  // Bad family or transaction id, or no owner.
  return IpAddress();
}

bool StunXorAddressAttribute::Read(ByteBufferReader* buf) {
  if (!StunAddressAttribute::Read(buf)) return false;
  uint16_t xoredport = static_cast<uint16_t>(port() ^ (kStunMagicCookie >> 16));
  IpAddress xored_ip = GetXoredIP();
  SetAddress(SocketAddress(xored_ip, xoredport));
  return true;
}

bool StunXorAddressAttribute::Write(ByteBufferWriter* buf) const {
  StunAddressFamily address_family = family();
  if (address_family == STUN_ADDRESS_UNDEF) return false;
  IpAddress xored_ip = GetXoredIP();
  if (xored_ip.family() == IpFamily::kUnspec) return false;
  buf->WriteUInt8(0);
  buf->WriteUInt8(static_cast<uint8_t>(family()));
  buf->WriteUInt16(static_cast<uint16_t>(port() ^ (kStunMagicCookie >> 16)));
  buf->Write(AddressBytes(xored_ip));
  return true;
}

// UInt32.

StunUInt32Attribute::StunUInt32Attribute(uint16_t type, uint32_t value) : StunAttribute(type, SIZE), bits_(value) {}

StunUInt32Attribute::StunUInt32Attribute(uint16_t type) : StunAttribute(type, SIZE), bits_(0) {}

StunAttributeValueType StunUInt32Attribute::value_type() const { return STUN_VALUE_UINT32; }

bool StunUInt32Attribute::GetBit(size_t index) const { return ((bits_ >> index) & 0x1) != 0; }

void StunUInt32Attribute::SetBit(size_t index, bool value) {
  bits_ &= ~(1u << index);
  bits_ |= value ? (1u << index) : 0;
}

bool StunUInt32Attribute::Read(ByteBufferReader* buf) { return length() == SIZE && buf->ReadUInt32(&bits_); }

bool StunUInt32Attribute::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt32(bits_);
  return true;
}

// UInt64.

StunUInt64Attribute::StunUInt64Attribute(uint16_t type, uint64_t value) : StunAttribute(type, SIZE), bits_(value) {}

StunUInt64Attribute::StunUInt64Attribute(uint16_t type) : StunAttribute(type, SIZE), bits_(0) {}

StunAttributeValueType StunUInt64Attribute::value_type() const { return STUN_VALUE_UINT64; }

bool StunUInt64Attribute::Read(ByteBufferReader* buf) { return length() == SIZE && buf->ReadUInt64(&bits_); }

bool StunUInt64Attribute::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt64(bits_);
  return true;
}

// Byte string.

StunByteStringAttribute::StunByteStringAttribute(uint16_t type) : StunAttribute(type, 0) {}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type, std::string_view str) : StunAttribute(type, 0) {
  CopyBytes(str);
}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type, std::span<const uint8_t> bytes)
    : StunAttribute(type, 0) {
  CopyBytes(bytes);
}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type, const std::vector<uint32_t>& values)
    : StunAttribute(type, 0) {
  ByteBufferWriter writer;
  for (const auto& value : values) writer.WriteUInt32(value);
  CopyBytes(writer.DataView());
}

StunByteStringAttribute::StunByteStringAttribute(uint16_t type, uint16_t length) : StunAttribute(type, length) {}

StunAttributeValueType StunByteStringAttribute::value_type() const { return STUN_VALUE_BYTE_STRING; }

std::optional<std::vector<uint32_t>> StunByteStringAttribute::GetUInt32Vector() const {
  if (length() % 4 != 0) return std::nullopt;
  std::vector<uint32_t> values;
  ByteBufferReader reader(array_view());
  uint32_t value;
  while (reader.ReadUInt32(&value)) values.push_back(value);
  return values;
}

void StunByteStringAttribute::CopyBytes(std::string_view bytes) { CopyBytes(AsUint8Span(bytes)); }

void StunByteStringAttribute::CopyBytes(std::span<const uint8_t> bytes) {
  bytes_.assign(bytes.begin(), bytes.end());
  SetLength(static_cast<uint16_t>(bytes.size()));
}

bool StunByteStringAttribute::Read(ByteBufferReader* buf) {
  bytes_.resize(length());
  if (!buf->ReadBytes(bytes_)) return false;
  ConsumePadding(buf);
  return true;
}

bool StunByteStringAttribute::Write(ByteBufferWriter* buf) const {
  // The length must be legal for the type.
  if (!LengthValid(type(), static_cast<int>(length()))) return false;
  buf->Write(array_view());
  WritePadding(buf);
  return true;
}

// Error code.

const uint16_t StunErrorCodeAttribute::MIN_SIZE = 4;

StunErrorCodeAttribute::StunErrorCodeAttribute(uint16_t type, int code, const std::string& reason)
    : StunAttribute(type, 0) {
  SetCode(code);
  SetReason(reason);
}

StunErrorCodeAttribute::StunErrorCodeAttribute(uint16_t type, uint16_t length) : StunAttribute(type, length) {}

StunAttributeValueType StunErrorCodeAttribute::value_type() const { return STUN_VALUE_ERROR_CODE; }

int StunErrorCodeAttribute::code() const { return class_ * 100 + number_; }

void StunErrorCodeAttribute::SetCode(int code) {
  class_ = static_cast<uint8_t>(code / 100);
  number_ = static_cast<uint8_t>(code % 100);
}

void StunErrorCodeAttribute::SetReason(const std::string& reason) {
  SetLength(static_cast<uint16_t>(MIN_SIZE + reason.size()));
  reason_ = reason;
}

bool StunErrorCodeAttribute::Read(ByteBufferReader* buf) {
  uint32_t val;
  if (length() < MIN_SIZE || !buf->ReadUInt32(&val)) return false;
  // libwebrtc logs (and ignores) error-code bits above bit 10.
  class_ = static_cast<uint8_t>((val >> 8) & 0x7);
  number_ = static_cast<uint8_t>(val & 0xff);
  if (!buf->ReadString(&reason_, length() - 4)) return false;
  ConsumePadding(buf);
  return true;
}

bool StunErrorCodeAttribute::Write(ByteBufferWriter* buf) const {
  buf->WriteUInt32(static_cast<uint32_t>(class_ << 8 | number_));
  buf->WriteString(reason_);
  WritePadding(buf);
  return true;
}

// UInt16 list.

StunUInt16ListAttribute::StunUInt16ListAttribute(uint16_t type, uint16_t length) : StunAttribute(type, length) {}

StunAttributeValueType StunUInt16ListAttribute::value_type() const { return STUN_VALUE_UINT16_LIST; }

void StunUInt16ListAttribute::AddType(uint16_t value) {
  attr_types_.push_back(value);
  SetLength(static_cast<uint16_t>(attr_types_.size() * 2));
}

void StunUInt16ListAttribute::AddTypeAtIndex(uint16_t index, uint16_t value) {
  if (attr_types_.size() < static_cast<size_t>(index + 1)) attr_types_.resize(index + 1);
  attr_types_[index] = value;
  SetLength(static_cast<uint16_t>(attr_types_.size() * 2));
}

bool StunUInt16ListAttribute::Read(ByteBufferReader* buf) {
  if (length() % 2) return false;
  for (size_t i = 0; i < length() / 2; i++) {
    uint16_t attr;
    if (!buf->ReadUInt16(&attr)) return false;
    attr_types_.push_back(attr);
  }
  // RFC 5389 padding at the end (RFC 3489 repeated an entry instead).
  ConsumePadding(buf);
  return true;
}

bool StunUInt16ListAttribute::Write(ByteBufferWriter* buf) const {
  for (uint16_t t : attr_types_) buf->WriteUInt16(t);
  WritePadding(buf);
  return true;
}

// ---------------------------------------------------------------- helpers

std::string StunMethodToString(int msg_type) {
  switch (msg_type) {
    case STUN_BINDING_REQUEST:
      return "STUN BINDING request";
    case STUN_BINDING_INDICATION:
      return "STUN BINDING indication";
    case STUN_BINDING_RESPONSE:
      return "STUN BINDING response";
    case STUN_BINDING_ERROR_RESPONSE:
      return "STUN BINDING error response";
    case GOOG_PING_REQUEST:
      return "GOOG PING request";
    case GOOG_PING_RESPONSE:
      return "GOOG PING response";
    case GOOG_PING_ERROR_RESPONSE:
      return "GOOG PING error response";
    case STUN_ALLOCATE_REQUEST:
      return "TURN ALLOCATE request";
    case STUN_ALLOCATE_RESPONSE:
      return "TURN ALLOCATE response";
    case STUN_ALLOCATE_ERROR_RESPONSE:
      return "TURN ALLOCATE error response";
    case TURN_REFRESH_REQUEST:
      return "TURN REFRESH request";
    case TURN_REFRESH_RESPONSE:
      return "TURN REFRESH response";
    case TURN_REFRESH_ERROR_RESPONSE:
      return "TURN REFRESH error response";
    case TURN_SEND_INDICATION:
      return "TURN SEND INDICATION";
    case TURN_DATA_INDICATION:
      return "TURN DATA INDICATION";
    case TURN_CREATE_PERMISSION_REQUEST:
      return "TURN CREATE PERMISSION request";
    case TURN_CREATE_PERMISSION_RESPONSE:
      return "TURN CREATE PERMISSION response";
    case TURN_CREATE_PERMISSION_ERROR_RESPONSE:
      return "TURN CREATE PERMISSION error response";
    case TURN_CHANNEL_BIND_REQUEST:
      return "TURN CHANNEL BIND request";
    case TURN_CHANNEL_BIND_RESPONSE:
      return "TURN CHANNEL BIND response";
    case TURN_CHANNEL_BIND_ERROR_RESPONSE:
      return "TURN CHANNEL BIND error response";
    default:
      return "UNKNOWN<" + std::to_string(msg_type) + ">";
  }
}

int GetStunSuccessResponseType(int req_type) { return IsStunRequestType(req_type) ? (req_type | 0x100) : -1; }

int GetStunErrorResponseType(int req_type) { return IsStunRequestType(req_type) ? (req_type | 0x110) : -1; }

bool IsStunRequestType(int msg_type) { return ((msg_type & kStunTypeMask) == 0x000); }

bool IsStunIndicationType(int msg_type) { return ((msg_type & kStunTypeMask) == 0x010); }

bool IsStunSuccessResponseType(int msg_type) { return ((msg_type & kStunTypeMask) == 0x100); }

bool IsStunErrorResponseType(int msg_type) { return ((msg_type & kStunTypeMask) == 0x110); }

bool ComputeStunCredentialHash(const std::string& username, const std::string& realm, const std::string& password,
                               std::string* hash) {
  // RFC 5389 15.4: key = MD5(username ":" realm ":" SASLprep(password)).
  std::string input = username;
  input += ':';
  input += realm;
  input += ':';
  input += password;
  uint8_t digest[16];
  if (g_crypto.md5 == nullptr || !g_crypto.md5(AsUint8Span(input), digest)) return false;
  *hash = std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
  return true;
}

std::unique_ptr<StunAttribute> CopyStunAttribute(const StunAttribute& attribute, ByteBufferWriter* tmp_buffer_ptr) {
  ByteBufferWriter tmpBuffer;
  if (tmp_buffer_ptr == nullptr) tmp_buffer_ptr = &tmpBuffer;
  std::unique_ptr<StunAttribute> copy(StunAttribute::Create(attribute.value_type(),
                                                            static_cast<uint16_t>(attribute.type()),
                                                            static_cast<uint16_t>(attribute.length()), nullptr));
  if (!copy) return nullptr;
  tmp_buffer_ptr->Clear();
  if (!attribute.Write(tmp_buffer_ptr)) return nullptr;
  ByteBufferReader reader(*tmp_buffer_ptr);
  if (!copy->Read(&reader)) return nullptr;
  return copy;
}

StunAttributeValueType TurnMessage::GetAttributeValueType(int type) const {
  switch (type) {
    case STUN_ATTR_CHANNEL_NUMBER:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_LIFETIME:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_XOR_PEER_ADDRESS:
      return STUN_VALUE_XOR_ADDRESS;
    case STUN_ATTR_DATA:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_XOR_RELAYED_ADDRESS:
      return STUN_VALUE_XOR_ADDRESS;
    case STUN_ATTR_EVEN_PORT:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_REQUESTED_TRANSPORT:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_DONT_FRAGMENT:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_RESERVATION_TOKEN:
      return STUN_VALUE_BYTE_STRING;
    default:
      return StunMessage::GetAttributeValueType(type);
  }
}

StunMessage* TurnMessage::CreateNew() const { return new TurnMessage(); }

StunAttributeValueType IceMessage::GetAttributeValueType(int type) const {
  switch (type) {
    case STUN_ATTR_PRIORITY:
    case STUN_ATTR_GOOG_NETWORK_INFO:
    case STUN_ATTR_NOMINATION:
      return STUN_VALUE_UINT32;
    case STUN_ATTR_USE_CANDIDATE:
      return STUN_VALUE_BYTE_STRING;
    case STUN_ATTR_ICE_CONTROLLED:
      return STUN_VALUE_UINT64;
    case STUN_ATTR_ICE_CONTROLLING:
      return STUN_VALUE_UINT64;
    default:
      return StunMessage::GetAttributeValueType(type);
  }
}

StunMessage* IceMessage::CreateNew() const { return new IceMessage(); }

}  // namespace sm::p2p
