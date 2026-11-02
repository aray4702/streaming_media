// TURN client port over UDP (issue #1031, C4 part 1, milestone 3): libwebrtc
// M157's p2p/base/turn_port.{h,cc} and p2p/client/turn_port_factory.{h,cc}
// (compare/webrtc/reference/p2p/base/ and p2p/client/).
//
// A TurnPort allocates a relayed address on a TURN server (RFC 5766) and
// gathers it as a relay candidate:
// - Allocate with REQUESTED-TRANSPORT UDP; a 401 gives the realm and nonce,
//   and the request goes again with the long-term credential (USERNAME,
//   REALM, NONCE, MESSAGE-INTEGRITY keyed by MD5(username:realm:password)); a
//   second 401 fails the port. A 300 with ALTERNATE-SERVER redirects (not to
//   an address tried before, of another family, loopback, the any address or
//   a disallowed port); a 437 (allocation mismatch) starts over on a new
//   socket, at most twice. A 438 (stale nonce) on Refresh, CreatePermission
//   or ChannelBind takes the new nonce and sends again.
// - Refresh one minute before the lifetime ends (half the lifetime below 2
//   minutes, at most 59 minutes ahead); a failed refresh makes the port
//   receive-only and fails and prunes its connections. Release sends a
//   Refresh with LIFETIME 0, and the port closes on its answer.
// - Each remote address gets a TurnEntry: a CreatePermission at once,
//   refreshed every 4 minutes (the 5 minute permission less 1); the first
//   payload sent goes in a Send indication and starts a ChannelBind (channels
//   from 0x4000 up); once bound, data goes as ChannelData and the bind is
//   refreshed every 4 minutes. A permission or bind that fails or times out
//   fails and prunes the connection. An entry with no connection left is
//   deleted 5 minutes later unless a connection takes it again.
// - Incoming: ChannelData and Data indications are unwrapped and handed to
//   the connection for the peer address (or the port's unknown-address
//   handling); other messages from the server are responses.
// - The relay candidate: relay type, protocol udp, relay protocol the first
//   hop's, type preference ICE_TYPE_PREFERENCE_RELAY_UDP, the server's
//   relative priority as relay preference, the server-reflexive address as
//   related address, URL "turn:<host>:<port>?transport=udp".
//
// The library's time model replaces libwebrtc's network thread: nothing reads
// a clock or posts a task. What libwebrtc posts (the port error after an
// allocate failure, the refresh-error handling, the allocate-mismatch retry,
// the alternate-server retry, the close after a release, an entry's delayed
// delete) is queued and run by Process(now) at NextProcessTime(). The port
// remembers the latest `now` it was given, which its destructor uses to send
// the release (libwebrtc sends it from the destructor too).
//
// Not here (C4 parts 2-4, #1034-#1036): TCP, TLS and DTLS to the server,
// DNS resolution of the server's name (addresses carry no hostnames), the
// TurnCustomizer, the TURN logging id is kept but no factory field sets it,
// and the local network access permission (granted, as with no permission
// factory). Port options apply to the socket once it exists, as in libwebrtc.
// As libwebrtc's, the Create for a socket of its own passes no ICE
// tiebreaker (0); the shared-socket Create passes the allocator's.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/port.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/stun_request.h"

namespace sm::p2p {

// Comprehension-optional; servers that don't know it ignore it.
inline constexpr int STUN_ATTR_TURN_LOGGING_ID = 0xff05;
inline constexpr int TURN_DEFAULT_PORT = 3478;
inline constexpr int TURN_CHANNEL_NUMBER_START = 0x4000;
inline constexpr TimeDelta kTurnPermissionTimeout = TimeDelta::Seconds(5 * 60);
inline constexpr size_t TURN_CHANNEL_HEADER_SIZE = 4;
// Retry at most twice (three ALLOCATE requests) on 437, as RFC 5766.
inline constexpr size_t MAX_ALLOCATE_MISMATCH_RETRIES = 2;
inline constexpr int TURN_SUCCESS_RESULT_CODE = 0;

class TurnEntry;

class TurnPort : public Port {
 public:
  enum PortState {
    STATE_CONNECTING,    // Initial state, cannot send any packets.
    STATE_CONNECTED,     // Socket connected, ready to send stun requests.
    STATE_READY,         // Received allocate success, can send any packets.
    STATE_RECEIVEONLY,   // Had REFRESH_REQUEST error, cannot send any packets.
    STATE_DISCONNECTED,  // TCP connection died, cannot send/receive any packets.
  };

  static bool Validate(const CreateRelayPortArgs& args);
  // On the allocator's shared UDP socket.
  static std::unique_ptr<TurnPort> Create(Timestamp now, const CreateRelayPortArgs& args, AsyncPacketSocket* socket);
  // On a socket of its own in [min_port, max_port].
  static std::unique_ptr<TurnPort> Create(Timestamp now, const CreateRelayPortArgs& args, int min_port, int max_port);
  ~TurnPort() override;

  const ProtocolAddress& server_address() const { return server_address_; }
  SocketAddress GetLocalAddress() const;
  bool ready() const { return state_ == STATE_READY; }
  bool connected() const { return state_ == STATE_READY || state_ == STATE_CONNECTED; }
  PortState state() const { return state_; }
  const RelayCredentials& credentials() const { return credentials_; }
  void set_credentials(const RelayCredentials& credentials) { credentials_ = credentials; }
  ProtocolType GetProtocol() const override { return server_address_.proto; }
  TlsCertPolicy GetTlsCertPolicy() const { return tls_cert_policy_; }
  void SetTlsCertPolicy(TlsCertPolicy policy) { tls_cert_policy_ = policy; }
  void SetTurnLoggingId(std::string_view id) { turn_logging_id_.assign(id); }
  const std::vector<std::string>& GetTlsAlpnProtocols() const { return tls_alpn_protocols_; }
  const std::vector<std::string>& GetTlsEllipticCurves() const { return tls_elliptic_curves_; }

  // Releases the allocation: a Refresh with LIFETIME 0.
  void Release(Timestamp now);

  void PrepareAddress(Timestamp now) override;
  Connection* CreateConnection(Timestamp now, const Candidate& c, CandidateOrigin origin) override;
  int SendTo(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr, const PacketOptions& options,
             bool payload) override;
  int SetOption(SocketOption opt, int value) override;
  int GetOption(SocketOption opt, int* value) override;
  int GetError() override { return error_; }
  bool HandleIncomingPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet) override;
  bool CanHandleIncomingPacketsFrom(const SocketAddress& addr) const override {
    return server_address_.address == addr;
  }
  // Only to an address with a connection.
  void SendBindingErrorResponse(Timestamp now, StunMessage* message, const SocketAddress& addr, int error_code,
                                std::string_view reason) override;
  bool SupportsProtocol(std::string_view protocol) const override { return protocol == UDP_PROTOCOL_NAME; }

  void CancelPendingTasks() override;
  void Process(Timestamp now) override;
  Timestamp NextProcessTime() const override;

  const std::string& hash() const { return hash_; }
  const std::string& nonce() const { return nonce_; }
  const std::string& realm() const { return realm_; }
  int error() const { return error_; }
  AsyncPacketSocket* socket() const { return socket_; }
  StunRequestManager& request_manager() { return request_manager_; }
  bool HasRequests() const { return !request_manager_.empty(); }
  // Tests: the fixed-seed transaction ids (StunRequestManager).
  void set_allow_predictable_ids_for_testing(bool allow) {
    request_manager_.set_allow_predictable_ids_for_testing(allow);
  }
  size_t entry_count_for_test() const { return entries_.size(); }
  const TurnEntry* FindEntryForTest(const SocketAddress& addr) const { return FindEntry(addr); }

  void HandleConnectionDestroyed(Connection* conn) override;
  // The socket closed: fatal for every protocol.
  void OnSocketClose(AsyncPacketSocket* socket, int error);
  void CloseForTest(Timestamp now) { Close(now); }

  class CallbacksForTest {
   public:
    virtual ~CallbacksForTest() = default;
    virtual void OnTurnCreatePermissionResult(int code) = 0;
    virtual void OnTurnRefreshResult(int code) = 0;
    virtual void OnTurnPortClosed() = 0;
  };
  void SetCallbacksForTest(CallbacksForTest* callbacks) { callbacks_for_test_ = callbacks; }

 protected:
  TurnPort(Timestamp now, const Parameters& args, AsyncPacketSocket* socket, const ProtocolAddress& server_address,
           const RelayCredentials& credentials, int server_priority,
           const std::vector<std::string>& tls_alpn_protocols,
           const std::vector<std::string>& tls_elliptic_curves);
  TurnPort(Timestamp now, const Parameters& args, uint16_t min_port, uint16_t max_port,
           const ProtocolAddress& server_address, const RelayCredentials& credentials, int server_priority,
           const std::vector<std::string>& tls_alpn_protocols,
           const std::vector<std::string>& tls_elliptic_curves);

  bool CreateOrRefreshEntry(Timestamp now, Connection* conn, int channel_number);
  Dscp StunDscpValue() const override { return stun_dscp_value_; }
  void Close(Timestamp now);

 private:
  friend class TurnEntry;
  friend class TurnAllocateRequest;
  friend class TurnRefreshRequest;
  friend class TurnCreatePermissionRequest;
  friend class TurnChannelBindRequest;

  static bool AllowedTurnPort(int port);
  void OnLocalNetworkAccessPermissionGranted(Timestamp now);
  bool CreateTurnClientSocket();
  void OnReadPacket(AsyncPacketSocket* socket, const ReceivedPacket& packet);
  void OnReadyToSend(AsyncPacketSocket* socket);
  void OnAllocateMismatch(Timestamp now);
  void TryAlternateServer(Timestamp now);
  void set_nonce(std::string_view nonce) { nonce_.assign(nonce); }
  void set_realm(std::string_view realm);
  void OnRefreshError();
  void HandleRefreshError();
  bool SetAlternateServer(const SocketAddress& address);
  void AddRequestAuthInfo(StunMessage* msg);
  void OnSendStunPacket(Timestamp now, std::span<const uint8_t> data, StunRequest* request);
  void OnAllocateSuccess(const SocketAddress& address, const SocketAddress& stun_address);
  void OnAllocateError(int error_code, std::string_view reason);
  void OnAllocateRequestTimeout();
  void HandleDataIndication(const ReceivedPacket& packet);
  void HandleChannelData(uint16_t channel_id, const ReceivedPacket& packet);
  void DispatchPacket(const ReceivedPacket& packet, ProtocolType proto);
  bool ScheduleRefresh(Timestamp now, uint32_t lifetime);
  void SendRequest(Timestamp now, std::unique_ptr<StunRequest> request, int delay_ms);
  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options);
  void UpdateHash();
  bool UpdateNonce(StunMessage* response);
  void ResetNonce();
  bool HasPermission(const IpAddress& ipaddr) const;
  TurnEntry* FindEntry(const SocketAddress& address) const;
  TurnEntry* FindEntry(uint16_t channel_id) const;
  bool FailAndPruneConnection(const SocketAddress& address);
  void MaybeAddTurnLoggingId(StunMessage* message);
  std::string ReconstructServerUrl() const;
  // A task libwebrtc posts to the network thread: run at the next Process.
  void Post(std::function<void(Timestamp)> task);
  void Touch(Timestamp now) {
    if (now > last_now_) last_now_ = now;
  }

  ProtocolAddress server_address_;
  std::string server_url_;
  TlsCertPolicy tls_cert_policy_ = TlsCertPolicy::TLS_CERT_POLICY_SECURE;
  std::vector<std::string> tls_alpn_protocols_;
  std::vector<std::string> tls_elliptic_curves_;
  RelayCredentials credentials_;
  std::set<SocketAddress> attempted_server_addresses_;
  AsyncPacketSocket* socket_;
  std::unique_ptr<AsyncPacketSocket> owned_socket_;
  std::map<SocketOption, int> socket_options_;
  int error_ = 0;
  Dscp stun_dscp_value_ = Dscp::kNoChange;
  StunRequestManager request_manager_;
  std::string realm_;  // From a 401/438 response.
  std::string nonce_;  // From a 401/438 response.
  std::string hash_;   // MD5(username:realm:password).
  int next_channel_number_ = TURN_CHANNEL_NUMBER_START;
  std::vector<std::unique_ptr<TurnEntry>> entries_;
  // Entries without connections and when they are deleted (HandleConnectionDestroyed).
  std::map<TurnEntry*, Timestamp> entry_deletions_;
  PortState state_ = STATE_CONNECTING;
  int server_priority_;
  size_t allocate_mismatch_retries_ = 0;
  std::string turn_logging_id_;
  CallbacksForTest* callbacks_for_test_ = nullptr;
  std::vector<std::function<void(Timestamp)>> posted_;
  Timestamp last_now_;
};

// p2p/client/turn_port_factory: the allocator's default relay port factory.
class TurnPortFactory final : public RelayPortFactoryInterface {
 public:
  std::unique_ptr<Port> Create(Timestamp now, const CreateRelayPortArgs& args, AsyncPacketSocket* udp_socket) override;
  std::unique_ptr<Port> Create(Timestamp now, const CreateRelayPortArgs& args, int min_port, int max_port) override;
};

}  // namespace sm::p2p
