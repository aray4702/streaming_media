// The SCTP transport's interface (C7 part 3, #1069, milestone 3): libwebrtc M157's media/sctp/sctp_transport_internal.h
// and the pieces of api/ it uses (api/transport/data_channel_transport_interface.h's DataMessageType, SendDataParams
// and DataChannelSink; api/rtc_error.h's RTCError, the fields the SCTP layer sets; api/priority.h's PriorityValue).
// Those files are not in compare/webrtc/reference/ (#1069 asks for them); the names and values here are the ones
// media/sctp/dcsctp_transport.cc and pc/sctp_transport.cc, which are, use. The constants are checked against
// reference/ where it has them: kMaxSctpStreams against api/webrtc_sdp_unittest.cc's "a=sctpmap:5000
// webrtc-datachannel 65535", kSctpDefaultPort against its a=sctp-port:5000, kSctpSendBufferSize against
// media_session.cc and dcsctp's own max_message_size default (256 KiB).
//
// The library's time model: calls that can send take `now` (the DTLS transport's SendPacket needs it).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/core/units.h"

namespace sm::p2p {
class DtlsTransport;
}

namespace sm::sctp {

// The size of the SCTP association send buffer: 256 KiB, the usrsctp default, and the max message size offered in
// SDP (media_session.cc).
inline constexpr int kSctpSendBufferSize = 256 * 1024;
// The number of streams negotiated (RTCConfiguration::max_sctp_streams' default).
inline constexpr int kMaxSctpStreams = 65535;
inline constexpr int kMaxSctpSid = kMaxSctpStreams - 1;
inline constexpr int kMinSctpSid = 0;
// The SCTP port both ends use (a=sctp-port); not related to the ports at the IP level.
inline constexpr int kSctpDefaultPort = 5000;
// DataChannelInterface::MaxSendQueueSize(): the per-channel send queue limit, 16 MiB.
inline constexpr size_t kMaxSendQueueSize = 16 * 1024 * 1024;

// The error cause codes of RFC 4960 section 3.3.10 (SctpErrorCauseCode).
enum class SctpErrorCauseCode : uint16_t {
  kInvalidStreamIdentifier = 1,
  kMissingMandatoryParameter = 2,
  kStaleCookieError = 3,
  kOutOfResource = 4,
  kUnresolvableAddress = 5,
  kUnrecognizedChunkType = 6,
  kInvalidMandatoryParameter = 7,
  kUnrecognizedParameters = 8,
  kNoUserData = 9,
  kCookieReceivedWhileShuttingDown = 10,
  kRestartWithNewAddresses = 11,
  kUserInitiatedAbort = 12,
  kProtocolViolation = 13,
};

// api/rtc_error.h's RTCError, the part the SCTP layer uses.
enum class RtcErrorType {
  kNone,
  kUnsupportedOperation,
  kUnsupportedParameter,
  kInvalidParameter,
  kInvalidRange,
  kSyntaxError,
  kInvalidState,
  kInvalidModification,
  kNetworkError,
  kResourceExhausted,
  kInternalError,
  kOperationErrorWithData,
};
enum class RtcErrorDetailType { kNone, kSctpFailure };
class RtcError {
 public:
  RtcError() = default;
  explicit RtcError(RtcErrorType type, std::string message = {}) : type_(type), message_(std::move(message)) {}
  static RtcError OK() { return RtcError(); }
  bool ok() const { return type_ == RtcErrorType::kNone; }
  RtcErrorType type() const { return type_; }
  const std::string& message() const { return message_; }
  RtcErrorDetailType error_detail() const { return detail_; }
  void set_error_detail(RtcErrorDetailType detail) { detail_ = detail; }
  std::optional<uint16_t> sctp_cause_code() const { return sctp_cause_code_; }
  void set_sctp_cause_code(uint16_t code) { sctp_cause_code_ = code; }

 private:
  RtcErrorType type_ = RtcErrorType::kNone;
  std::string message_;
  RtcErrorDetailType detail_ = RtcErrorDetailType::kNone;
  std::optional<uint16_t> sctp_cause_code_;
};

// api/priority.h's PriorityValue: the RFC 8831 section 6.4 priority, 256 for "low" (Priority::kLow).
class PriorityValue {
 public:
  static constexpr uint16_t kVeryLow = 128;
  static constexpr uint16_t kLow = 256;
  static constexpr uint16_t kMedium = 512;
  static constexpr uint16_t kHigh = 1024;
  constexpr PriorityValue() = default;
  constexpr explicit PriorityValue(uint16_t value) : value_(value) {}
  constexpr uint16_t value() const { return value_; }

 private:
  uint16_t value_ = kLow;
};

// What a message carries (DataMessageType).
enum class DataMessageType { kText, kBinary, kControl };

// How a message is sent (SendDataParams): ordered or not, and its partial reliability (at most one of the two).
struct SendDataParams {
  DataMessageType type = DataMessageType::kText;
  bool ordered = false;
  std::optional<int> max_rtx_count;
  std::optional<int> max_rtx_ms;
};

// The receiver of what the transport reports (DataChannelSink).
class DataChannelSink {
 public:
  virtual ~DataChannelSink() = default;
  virtual void OnDataReceived(int channel_id, DataMessageType type, std::span<const uint8_t> buffer) = 0;
  // The remote end started closing the channel (it reset its outgoing stream).
  virtual void OnChannelClosing(int channel_id) = 0;
  // Both directions of the channel's stream are reset.
  virtual void OnChannelClosed(int channel_id) = 0;
  virtual void OnReadyToSend() = 0;
  virtual void OnTransportClosed(RtcError error) = 0;
  virtual void OnBufferedAmountLow(int /*channel_id*/) {}
  virtual void OnTransportConnected() {}
  virtual void OnMaxMessageSize(int /*max_message_size*/) {}
};

// The SDP-negotiated parameters of the association (SctpOptions).
struct SctpOptions {
  int local_port = kSctpDefaultPort;
  int remote_port = kSctpDefaultPort;
  int max_message_size = kSctpSendBufferSize;
  int max_sctp_streams = kMaxSctpStreams;
  // SCTP Negotiation Acceleration Protocol (SNAP): a=sctp-init of both descriptions, from GenerateConnectionToken.
  std::optional<std::vector<uint8_t>> local_init;
  std::optional<std::vector<uint8_t>> remote_init;
};

// SctpTransportInternal: an SCTP association over a DTLS transport, with streams by SID.
class SctpTransportInternal {
 public:
  virtual ~SctpTransportInternal() = default;
  virtual void SetOnConnectedCallback(std::function<void()> callback) = 0;
  virtual void SetDataChannelSink(DataChannelSink* sink) = 0;
  virtual p2p::DtlsTransport* dtls_transport() const = 0;
  // Starts the association with the negotiated options, or updates the max message size of a started one. False
  // if the ports (or SNAP's sctp-init) differ from the first call's.
  virtual bool Start(Timestamp now, const SctpOptions& options) = 0;
  // Records a stream (a data channel) and its priority. Sending on a SID that isn't open fails.
  virtual bool OpenStream(int sid, PriorityValue priority) = 0;
  // Starts closing a stream: resets its outgoing direction (RFC 6525); the channel is closed when both are.
  virtual bool ResetStream(Timestamp now, int sid) = 0;
  virtual RtcError SendData(Timestamp now, int sid, const SendDataParams& params,
                            std::span<const uint8_t> payload) = 0;
  // False after a send failed for a full send buffer, until it drains (OnReadyToSend).
  virtual bool ReadyToSendData() = 0;
  virtual int max_message_size() const = 0;
  virtual std::optional<int> max_outbound_streams() const = 0;
  virtual std::optional<int> max_inbound_streams() const = 0;
  virtual size_t buffered_amount(int sid) const = 0;
  virtual size_t buffered_amount_low_threshold(int sid) const = 0;
  virtual void SetBufferedAmountLowThreshold(int sid, size_t bytes) = 0;
  virtual size_t EarlyReceivedPacketCountForTesting() const { return 0; }

  // The library's time model: the earliest time the association's timers need Process, and Process itself.
  virtual Timestamp NextProcessTime() const = 0;
  virtual void Process(Timestamp now) = 0;
};

}  // namespace sm::sctp
