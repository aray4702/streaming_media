// One data channel over SCTP, after libwebrtc M157's pc/sctp_data_channel.{h,cc} (compare/webrtc/reference/).
// C7 part 4 (#1070), built with SM_WITH_DATACHANNEL=ON.
//
// The channel runs the DCEP handshake (an opener sends DATA_CHANNEL_OPEN and waits for DATA_CHANNEL_ACK; an
// acker answers the OPEN; a negotiated channel has none), the states connecting -> open -> closing -> closed, the
// closing procedure (an outgoing stream reset once the send buffer is empty) and the buffered amount reporting
// (OnBufferedAmountChange once 100 KiB was sent or the buffer emptied). It holds a weak reference to its
// controller (DataChannelController), as libwebrtc's WeakPtr.
//
// Differences from libwebrtc: no signaling/network thread split, no proxy and no SendAsync (one thread, as the
// library); calls that may send take `Timestamp now`; observers are called directly instead of through a posted
// task.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "sm/core/units.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/pc/data_channel_interface.h"
#include "sm/pc/sctp_utils.h"

namespace sm::pc {

class SctpDataChannel;

// What a channel needs from its controller (pc/sctp_data_channel.h).
class SctpDataChannelControllerInterface {
 public:
  virtual RtcError SendData(Timestamp now, StreamId sid, const sctp::SendDataParams& params,
                            std::span<const uint8_t> payload) = 0;
  virtual RtcError AddSctpDataStream(StreamId sid, PriorityValue priority) = 0;
  virtual void RemoveSctpDataStream(Timestamp now, StreamId sid) = 0;
  virtual void OnChannelStateChanged(SctpDataChannel* data_channel, DataState state) = 0;
  virtual size_t buffered_amount(StreamId sid) const = 0;
  virtual size_t buffered_amount_low_threshold(StreamId sid) const = 0;
  virtual void SetBufferedAmountLowThreshold(StreamId sid, size_t bytes) = 0;

 protected:
  virtual ~SctpDataChannelControllerInterface() = default;
};

// The controller's liveness token: channels keep a weak_ptr to it (libwebrtc's WeakPtr<...>).
using ControllerHandle = std::shared_ptr<SctpDataChannelControllerInterface*>;

struct InternalDataChannelInit : public DataChannelInit {
  enum OpenHandshakeRole { kOpener, kAcker, kNone };
  InternalDataChannelInit() = default;
  // A negotiated channel has no handshake; an in-band one gets its id later (id = -1). maxRetransmits and
  // maxRetransmitTime below 0 are dropped, above 65535 clamped, as libwebrtc.
  explicit InternalDataChannelInit(const DataChannelInit& base);
  bool IsValid() const;
  OpenHandshakeRole open_handshake_role = kOpener;
  // The DTLS role to use for the stream id when DTLS has not decided it yet.
  std::optional<p2p::SslRole> fallback_ssl_role;
};

// Hands out stream ids: even for the DTLS client, odd for the server (RFC 8832 section 6).
class SctpSidAllocator {
 public:
  // max_sid can only go down.
  void SetMaxSid(int max_sid) {
    if (max_sid >= 0 && max_sid <= max_sid_) max_sid_ = max_sid;
  }
  std::optional<StreamId> AllocateSid(p2p::SslRole role);
  bool ReserveSid(StreamId sid);
  void ReleaseSid(StreamId sid);

 private:
  int max_sid_ = sctp::kMaxSctpSid;
  std::set<StreamId> used_sids_;
};

class SctpDataChannel {
 public:
  static std::shared_ptr<SctpDataChannel> Create(std::weak_ptr<SctpDataChannelControllerInterface*> controller,
                                                 std::string_view label, bool connected_to_transport,
                                                 const InternalDataChannelInit& config,
                                                 std::optional<int> max_message_size);
  SctpDataChannel(const InternalDataChannelInit& config, std::weak_ptr<SctpDataChannelControllerInterface*> controller,
                  std::string_view label, bool connected_to_transport, std::optional<int> max_message_size);
  SctpDataChannel(const SctpDataChannel&) = delete;
  SctpDataChannel& operator=(const SctpDataChannel&) = delete;

  // DataChannelInterface.
  // Registering delivers the max message size and the messages that arrived before.
  void RegisterObserver(DataChannelObserver* observer);
  void UnregisterObserver();
  std::string label() const { return label_; }
  bool reliable() const { return !max_retransmits_ && !max_retransmit_time_; }
  bool ordered() const { return ordered_; }
  std::optional<int> maxPacketLifeTime() const { return max_retransmit_time_; }
  std::optional<int> maxRetransmitsOpt() const { return max_retransmits_; }
  std::string protocol() const { return protocol_; }
  bool negotiated() const { return negotiated_; }
  // The stream id, -1 until allocated.
  int id() const { return id_n_ ? id_n_->stream_id_int() : -1; }
  PriorityValue priority() const { return priority_.value_or(PriorityValue(PriorityValue::kLow)); }
  // Bytes queued in SCTP for this channel.
  uint64_t buffered_amount() const;
  // Starts the closing procedure: closing, then closed once the send buffer drained and the stream was reset.
  void Close(Timestamp now);
  DataState state() const { return state_; }
  RtcError error() const { return error_; }
  uint32_t messages_sent() const { return messages_sent_; }
  uint64_t bytes_sent() const { return bytes_sent_; }
  uint32_t messages_received() const { return messages_received_; }
  uint64_t bytes_received() const { return bytes_received_; }
  // Sends a message. False if the channel is not open or the transport refused it (the channel then closes
  // with a network error, as libwebrtc).
  bool Send(Timestamp now, const DataBuffer& buffer);
  RtcError SendImpl(Timestamp now, const DataBuffer& buffer);

  // From the controller.
  void CloseAbruptlyWithError(RtcError error);
  void CloseAbruptlyWithDataChannelFailure(const std::string& message);
  void OnTransportReady(Timestamp now);
  void OnDataReceived(sctp::DataMessageType type, std::span<const uint8_t> payload);
  void SetSctpSid_n(StreamId sid);
  void OnClosingProcedureStartedRemotely();
  void OnClosingProcedureComplete();
  void OnTransportChannelCreated() { connected_to_transport_ = true; }
  void OnTransportChannelClosed(RtcError error) { CloseAbruptlyWithError(std::move(error)); }
  void OnBufferedAmountLow(Timestamp now);
  void OnMaxMessageSize(int max_message_size);
  DataChannelStats GetStats() const;
  int internal_id() const { return internal_id_; }
  std::optional<StreamId> sid_n() const { return id_n_; }

 private:
  enum HandshakeState {
    kHandshakeInit,
    kHandshakeShouldSendOpen,
    kHandshakeShouldSendAck,
    kHandshakeWaitingForAck,
    kHandshakeReady
  };
  SctpDataChannelControllerInterface* controller() const {
    auto c = controller_.lock();
    return c ? *c : nullptr;
  }
  void UpdateState(Timestamp now);
  void SetState(DataState state);
  void DeliverQueuedReceivedData();
  RtcError SendDataMessage(Timestamp now, const DataBuffer& buffer);
  bool SendControlMessage(Timestamp now, std::span<const uint8_t> buffer);
  void MaybeSendOnBufferedAmountChanged();

  std::optional<StreamId> id_n_;
  const int internal_id_;
  const std::string label_;
  const std::string protocol_;
  const std::optional<int> max_retransmit_time_;
  const std::optional<int> max_retransmits_;
  const std::optional<PriorityValue> priority_;
  const bool negotiated_;
  const bool ordered_;
  size_t expected_buffer_amount_ = 0;
  DataChannelObserver* observer_ = nullptr;
  DataState state_ = DataState::kConnecting;
  RtcError error_;
  uint32_t messages_sent_ = 0;
  uint64_t bytes_sent_ = 0;
  uint32_t messages_received_ = 0;
  uint64_t bytes_received_ = 0;
  std::optional<int> max_message_size_;
  std::weak_ptr<SctpDataChannelControllerInterface*> controller_;
  HandshakeState handshake_state_ = kHandshakeInit;
  bool started_closing_procedure_ = false;
  bool connected_to_transport_ = false;
  std::deque<DataBuffer> queued_received_data_;
  size_t queued_received_bytes_ = 0;
};

}  // namespace sm::pc
