// libwebrtc M157's pc/sctp_transport.cc (compare/webrtc/reference/). See sm/sctp/sctp_transport.h.
#include "sm/sctp/sctp_transport.h"

#include <algorithm>
#include <utility>

#include "sm/dcsctp/rtc_compat.h"

namespace sm::sctp {

SctpTransport::SctpTransport(std::unique_ptr<SctpTransportInternal> internal, p2p::DtlsTransport* dtls_transport)
    : info_(SctpTransportState::kConnecting, dtls_transport, /*max_message_size=*/std::nullopt,
            /*max_channels=*/std::nullopt),
      internal_sctp_transport_(std::move(internal)),
      dtls_transport_(dtls_transport) {
  if (internal_sctp_transport_->dtls_transport()) {
    internal_sctp_transport_->dtls_transport()->SubscribeDtlsTransportState(
        this, [this](p2p::DtlsTransport* transport, p2p::DtlsTransportState state) {
          OnDtlsStateChange(transport, state);
        });
  }
  internal_sctp_transport_->SetOnConnectedCallback([this]() { OnAssociationChangeCommunicationUp(); });
}

SctpTransport::~SctpTransport() {
  if (internal_sctp_transport_ && internal_sctp_transport_->dtls_transport()) {
    internal_sctp_transport_->dtls_transport()->UnsubscribeDtlsTransportState(this);
  }
}

RtcError SctpTransport::OpenChannel(int channel_id, PriorityValue priority) {
  internal_sctp_transport_->OpenStream(channel_id, priority);
  return RtcError::OK();
}

RtcError SctpTransport::SendData(Timestamp now, int channel_id, const SendDataParams& params,
                                 std::span<const uint8_t> buffer) {
  return internal_sctp_transport_->SendData(now, channel_id, params, buffer);
}

RtcError SctpTransport::CloseChannel(Timestamp now, int channel_id) {
  internal_sctp_transport_->ResetStream(now, channel_id);
  return RtcError::OK();
}

void SctpTransport::SetDataSink(DataChannelSink* sink) { internal_sctp_transport_->SetDataChannelSink(sink); }

bool SctpTransport::IsReadyToSend() const { return internal_sctp_transport_->ReadyToSendData(); }

size_t SctpTransport::buffered_amount(int channel_id) const {
  return internal_sctp_transport_->buffered_amount(channel_id);
}

size_t SctpTransport::buffered_amount_low_threshold(int channel_id) const {
  return internal_sctp_transport_->buffered_amount_low_threshold(channel_id);
}

void SctpTransport::SetBufferedAmountLowThreshold(int channel_id, size_t bytes) {
  internal_sctp_transport_->SetBufferedAmountLowThreshold(channel_id, bytes);
}

std::optional<p2p::SslRole> SctpTransport::DtlsRole() const {
  if (!internal_sctp_transport_ || !internal_sctp_transport_->dtls_transport()) {
    return std::nullopt;
  }
  p2p::SslRole role;
  if (internal_sctp_transport_->dtls_transport()->GetDtlsRole(&role)) {
    return role;
  }
  return std::nullopt;
}

void SctpTransport::Clear() {
  SM_DCSCTP_DCHECK(internal_sctp_transport_);
  // The internal transport is deleted; the DTLS transport is only dropped.
  if (internal_sctp_transport_->dtls_transport()) {
    internal_sctp_transport_->dtls_transport()->UnsubscribeDtlsTransportState(this);
  }
  dtls_transport_ = nullptr;
  internal_sctp_transport_ = nullptr;
  UpdateInformation(SctpTransportState::kClosed);
}

bool SctpTransport::Start(Timestamp now, const SctpOptions& options) {
  info_ = SctpTransportInformation(info_.state(), info_.dtls_transport(), options.max_message_size,
                                   info_.MaxChannels());
  if (!internal_sctp_transport_->Start(now, options)) {
    SM_DCSCTP_LOG(kError) << "Failed to push down SCTP parameters, closing.";
    UpdateInformation(SctpTransportState::kClosed);
    return false;
  }
  return true;
}

void SctpTransport::UpdateInformation(SctpTransportState state) {
  bool must_send_update = (state != info_.state());
  info_ = SctpTransportInformation(state, dtls_transport_, info_.MaxMessageSize(), info_.MaxChannels());
  if (observer_ && must_send_update) {
    observer_->OnStateChange(info_);
  }
}

void SctpTransport::OnAssociationChangeCommunicationUp() {
  if (internal_sctp_transport_->max_outbound_streams() && internal_sctp_transport_->max_inbound_streams()) {
    int max_channels = std::min(*(internal_sctp_transport_->max_outbound_streams()),
                                *(internal_sctp_transport_->max_inbound_streams()));
    // Record max channels.
    info_ = SctpTransportInformation(info_.state(), info_.dtls_transport(), info_.MaxMessageSize(), max_channels);
  }
  UpdateInformation(SctpTransportState::kConnected);
}

void SctpTransport::OnDtlsStateChange(p2p::DtlsTransport* /*transport*/, p2p::DtlsTransportState state) {
  if (state == p2p::DtlsTransportState::kClosed || state == p2p::DtlsTransportState::kFailed) {
    UpdateInformation(SctpTransportState::kClosed);
    // TODO(http://bugs.webrtc.org/11090): Close all the data channels
  }
}

}  // namespace sm::sctp
