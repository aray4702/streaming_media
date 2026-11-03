// The RTP transport: libwebrtc's pc/rtp_transport.{h,cc} and pc/rtp_transport_internal.h at M157
// (compare/webrtc/reference/), ported for DTLS-SRTP (C5 part 3, #1007). It sends and receives the RTP and RTCP of one
// RTP session over a packet transport (ICE, or DTLS over ICE), with or without rtcp-mux, and hands each received RTP
// packet to the sink the RtpDemuxer picks (sm::sync::RtpDemuxer, libwebrtc's call/rtp_demuxer.cc).
//
// What it does, as libwebrtc does:
// - With rtcp-mux, RTP and RTCP share the RTP packet transport; without, RTCP goes over its own.
// - A received packet is classified by its payload-type byte (RFC 5761): RTCP goes to the RTCP subscribers, RTP is
//   parsed with the header extensions registered for the session (by MID, newest registration first on an id
//   conflict, RFC 8843 section 7.1.3) and demultiplexed; a packet no sink takes goes to the undemuxable handler.
//   Packets that are neither, of a bad size, or DTLS application data are dropped.
// - Ready to send is the RTP transport's, and the RTCP transport's too without rtcp-mux; writable likewise.
//
// The library's time model: nothing reads a clock or starts a thread. Sending takes `now` (the packet transport's
// SendPacket does), and callbacks run on the caller's thread inside the call that caused them.
//
// Differences from libwebrtc, none of them on the wire:
// - Packets are std::vector<uint8_t> (protected in place, growing for the SRTP trailer), not CopyOnWriteBuffer, and
//   a received RTP packet reaches its sink as RtpPacketReceived (the bytes and the parsed sm::RtpPacket).
// - The network route (SubscribeNetworkRouteChanged, and the SRTP overhead SrtpTransport adds to it behind the
//   WebRTC-UpdateNetworkRouteOnSrtpActivation field trial, off by default) is left out until ICE reports candidate
//   pairs (#1002), as in sm/p2p/packet_transport_internal.h.
// - ECN is left out: the packet transports don't report it yet, so RTCP is never sent as ECT(1).
// - A ready-to-send change during a ready-to-send callback is re-checked when that callback returns; libwebrtc posts
//   it as a task to the same thread.
// - RTCError results are bool.
// - No SequenceChecker: like every module in the library, a transport is used from one thread at a time.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sm/core/rtp_packet.h"
#include "sm/core/units.h"
#include "sm/p2p/packet_transport_internal.h"
#include "sm/pc/rtp_parameters.h"
#include "sm/sync/header_extensions.h"

namespace sm::pc {

// media/base/rtp_utils.h and modules/rtp_rtcp/source/rtp_util.h.
enum class RtpPacketType { kRtp, kRtcp, kUnknown };
inline constexpr size_t kMinRtpPacketLen = 12;
inline constexpr size_t kMinRtcpPacketLen = 4;
inline constexpr size_t kMaxRtpPacketLen = 2048;
bool IsRtpPacket(std::span<const uint8_t> packet);
bool IsRtcpPacket(std::span<const uint8_t> packet);
RtpPacketType InferRtpPacketType(std::span<const uint8_t> packet);
bool IsValidRtpPacketSize(RtpPacketType packet_type, size_t size);

// api/rtp_parameters.h's RtpExtension (an a=extmap line) is sm/pc/rtp_parameters.h's, shared with the session
// description model (#1064).
using RtpHeaderExtensions = std::vector<RtpExtension>;
// RtpHeaderExtensionId's range (api/rtp_header_extension_id.h).
inline constexpr int kMinRtpExtensionId = 1;
inline constexpr int kMaxRtpExtensionId = 255;

using RtpDemuxerCriteria = sync::RtpDemuxerCriteria;

// RtpPacketReceived as the transport hands it to a sink: the bytes (unprotected) and the parsed packet.
struct RtpPacketReceived {
  std::vector<uint8_t> buffer;
  RtpPacket packet;
  Timestamp arrival_time = Timestamp::MinusInfinity();
};

// call/rtp_packet_sink_interface.h.
class RtpPacketSinkInterface {
 public:
  virtual ~RtpPacketSinkInterface() = default;
  virtual void OnRtpPacket(const RtpPacketReceived& packet) = 0;
};

// libwebrtc's RtpTransportInternal: the interface BaseChannel uses, with its signals.
class RtpTransportInternal {
 public:
  using RtcpCallback = std::function<void(const std::vector<uint8_t>& packet, std::optional<Timestamp> arrival)>;
  using BoolCallback = std::function<void(bool)>;
  using SentCallback = std::function<void(const p2p::SentPacketInfo&)>;
  using UndemuxableCallback = std::function<void(const RtpPacketReceived&)>;

  virtual ~RtpTransportInternal() = default;
  virtual void SetRtcpMuxEnabled(bool enable) = 0;
  virtual const std::string& transport_name() const = 0;
  virtual int SetRtpOption(p2p::SocketOption opt, int value) = 0;
  virtual int SetRtcpOption(p2p::SocketOption opt, int value) = 0;
  virtual bool rtcp_mux_enabled() const = 0;
  virtual bool IsReadyToSend() const = 0;
  virtual bool IsWritable(bool rtcp) const = 0;
  // Sends one packet; `packet` may be changed (protected in place). False if it wasn't sent whole.
  virtual bool SendRtpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                             Timestamp now) = 0;
  virtual bool SendRtcpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                              Timestamp now) = 0;
  virtual bool RegisterRtpHeaderExtensionMap(std::string_view mid, const RtpHeaderExtensions& extensions) = 0;
  virtual bool VerifyRtpHeaderExtensionMap(const RtpHeaderExtensions& extensions) const = 0;
  virtual void UnregisterRtpHeaderExtensionMap(std::string_view mid) = 0;
  virtual bool IsSrtpActive() const = 0;
  virtual bool RegisterRtpDemuxerSink(const RtpDemuxerCriteria& criteria, RtpPacketSinkInterface* sink) = 0;
  virtual bool UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) = 0;
  virtual void SetActivePayloadTypeDemuxing(bool enabled) = 0;

  // True when every transport in use is ready to send (more specific than writable).
  void SubscribeReadyToSend(const void* tag, BoolCallback cb) { ready_to_send_.Add(tag, std::move(cb)); }
  void UnsubscribeReadyToSend(const void* tag) { ready_to_send_.Remove(tag); }
  // Each received (unprotected) RTCP packet.
  void SubscribeRtcpPacketReceived(const void* tag, RtcpCallback cb) { rtcp_received_.Add(tag, std::move(cb)); }
  // A received RTP packet that no sink takes.
  void SetUnDemuxableRtpPacketReceivedHandler(UndemuxableCallback cb) { undemuxable_ = std::move(cb); }
  // The writable state may have changed.
  void SubscribeWritableState(const void* tag, BoolCallback cb) { writable_state_.Add(tag, std::move(cb)); }
  void UnsubscribeWritableState(const void* tag) { writable_state_.Remove(tag); }
  void SubscribeSentPacket(const void* tag, SentCallback cb) { sent_packet_.Add(tag, std::move(cb)); }
  void UnsubscribeSentPacket(const void* tag) { sent_packet_.Remove(tag); }

 protected:
  void SendReadyToSend(bool ready) { ready_to_send_.Send(ready); }
  void SendRtcpPacketReceived(const std::vector<uint8_t>& packet, std::optional<Timestamp> arrival) {
    rtcp_received_.Send(packet, arrival);
  }
  void NotifyUnDemuxableRtpPacketReceived(const RtpPacketReceived& packet) {
    if (undemuxable_) undemuxable_(packet);
  }
  void SendWritableState(bool writable) { writable_state_.Send(writable); }
  void SendSentPacket(const p2p::SentPacketInfo& info) { sent_packet_.Send(info); }

 private:
  p2p::TaggedCallbackList<bool> ready_to_send_;
  p2p::TaggedCallbackList<const std::vector<uint8_t>&, std::optional<Timestamp>> rtcp_received_;
  UndemuxableCallback undemuxable_;
  p2p::TaggedCallbackList<bool> writable_state_;
  p2p::TaggedCallbackList<const p2p::SentPacketInfo&> sent_packet_;
};

class RtpTransport : public RtpTransportInternal {
 public:
  explicit RtpTransport(bool rtcp_mux_enabled);
  ~RtpTransport() override;
  RtpTransport(const RtpTransport&) = delete;
  RtpTransport& operator=(const RtpTransport&) = delete;

  bool rtcp_mux_enabled() const override { return rtcp_mux_enabled_; }
  void SetRtcpMuxEnabled(bool enable) override;
  const std::string& transport_name() const override;
  int SetRtpOption(p2p::SocketOption opt, int value) override;
  int SetRtcpOption(p2p::SocketOption opt, int value) override;

  // The packet transports must outlive this transport, or be replaced first: it unsubscribes from them when it
  // goes (libwebrtc leaves that to the owners' destruction order).
  p2p::PacketTransportInternal* rtp_packet_transport() const { return rtp_packet_transport_; }
  void SetRtpPacketTransport(p2p::PacketTransportInternal* rtp);
  void SetRtpPacketTransportOwned(std::unique_ptr<p2p::PacketTransportInternal> rtp);
  p2p::PacketTransportInternal* rtcp_packet_transport() const { return rtcp_packet_transport_; }
  void SetRtcpPacketTransport(p2p::PacketTransportInternal* rtcp);
  void SetRtcpPacketTransportOwned(std::unique_ptr<p2p::PacketTransportInternal> rtcp);

  bool IsReadyToSend() const override { return ready_to_send_; }
  bool IsWritable(bool rtcp) const override;
  bool SendRtpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                     Timestamp now) override;
  bool SendRtcpPacket(std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                      Timestamp now) override;
  bool IsSrtpActive() const override { return false; }

  // An id outside 1-255 or used twice in `extensions`, or an id active on another MID with another URI, fails.
  bool VerifyRtpHeaderExtensionMap(const RtpHeaderExtensions& extensions) const override;
  bool RegisterRtpHeaderExtensionMap(std::string_view mid, const RtpHeaderExtensions& extensions) override;
  void UnregisterRtpHeaderExtensionMap(std::string_view mid) override;
  // The merged id <-> URI map the received packets are parsed with.
  const sync::ExtensionMap& header_extension_map() const { return header_extension_map_; }

  // Replaces the sink's criteria (a sink has one registration). False if the demuxer refuses them.
  bool RegisterRtpDemuxerSink(const RtpDemuxerCriteria& criteria, RtpPacketSinkInterface* sink) override;
  bool UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) override;
  void SetActivePayloadTypeDemuxing(bool enabled) override;

 protected:
  void DemuxPacket(std::vector<uint8_t> packet, Timestamp arrival_time);
  std::vector<uint32_t> GetSsrcsForSink(RtpPacketSinkInterface* sink) const;

  // Overridden by SrtpTransport.
  virtual void OnRtpPacketReceived(const p2p::ReceivedPacket& packet);
  virtual void OnRtcpPacketReceived(const p2p::ReceivedPacket& packet);
  // Overridden by SrtpTransport and DtlsSrtpTransport.
  virtual void OnWritableState(p2p::PacketTransportInternal* packet_transport);

 private:
  bool SendPacket(bool rtcp, std::vector<uint8_t>& packet, const p2p::PacketOptions& options, int flags,
                  Timestamp now);
  void ChangePacketTransport(p2p::PacketTransportInternal* new_transport,
                             p2p::PacketTransportInternal*& transport_to_change);
  void OnReadyToSend(p2p::PacketTransportInternal* transport);
  void OnSentPacket(p2p::PacketTransportInternal* transport, const p2p::SentPacketInfo& sent_packet);
  void OnReadPacket(p2p::PacketTransportInternal* transport, const p2p::ReceivedPacket& packet);
  void SetReadyToSend(bool rtcp, bool ready);
  void MaybeSignalReadyToSend();
  bool IsTransportWritable() const;
  void RebuildMergedMap();
  int SinkId(RtpPacketSinkInterface* sink);

  bool rtcp_mux_enabled_;
  p2p::PacketTransportInternal* rtp_packet_transport_ = nullptr;
  p2p::PacketTransportInternal* rtcp_packet_transport_ = nullptr;
  std::unique_ptr<p2p::PacketTransportInternal> owned_rtp_packet_transport_;
  std::unique_ptr<p2p::PacketTransportInternal> owned_rtcp_packet_transport_;

  bool ready_to_send_ = false;
  bool rtp_ready_to_send_ = false;
  bool rtcp_ready_to_send_ = false;
  // Guards against recursive ready-to-send signals; a change seen meanwhile is re-checked afterwards.
  bool processing_ready_to_send_ = false;
  bool ready_to_send_recheck_ = false;

  // Used for identifying the MID for the demuxer. The demuxer reads this map, which is rebuilt in place.
  sync::ExtensionMap header_extension_map_;
  // The registered extensions by MID, in registration order (newest last).
  std::vector<std::pair<std::string, RtpHeaderExtensions>> header_extensions_by_mid_;
  sync::RtpDemuxer rtp_demuxer_;
  std::map<RtpPacketSinkInterface*, int> sink_ids_;
  std::map<int, RtpPacketSinkInterface*> sinks_;
  int next_sink_id_ = 0;
};

}  // namespace sm::pc
