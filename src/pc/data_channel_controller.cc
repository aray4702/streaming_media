// Ported from libwebrtc M157 pc/data_channel_controller.cc (compare/webrtc/reference/pc/data_channel_controller.cc),
// without the thread hops (sm/pc/data_channel_controller.h).
#include "sm/pc/data_channel_controller.h"

#include <algorithm>
#include <string>
#include <utility>

namespace sm::pc {

DataChannelController::DataChannelController()
    : self_(std::make_shared<SctpDataChannelControllerInterface*>(this)) {}

DataChannelController::~DataChannelController() {
  if (data_channel_transport_) data_channel_transport_->SetDataSink(nullptr);
  self_.reset();
}

void DataChannelController::Event(const char* name, double value, int64_t aux) const {
  SM_EVENT(events_, now_, "dc", name, value, aux);
}

std::shared_ptr<SctpDataChannel>* DataChannelController::Find(int channel_id) {
  auto it = std::find_if(sctp_data_channels_n_.begin(), sctp_data_channels_n_.end(), [&](const auto& c) {
    return c->sid_n().has_value() && c->sid_n()->stream_id_int() == channel_id;
  });
  return it == sctp_data_channels_n_.end() ? nullptr : &*it;
}

RtcError DataChannelController::SendData(Timestamp now, StreamId sid, const sctp::SendDataParams& params,
                                         std::span<const uint8_t> payload) {
  SetNow(now);
  if (!data_channel_transport_) return RtcError(RtcErrorType::kInvalidState);
  RtcError result = data_channel_transport_->SendData(now_, sid.stream_id_int(), params, payload);
  if (!result.ok()) Event("send_error", static_cast<double>(result.type()), sid.stream_id_int());
  return result;
}

RtcError DataChannelController::AddSctpDataStream(StreamId sid, PriorityValue priority) {
  if (data_channel_transport_) return data_channel_transport_->OpenChannel(sid.stream_id_int(), priority);
  return RtcError::OK();
}

void DataChannelController::RemoveSctpDataStream(Timestamp now, StreamId sid) {
  SetNow(now);
  if (data_channel_transport_) data_channel_transport_->CloseChannel(now_, sid.stream_id_int());
}

void DataChannelController::OnChannelStateChanged(SctpDataChannel* channel, DataState state) {
  Event("state", static_cast<double>(state), channel->id());
  if (state == DataState::kClosed) OnSctpDataChannelClosed(channel);
  channel_usage_ = sctp_data_channels_n_.empty() ? DataChannelUsage::kHaveBeenUsed : DataChannelUsage::kInUse;
}

size_t DataChannelController::buffered_amount(StreamId sid) const {
  if (!data_channel_transport_) return 0;
  return data_channel_transport_->buffered_amount(sid.stream_id_int());
}

size_t DataChannelController::buffered_amount_low_threshold(StreamId sid) const {
  if (!data_channel_transport_) return 0;
  return data_channel_transport_->buffered_amount_low_threshold(sid.stream_id_int());
}

void DataChannelController::SetBufferedAmountLowThreshold(StreamId sid, size_t bytes) {
  if (!data_channel_transport_) return;
  data_channel_transport_->SetBufferedAmountLowThreshold(sid.stream_id_int(), bytes);
}

void DataChannelController::OnTransportConnected() {
  if (!data_channel_transport_) return;
  if (auto max_channels = data_channel_transport_->MaxChannels()) sid_allocator_.SetMaxSid(*max_channels - 1);
  if (auto role = data_channel_transport_->DtlsRole()) AllocateSctpSids(*role);
}

void DataChannelController::OnDataReceived(int channel_id, sctp::DataMessageType type,
                                           std::span<const uint8_t> buffer) {
  if (HandleOpenMessage_n(channel_id, type, buffer)) return;
  if (auto* channel = Find(channel_id)) (*channel)->OnDataReceived(type, buffer);
}

void DataChannelController::OnChannelClosing(int channel_id) {
  if (auto* channel = Find(channel_id)) (*channel)->OnClosingProcedureStartedRemotely();
}

void DataChannelController::OnChannelClosed(int channel_id) {
  StreamId sid(static_cast<uint16_t>(channel_id));
  sid_allocator_.ReleaseSid(sid);
  auto it = std::find_if(sctp_data_channels_n_.begin(), sctp_data_channels_n_.end(),
                         [&](const auto& c) { return c->sid_n() == sid; });
  if (it != sctp_data_channels_n_.end()) {
    std::shared_ptr<SctpDataChannel> channel = std::move(*it);
    sctp_data_channels_n_.erase(it);
    channel->OnClosingProcedureComplete();
  }
}

void DataChannelController::OnReadyToSend() {
  auto copy = sctp_data_channels_n_;
  for (const auto& channel : copy) {
    if (channel->sid_n().has_value()) channel->OnTransportReady(now_);
  }
}

void DataChannelController::OnTransportClosed(RtcError error) {
  // Close every channel; the sids are released and the channels dropped from the list.
  std::vector<std::shared_ptr<SctpDataChannel>> temp_sctp_dcs;
  temp_sctp_dcs.swap(sctp_data_channels_n_);
  for (const auto& channel : temp_sctp_dcs) {
    channel->OnTransportChannelClosed(error);
    if (channel->sid_n().has_value()) sid_allocator_.ReleaseSid(*channel->sid_n());
  }
}

void DataChannelController::OnBufferedAmountLow(int channel_id) {
  if (auto* channel = Find(channel_id)) {
    std::shared_ptr<SctpDataChannel> keep = *channel;
    keep->OnBufferedAmountLow(now_);
  }
}

void DataChannelController::OnMaxMessageSize(int max_message_size) {
  max_message_size_ = max_message_size;
  for (auto& channel : sctp_data_channels_n_) channel->OnMaxMessageSize(max_message_size);
}

void DataChannelController::SetupDataChannelTransport_n(sctp::SctpTransport* transport) {
  set_data_channel_transport(transport);
}

void DataChannelController::TeardownDataChannelTransport_n(RtcError error) {
  OnTransportClosed(std::move(error));
  set_data_channel_transport(nullptr);
  pending_ready_.clear();
  // InvalidateWeakPtrs: channels the application still holds no longer reach this controller.
  self_ = std::make_shared<SctpDataChannelControllerInterface*>(this);
}

void DataChannelController::OnTransportChanged(sctp::SctpTransport* new_data_channel_transport) {
  if (data_channel_transport_ && data_channel_transport_ != new_data_channel_transport) {
    set_data_channel_transport(new_data_channel_transport);
  }
}

std::vector<DataChannelStats> DataChannelController::GetDataChannelStats() const {
  std::vector<DataChannelStats> stats;
  stats.reserve(sctp_data_channels_n_.size());
  for (const auto& channel : sctp_data_channels_n_) stats.push_back(channel->GetStats());
  return stats;
}

Timestamp DataChannelController::NextProcessTime() const {
  return pending_ready_.empty() ? Timestamp::PlusInfinity() : now_;
}

void DataChannelController::Process(Timestamp now) {
  SetNow(now);
  std::vector<std::weak_ptr<SctpDataChannel>> ready;
  ready.swap(pending_ready_);
  for (auto& weak : ready) {
    if (auto channel = weak.lock(); channel && channel->state() != DataState::kClosed) {
      channel->OnTransportReady(now_);
    }
  }
}

bool DataChannelController::HandleOpenMessage_n(int channel_id, sctp::DataMessageType type,
                                                std::span<const uint8_t> buffer) {
  if (type != sctp::DataMessageType::kControl || !IsOpenMessage(buffer)) return false;
  // Received OPEN message; parse and signal that a new data channel should be created.
  std::string label;
  InternalDataChannelInit config;
  config.id = channel_id;
  if (!ParseDataChannelOpenMessage(buffer, &label, &config)) return true;
  config.open_handshake_role = InternalDataChannelInit::kAcker;
  RtcError error;
  std::shared_ptr<SctpDataChannel> channel = CreateDataChannelInternal(label, config, &error);
  if (!channel) return true;
  Event("remote_open", 0, channel_id);
  channel_usage_ = DataChannelUsage::kInUse;
  const bool ready_to_send = data_channel_transport_ && data_channel_transport_->IsReadyToSend();
  if (on_data_channel_) on_data_channel_(channel);
  // The ACK goes out from the posted task, after the application saw the channel.
  if (ready_to_send) pending_ready_.push_back(channel);
  return true;
}

RtcError DataChannelController::ReserveOrAllocateSid(std::optional<StreamId>& sid,
                                                     std::optional<p2p::SslRole> fallback_ssl_role) {
  if (sid.has_value()) {
    if (sid_allocator_.ReserveSid(*sid)) return RtcError::OK();
    // A channel still closing on that sid gives it up for the new one.
    auto it = std::find_if(sctp_data_channels_n_.begin(), sctp_data_channels_n_.end(),
                           [&](const auto& c) { return c->sid_n() == sid; });
    if (it != sctp_data_channels_n_.end() && (*it)->state() == DataState::kClosing) {
      std::shared_ptr<SctpDataChannel> old = *it;
      old->CloseAbruptlyWithError(RtcError(RtcErrorType::kOperationErrorWithData, "Closed due to stream reuse."));
      if (sid_allocator_.ReserveSid(*sid)) return RtcError::OK();
    }
    return RtcError(RtcErrorType::kInvalidRange, "StreamId reserved.");
  }
  // No sid yet: allocate one by DTLS role if it is known, else after the transport connects.
  std::optional<p2p::SslRole> role;
  if (data_channel_transport_) {
    role = data_channel_transport_->DtlsRole();
    if (!role) role = fallback_ssl_role;
  }
  if (role) {
    sid = sid_allocator_.AllocateSid(*role);
    if (!sid.has_value()) return RtcError(RtcErrorType::kResourceExhausted);
  }
  return RtcError::OK();
}

std::shared_ptr<SctpDataChannel> DataChannelController::CreateDataChannelInternal(std::string_view label,
                                                                                  InternalDataChannelInit& config,
                                                                                  RtcError* error) {
  std::optional<StreamId> sid;
  if (config.id != -1) {
    if (config.id < 0 || config.id > sctp::kMaxSctpSid) {
      *error = RtcError(RtcErrorType::kInvalidRange, "StreamId out of range.");
      return nullptr;
    }
    sid = StreamId(static_cast<uint16_t>(config.id));
  }
  // Labels and protocols of all channels together are limited to 1 MiB.
  size_t total_strings_size = label.size() + config.protocol.size();
  for (const auto& dc : sctp_data_channels_n_) total_strings_size += dc->label().size() + dc->protocol().size();
  if (total_strings_size > 1024 * 1024) {
    *error = RtcError(RtcErrorType::kResourceExhausted, "DataChannel labels and protocols use too much memory");
    return nullptr;
  }
  RtcError err = ReserveOrAllocateSid(sid, config.fallback_ssl_role);
  if (!err.ok()) {
    *error = err;
    return nullptr;
  }
  if (sid.has_value()) {
    config.id = sid->stream_id_int();
    err = AddSctpDataStream(*sid, config.priority.value_or(PriorityValue(PriorityValue::kLow)));
    if (!err.ok()) {
      sid_allocator_.ReleaseSid(*sid);
      *error = err;
      return nullptr;
    }
  }
  auto channel = SctpDataChannel::Create(self_, label, data_channel_transport_ != nullptr, config, max_message_size_);
  sctp_data_channels_n_.push_back(channel);
  return channel;
}

std::shared_ptr<SctpDataChannel> DataChannelController::CreateDataChannel(Timestamp now, std::string_view label,
                                                                          const DataChannelInit& init,
                                                                          RtcError* error) {
  SetNow(now);
  RtcError local_error;
  RtcError* out = error ? error : &local_error;
  *out = RtcError::OK();
  InternalDataChannelInit new_config(init);
  if (!new_config.IsValid()) {
    *out = RtcError(RtcErrorType::kInvalidParameter, "Invalid DataChannelInit");
    return nullptr;
  }
  auto channel = CreateDataChannelInternal(label, new_config, out);
  if (!channel) return nullptr;
  if (data_channel_transport_ && data_channel_transport_->IsReadyToSend()) pending_ready_.push_back(channel);
  channel_usage_ = DataChannelUsage::kInUse;
  return channel;
}

void DataChannelController::AllocateSctpSids(p2p::SslRole role) {
  const bool ready_to_send = data_channel_transport_ && data_channel_transport_->IsReadyToSend();
  std::vector<std::shared_ptr<SctpDataChannel>> channels_to_start;
  std::vector<std::shared_ptr<SctpDataChannel>> channels_to_close;
  for (auto it = sctp_data_channels_n_.begin(); it != sctp_data_channels_n_.end();) {
    if (!(*it)->sid_n().has_value()) {
      std::optional<StreamId> sid = sid_allocator_.AllocateSid(role);
      if (sid.has_value()) {
        (*it)->SetSctpSid_n(*sid);
        AddSctpDataStream(*sid, (*it)->priority());
        channels_to_start.push_back(*it);
      } else {
        channels_to_close.push_back(std::move(*it));
        it = sctp_data_channels_n_.erase(it);
        continue;
      }
    }
    ++it;
  }
  if (ready_to_send) {
    for (auto& channel : channels_to_start) channel->OnTransportReady(now_);
  }
  for (const auto& channel : channels_to_close) channel->CloseAbruptlyWithDataChannelFailure("Failed to allocate SCTP SID");
}

void DataChannelController::OnSctpDataChannelClosed(SctpDataChannel* channel) {
  if (channel->sid_n().has_value()) sid_allocator_.ReleaseSid(*channel->sid_n());
  auto it = std::find_if(sctp_data_channels_n_.begin(), sctp_data_channels_n_.end(),
                         [&](const auto& c) { return c.get() == channel; });
  if (it != sctp_data_channels_n_.end()) sctp_data_channels_n_.erase(it);
}

void DataChannelController::set_data_channel_transport(sctp::SctpTransport* transport) {
  if (data_channel_transport_) data_channel_transport_->SetDataSink(nullptr);
  data_channel_transport_ = transport;
  if (data_channel_transport_) {
    // Channels created before the transport learn of it now.
    NotifyDataChannelsOfTransportCreated();
    data_channel_transport_->SetDataSink(this);
  }
}

void DataChannelController::NotifyDataChannelsOfTransportCreated() {
  for (const auto& channel : sctp_data_channels_n_) {
    if (channel->sid_n().has_value()) AddSctpDataStream(*channel->sid_n(), channel->priority());
    channel->OnTransportChannelCreated();
  }
}

// DataChannelSession: the runtime switch.

DataChannelSession::DataChannelSession(Timestamp now, p2p::DtlsTransport* dtls, Config config)
    : config_(std::move(config)) {
  if (!config_.enable_data_channels) return;
  auto internal = std::make_unique<sctp::DcSctpTransport>(now, dtls, config_.transport);
  sctp_ = std::make_unique<sctp::SctpTransport>(std::move(internal), dtls);
  controller_ = std::make_unique<DataChannelController>();
  controller_->SetNow(now);
  controller_->SetupDataChannelTransport_n(sctp_.get());
}

DataChannelSession::~DataChannelSession() {
  if (controller_) {
    controller_->TeardownDataChannelTransport_n(RtcError(RtcErrorType::kOperationErrorWithData, "Transport closed"));
  }
  controller_.reset();
  sctp_.reset();
}

bool DataChannelSession::Start(Timestamp now) {
  if (!sctp_) return false;
  controller_->SetNow(now);
  return sctp_->Start(now, config_.sctp);
}

std::shared_ptr<SctpDataChannel> DataChannelSession::CreateDataChannel(Timestamp now, std::string_view label,
                                                                       const DataChannelInit& config,
                                                                       RtcError* error) {
  if (!controller_) {
    if (error) *error = RtcError(RtcErrorType::kInvalidState, "Data channels are off for this connection");
    return nullptr;
  }
  return controller_->CreateDataChannel(now, label, config, error);
}

void DataChannelSession::SetOnDataChannel(std::function<void(std::shared_ptr<SctpDataChannel>)> callback) {
  if (controller_) controller_->SetOnDataChannel(std::move(callback));
}

void DataChannelSession::SetEventSink(EventSink* sink) {
  if (controller_) controller_->SetEventSink(sink);
}

Timestamp DataChannelSession::NextProcessTime() const {
  if (!sctp_) return Timestamp::PlusInfinity();
  return std::min(controller_->NextProcessTime(), sctp_->internal()->NextProcessTime());
}

void DataChannelSession::Process(Timestamp now) {
  if (!sctp_) return;
  controller_->SetNow(now);
  sctp_->internal()->Process(now);
  controller_->Process(now);
}

}  // namespace sm::pc
