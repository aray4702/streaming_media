// libwebrtc M157's media/sctp/dcsctp_transport.cc (compare/webrtc/reference/), on the library's time model. See
// sm/sctp/dcsctp_transport.h.
#include "sm/sctp/dcsctp_transport.h"

#include <atomic>
#include <limits>
#include <string>
#include <utility>

#include "sm/dcsctp/rtc_compat.h"
#include "sm/p2p/socket.h"

namespace sm::sctp {

namespace {
using dcsctp::SendPacketStatus;

// When there is packet loss for a long time, the SCTP retry timers use exponential backoff, which can grow to very
// long durations, and when the connection recovers it may take a long time to reach the new backoff duration.
// Limiting it reduces the time to recover.
constexpr dcsctp::DurationMs kMaxTimerBackoffDuration = dcsctp::DurationMs(3000);

enum class WebrtcPPID : dcsctp::PPID::UnderlyingType {
  // https://www.rfc-editor.org/rfc/rfc8832.html#section-8.1
  kDCEP = 50,
  // https://www.rfc-editor.org/rfc/rfc8831.html#section-8
  kString = 51,
  kBinaryPartial = 52,  // Deprecated
  kBinary = 53,
  kStringPartial = 54,  // Deprecated
  kStringEmpty = 56,
  kBinaryEmpty = 57,
};

WebrtcPPID ToPPID(DataMessageType message_type, size_t size) {
  switch (message_type) {
    case DataMessageType::kControl:
      return WebrtcPPID::kDCEP;
    case DataMessageType::kText:
      return size > 0 ? WebrtcPPID::kString : WebrtcPPID::kStringEmpty;
    case DataMessageType::kBinary:
      return size > 0 ? WebrtcPPID::kBinary : WebrtcPPID::kBinaryEmpty;
  }
  return WebrtcPPID::kBinary;
}

std::optional<DataMessageType> ToDataMessageType(dcsctp::PPID ppid) {
  switch (static_cast<WebrtcPPID>(ppid.value())) {
    case WebrtcPPID::kDCEP:
      return DataMessageType::kControl;
    case WebrtcPPID::kString:
    case WebrtcPPID::kStringPartial:
    case WebrtcPPID::kStringEmpty:
      return DataMessageType::kText;
    case WebrtcPPID::kBinary:
    case WebrtcPPID::kBinaryPartial:
    case WebrtcPPID::kBinaryEmpty:
      return DataMessageType::kBinary;
  }
  return std::nullopt;
}

std::optional<SctpErrorCauseCode> ToErrorCauseCode(dcsctp::ErrorKind error) {
  switch (error) {
    case dcsctp::ErrorKind::kParseFailed:
      return SctpErrorCauseCode::kUnrecognizedParameters;
    case dcsctp::ErrorKind::kPeerReported:
      return SctpErrorCauseCode::kUserInitiatedAbort;
    case dcsctp::ErrorKind::kWrongSequence:
    case dcsctp::ErrorKind::kProtocolViolation:
      return SctpErrorCauseCode::kProtocolViolation;
    case dcsctp::ErrorKind::kResourceExhaustion:
      return SctpErrorCauseCode::kOutOfResource;
    case dcsctp::ErrorKind::kTooManyRetries:
    case dcsctp::ErrorKind::kUnsupportedOperation:
    case dcsctp::ErrorKind::kNoError:
    case dcsctp::ErrorKind::kNotConnected:
      // No SCTP error cause code matches those.
      break;
  }
  return std::nullopt;
}

bool IsEmptyPPID(dcsctp::PPID ppid) {
  WebrtcPPID webrtc_ppid = static_cast<WebrtcPPID>(ppid.value());
  return webrtc_ppid == WebrtcPPID::kStringEmpty || webrtc_ppid == WebrtcPPID::kBinaryEmpty;
}

std::string GetDebugName() {
  static std::atomic<int> instance_count = 0;
  return "DcSctpTransport" + std::to_string(instance_count++);
}

}  // namespace

DcSctpTransport::DcSctpTransport(Timestamp now, p2p::DtlsTransport* transport, Config config)
    : DcSctpTransport(now, transport, config, std::make_unique<dcsctp::DcSctpSocketFactory>()) {}

DcSctpTransport::DcSctpTransport(Timestamp now, p2p::DtlsTransport* transport, Config config,
                                 std::unique_ptr<dcsctp::DcSctpSocketFactory> socket_factory)
    : transport_(transport),
      config_(config),
      random_(static_cast<uint64_t>(now.us())),
      now_(now),
      socket_factory_(std::move(socket_factory)),
      timeout_factory_([this](dcsctp::TimeoutID timeout_id) { socket_->HandleTimeout(timeout_id); }, now),
      debug_name_(GetDebugName()) {
  ConnectTransportSignals();
}

DcSctpTransport::~DcSctpTransport() {
  DisconnectTransportSignals();
  if (socket_) {
    socket_->Close();
  }
}

void DcSctpTransport::SetNow(Timestamp now) {
  if (now > now_) now_ = now;
  timeout_factory_.set_now(now_);
}

void DcSctpTransport::SetOnConnectedCallback(std::function<void()> callback) {
  on_connected_callback_ = std::move(callback);
}

void DcSctpTransport::SetDataChannelSink(DataChannelSink* sink) {
  data_channel_sink_ = sink;
  if (data_channel_sink_ && ready_to_send_data_) {
    data_channel_sink_->OnReadyToSend();
  }
}

p2p::DtlsTransport* DcSctpTransport::dtls_transport() const { return transport_; }

bool DcSctpTransport::Start(Timestamp now, const SctpOptions& options) {
  SM_DCSCTP_DCHECK(options.max_message_size > 0);
  SetNow(now);
  if (socket_ == nullptr) {
    dcsctp::DcSctpOptions dcsctp_options = CreateDcSctpOptions(options, config_);
    if (options.local_init.has_value()) {
      local_init_ = *options.local_init;
    }
    if (options.remote_init.has_value()) {
      remote_init_ = *options.remote_init;
    }
    socket_ = socket_factory_->Create(debug_name_, *this, nullptr, dcsctp_options);
  } else {
    if (options.local_port != socket_->options().local_port ||
        options.remote_port != socket_->options().remote_port) {
      SM_DCSCTP_LOG(kError) << debug_name_ << "->Start(local=" << options.local_port
                            << ", remote=" << options.remote_port
                            << "): Can't change ports on already started transport.";
      return false;
    }
    bool negotiating_snap = options.local_init.has_value() && options.remote_init.has_value();
    if (negotiating_snap && (options.local_init != local_init_ || options.remote_init != remote_init_)) {
      SM_DCSCTP_LOG(kError) << debug_name_ << "->Start(): Can't change sctp-init on already started transport.";
      return false;
    }
    socket_->SetMaxMessageSize(static_cast<size_t>(options.max_message_size));
  }

  MaybeConnectSocket();

  for (const auto& [sid, stream_state] : stream_states_) {
    socket_->SetStreamPriority(sid, stream_state.priority);
  }
  if (data_channel_sink_ != nullptr) {
    data_channel_sink_->OnMaxMessageSize(options.max_message_size);
  }

  return true;
}

bool DcSctpTransport::OpenStream(int sid, PriorityValue priority) {
  StreamState stream_state;
  stream_state.priority = dcsctp::StreamPriority(priority.value());
  stream_states_.insert_or_assign(dcsctp::StreamID(static_cast<uint16_t>(sid)), stream_state);
  if (socket_) {
    socket_->SetStreamPriority(dcsctp::StreamID(static_cast<uint16_t>(sid)),
                               dcsctp::StreamPriority(priority.value()));
  }
  return true;
}

bool DcSctpTransport::ResetStream(Timestamp now, int sid) {
  SetNow(now);
  if (!socket_) {
    SM_DCSCTP_LOG(kError) << debug_name_ << "->ResetStream(sid=" << sid << "): Transport is not started.";
    return false;
  }

  dcsctp::StreamID streams[1] = {dcsctp::StreamID(static_cast<uint16_t>(sid))};

  auto it = stream_states_.find(streams[0]);
  if (it == stream_states_.end()) {
    SM_DCSCTP_LOG(kError) << debug_name_ << "->ResetStream(sid=" << sid << "): Stream is not open.";
    return false;
  }

  StreamState& stream_state = it->second;
  if (stream_state.closure_initiated || stream_state.incoming_reset_done || stream_state.outgoing_reset_done) {
    // The closing procedure was already initiated by the remote, don't do anything.
    return false;
  }
  stream_state.closure_initiated = true;
  socket_->ResetStreams(streams);
  return true;
}

RtcError DcSctpTransport::SendData(Timestamp now, int sid, const SendDataParams& params,
                                   std::span<const uint8_t> payload) {
  SetNow(now);
  if (!socket_) {
    SM_DCSCTP_LOG(kError) << debug_name_ << "->SendData(...): Transport is not started.";
    return RtcError(RtcErrorType::kInvalidState);
  }

  // A message may be sent at the same time a data channel is closing, before the sender knows. Keep track of the
  // active data channels and skip messages for the ones that are not open, or closing: the data channel API allows
  // discarding queued messages when the channel is closing.
  auto stream_state = stream_states_.find(dcsctp::StreamID(static_cast<uint16_t>(sid)));
  if (stream_state == stream_states_.end()) {
    return RtcError(RtcErrorType::kInvalidState);
  }

  if (stream_state->second.closure_initiated || stream_state->second.incoming_reset_done ||
      stream_state->second.outgoing_reset_done) {
    return RtcError(RtcErrorType::kInvalidState);
  }

  auto max_message_size = socket_->options().max_message_size;
  if (max_message_size > 0 && payload.size() > max_message_size) {
    SM_DCSCTP_LOG(kWarning) << debug_name_
                            << "->SendData(...): Trying to send packet bigger than the max message size: "
                            << payload.size() << " vs max of " << max_message_size;
    return RtcError(RtcErrorType::kInvalidRange);
  }

  std::vector<uint8_t> message_payload(payload.begin(), payload.end());
  if (message_payload.empty()) {
    // https://www.rfc-editor.org/rfc/rfc8831.html#section-6.6
    // SCTP does not support the sending of empty user messages. Therefore, if an empty message has to be sent, the
    // appropriate PPID (WebRTC String Empty or WebRTC Binary Empty) is used, and the SCTP user message of one zero
    // byte is sent.
    message_payload.push_back('\0');
  }

  dcsctp::DcSctpMessage message(dcsctp::StreamID(static_cast<uint16_t>(sid)),
                                dcsctp::PPID(static_cast<uint16_t>(ToPPID(params.type, payload.size()))),
                                std::move(message_payload));

  dcsctp::SendOptions send_options;
  send_options.unordered = dcsctp::IsUnordered(!params.ordered);
  if (params.max_rtx_ms.has_value()) {
    SM_DCSCTP_DCHECK(*params.max_rtx_ms >= 0 && *params.max_rtx_ms <= std::numeric_limits<uint16_t>::max());
    send_options.lifetime = dcsctp::DurationMs(*params.max_rtx_ms);
  }
  if (params.max_rtx_count.has_value()) {
    SM_DCSCTP_DCHECK(*params.max_rtx_count >= 0 && *params.max_rtx_count <= std::numeric_limits<uint16_t>::max());
    send_options.max_retransmissions = static_cast<size_t>(*params.max_rtx_count);
  }

  dcsctp::SendStatus error = socket_->Send(std::move(message), send_options);
  switch (error) {
    case dcsctp::SendStatus::kSuccess:
      return RtcError::OK();
    case dcsctp::SendStatus::kErrorResourceExhaustion:
      ready_to_send_data_ = false;
      return RtcError(RtcErrorType::kResourceExhausted);
    default: {
      std::string_view error_message = dcsctp::ToString(error);
      SM_DCSCTP_LOG(kError) << debug_name_ << "->SendData(...): send() failed with error " << error_message << ".";
      return RtcError(RtcErrorType::kNetworkError, std::string(error_message));
    }
  }
}

bool DcSctpTransport::ReadyToSendData() { return ready_to_send_data_; }

int DcSctpTransport::max_message_size() const {
  if (!socket_) {
    return 0;
  }
  return static_cast<int>(socket_->options().max_message_size);
}

std::optional<int> DcSctpTransport::max_outbound_streams() const {
  if (!socket_) return std::nullopt;
  auto metrics = socket_->GetMetrics();
  if (!metrics.has_value()) return std::nullopt;
  return metrics->negotiated_maximum_outgoing_streams;
}

std::optional<int> DcSctpTransport::max_inbound_streams() const {
  if (!socket_) return std::nullopt;
  auto metrics = socket_->GetMetrics();
  if (!metrics.has_value()) return std::nullopt;
  return metrics->negotiated_maximum_incoming_streams;
}

size_t DcSctpTransport::buffered_amount(int sid) const {
  if (!socket_) return 0;
  return socket_->buffered_amount(dcsctp::StreamID(static_cast<uint16_t>(sid)));
}

size_t DcSctpTransport::buffered_amount_low_threshold(int sid) const {
  if (!socket_) return 0;
  return socket_->buffered_amount_low_threshold(dcsctp::StreamID(static_cast<uint16_t>(sid)));
}

void DcSctpTransport::SetBufferedAmountLowThreshold(int sid, size_t bytes) {
  if (!socket_) return;
  socket_->SetBufferedAmountLowThreshold(dcsctp::StreamID(static_cast<uint16_t>(sid)), bytes);
}

Timestamp DcSctpTransport::NextProcessTime() const { return timeout_factory_.NextProcessTime(); }

void DcSctpTransport::Process(Timestamp now) {
  SetNow(now);
  timeout_factory_.Process(now_);
}

SendPacketStatus DcSctpTransport::SendPacketWithStatus(std::span<const uint8_t> data) {
  SM_DCSCTP_DCHECK(socket_);

  if (data.size() > socket_->options().mtu) {
    SM_DCSCTP_LOG(kError) << debug_name_
                          << "->SendPacket(...): SCTP seems to have made a packet that is bigger than its official "
                             "MTU: "
                          << data.size() << " vs max of " << socket_->options().mtu;
    return SendPacketStatus::kError;
  }

  if (!transport_ || !transport_->writable()) return SendPacketStatus::kError;

  int result = transport_->SendPacket(now_, data, p2p::PacketOptions(), 0);

  if (result < 0) {
    SM_DCSCTP_LOG(kWarning) << debug_name_ << "->SendPacket(length=" << data.size()
                            << ") failed with error: " << transport_->GetError() << ".";
    if (p2p::IsBlockingError(transport_->GetError())) {
      return SendPacketStatus::kTemporaryFailure;
    }
    return SendPacketStatus::kError;
  }
  return SendPacketStatus::kSuccess;
}

std::unique_ptr<dcsctp::Timeout> DcSctpTransport::CreateTimeout(dcsctp::DelayPrecision precision) {
  return timeout_factory_.CreateTimeout(precision);
}

dcsctp::TimeMs DcSctpTransport::TimeMillis() { return dcsctp::TimeMs(now_.ms()); }

Timestamp DcSctpTransport::Now() { return now_; }

uint32_t DcSctpTransport::GetRandomInt(uint32_t low, uint32_t high) {
  // webrtc::Random::Rand(low, high): uniform in [low, high].
  return static_cast<uint32_t>(random_.UniformInt(low, high));
}

void DcSctpTransport::OnTotalBufferedAmountLow() {
  if (!ready_to_send_data_) {
    ready_to_send_data_ = true;
    if (data_channel_sink_) {
      data_channel_sink_->OnReadyToSend();
    }
  }
}

void DcSctpTransport::OnBufferedAmountLow(dcsctp::StreamID stream_id) {
  if (data_channel_sink_) {
    data_channel_sink_->OnBufferedAmountLow(*stream_id);
  }
}

void DcSctpTransport::OnMessageReceived(dcsctp::DcSctpMessage message) {
  auto type = ToDataMessageType(message.ppid());
  if (!type.has_value()) {
    SM_DCSCTP_LOG(kVerbose) << debug_name_ << "->OnMessageReceived(): Received an unknown PPID "
                            << message.ppid().value() << " on an SCTP packet. Dropping.";
    return;
  }
  std::span<const uint8_t> receive_buffer;
  if (!IsEmptyPPID(message.ppid())) receive_buffer = message.payload();

  if (data_channel_sink_) {
    data_channel_sink_->OnDataReceived(message.stream_id().value(), *type, receive_buffer);
  }
}

void DcSctpTransport::OnError(dcsctp::ErrorKind error, std::string_view message) {
  if (error == dcsctp::ErrorKind::kResourceExhaustion) {
    // A message failed to be enqueued because the send buffer is full: a very common (and wanted) state for high
    // throughput sending and benchmarks.
    SM_DCSCTP_LOG(kVerbose) << debug_name_ << "->OnError(error=" << dcsctp::ToString(error) << ", message=" << message
                            << ").";
  } else {
    SM_DCSCTP_LOG(kError) << debug_name_ << "->OnError(error=" << dcsctp::ToString(error) << ", message=" << message
                          << ").";
  }
}

void DcSctpTransport::OnAborted(dcsctp::ErrorKind error, std::string_view message) {
  SM_DCSCTP_LOG(kError) << debug_name_ << "->OnAborted(error=" << dcsctp::ToString(error) << ", message=" << message
                        << ").";
  ready_to_send_data_ = false;
  RtcError rtc_error(RtcErrorType::kOperationErrorWithData, std::string(message));
  rtc_error.set_error_detail(RtcErrorDetailType::kSctpFailure);
  auto code = ToErrorCauseCode(error);
  if (code.has_value()) {
    rtc_error.set_sctp_cause_code(static_cast<uint16_t>(*code));
  }
  if (data_channel_sink_) {
    data_channel_sink_->OnTransportClosed(rtc_error);
  }
}

void DcSctpTransport::OnConnected() {
  ready_to_send_data_ = true;
  if (on_connected_callback_) {
    on_connected_callback_();
  }
  if (data_channel_sink_) {
    data_channel_sink_->OnTransportConnected();
    data_channel_sink_->OnReadyToSend();
  }
}

void DcSctpTransport::OnClosed() { ready_to_send_data_ = false; }

void DcSctpTransport::OnConnectionRestarted() {}

void DcSctpTransport::OnStreamsResetFailed(std::span<const dcsctp::StreamID> outgoing_streams,
                                           std::string_view reason) {
  for (auto& stream_id : outgoing_streams) {
    SM_DCSCTP_LOG(kWarning) << debug_name_ << "->OnStreamsResetFailed(...): Outgoing stream reset failed, sid="
                            << stream_id.value() << ", reason: " << reason << ".";
  }
}

void DcSctpTransport::OnStreamsResetPerformed(std::span<const dcsctp::StreamID> outgoing_streams) {
  for (auto& stream_id : outgoing_streams) {
    auto it = stream_states_.find(stream_id);
    if (it == stream_states_.end()) {
      // Ignoring an outgoing stream reset for a closed stream.
      return;
    }

    StreamState& stream_state = it->second;
    stream_state.outgoing_reset_done = true;

    if (stream_state.incoming_reset_done) {
      // When the close was not initiated locally, the data channel close procedure ends when the remote ACKs the
      // reset.
      if (data_channel_sink_) {
        data_channel_sink_->OnChannelClosed(stream_id.value());
      }
      stream_states_.erase(stream_id);
    }
  }
}

void DcSctpTransport::OnIncomingStreamsReset(std::span<const dcsctp::StreamID> incoming_streams) {
  for (auto& stream_id : incoming_streams) {
    auto it = stream_states_.find(stream_id);
    if (it == stream_states_.end()) return;

    StreamState& stream_state = it->second;
    stream_state.incoming_reset_done = true;

    if (!stream_state.closure_initiated) {
      // An incoming stream reset for a close not started locally: reset the stream in the other direction too.
      dcsctp::StreamID streams[1] = {stream_id};
      socket_->ResetStreams(streams);
      if (data_channel_sink_) {
        data_channel_sink_->OnChannelClosing(stream_id.value());
      }
    }

    if (stream_state.outgoing_reset_done) {
      // The close procedure that was initiated locally is complete when the incoming reset comes.
      if (data_channel_sink_) {
        data_channel_sink_->OnChannelClosed(stream_id.value());
      }
      stream_states_.erase(stream_id);
    }
  }
}

void DcSctpTransport::ConnectTransportSignals() {
  if (!transport_) {
    return;
  }
  transport_->SubscribeWritableState(
      this, [this](p2p::PacketTransportInternal* transport, Timestamp now) {
        OnTransportWritableState(transport, now);
      });
  transport_->RegisterReceivedPacketCallback(
      this, [this](p2p::PacketTransportInternal* transport, const p2p::ReceivedPacket& packet) {
        OnTransportReadPacket(transport, packet);
      });
  transport_->SetOnCloseCallback([this]() {
    if (data_channel_sink_) {
      data_channel_sink_->OnTransportClosed({});
    }
  });
  transport_->SubscribeDtlsTransportState(this, [this](p2p::DtlsTransport* transport, p2p::DtlsTransportState state) {
    OnDtlsTransportState(transport, state);
  });
}

void DcSctpTransport::DisconnectTransportSignals() {
  if (!transport_) {
    return;
  }
  transport_->UnsubscribeWritableState(this);
  transport_->DeregisterReceivedPacketCallback(this);
  transport_->SetOnCloseCallback(nullptr);
  transport_->UnsubscribeDtlsTransportState(this);
}

void DcSctpTransport::OnTransportWritableState(p2p::PacketTransportInternal* /*transport*/, Timestamp now) {
  SetNow(now);
  MaybeConnectSocket();
}

void DcSctpTransport::OnDtlsTransportState(p2p::DtlsTransport* /*transport*/, p2p::DtlsTransportState state) {
  if (state == p2p::DtlsTransportState::kNew && socket_) {
    // A DTLS restart: reset the socket so that an SCTP INIT goes before any outgoing message. This is needed after
    // the DTLS fingerprint changed, since the peer discards messages with crypto derived from the old one. The
    // socket is started again (with changed parameters) later.
    socket_.reset();
  }
}

void DcSctpTransport::OnTransportReadPacket(p2p::PacketTransportInternal* /*transport*/,
                                            const p2p::ReceivedPacket& packet) {
  if (packet.decryption_info != p2p::ReceivedPacket::kDtlsDecrypted) {
    // Only SCTP packets.
    return;
  }
  if (packet.arrival_time.IsFinite()) SetNow(packet.arrival_time);

  // With SNAP the socket is created (in Start()) before it is connected (in MaybeConnectSocket(), once the transport
  // is writable). Buffer packets that arrive in that window: the still-closed socket would reject them as having an
  // invalid verification tag.
  bool snap_pending_connect = socket_ != nullptr && local_init_.has_value() && remote_init_.has_value() &&
                              socket_->state() == dcsctp::SocketState::kClosed;
  if (socket_ && !snap_pending_connect) {
    socket_->ReceivePacket(packet.payload);
    return;
  }

  // Buffering decrypted packets is only needed in an edge case of SNAP.
  if (early_received_packets_.size() >= kMaxEarlyReceivedPackets) {
    early_received_packets_.erase(early_received_packets_.begin());
  }
  early_received_packets_.emplace_back(packet.payload.begin(), packet.payload.end());
}

void DcSctpTransport::MaybeConnectSocket() {
  if (transport_ && transport_->writable() && socket_ && socket_->state() == dcsctp::SocketState::kClosed) {
    if (!(local_init_.has_value() && remote_init_.has_value())) {
      socket_->Connect();
      return;
    }
    socket_->ConnectWithConnectionToken(*local_init_, *remote_init_);
    // Replay the data channel packets that arrived before the socket existed.
    std::vector<std::vector<uint8_t>> packets = std::move(early_received_packets_);
    early_received_packets_.clear();
    for (const std::vector<uint8_t>& packet : packets) {
      socket_->ReceivePacket(packet);
    }
  }
}

dcsctp::DcSctpOptions DcSctpTransport::CreateDcSctpOptions(const SctpOptions& options, const Config& config) {
  dcsctp::DcSctpOptions dcsctp_options;
  dcsctp_options.local_port = options.local_port;
  dcsctp_options.remote_port = options.remote_port;
  dcsctp_options.max_message_size = static_cast<size_t>(options.max_message_size);
  dcsctp_options.max_timer_backoff_duration = kMaxTimerBackoffDuration;
  // Don't close the connection automatically on too many retransmissions.
  dcsctp_options.max_retransmissions = std::nullopt;
  dcsctp_options.max_init_retransmits = std::nullopt;
  dcsctp_options.per_stream_send_queue_limit = kMaxSendQueueSize;
  dcsctp_options.announced_maximum_outgoing_streams = static_cast<uint16_t>(options.max_sctp_streams);
  // Set only to avoid denial of service. Practically unlimited.
  dcsctp_options.max_send_buffer_size = std::numeric_limits<size_t>::max();
  dcsctp_options.enable_message_interleaving = config.enable_message_interleaving;
  return dcsctp_options;
}

std::vector<uint8_t> DcSctpTransport::GenerateConnectionToken(Timestamp now, Config config) {
  Random random(static_cast<uint64_t>(now.us()));
  auto temp_factory = std::make_unique<dcsctp::DcSctpSocketFactory>();
  return temp_factory->GenerateConnectionToken(
      CreateDcSctpOptions({}, config),
      [&random](uint32_t low, uint32_t high) { return static_cast<uint32_t>(random.UniformInt(low, high)); });
}

size_t DcSctpTransport::EarlyReceivedPacketCountForTesting() const { return early_received_packets_.size(); }

dcsctp::SocketState DcSctpTransport::socket_state() const {
  return socket_ ? socket_->state() : dcsctp::SocketState::kClosed;
}

}  // namespace sm::sctp
