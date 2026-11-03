// The connection's data channels, after libwebrtc M157's pc/data_channel_controller.{h,cc}
// (compare/webrtc/reference/). C7 part 4 (#1070), built with SM_WITH_DATACHANNEL=ON.
//
// Creates channels (in-band ones allocate an even or odd stream id by DTLS role once it is known; negotiated ones
// reserve theirs), answers a remote DATA_CHANNEL_OPEN with a new channel, routes received messages, stream resets,
// buffered-amount-low and transport closure to the channels, and sends their data on the SCTP transport.
//
// Differences from libwebrtc: one thread, so the tasks libwebrtc posts to the network thread (a new channel's
// OnTransportReady) run from Process(now), and those it posts to the signaling thread (OnDataChannel) are called
// directly; no PeerConnection behind it (the new-channel callback replaces PeerConnectionObserver::OnDataChannel);
// no DataChannelEventObserverInterface; events go to an EventSink.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/units.h"
#include "sm/pc/sctp_data_channel.h"
#include "sm/sctp/dcsctp_transport.h"
#include "sm/sctp/sctp_transport.h"

namespace sm::pc {

class DataChannelController : public sctp::DataChannelSink, public SctpDataChannelControllerInterface {
 public:
  DataChannelController();
  ~DataChannelController() override;
  DataChannelController(const DataChannelController&) = delete;
  DataChannelController& operator=(const DataChannelController&) = delete;

  // Events "dc": "state" (value = DataState, aux = stream id), "remote_open" (aux = stream id), "send_error"
  // (value = RtcErrorType, aux = stream id).
  void SetEventSink(EventSink* sink) { events_ = sink; }
  // A remote peer opened a channel in-band (PeerConnectionObserver::OnDataChannel).
  void SetOnDataChannel(std::function<void(std::shared_ptr<SctpDataChannel>)> callback) {
    on_data_channel_ = std::move(callback);
  }

  // RTCPeerConnection.createDataChannel(). Null, with `error` set, if the config is invalid, the id is taken or
  // out of range, or no id is left.
  std::shared_ptr<SctpDataChannel> CreateDataChannel(Timestamp now, std::string_view label,
                                                     const DataChannelInit& config, RtcError* error = nullptr);
  bool HasDataChannels() const { return channel_usage_ == DataChannelUsage::kInUse; }
  bool HasUsedDataChannels() const { return channel_usage_ != DataChannelUsage::kNeverUsed; }
  std::vector<DataChannelStats> GetDataChannelStats() const;

  // Transport life cycle.
  void SetupDataChannelTransport_n(sctp::SctpTransport* transport);
  void TeardownDataChannelTransport_n(RtcError error);
  void OnTransportChanged(sctp::SctpTransport* new_data_channel_transport);

  // Work libwebrtc posts as tasks: new channels becoming ready.
  Timestamp NextProcessTime() const;
  void Process(Timestamp now);
  // The clock for work started from a transport callback (which carries no time).
  void SetNow(Timestamp now) {
    if (now > now_) now_ = now;
  }

  // SctpDataChannelControllerInterface.
  RtcError SendData(Timestamp now, StreamId sid, const sctp::SendDataParams& params,
                    std::span<const uint8_t> payload) override;
  RtcError AddSctpDataStream(StreamId sid, PriorityValue priority) override;
  void RemoveSctpDataStream(Timestamp now, StreamId sid) override;
  void OnChannelStateChanged(SctpDataChannel* channel, DataState state) override;
  size_t buffered_amount(StreamId sid) const override;
  size_t buffered_amount_low_threshold(StreamId sid) const override;
  void SetBufferedAmountLowThreshold(StreamId sid, size_t bytes) override;

  // sctp::DataChannelSink.
  void OnDataReceived(int channel_id, sctp::DataMessageType type, std::span<const uint8_t> buffer) override;
  void OnChannelClosing(int channel_id) override;
  void OnChannelClosed(int channel_id) override;
  void OnReadyToSend() override;
  void OnTransportClosed(RtcError error) override;
  void OnBufferedAmountLow(int channel_id) override;
  void OnTransportConnected() override;
  void OnMaxMessageSize(int max_message_size) override;

 private:
  enum class DataChannelUsage { kNeverUsed, kHaveBeenUsed, kInUse };

  std::shared_ptr<SctpDataChannel> CreateDataChannelInternal(std::string_view label, InternalDataChannelInit& config,
                                                             RtcError* error);
  RtcError ReserveOrAllocateSid(std::optional<StreamId>& sid, std::optional<p2p::SslRole> fallback_ssl_role);
  bool HandleOpenMessage_n(int channel_id, sctp::DataMessageType type, std::span<const uint8_t> buffer);
  void AllocateSctpSids(p2p::SslRole role);
  void OnSctpDataChannelClosed(SctpDataChannel* channel);
  void set_data_channel_transport(sctp::SctpTransport* transport);
  void NotifyDataChannelsOfTransportCreated();
  std::shared_ptr<SctpDataChannel>* Find(int channel_id);
  void Event(const char* name, double value, int64_t aux) const;

  // Liveness token for the channels' weak references; reset in teardown (InvalidateWeakPtrs).
  ControllerHandle self_;
  sctp::SctpTransport* data_channel_transport_ = nullptr;
  std::vector<std::shared_ptr<SctpDataChannel>> sctp_data_channels_n_;
  std::vector<std::weak_ptr<SctpDataChannel>> pending_ready_;
  SctpSidAllocator sid_allocator_;
  std::optional<int> max_message_size_;
  DataChannelUsage channel_usage_ = DataChannelUsage::kNeverUsed;
  std::function<void(std::shared_ptr<SctpDataChannel>)> on_data_channel_;
  EventSink* events_ = nullptr;
  Timestamp now_ = Timestamp::MinusInfinity();
};

// The runtime switch per connection (C7): with data channels off, a connection has no SCTP transport, so no
// association, and creating a channel fails. On, it owns the SCTP transport over the connection's DTLS
// transport (sm/sctp/) and the controller. libwebrtc has no such switch (a PeerConnection always offers data
// channels when asked); the default is on, as libwebrtc. On the endpoint (sm/pc/peer_endpoint.h, #1091) the same
// switch is PeerEndpointConfig::enable_data_channels, which also decides the m=application section.
class DataChannelSession {
 public:
  struct Config {
    bool enable_data_channels = true;
    // The SCTP association's settings (ports, max message size; a=sctp-port, a=max-message-size).
    sctp::SctpOptions sctp;
    sctp::DcSctpTransport::Config transport;
  };
  DataChannelSession(Timestamp now, p2p::DtlsTransport* dtls, Config config);
  ~DataChannelSession();
  DataChannelSession(const DataChannelSession&) = delete;
  DataChannelSession& operator=(const DataChannelSession&) = delete;

  bool enabled() const { return sctp_ != nullptr; }
  // Starts the association (after the description was applied). False if data channels are off.
  bool Start(Timestamp now);
  // createDataChannel(); fails with kInvalidState when data channels are off for this connection.
  std::shared_ptr<SctpDataChannel> CreateDataChannel(Timestamp now, std::string_view label,
                                                     const DataChannelInit& config, RtcError* error = nullptr);
  void SetOnDataChannel(std::function<void(std::shared_ptr<SctpDataChannel>)> callback);
  void SetEventSink(EventSink* sink);
  // Null when data channels are off.
  sctp::SctpTransport* sctp_transport() { return sctp_.get(); }
  DataChannelController* controller() { return controller_.get(); }

  Timestamp NextProcessTime() const;
  void Process(Timestamp now);

 private:
  const Config config_;
  std::unique_ptr<sctp::SctpTransport> sctp_;
  std::unique_ptr<DataChannelController> controller_;
};

}  // namespace sm::pc
