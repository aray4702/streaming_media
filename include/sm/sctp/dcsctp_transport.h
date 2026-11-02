// SCTP over DTLS (C7 part 3, #1069, milestone 3): libwebrtc M157's media/sctp/dcsctp_transport.{h,cc}
// (compare/webrtc/reference/), which runs the dcSCTP socket (sm/dcsctp, #1032 and #1060) over the DTLS transport
// (sm/p2p/dtls_transport.h, #1006).
//
// What it does, as libwebrtc does:
// - Start() creates the socket from the negotiated options (ports, default 5000; the max message size; the streams
//   announced) with dcsctp_transport.cc's settings: timer backoff capped at 3 s, no association close on too many
//   retransmissions, a 16 MiB per-stream send queue and no total limit. A second Start() may change only the max
//   message size.
// - The association starts (INIT) once the DTLS transport is writable and the socket is created, whichever comes
//   last; with SNAP (both sctp-init values) it connects with the connection tokens and replays the decrypted
//   packets that arrived before (up to 32).
// - Messages go by SID with the WebRTC PPIDs of RFC 8831 section 8 (51 string, 53 binary, 56 / 57 empty string /
//   binary, sent as one zero byte; 50 DCEP), ordered or unordered, with max retransmits or a max lifetime. A
//   message to a SID that isn't open, or that is closing, fails with kInvalidState; one over the max message size
//   with kInvalidRange; a full send queue with kResourceExhausted, and ReadyToSendData() is false until the buffer
//   drains (OnReadyToSend).
// - Received messages reach the sink with their type (the PPID); unknown PPIDs are dropped.
// - Closing a channel resets the stream both ways (RFC 8831 section 6.7): the side that receives a reset resets its
//   own direction too and reports OnChannelClosing; both report OnChannelClosed when both directions are reset.
// - A DTLS restart (the DTLS state back to kNew) drops the socket, so that the next Start() sends a new INIT.
//
// The library's time model: nothing reads a clock or starts a thread. Calls that can send take `now`; received
// packets carry their arrival time; the socket's timers are NextProcessTime()/Process(now), which the owner runs.
//
// Differences from libwebrtc, none of them on the wire:
// - Timeouts come from dcsctp's ProcessTimeoutFactory (sm/dcsctp/timer/process_timeout.h) instead of
//   TaskQueueTimeoutFactory on the network thread.
// - The random generator is sm::Random (xoshiro256**) seeded, as libwebrtc's webrtc::Random, from the creation time
//   in microseconds; only verification tags and initial TSNs differ.
// - No TextPcapPacketObserver (not ported, #1060), no TRACE_EVENT, and the WebRTC-DataChannelMessageInterleaving
//   field trial is DcSctpTransport::Config::enable_message_interleaving (off, as the trial).
// - Payloads are std::span<const uint8_t> instead of CopyOnWriteBuffer.
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
#include <vector>

#include "sm/core/random.h"
#include "sm/core/units.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/dcsctp_socket_factory.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/timer/process_timeout.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/sctp/sctp_transport_internal.h"

namespace sm::sctp {

// The field trials dcsctp_transport.cc reads.
struct DcSctpTransportConfig {
  // The WebRTC-DataChannelMessageInterleaving field trial (RFC 8260 I-DATA), off by default.
  bool enable_message_interleaving = false;
};

class DcSctpTransport : public SctpTransportInternal, public dcsctp::DcSctpSocketCallbacks {
 public:
  using Config = DcSctpTransportConfig;

  // `transport` (may be null, as in libwebrtc's tests) must outlive this transport.
  DcSctpTransport(Timestamp now, p2p::DtlsTransport* transport, Config config = {});
  DcSctpTransport(Timestamp now, p2p::DtlsTransport* transport, Config config,
                  std::unique_ptr<dcsctp::DcSctpSocketFactory> socket_factory);
  ~DcSctpTransport() override;
  DcSctpTransport(const DcSctpTransport&) = delete;
  DcSctpTransport& operator=(const DcSctpTransport&) = delete;

  // SctpTransportInternal
  void SetOnConnectedCallback(std::function<void()> callback) override;
  void SetDataChannelSink(DataChannelSink* sink) override;
  p2p::DtlsTransport* dtls_transport() const override;
  bool Start(Timestamp now, const SctpOptions& options) override;
  bool OpenStream(int sid, PriorityValue priority) override;
  bool ResetStream(Timestamp now, int sid) override;
  RtcError SendData(Timestamp now, int sid, const SendDataParams& params, std::span<const uint8_t> payload) override;
  bool ReadyToSendData() override;
  int max_message_size() const override;
  std::optional<int> max_outbound_streams() const override;
  std::optional<int> max_inbound_streams() const override;
  size_t buffered_amount(int sid) const override;
  size_t buffered_amount_low_threshold(int sid) const override;
  void SetBufferedAmountLowThreshold(int sid, size_t bytes) override;
  Timestamp NextProcessTime() const override;
  void Process(Timestamp now) override;

  // SNAP's a=sctp-init value for an offer or answer.
  static std::vector<uint8_t> GenerateConnectionToken(Timestamp now, Config config = {});

  // Packets buffered while waiting for the socket to be created or (SNAP) connected. See early_received_packets_.
  size_t EarlyReceivedPacketCountForTesting() const override;
  // The socket's state, for tests and stats; kClosed before Start().
  dcsctp::SocketState socket_state() const;

 private:
  // dcsctp::DcSctpSocketCallbacks
  dcsctp::SendPacketStatus SendPacketWithStatus(std::span<const uint8_t> data) override;
  std::unique_ptr<dcsctp::Timeout> CreateTimeout(dcsctp::DelayPrecision precision) override;
  dcsctp::TimeMs TimeMillis() override;
  Timestamp Now() override;
  uint32_t GetRandomInt(uint32_t low, uint32_t high) override;
  void OnTotalBufferedAmountLow() override;
  void OnBufferedAmountLow(dcsctp::StreamID stream_id) override;
  void OnMessageReceived(dcsctp::DcSctpMessage message) override;
  void OnError(dcsctp::ErrorKind error, std::string_view message) override;
  void OnAborted(dcsctp::ErrorKind error, std::string_view message) override;
  void OnConnected() override;
  void OnClosed() override;
  void OnConnectionRestarted() override;
  void OnStreamsResetFailed(std::span<const dcsctp::StreamID> outgoing_streams, std::string_view reason) override;
  void OnStreamsResetPerformed(std::span<const dcsctp::StreamID> outgoing_streams) override;
  void OnIncomingStreamsReset(std::span<const dcsctp::StreamID> incoming_streams) override;

  // Transport callbacks
  void ConnectTransportSignals();
  void DisconnectTransportSignals();
  void OnTransportWritableState(p2p::PacketTransportInternal* transport, Timestamp now);
  void OnTransportReadPacket(p2p::PacketTransportInternal* transport, const p2p::ReceivedPacket& packet);
  void OnDtlsTransportState(p2p::DtlsTransport* transport, p2p::DtlsTransportState state);
  void MaybeConnectSocket();
  void SetNow(Timestamp now);

  static dcsctp::DcSctpOptions CreateDcSctpOptions(const SctpOptions& options, const Config& config);

  p2p::DtlsTransport* const transport_;
  const Config config_;
  Random random_;
  Timestamp now_;

  const std::unique_ptr<dcsctp::DcSctpSocketFactory> socket_factory_;
  // Declared before socket_: the socket's timeouts unregister from it when the socket goes.
  dcsctp::ProcessTimeoutFactory timeout_factory_;
  std::unique_ptr<dcsctp::DcSctpSocketInterface> socket_;
  const std::string debug_name_;

  // The state of each open or closing data channel. Reset needs to happen both ways before the channel is closed.
  struct StreamState {
    // The local end initiated the reset. A reset received for a stream that isn't being reset locally starts the
    // closing procedure remotely (OnChannelClosing).
    bool closure_initiated = false;
    // OnIncomingStreamsReset came.
    bool incoming_reset_done = false;
    // OnStreamsResetPerformed came.
    bool outgoing_reset_done = false;
    // The stream's priority, RFC 8831 section 6.4.
    dcsctp::StreamPriority priority = dcsctp::StreamPriority(PriorityValue::kLow);
  };
  // libwebrtc's flat_map: ordered by SID.
  std::map<dcsctp::StreamID, StreamState> stream_states_;
  bool ready_to_send_data_ = false;
  std::function<void()> on_connected_callback_;
  DataChannelSink* data_channel_sink_ = nullptr;

  std::optional<std::vector<uint8_t>> local_init_;
  std::optional<std::vector<uint8_t>> remote_init_;

  // With SNAP the answerer can (if data channels are negotiated after the DTLS handshake) start sending data
  // channel packets as soon as it has processed the offer, so they may arrive before the answer. Buffering (a
  // limited number of) them avoids a resend after a timeout.
  static constexpr size_t kMaxEarlyReceivedPackets = 32;
  std::vector<std::vector<uint8_t>> early_received_packets_;
};

}  // namespace sm::sctp
