// libwebrtc's pc/rtp_transport.cc (M157); see sm/pc/rtp_transport.h.
#include "sm/pc/rtp_transport.h"

#include <algorithm>
#include <bitset>

#include "sm/core/logging.h"

namespace sm::pc {
namespace {

constexpr uint8_t kRtpVersion = 2;

bool HasCorrectRtpVersion(std::span<const uint8_t> packet) { return packet[0] >> 6 == kRtpVersion; }
// RFC 5761 section 4: payload types 64-95 (RTCP packet types 192-223 with the marker bit) are RTCP.
bool PayloadTypeIsReservedForRtcp(uint8_t payload_type) { return 64 <= payload_type && payload_type < 96; }

void RemoveExtensionMapForMid(std::string_view mid,
                              std::vector<std::pair<std::string, RtpHeaderExtensions>>& extensions) {
  auto it = std::find_if(extensions.begin(), extensions.end(), [mid](const auto& kv) { return kv.first == mid; });
  if (it != extensions.end()) extensions.erase(it);
}

bool VerifyExtensionIds(const RtpHeaderExtensions& extensions) {
  std::bitset<1 + kMaxRtpExtensionId> id_used;
  for (const RtpExtension& extension : extensions) {
    if (extension.id < kMinRtpExtensionId || extension.id > kMaxRtpExtensionId) {
      SM_LOG(kWarning, "RtpTransport: bad extension ID %d for %s", extension.id, extension.uri.c_str());
      return false;
    }
    if (id_used[extension.id]) {
      SM_LOG(kWarning, "RtpTransport: duplicate extension ID %d for %s", extension.id, extension.uri.c_str());
      return false;
    }
    id_used[extension.id] = true;
  }
  return true;
}

}  // namespace

// modules/rtp_rtcp/source/rtp_util.cc.
bool IsRtpPacket(std::span<const uint8_t> packet) {
  return packet.size() >= kMinRtpPacketLen && HasCorrectRtpVersion(packet) &&
         !PayloadTypeIsReservedForRtcp(packet[1] & 0x7F);
}
bool IsRtcpPacket(std::span<const uint8_t> packet) {
  return packet.size() >= kMinRtcpPacketLen && HasCorrectRtpVersion(packet) &&
         PayloadTypeIsReservedForRtcp(packet[1] & 0x7F);
}
// media/base/rtp_utils.cc.
RtpPacketType InferRtpPacketType(std::span<const uint8_t> packet) {
  if (IsRtcpPacket(packet)) return RtpPacketType::kRtcp;
  if (IsRtpPacket(packet)) return RtpPacketType::kRtp;
  return RtpPacketType::kUnknown;
}
bool IsValidRtpPacketSize(RtpPacketType packet_type, size_t size) {
  const size_t min_packet_length = packet_type == RtpPacketType::kRtcp ? kMinRtcpPacketLen : kMinRtpPacketLen;
  return size >= min_packet_length && size <= kMaxRtpPacketLen;
}

RtpTransport::RtpTransport(bool rtcp_mux_enabled)
    : rtcp_mux_enabled_(rtcp_mux_enabled), rtp_demuxer_(header_extension_map_) {}

RtpTransport::~RtpTransport() {
  // Stop the callbacks into this object; owned transports go with it.
  ChangePacketTransport(nullptr, rtp_packet_transport_);
  ChangePacketTransport(nullptr, rtcp_packet_transport_);
}

void RtpTransport::SetRtcpMuxEnabled(bool enable) {
  rtcp_mux_enabled_ = enable;
  MaybeSignalReadyToSend();
}

const std::string& RtpTransport::transport_name() const { return rtp_packet_transport_->transport_name(); }

int RtpTransport::SetRtpOption(p2p::SocketOption opt, int value) {
  return rtp_packet_transport_->SetOption(opt, value);
}

int RtpTransport::SetRtcpOption(p2p::SocketOption opt, int value) {
  if (rtcp_packet_transport_) return rtcp_packet_transport_->SetOption(opt, value);
  return -1;
}

void RtpTransport::ChangePacketTransport(p2p::PacketTransportInternal* new_packet_transport,
                                         p2p::PacketTransportInternal*& transport_to_change) {
  if (new_packet_transport == transport_to_change) return;
  if (transport_to_change) {
    transport_to_change->UnsubscribeReadyToSend(this);
    transport_to_change->DeregisterReceivedPacketCallback(this);
    transport_to_change->UnsubscribeWritableState(this);
    transport_to_change->UnsubscribeSentPacket(this);
  }
  if (new_packet_transport) {
    new_packet_transport->SubscribeReadyToSend(
        this, [this](p2p::PacketTransportInternal* transport, Timestamp) { OnReadyToSend(transport); });
    new_packet_transport->RegisterReceivedPacketCallback(
        this, [this](p2p::PacketTransportInternal* transport, const p2p::ReceivedPacket& packet) {
          OnReadPacket(transport, packet);
        });
    new_packet_transport->SubscribeWritableState(
        this, [this](p2p::PacketTransportInternal* transport, Timestamp) { OnWritableState(transport); });
    new_packet_transport->SubscribeSentPacket(
        this, [this](p2p::PacketTransportInternal* transport, const p2p::SentPacketInfo& info) {
          OnSentPacket(transport, info);
        });
  }
  transport_to_change = new_packet_transport;
}

void RtpTransport::SetRtcpPacketTransportOwned(std::unique_ptr<p2p::PacketTransportInternal> new_packet_transport) {
  SetRtcpPacketTransport(new_packet_transport.get());
  owned_rtcp_packet_transport_ = std::move(new_packet_transport);
}

void RtpTransport::SetRtpPacketTransportOwned(std::unique_ptr<p2p::PacketTransportInternal> new_packet_transport) {
  SetRtpPacketTransport(new_packet_transport.get());
  owned_rtp_packet_transport_ = std::move(new_packet_transport);
}

void RtpTransport::SetRtpPacketTransport(p2p::PacketTransportInternal* new_packet_transport) {
  std::unique_ptr<p2p::PacketTransportInternal> delete_on_exit;
  if (new_packet_transport != owned_rtp_packet_transport_.get()) {
    delete_on_exit = std::move(owned_rtp_packet_transport_);
  }
  ChangePacketTransport(new_packet_transport, rtp_packet_transport_);
  // Assumes the transport is ready to send if it is writable.
  SetReadyToSend(/*rtcp=*/false, rtp_packet_transport_ && rtp_packet_transport_->writable());
}

void RtpTransport::SetRtcpPacketTransport(p2p::PacketTransportInternal* new_packet_transport) {
  std::unique_ptr<p2p::PacketTransportInternal> delete_on_exit;
  if (new_packet_transport != owned_rtcp_packet_transport_.get()) {
    // rtcp_packet_transport_ might still point to owned_rtcp_packet_transport_, so the owned object is kept alive
    // while the transport changes.
    delete_on_exit = std::move(owned_rtcp_packet_transport_);
  }
  ChangePacketTransport(new_packet_transport, rtcp_packet_transport_);
  // Assumes the transport is ready to send if it is writable.
  SetReadyToSend(/*rtcp=*/true, rtcp_packet_transport_ && rtcp_packet_transport_->writable());
}

bool RtpTransport::IsWritable(bool rtcp) const {
  p2p::PacketTransportInternal* transport =
      rtcp && !rtcp_mux_enabled_ ? rtcp_packet_transport_ : rtp_packet_transport_;
  return transport && transport->writable();
}

bool RtpTransport::SendRtpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                                 Timestamp now) {
  return SendPacket(false, packet, options, flags, now);
}

bool RtpTransport::SendRtcpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                                  Timestamp now) {
  return SendPacket(true, packet, options, flags, now);
}

bool RtpTransport::SendPacket(bool rtcp, std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                              Timestamp now) {
  p2p::PacketTransportInternal* transport =
      rtcp && !rtcp_mux_enabled_ ? rtcp_packet_transport_ : rtp_packet_transport_;
  if (!transport) return false;  // libwebrtc dereferences it; no transport sends nothing here.
  const int ret = transport->SendPacket(now, packet, options, flags);
  return ret == static_cast<int>(packet.size());
}

bool RtpTransport::VerifyRtpHeaderExtensionMap(const RtpHeaderExtensions& extensions) const {
  if (!VerifyExtensionIds(extensions)) return false;
  for (const RtpExtension& new_extension : extensions) {
    for (const auto& [mid, active_extensions] : header_extensions_by_mid_) {
      auto it = std::find_if(active_extensions.begin(), active_extensions.end(),
                             [&](const RtpExtension& ext) { return ext.id == new_extension.id; });
      if (it != active_extensions.end() && it->uri != new_extension.uri) {
        SM_LOG(kWarning, "RtpTransport: RTP extension ID reassignment not supported (active MID %s, id=%d)",
                       mid.c_str(), new_extension.id);
        return false;
      }
    }
  }
  return true;
}

bool RtpTransport::RegisterRtpHeaderExtensionMap(std::string_view mid, const RtpHeaderExtensions& extensions) {
  if (!VerifyRtpHeaderExtensionMap(extensions)) return false;
  auto existing = std::find_if(header_extensions_by_mid_.begin(), header_extensions_by_mid_.end(),
                               [mid](const auto& kv) { return kv.first == mid; });
  if (existing != header_extensions_by_mid_.end() && existing->second == extensions) return true;
  RemoveExtensionMapForMid(mid, header_extensions_by_mid_);
  header_extensions_by_mid_.emplace_back(std::string(mid), extensions);
  RebuildMergedMap();
  return true;
}

void RtpTransport::UnregisterRtpHeaderExtensionMap(std::string_view mid) {
  RemoveExtensionMapForMid(mid, header_extensions_by_mid_);
  RebuildMergedMap();
}

void RtpTransport::RebuildMergedMap() {
  // RFC 8843 section 7.1.3: one id per extension across a BUNDLE group. Transient conflicts (forked signalling, a
  // buggy peer) are resolved in favour of the most recently registered MID, so the newest registration goes first.
  sync::ExtensionMap merged_map;
  for (auto rit = header_extensions_by_mid_.rbegin(); rit != header_extensions_by_mid_.rend(); ++rit) {
    for (const RtpExtension& extension : rit->second) {
      // Only registered if the id is not already in use (RegisterByUri also refuses a URI it doesn't know).
      if (!merged_map.UriOf(static_cast<uint8_t>(extension.id))) {
        merged_map.Register(static_cast<uint8_t>(extension.id), extension.uri);
      }
    }
  }
  merged_map.SetExtmapAllowMixed(header_extension_map_.extmap_allow_mixed());
  header_extension_map_ = std::move(merged_map);  // In place: the demuxer reads this object.
}

void RtpTransport::SetActivePayloadTypeDemuxing(bool enabled) { rtp_demuxer_.set_use_payload_type_demuxing(enabled); }

int RtpTransport::SinkId(RtpPacketSinkInterface* sink) {
  auto it = sink_ids_.find(sink);
  if (it != sink_ids_.end()) return it->second;
  const int id = next_sink_id_++;
  sink_ids_[sink] = id;
  sinks_[id] = sink;
  return id;
}

bool RtpTransport::RegisterRtpDemuxerSink(const RtpDemuxerCriteria& criteria, RtpPacketSinkInterface* sink) {
  const int id = SinkId(sink);
  rtp_demuxer_.RemoveSink(id);
  if (!rtp_demuxer_.AddSink(id, criteria)) {
    SM_LOG(kWarning, "RtpTransport: failed to register the sink for RTP demuxer");
    return false;
  }
  return true;
}

bool RtpTransport::UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) {
  auto it = sink_ids_.find(sink);
  if (it == sink_ids_.end() || !rtp_demuxer_.RemoveSink(it->second)) {
    SM_LOG(kWarning, "RtpTransport: failed to unregister the sink for RTP demuxer");
    return false;
  }
  return true;
}

std::vector<uint32_t> RtpTransport::GetSsrcsForSink(RtpPacketSinkInterface* sink) const {
  auto it = sink_ids_.find(sink);
  if (it == sink_ids_.end()) return {};
  return rtp_demuxer_.GetSsrcsForSink(it->second);
}

void RtpTransport::DemuxPacket(std::vector<uint8_t> packet, Timestamp arrival_time) {
  auto parsed = RtpPacket::Parse(packet);
  if (!parsed) {
    SM_LOG(kWarning, "RtpTransport: failed to parse the incoming RTP packet before demuxing; dropped");
    return;
  }
  RtpPacketReceived received{std::move(packet), std::move(*parsed), arrival_time};
  const std::optional<int> sink = rtp_demuxer_.Demux(received.packet);
  if (!sink) {
    NotifyUnDemuxableRtpPacketReceived(received);
    return;
  }
  auto it = sinks_.find(*sink);
  if (it != sinks_.end()) it->second->OnRtpPacket(received);
}

bool RtpTransport::IsTransportWritable() const {
  p2p::PacketTransportInternal* rtcp_packet_transport = rtcp_mux_enabled_ ? nullptr : rtcp_packet_transport_;
  return rtp_packet_transport_ && rtp_packet_transport_->writable() &&
         (!rtcp_packet_transport || rtcp_packet_transport->writable());
}

void RtpTransport::OnReadyToSend(p2p::PacketTransportInternal* transport) {
  SetReadyToSend(transport == rtcp_packet_transport_, true);
}

void RtpTransport::OnWritableState(p2p::PacketTransportInternal* /*packet_transport*/) {
  SendWritableState(IsTransportWritable());
}

void RtpTransport::OnSentPacket(p2p::PacketTransportInternal* /*transport*/, const p2p::SentPacketInfo& sent_packet) {
  SendSentPacket(sent_packet);
}

void RtpTransport::OnRtpPacketReceived(const p2p::ReceivedPacket& packet) {
  DemuxPacket(std::vector<uint8_t>(packet.payload.begin(), packet.payload.end()), packet.arrival_time);
}

void RtpTransport::OnRtcpPacketReceived(const p2p::ReceivedPacket& packet) {
  SendRtcpPacketReceived(std::vector<uint8_t>(packet.payload.begin(), packet.payload.end()), packet.arrival_time);
}

void RtpTransport::OnReadPacket(p2p::PacketTransportInternal* /*transport*/, const p2p::ReceivedPacket& packet) {
  // DTLS-decrypted application data is not RTP/RTCP.
  if (packet.decryption_info == p2p::ReceivedPacket::kDtlsDecrypted) return;
  // With rtcp-mux, RTCP arrives on the RTP transport too: the payload-type byte tells them apart.
  const RtpPacketType packet_type = InferRtpPacketType(packet.payload);
  if (packet_type == RtpPacketType::kUnknown) return;
  // Protect ourselves against crazy data.
  if (!IsValidRtpPacketSize(packet_type, packet.payload.size())) {
    SM_LOG(kWarning, "RtpTransport: dropping incoming %s packet: wrong size=%zu",
                   packet_type == RtpPacketType::kRtcp ? "RTCP" : "RTP", packet.payload.size());
    return;
  }
  if (packet_type == RtpPacketType::kRtcp) {
    OnRtcpPacketReceived(packet);
  } else {
    OnRtpPacketReceived(packet);
  }
}

void RtpTransport::SetReadyToSend(bool rtcp, bool ready) {
  if (rtcp) {
    rtcp_ready_to_send_ = ready;
  } else {
    rtp_ready_to_send_ = ready;
  }
  MaybeSignalReadyToSend();
}

void RtpTransport::MaybeSignalReadyToSend() {
  if (processing_ready_to_send_) {
    // Re-checked once the current signal returns (libwebrtc posts this to the same thread); the state may be back
    // where it was by then, and then nothing is signalled.
    ready_to_send_recheck_ = true;
    return;
  }
  do {
    ready_to_send_recheck_ = false;
    const bool ready_to_send = rtp_ready_to_send_ && (rtcp_ready_to_send_ || rtcp_mux_enabled_);
    if (ready_to_send == ready_to_send_) continue;
    ready_to_send_ = ready_to_send;
    processing_ready_to_send_ = true;
    SendReadyToSend(ready_to_send);
    processing_ready_to_send_ = false;
  } while (ready_to_send_recheck_);
}

}  // namespace sm::pc
