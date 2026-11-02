// libwebrtc's pc/srtp_transport.cc (M157); see sm/pc/srtp_transport.h.
#include "sm/pc/srtp_transport.h"

#include <utility>

#include "sm/core/logging.h"

namespace sm::pc {
namespace {

// Limits the error logging when there are lots of bad packets.
constexpr int kFailureLogThrottleCount = 100;

uint16_t ParseRtpSequenceNumber(const std::vector<uint8_t>& p) {
  return p.size() >= 4 ? static_cast<uint16_t>((p[2] << 8) | p[3]) : 0;
}
uint32_t ParseRtpSsrc(const std::vector<uint8_t>& p) {
  return p.size() >= 12 ? (uint32_t{p[8]} << 24) | (uint32_t{p[9]} << 16) | (uint32_t{p[10]} << 8) | p[11] : 0;
}
int GetRtcpType(const std::vector<uint8_t>& p) { return p.size() >= 2 ? p[1] : -1; }

}  // namespace

SrtpTransport::SrtpTransport(bool rtcp_mux_enabled, SrtpTransportOptions options)
    : RtpTransport(rtcp_mux_enabled), options_(options) {}

SrtpTransport::~SrtpTransport() = default;

bool SrtpTransport::SendRtpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                                  Timestamp now) {
  if (!IsSrtpActive()) {
    SM_LOG(kError, "SrtpTransport: failed to send the packet because SRTP transport is inactive");
    return false;
  }
  if (!ProtectRtp(packet)) {
    SM_LOG(kError, "SrtpTransport: failed to protect RTP packet: size=%zu, seqnum=%u, SSRC=%u", packet.size(),
           ParseRtpSequenceNumber(packet), ParseRtpSsrc(packet));
    return false;
  }
  return RtpTransport::SendRtpPacket(packet, options, flags, now);
}

bool SrtpTransport::SendRtcpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                                   Timestamp now) {
  if (!IsSrtpActive()) {
    SM_LOG(kError, "SrtpTransport: failed to send the packet because SRTP transport is inactive");
    return false;
  }
  if (!ProtectRtcp(packet)) {
    SM_LOG(kError, "SrtpTransport: failed to protect RTCP packet: size=%zu, type=%d", packet.size(),
           GetRtcpType(packet));
    return false;
  }
  return RtpTransport::SendRtcpPacket(packet, options, flags, now);
}

void SrtpTransport::OnRtpPacketReceived(const p2p::ReceivedPacket& packet) {
  if (!IsSrtpActive()) {
    SM_LOG(kWarning, "SrtpTransport: inactive SRTP transport received an RTP packet; dropped");
    return;
  }
  std::vector<uint8_t> payload(packet.payload.begin(), packet.payload.end());
  if (!UnprotectRtp(payload)) {
    if (decryption_failure_count_ % kFailureLogThrottleCount == 0) {
      SM_LOG(kError,
             "SrtpTransport: failed to unprotect RTP packet: size=%zu, seqnum=%u, SSRC=%u, previous failure count: %d",
             payload.size(), ParseRtpSequenceNumber(payload), ParseRtpSsrc(payload), decryption_failure_count_);
    }
    ++decryption_failure_count_;
    return;
  }
  DemuxPacket(std::move(payload), packet.arrival_time);
}

void SrtpTransport::OnRtcpPacketReceived(const p2p::ReceivedPacket& packet) {
  if (!IsSrtpActive()) {
    SM_LOG(kWarning, "SrtpTransport: inactive SRTP transport received an RTCP packet; dropped");
    return;
  }
  std::vector<uint8_t> payload(packet.payload.begin(), packet.payload.end());
  if (!UnprotectRtcp(payload)) {
    if (rtcp_decryption_failure_count_ % kFailureLogThrottleCount == 0) {
      SM_LOG(kError, "SrtpTransport: failed to unprotect RTCP packet: size=%zu, type=%d, previous failure count: %d",
             payload.size(), GetRtcpType(payload), rtcp_decryption_failure_count_);
    }
    ++rtcp_decryption_failure_count_;
    return;
  }
  SendRtcpPacketReceived(payload, packet.arrival_time);
}

void SrtpTransport::OnWritableState(p2p::PacketTransportInternal* /*packet_transport*/) {
  SendWritableState(IsWritable(/*rtcp=*/false) && IsWritable(/*rtcp=*/true));
}

bool SrtpTransport::UseCryptex(bool enable, bool require) {
  enable_cryptex_ = enable;
  require_cryptex_ = require;
  if (send_session_ && !send_session_->UseCryptex(enable_cryptex_, require_cryptex_, /*sending_session=*/true)) {
    SM_LOG(kError, "SrtpTransport: updating send session cryptex failed");
    return false;
  }
  if (recv_session_ && !recv_session_->UseCryptex(enable_cryptex_, require_cryptex_, /*sending_session=*/false)) {
    SM_LOG(kError, "SrtpTransport: updating recv session cryptex failed");
    return false;
  }
  return true;
}

bool SrtpTransport::SetRtpParams(int send_crypto_suite, const std::vector<uint8_t>& send_key,
                                 const std::vector<int>& send_extension_ids, int recv_crypto_suite,
                                 const std::vector<uint8_t>& recv_key, const std::vector<int>& recv_extension_ids) {
  // Parameters set for the first time create new sessions (SetSend/SetReceive); later ones update the existing
  // sessions (UpdateSend/UpdateReceive, srtp_update).
  bool new_sessions = false;
  if (!send_session_) {
    CreateSrtpSessions();
    new_sessions = true;
  }
  if (!send_session_->UseCryptex(enable_cryptex_, require_cryptex_, /*sending_session=*/true)) {
    SM_LOG(kError, "SrtpTransport: updating send session cryptex failed");
    return false;
  }
  bool ret = new_sessions ? send_session_->SetSend(send_crypto_suite, send_key, send_extension_ids)
                          : send_session_->UpdateSend(send_crypto_suite, send_key, send_extension_ids);
  if (!ret) {
    ResetParams();
    return false;
  }
  if (!recv_session_->UseCryptex(enable_cryptex_, require_cryptex_, /*sending_session=*/false)) {
    SM_LOG(kError, "SrtpTransport: updating recv session cryptex failed");
    return false;
  }
  ret = new_sessions ? recv_session_->SetReceive(recv_crypto_suite, recv_key, recv_extension_ids)
                     : recv_session_->UpdateReceive(recv_crypto_suite, recv_key, recv_extension_ids);
  if (!ret) {
    ResetParams();
    return false;
  }
  SM_LOG(kInfo, "SRTP %s with negotiated parameters: send crypto_suite %d recv crypto_suite %d cryptex %d/%d",
         new_sessions ? "activated" : "updated", send_crypto_suite, recv_crypto_suite, enable_cryptex_,
         require_cryptex_);
  MaybeUpdateWritableState();
  return true;
}

bool SrtpTransport::SetRtcpParams(int send_crypto_suite, const std::vector<uint8_t>& send_key,
                                  const std::vector<int>& send_extension_ids, int recv_crypto_suite,
                                  const std::vector<uint8_t>& recv_key, const std::vector<int>& recv_extension_ids) {
  // Only once, but safe after SetRtpParams.
  if (send_rtcp_session_ || recv_rtcp_session_) {
    SM_LOG(kError, "SrtpTransport: tried to set SRTCP params when filter already active");
    return false;
  }
  send_rtcp_session_ = std::make_unique<tls::SrtpSession>();
  if (!send_rtcp_session_->UseCryptex(enable_cryptex_, require_cryptex_, /*sending_session=*/true)) return false;
  if (!send_rtcp_session_->SetSend(send_crypto_suite, send_key, send_extension_ids)) return false;
  recv_rtcp_session_ = std::make_unique<tls::SrtpSession>();
  if (!recv_rtcp_session_->UseCryptex(enable_cryptex_, require_cryptex_, /*sending_session=*/false)) return false;
  if (!recv_rtcp_session_->SetReceive(recv_crypto_suite, recv_key, recv_extension_ids)) return false;
  SM_LOG(kInfo, "SRTCP activated with negotiated parameters: send crypto_suite %d recv crypto_suite %d cryptex %d/%d",
         send_crypto_suite, recv_crypto_suite, enable_cryptex_, require_cryptex_);
  MaybeUpdateWritableState();
  return true;
}

bool SrtpTransport::IsSrtpActive() const { return send_session_ && recv_session_; }

bool SrtpTransport::IsWritable(bool rtcp) const { return IsSrtpActive() && RtpTransport::IsWritable(rtcp); }

void SrtpTransport::ResetParams() {
  send_session_ = nullptr;
  recv_session_ = nullptr;
  send_rtcp_session_ = nullptr;
  recv_rtcp_session_ = nullptr;
  MaybeUpdateWritableState();
  SM_LOG(kInfo, "SrtpTransport: the params in SRTP transport are reset");
}

void SrtpTransport::CreateSrtpSessions() {
  send_session_ = std::make_unique<tls::SrtpSession>();
  recv_session_ = std::make_unique<tls::SrtpSession>();
}

bool SrtpTransport::ProtectRtp(std::vector<uint8_t>& buffer) {
  if (!IsSrtpActive()) return false;
  return send_session_->ProtectRtp(buffer);
}

bool SrtpTransport::ProtectRtcp(std::vector<uint8_t>& buffer) {
  if (!IsSrtpActive()) return false;
  if (send_rtcp_session_) return send_rtcp_session_->ProtectRtcp(buffer);
  return send_session_->ProtectRtcp(buffer);
}

bool SrtpTransport::UnprotectRtp(std::vector<uint8_t>& buffer) {
  if (!IsSrtpActive()) return false;
  return recv_session_->UnprotectRtp(buffer);
}

bool SrtpTransport::UnprotectRtcp(std::vector<uint8_t>& buffer) {
  if (!IsSrtpActive()) return false;
  if (recv_rtcp_session_) return recv_rtcp_session_->UnprotectRtcp(buffer);
  return recv_session_->UnprotectRtcp(buffer);
}

bool SrtpTransport::GetSrtpOverhead(int* srtp_overhead) const {
  if (!IsSrtpActive()) {
    SM_LOG(kWarning, "SrtpTransport: failed to GetSrtpOverhead: SRTP not active");
    return false;
  }
  *srtp_overhead = send_session_->GetSrtpOverhead();
  return true;
}

void SrtpTransport::MaybeUpdateWritableState() {
  const bool writable = IsWritable(/*rtcp=*/true) && IsWritable(/*rtcp=*/false);
  // Only fire the signal if the writable state changes.
  if (writable_ != writable) {
    writable_ = writable;
    SendWritableState(writable_);
  }
}

bool SrtpTransport::UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) {
  if (recv_session_ && options_.remove_receive_stream) {
    // The SSRCs explicitly registered with the demuxer (signalled in SDP) leave the SRTP session.
    for (const uint32_t ssrc : GetSsrcsForSink(sink)) {
      if (!recv_session_->RemoveSsrcFromSession(ssrc)) {
        SM_LOG(kWarning, "SrtpTransport: could not remove SSRC %u from SRTP session", ssrc);
      }
    }
  }
  return RtpTransport::UnregisterRtpDemuxerSink(sink);
}

}  // namespace sm::pc
