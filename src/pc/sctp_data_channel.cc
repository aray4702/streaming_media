// Ported from libwebrtc M157 pc/sctp_data_channel.cc (compare/webrtc/reference/pc/sctp_data_channel.cc), without
// the thread hops (sm/pc/sctp_data_channel.h).
#include "sm/pc/sctp_data_channel.h"

#include <atomic>
#include <limits>
#include <utility>


namespace sm::pc {
namespace {

// Received messages held while there is no observer or the channel is not open yet.
constexpr size_t kMaxQueuedReceivedDataBytes = 16 * 1024 * 1024;

std::atomic<int> g_unique_id{0};
int GenerateUniqueId() { return ++g_unique_id; }

}  // namespace

InternalDataChannelInit::InternalDataChannelInit(const DataChannelInit& base)
    : DataChannelInit(base), open_handshake_role(kOpener) {
  if (base.negotiated) {
    open_handshake_role = kNone;
  } else {
    // Allocated later by DTLS role.
    id = -1;
  }
  // Values out of range are accepted and adjusted, for backwards compatibility.
  if (maxRetransmits) {
    if (*maxRetransmits < 0) {
      maxRetransmits = std::nullopt;
    } else if (*maxRetransmits > std::numeric_limits<uint16_t>::max()) {
      maxRetransmits = std::numeric_limits<uint16_t>::max();
    }
  }
  if (maxRetransmitTime) {
    if (*maxRetransmitTime < 0) {
      maxRetransmitTime = std::nullopt;
    } else if (*maxRetransmitTime > std::numeric_limits<uint16_t>::max()) {
      maxRetransmitTime = std::numeric_limits<uint16_t>::max();
    }
  }
}

bool InternalDataChannelInit::IsValid() const {
  if (id < -1) return false;
  if (maxRetransmits.has_value() && maxRetransmits.value() < 0) return false;
  if (maxRetransmitTime.has_value() && maxRetransmitTime.value() < 0) return false;
  // maxRetransmits and maxRetransmitTime are mutually exclusive.
  if (maxRetransmits.has_value() && maxRetransmitTime.has_value()) return false;
  return true;
}

std::optional<StreamId> SctpSidAllocator::AllocateSid(p2p::SslRole role) {
  int potential_sid = (role == p2p::SslRole::kClient) ? 0 : 1;
  while (potential_sid <= max_sid_) {
    StreamId sid(static_cast<uint16_t>(potential_sid));
    if (used_sids_.insert(sid).second) return sid;
    potential_sid += 2;
  }
  return std::nullopt;
}

bool SctpSidAllocator::ReserveSid(StreamId sid) { return used_sids_.insert(sid).second; }

void SctpSidAllocator::ReleaseSid(StreamId sid) { used_sids_.erase(sid); }

std::shared_ptr<SctpDataChannel> SctpDataChannel::Create(
    std::weak_ptr<SctpDataChannelControllerInterface*> controller, std::string_view label,
    bool connected_to_transport, const InternalDataChannelInit& config, std::optional<int> max_message_size) {
  return std::make_shared<SctpDataChannel>(config, std::move(controller), label, connected_to_transport,
                                           max_message_size);
}

SctpDataChannel::SctpDataChannel(const InternalDataChannelInit& config,
                                 std::weak_ptr<SctpDataChannelControllerInterface*> controller,
                                 std::string_view label, bool connected_to_transport,
                                 std::optional<int> max_message_size)
    : id_n_(config.id == -1 ? std::nullopt : std::make_optional(StreamId(static_cast<uint16_t>(config.id)))),
      internal_id_(GenerateUniqueId()),
      label_(label),
      protocol_(config.protocol),
      max_retransmit_time_(config.maxRetransmitTime),
      max_retransmits_(config.maxRetransmits),
      priority_(config.priority),
      negotiated_(config.negotiated),
      ordered_(config.ordered),
      max_message_size_(max_message_size),
      controller_(std::move(controller)),
      connected_to_transport_(connected_to_transport) {
  switch (config.open_handshake_role) {
    case InternalDataChannelInit::kNone:  // Pre-negotiated.
      handshake_state_ = kHandshakeReady;
      break;
    case InternalDataChannelInit::kOpener:
      handshake_state_ = kHandshakeShouldSendOpen;
      break;
    case InternalDataChannelInit::kAcker:
      handshake_state_ = kHandshakeShouldSendAck;
      break;
  }
}

void SctpDataChannel::RegisterObserver(DataChannelObserver* observer) {
  observer_ = observer;
  if (max_message_size_ && observer_) observer_->OnMaxMessageSize(*max_message_size_);
  DeliverQueuedReceivedData();
}

void SctpDataChannel::UnregisterObserver() { observer_ = nullptr; }

uint64_t SctpDataChannel::buffered_amount() const {
  SctpDataChannelControllerInterface* c = controller();
  if (c != nullptr && id_n_.has_value()) return c->buffered_amount(*id_n_);
  return 0u;
}

void SctpDataChannel::Close(Timestamp now) {
  if (state_ == DataState::kClosing || state_ == DataState::kClosed) return;
  SetState(DataState::kClosing);
  UpdateState(now);
}

bool SctpDataChannel::Send(Timestamp now, const DataBuffer& buffer) { return SendImpl(now, buffer).ok(); }

RtcError SctpDataChannel::SendImpl(Timestamp now, const DataBuffer& buffer) {
  // Counted even if the send fails, as libwebrtc: the change callback compares against it.
  expected_buffer_amount_ += buffer.size();
  if (state_ != DataState::kOpen) {
    error_ = RtcError(RtcErrorType::kInvalidState);
    return error_;
  }
  return SendDataMessage(now, buffer);
}

void SctpDataChannel::SetSctpSid_n(StreamId sid) { id_n_ = sid; }

void SctpDataChannel::OnClosingProcedureStartedRemotely() {
  if (state_ != DataState::kClosing && state_ != DataState::kClosed) {
    // The remote reset our incoming stream; we reset our outgoing one in response (the transport does that), so
    // the closing procedure has started.
    started_closing_procedure_ = true;
    SetState(DataState::kClosing);
  }
}

void SctpDataChannel::OnClosingProcedureComplete() { SetState(DataState::kClosed); }

void SctpDataChannel::OnBufferedAmountLow(Timestamp now) {
  MaybeSendOnBufferedAmountChanged();
  if (state_ == DataState::kClosing && !started_closing_procedure_ && id_n_.has_value() && buffered_amount() == 0) {
    started_closing_procedure_ = true;
    if (SctpDataChannelControllerInterface* c = controller()) c->RemoveSctpDataStream(now, *id_n_);
  }
}

void SctpDataChannel::OnMaxMessageSize(int max_message_size) {
  max_message_size_ = max_message_size;
  if (observer_) observer_->OnMaxMessageSize(max_message_size);
}

DataChannelStats SctpDataChannel::GetStats() const {
  DataChannelStats stats;
  stats.internal_id = internal_id_;
  stats.id = id();
  stats.label = label_;
  stats.protocol = protocol_;
  stats.state = state_;
  stats.messages_sent = messages_sent_;
  stats.messages_received = messages_received_;
  stats.bytes_sent = bytes_sent_;
  stats.bytes_received = bytes_received_;
  return stats;
}

void SctpDataChannel::OnDataReceived(sctp::DataMessageType type, std::span<const uint8_t> payload) {
  if (type == sctp::DataMessageType::kControl) {
    if (handshake_state_ != kHandshakeWaitingForAck) {
      // Ignore it if we are not expecting an ACK.
      return;
    }
    if (ParseDataChannelOpenAckMessage(payload)) {
      handshake_state_ = kHandshakeReady;
    }
    return;
  }
  // Data before the ACK means the remote got the OPEN (RFC 8832 section 6).
  if (handshake_state_ == kHandshakeWaitingForAck) handshake_state_ = kHandshakeReady;

  DataBuffer buffer({payload.begin(), payload.end()}, type == sctp::DataMessageType::kBinary);
  if (state_ == DataState::kOpen && observer_) {
    ++messages_received_;
    bytes_received_ += buffer.size();
    observer_->OnMessage(buffer);
  } else {
    if (queued_received_bytes_ + payload.size() > kMaxQueuedReceivedDataBytes) {
      queued_received_data_.clear();
      queued_received_bytes_ = 0;
      CloseAbruptlyWithError(
          RtcError(RtcErrorType::kResourceExhausted, "Queued received data exceeds the max buffer size."));
      return;
    }
    queued_received_bytes_ += buffer.size();
    queued_received_data_.push_back(std::move(buffer));
  }
}

void SctpDataChannel::OnTransportReady(Timestamp now) { UpdateState(now); }

void SctpDataChannel::CloseAbruptlyWithError(RtcError error) {
  if (state_ == DataState::kClosed) return;
  connected_to_transport_ = false;
  // Closing is announced before closed, as libwebrtc.
  SetState(DataState::kClosing);
  error_ = std::move(error);
  SetState(DataState::kClosed);
}

void SctpDataChannel::CloseAbruptlyWithDataChannelFailure(const std::string& message) {
  RtcError error(RtcErrorType::kOperationErrorWithData, message);
  CloseAbruptlyWithError(std::move(error));
}

void SctpDataChannel::UpdateState(Timestamp now) {
  SctpDataChannelControllerInterface* c = controller();
  switch (state_) {
    case DataState::kConnecting: {
      if (connected_to_transport_ && c) {
        if (handshake_state_ == kHandshakeShouldSendOpen) {
          std::vector<uint8_t> payload;
          WriteDataChannelOpenMessage(label_, protocol_, priority_, ordered_, max_retransmits_,
                                      max_retransmit_time_, &payload);
          SendControlMessage(now, payload);
        } else if (handshake_state_ == kHandshakeShouldSendAck) {
          std::vector<uint8_t> payload;
          WriteDataChannelOpenAckMessage(&payload);
          SendControlMessage(now, payload);
        }
        if (handshake_state_ == kHandshakeReady || handshake_state_ == kHandshakeWaitingForAck) {
          SetState(DataState::kOpen);
          // Deliver what arrived before the channel opened.
          DeliverQueuedReceivedData();
        }
      }
      break;
    }
    case DataState::kOpen:
      break;
    case DataState::kClosing: {
      if (connected_to_transport_ && c && id_n_.has_value()) {
        // Reset the stream only once everything queued was sent; OnBufferedAmountLow resumes it otherwise.
        if (c->buffered_amount(*id_n_) == 0 && !started_closing_procedure_) {
          started_closing_procedure_ = true;
          c->RemoveSctpDataStream(now, *id_n_);
        }
      } else {
        // Not connected (or no id yet): close at once.
        SetState(DataState::kClosed);
      }
      break;
    }
    case DataState::kClosed:
      break;
  }
}

void SctpDataChannel::SetState(DataState state) {
  if (state_ == state) return;
  state_ = state;
  if (observer_) observer_->OnStateChange();
  if (SctpDataChannelControllerInterface* c = controller()) c->OnChannelStateChanged(this, state_);
}

void SctpDataChannel::DeliverQueuedReceivedData() {
  if (!observer_ || state_ != DataState::kOpen) return;
  while (!queued_received_data_.empty() && observer_ && state_ == DataState::kOpen) {
    DataBuffer buffer = std::move(queued_received_data_.front());
    queued_received_data_.pop_front();
    queued_received_bytes_ -= buffer.size();
    ++messages_received_;
    bytes_received_ += buffer.size();
    observer_->OnMessage(buffer);
  }
}

void SctpDataChannel::MaybeSendOnBufferedAmountChanged() {
  SctpDataChannelControllerInterface* c = controller();
  if (!c || !id_n_.has_value() || !observer_) return;
  // Report once the buffer emptied or 100 KiB went out since the last report.
  static constexpr size_t kMinBufferedAmountDiffToTriggerCallback = 100 * 1024;
  const size_t actual_buffer_amount = c->buffered_amount(*id_n_);
  if (actual_buffer_amount > expected_buffer_amount_) return;
  if ((actual_buffer_amount == 0 && expected_buffer_amount_ != 0) ||
      (expected_buffer_amount_ - actual_buffer_amount > kMinBufferedAmountDiffToTriggerCallback)) {
    const uint64_t diff = expected_buffer_amount_ - actual_buffer_amount;
    expected_buffer_amount_ = actual_buffer_amount;
    observer_->OnBufferedAmountChange(diff);
  }
  c->SetBufferedAmountLowThreshold(*id_n_, actual_buffer_amount > kMinBufferedAmountDiffToTriggerCallback
                                               ? actual_buffer_amount - kMinBufferedAmountDiffToTriggerCallback
                                               : 0);
}

RtcError SctpDataChannel::SendDataMessage(Timestamp now, const DataBuffer& buffer) {
  SctpDataChannelControllerInterface* c = controller();
  if (!c || !id_n_.has_value()) {
    error_ = RtcError(RtcErrorType::kInvalidState);
    return error_;
  }
  sctp::SendDataParams send_params;
  send_params.ordered = ordered_;
  // Send unordered data ordered until the ACK arrived (RFC 8832 section 6), so it cannot overtake the OPEN.
  if (handshake_state_ != kHandshakeReady && !ordered_) send_params.ordered = true;
  send_params.max_rtx_count = max_retransmits_;
  send_params.max_rtx_ms = max_retransmit_time_;
  send_params.type = buffer.binary ? sctp::DataMessageType::kBinary : sctp::DataMessageType::kText;

  error_ = c->SendData(now, *id_n_, send_params, buffer.data);
  MaybeSendOnBufferedAmountChanged();
  if (error_.ok()) {
    ++messages_sent_;
    bytes_sent_ += buffer.size();
    return error_;
  }
  // Any failure closes the channel, as libwebrtc M157.
  RtcError result = error_;
  CloseAbruptlyWithError(RtcError(RtcErrorType::kNetworkError, "Failure to send data"));
  return result;
}

bool SctpDataChannel::SendControlMessage(Timestamp now, std::span<const uint8_t> buffer) {
  SctpDataChannelControllerInterface* c = controller();
  const bool is_open_message = handshake_state_ == kHandshakeShouldSendOpen;
  sctp::SendDataParams send_params;
  // The OPEN is always sent ordered and reliably.
  send_params.ordered = ordered_ || is_open_message;
  send_params.type = sctp::DataMessageType::kControl;
  RtcError err = c ? c->SendData(now, *id_n_, send_params, buffer) : RtcError(RtcErrorType::kInvalidState);
  if (err.ok()) {
    if (handshake_state_ == kHandshakeShouldSendAck) {
      handshake_state_ = kHandshakeReady;
    } else if (handshake_state_ == kHandshakeShouldSendOpen) {
      handshake_state_ = kHandshakeWaitingForAck;
    }
  } else {
    CloseAbruptlyWithError(RtcError(err.type(), "Failed to send a CONTROL message"));
  }
  return err.ok();
}

}  // namespace sm::pc
