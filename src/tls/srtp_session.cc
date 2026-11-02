// libwebrtc's pc/srtp_session.cc (M157) on libsrtp 2.8.0; see include/sm/tls/srtp_session.h. The code follows
// libwebrtc's line for line; the comments name the libwebrtc function each part ports.
#include "sm/tls/srtp_session.h"

#include <openssl/mem.h>
#include <srtp.h>

#include <cstring>
#include <mutex>
#include <optional>

#include "sm/tls/dtls_transport.h"

namespace sm::tls {

namespace {

constexpr uint16_t kRtpOneByteHeaderExtensionProfile = 0xBEDE;
constexpr uint16_t kRtpTwoByteHeaderExtensionProfile = 0x1000;
constexpr size_t kMinRtpPacketLen = 12;
constexpr size_t kSrtcpIndexLen = 4;  // SRTP_SRCTP_INDEX_LEN

// modules/rtp_rtcp/source/rtp_util.cc (not in compare/webrtc/reference/; RFC 3550 5.1 and RFC 8285 4.2).
uint16_t ParseRtpSequenceNumber(std::span<const uint8_t> p) {
  return p.size() >= 4 ? static_cast<uint16_t>(p[2] << 8 | p[3]) : 0;
}
uint32_t ParseRtpSsrc(std::span<const uint8_t> p) {
  return p.size() >= 12 ? (uint32_t{p[8]} << 24 | uint32_t{p[9]} << 16 | uint32_t{p[10]} << 8 | p[11]) : 0;
}
std::optional<uint16_t> ParseRtpExtensionProfile(std::span<const uint8_t> p) {
  if (p.size() < kMinRtpPacketLen || !(p[0] & 0x10)) return std::nullopt;
  const size_t offset = kMinRtpPacketLen + 4 * (p[0] & 0x0F);
  if (p.size() < offset + 2) return std::nullopt;
  return static_cast<uint16_t>(p[offset] << 8 | p[offset + 1]);
}

// LibSrtpInitializer: libsrtp is process-global; it is initialized when the usage count goes 0 -> 1 and shut down
// when it goes back to 0.
class LibSrtpInitializer {
 public:
  static LibSrtpInitializer& Get() {
    static LibSrtpInitializer* const instance = new LibSrtpInitializer();
    return *instance;
  }
  void ProhibitLibsrtpInitialization() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++usage_count_;
  }
  bool IncrementLibsrtpUsageCountAndMaybeInit(srtp_event_handler_func_t* event_handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (usage_count_ == 0) {
      // libwebrtc installs a log handler that forwards to RTC_LOG; the library doesn't log, so none is installed.
      if (srtp_init() != srtp_err_status_ok) return false;
      if (srtp_install_event_handler(event_handler) != srtp_err_status_ok) return false;
    }
    ++usage_count_;
    return true;
  }
  void DecrementLibsrtpUsageCountAndMaybeDeinit() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (--usage_count_ == 0) srtp_shutdown();
  }

 private:
  LibSrtpInitializer() = default;
  std::mutex mutex_;
  int usage_count_ = 0;
};

// HandleEventThunk / HandleEvent: libwebrtc only logs the events (SSRC collision, key limits, packet index
// limit); the library doesn't log, so the handler does nothing.
void HandleEventThunk(srtp_event_data_t*) {}

}  // namespace

void ProhibitLibsrtpInitialization() { LibSrtpInitializer::Get().ProhibitLibsrtpInitialization(); }

SrtpSession::~SrtpSession() {
  if (session_) {
    srtp_set_user_data(session_, nullptr);
    srtp_dealloc(session_);
  }
  if (inited_) LibSrtpInitializer::Get().DecrementLibsrtpUsageCountAndMaybeDeinit();
}

bool SrtpSession::UseCryptex(bool enable, bool require, bool sending_session) {
  if (session_) {
    srtp_ssrc_t ssrc;
    ssrc.type = sending_session ? ssrc_any_outbound : ssrc_any_inbound;
    ssrc.value = 0;
    if (srtp_set_stream_use_cryptex(session_, &ssrc, enable) != srtp_err_status_ok) return false;
  }
  use_cryptex_ = enable;
  require_cryptex_ = require;
  return true;
}

bool SrtpSession::SetSend(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids) {
  return SetKey(ssrc_any_outbound, crypto_suite, key, extension_ids);
}
bool SrtpSession::UpdateSend(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids) {
  return UpdateKey(ssrc_any_outbound, crypto_suite, key, extension_ids);
}
bool SrtpSession::SetReceive(int crypto_suite, std::span<const uint8_t> key, const std::vector<int>& extension_ids) {
  return SetKey(ssrc_any_inbound, crypto_suite, key, extension_ids);
}
bool SrtpSession::UpdateReceive(int crypto_suite, std::span<const uint8_t> key,
                                const std::vector<int>& extension_ids) {
  return UpdateKey(ssrc_any_inbound, crypto_suite, key, extension_ids);
}

bool SrtpSession::ProtectRtp(std::span<uint8_t> buffer, size_t& len, int64_t* index) {
  if (!session_ || len > buffer.size()) return false;
  // WebRTC never uses an MKI, so what srtp_protect adds is known from the suite: libwebrtc asks for exactly that
  // much room, not libsrtp's SRTP_MAX_TRAILER_LEN.
  if (buffer.size() < len + static_cast<size_t>(rtp_auth_tag_len_)) return false;
  int out_len = static_cast<int>(len);
  const int err = srtp_protect(session_, buffer.data(), &out_len);
  const int seq_num = ParseRtpSequenceNumber(buffer.first(len));
  if (err != srtp_err_status_ok) return false;
  len = static_cast<size_t>(out_len);
  last_send_seq_num_ = seq_num;
  return index ? GetSendStreamPacketIndex(buffer.first(len), index) : true;
}

bool SrtpSession::ProtectRtcp(std::span<uint8_t> buffer, size_t& len) {
  if (!session_ || len > buffer.size()) return false;
  if (buffer.size() < len + kSrtcpIndexLen + static_cast<size_t>(rtcp_auth_tag_len_)) return false;
  int out_len = static_cast<int>(len);
  if (srtp_protect_rtcp(session_, buffer.data(), &out_len) != srtp_err_status_ok) return false;
  len = static_cast<size_t>(out_len);
  return true;
}

bool SrtpSession::UnprotectRtp(std::span<uint8_t> buffer, size_t& len) {
  if (!session_ || len > buffer.size()) return false;
  int out_len = static_cast<int>(len);
  std::optional<uint16_t> extension_profile;
  if (require_cryptex_) {
    // After decryption this reads 0xBEDE or 0x1000, so it is parsed before.
    extension_profile = ParseRtpExtensionProfile(buffer.first(len));
  }
  int err = srtp_unprotect(session_, buffer.data(), &out_len);
  if (err == srtp_err_status_ok && require_cryptex_ &&
      (extension_profile == kRtpOneByteHeaderExtensionProfile ||
       extension_profile == kRtpTwoByteHeaderExtensionProfile)) {
    // Cryptex is required but the packet's extensions were sent in the clear (libwebrtc's extra check, until
    // libsrtp does it: bugs.webrtc.org/455813732).
    err = srtp_err_status_cryptex_err;
  }
  if (err != srtp_err_status_ok) {
    ++decryption_failure_count_;
    last_rtp_error_ = err;
    return false;
  }
  len = static_cast<size_t>(out_len);
  return true;
}

bool SrtpSession::UnprotectRtcp(std::span<uint8_t> buffer, size_t& len) {
  if (!session_ || len > buffer.size()) return false;
  int out_len = static_cast<int>(len);
  const int err = srtp_unprotect_rtcp(session_, buffer.data(), &out_len);
  if (err != srtp_err_status_ok) {
    ++rtcp_decryption_failure_count_;
    last_rtcp_error_ = err;
    return false;
  }
  len = static_cast<size_t>(out_len);
  return true;
}

bool SrtpSession::ProtectRtp(std::vector<uint8_t>& packet, int64_t* index) {
  size_t len = packet.size();
  packet.resize(len + static_cast<size_t>(rtp_auth_tag_len_));
  const bool ok = ProtectRtp(std::span<uint8_t>(packet), len, index);
  packet.resize(ok ? len : packet.size() - static_cast<size_t>(rtp_auth_tag_len_));
  return ok;
}
bool SrtpSession::ProtectRtcp(std::vector<uint8_t>& packet) {
  size_t len = packet.size();
  const size_t extra = kSrtcpIndexLen + static_cast<size_t>(rtcp_auth_tag_len_);
  packet.resize(len + extra);
  const bool ok = ProtectRtcp(std::span<uint8_t>(packet), len);
  packet.resize(ok ? len : packet.size() - extra);
  return ok;
}
bool SrtpSession::UnprotectRtp(std::vector<uint8_t>& packet) {
  size_t len = packet.size();
  if (!UnprotectRtp(std::span<uint8_t>(packet), len)) return false;
  packet.resize(len);
  return true;
}
bool SrtpSession::UnprotectRtcp(std::vector<uint8_t>& packet) {
  size_t len = packet.size();
  if (!UnprotectRtcp(std::span<uint8_t>(packet), len)) return false;
  packet.resize(len);
  return true;
}

bool SrtpSession::RemoveSsrcFromSession(uint32_t ssrc) {
  if (!session_) return false;
  // libsrtp takes the SSRC in network byte order (htonl).
  const uint8_t be[4] = {static_cast<uint8_t>(ssrc >> 24), static_cast<uint8_t>(ssrc >> 16),
                         static_cast<uint8_t>(ssrc >> 8), static_cast<uint8_t>(ssrc)};
  uint32_t net;
  std::memcpy(&net, be, 4);
  return srtp_remove_stream(session_, net) == srtp_err_status_ok;
}

bool SrtpSession::GetSendStreamPacketIndex(std::span<const uint8_t> packet, int64_t* index) {
  // srtp_get_stream_roc takes the SSRC in host order (libwebrtc passes ParseRtpSsrc's value).
  uint32_t roc;
  if (srtp_get_stream_roc(session_, ParseRtpSsrc(packet), &roc) != srtp_err_status_ok) return false;
  const uint64_t extended_seq_num = (uint64_t{roc} << 16) + ParseRtpSequenceNumber(packet);
  // Shifted left 16 bits and put in network byte order (NetworkToHost64(extended_seq_num << 16)).
  const uint64_t v = extended_seq_num << 16;
  uint8_t be[8];
  for (int i = 0; i < 8; ++i) be[i] = static_cast<uint8_t>(v >> (56 - 8 * i));
  uint64_t out;
  std::memcpy(&out, be, 8);
  *index = static_cast<int64_t>(out);
  return true;
}

bool SrtpSession::DoSetKey(int type, int crypto_suite, std::span<const uint8_t> key,
                           const std::vector<int>& extension_ids) {
  srtp_policy_t policy;
  std::memset(&policy, 0, sizeof(policy));
  if (!(srtp_crypto_policy_set_from_profile_for_rtp(&policy.rtp, static_cast<srtp_profile_t>(crypto_suite)) ==
            srtp_err_status_ok &&
        srtp_crypto_policy_set_from_profile_for_rtcp(&policy.rtcp, static_cast<srtp_profile_t>(crypto_suite)) ==
            srtp_err_status_ok)) {
    return false;  // Unsupported crypto suite.
  }
  if (key.size() != static_cast<size_t>(policy.rtp.cipher_key_len)) return false;

  policy.ssrc.type = static_cast<srtp_ssrc_type_t>(type);
  policy.ssrc.value = 0;
  policy.key = const_cast<uint8_t*>(key.data());
  policy.window_size = 1024;  // libwebrtc: "TODO(astor) parse window size from WSH session-param".
  policy.allow_repeat_tx = 1;
  // With both encrypted extension ids and cryptex, cryptex wins and libsrtp gets no ids.
  std::vector<int> ids;
  if (!extension_ids.empty() && !use_cryptex_) {
    ids = extension_ids;
    policy.enc_xtn_hdr = ids.data();
    policy.enc_xtn_hdr_count = static_cast<int>(ids.size());
  }
  policy.next = nullptr;

  if (!session_) {
    if (srtp_create(&session_, &policy) != srtp_err_status_ok) {
      session_ = nullptr;
      return false;
    }
    srtp_set_user_data(session_, this);
  } else if (srtp_update(session_, &policy) != srtp_err_status_ok) {
    return false;
  }
  if (srtp_set_stream_use_cryptex(session_, &policy.ssrc, use_cryptex_) != srtp_err_status_ok) return false;

  rtp_auth_tag_len_ = policy.rtp.auth_tag_len;
  rtcp_auth_tag_len_ = policy.rtcp.auth_tag_len;
  return true;
}

bool SrtpSession::SetKey(int type, int crypto_suite, std::span<const uint8_t> key,
                         const std::vector<int>& extension_ids) {
  if (session_) return false;  // Already created.
  // The first contact with libsrtp: initialize it if needed. libwebrtc increments the count again when SetKey is
  // retried after a failed key, and its destructor decrements once; here the count is taken once, so it balances.
  if (!inited_) {
    if (!LibSrtpInitializer::Get().IncrementLibsrtpUsageCountAndMaybeInit(&HandleEventThunk)) return false;
    inited_ = true;
  }
  return DoSetKey(type, crypto_suite, key, extension_ids);
}

bool SrtpSession::UpdateKey(int type, int crypto_suite, std::span<const uint8_t> key,
                            const std::vector<int>& extension_ids) {
  if (!session_) return false;
  return DoSetKey(type, crypto_suite, key, extension_ids);
}

SrtpKeys::~SrtpKeys() {
  OPENSSL_cleanse(send_key.data(), send_key.size());
  OPENSSL_cleanse(recv_key.data(), recv_key.size());
}

bool ExtractSrtpKeys(const DtlsTransport& dtls, SrtpKeys& keys) {
  if (dtls.state() != DtlsState::kConnected) return false;
  const int suite = dtls.srtp_crypto_suite();
  int key_len = 0, salt_len = 0;
  if (suite == 0 || !SrtpKeyAndSaltLength(suite, key_len, salt_len)) return false;
  // RFC 5705 exporter with RFC 5764's label: client_write_key | server_write_key | client_salt | server_salt.
  std::vector<uint8_t> material(static_cast<size_t>(2 * key_len + 2 * salt_len));
  if (!dtls.ExportSrtpKeyingMaterial(material)) {
    OPENSSL_cleanse(material.data(), material.size());
    return false;
  }
  const auto k = static_cast<size_t>(key_len), s = static_cast<size_t>(salt_len);
  std::vector<uint8_t> client(material.begin(), material.begin() + static_cast<long>(k));
  client.insert(client.end(), material.begin() + static_cast<long>(2 * k),
                material.begin() + static_cast<long>(2 * k + s));
  std::vector<uint8_t> server(material.begin() + static_cast<long>(k), material.begin() + static_cast<long>(2 * k));
  server.insert(server.end(), material.begin() + static_cast<long>(2 * k + s), material.end());
  OPENSSL_cleanse(material.data(), material.size());

  OPENSSL_cleanse(keys.send_key.data(), keys.send_key.size());
  OPENSSL_cleanse(keys.recv_key.data(), keys.recv_key.size());
  keys.crypto_suite = suite;
  if (dtls.role() == DtlsRole::kServer) {
    keys.send_key = std::move(server);
    keys.recv_key = std::move(client);
  } else {
    keys.send_key = std::move(client);
    keys.recv_key = std::move(server);
  }
  return true;
}

}  // namespace sm::tls
