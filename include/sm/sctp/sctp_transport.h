// The SCTP transport's state for the application (C7 part 3, #1069): libwebrtc M157's pc/sctp_transport.{h,cc}
// (compare/webrtc/reference/), with api/sctp_transport_interface.h's SctpTransportState, SctpTransportInformation
// and SctpTransportObserverInterface (not in reference/; the names are pc/sctp_transport.cc's). It owns the
// SctpTransportInternal (DcSctpTransport) and is the DataChannelTransportInterface the data channels (part 4) use.
//
// What it does, as libwebrtc does: the state starts at kConnecting, becomes kConnected when the association is up
// (with MaxChannels the smaller of the negotiated inbound and outbound stream counts), and kClosed when the DTLS
// transport closes or fails, when Start() fails, or on Clear(). The observer hears each state change.
//
// Differences from libwebrtc, none of them on the wire: no reference counting or thread hops (the library is used
// from one thread); dtls_transport() is the p2p::DtlsTransport itself rather than the api DtlsTransport wrapper.
#pragma once

#include <memory>
#include <optional>
#include <span>

#include "sm/core/units.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/sctp/sctp_transport_internal.h"

namespace sm::sctp {

enum class SctpTransportState { kNew, kConnecting, kConnected, kClosed };

class SctpTransportInformation {
 public:
  SctpTransportInformation() = default;
  SctpTransportInformation(SctpTransportState state, p2p::DtlsTransport* dtls_transport,
                           std::optional<double> max_message_size, std::optional<int> max_channels)
      : state_(state),
        dtls_transport_(dtls_transport),
        max_message_size_(max_message_size),
        max_channels_(max_channels) {}
  SctpTransportState state() const { return state_; }
  p2p::DtlsTransport* dtls_transport() const { return dtls_transport_; }
  std::optional<double> MaxMessageSize() const { return max_message_size_; }
  std::optional<int> MaxChannels() const { return max_channels_; }

 private:
  SctpTransportState state_ = SctpTransportState::kNew;
  p2p::DtlsTransport* dtls_transport_ = nullptr;
  std::optional<double> max_message_size_;
  std::optional<int> max_channels_;
};

class SctpTransportObserverInterface {
 public:
  virtual ~SctpTransportObserverInterface() = default;
  virtual void OnStateChange(SctpTransportInformation info) = 0;
};

class SctpTransport {
 public:
  SctpTransport(std::unique_ptr<SctpTransportInternal> internal, p2p::DtlsTransport* dtls_transport);
  ~SctpTransport();
  SctpTransport(const SctpTransport&) = delete;
  SctpTransport& operator=(const SctpTransport&) = delete;

  // SctpTransportInterface
  p2p::DtlsTransport* dtls_transport() const { return dtls_transport_; }
  SctpTransportInformation Information() const { return info_; }
  void RegisterObserver(SctpTransportObserverInterface* observer) { observer_ = observer; }
  void UnregisterObserver() { observer_ = nullptr; }

  // DataChannelTransportInterface
  RtcError OpenChannel(int channel_id, PriorityValue priority);
  RtcError SendData(Timestamp now, int channel_id, const SendDataParams& params, std::span<const uint8_t> buffer);
  RtcError CloseChannel(Timestamp now, int channel_id);
  void SetDataSink(DataChannelSink* sink);
  bool IsReadyToSend() const;
  size_t buffered_amount(int channel_id) const;
  size_t buffered_amount_low_threshold(int channel_id) const;
  void SetBufferedAmountLowThreshold(int channel_id, size_t bytes);
  std::optional<int> MaxChannels() const { return info_.MaxChannels(); }
  std::optional<p2p::SslRole> DtlsRole() const;

  // Drops the internal transport and the DTLS transport; the state becomes kClosed.
  void Clear();
  // Starts the internal transport; kClosed if that fails.
  bool Start(Timestamp now, const SctpOptions& options);
  SctpTransportInternal* internal() { return internal_sctp_transport_.get(); }
  const SctpTransportInternal* internal() const { return internal_sctp_transport_.get(); }

 private:
  void UpdateInformation(SctpTransportState state);
  void OnAssociationChangeCommunicationUp();
  void OnDtlsStateChange(p2p::DtlsTransport* transport, p2p::DtlsTransportState state);

  SctpTransportInformation info_;
  std::unique_ptr<SctpTransportInternal> internal_sctp_transport_;
  SctpTransportObserverInterface* observer_ = nullptr;
  p2p::DtlsTransport* dtls_transport_;
};

}  // namespace sm::sctp
