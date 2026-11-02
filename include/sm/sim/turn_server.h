// A TURN server for tests (issue #1031, C4 part 1; TCP and TLS, #1036):
// libwebrtc M157's p2p/test/turn_server.{h,cc} (compare/webrtc/reference/
// p2p/test/). It listens on internal packet sockets (UDP), and on listening
// stream sockets whose accepted connections (wrapped in a TLS adapter in
// server role for PROTO_TLS) become internal STUN-framed TCP sockets, and gives each client (source
// address, socket, protocol) one allocation with an external socket from the
// external socket factory:
// - Requests need MESSAGE-INTEGRITY with the long-term key the auth hook
//   gives for USERNAME and the server's realm; without it 401 with REALM and
//   a NONCE, with a stale or forged nonce 438 (nonces are hex(time) plus the
//   HMAC-MD5 of the time under a random key, good for 60 minutes; with
//   one-time nonces a nonce used twice is stale too), with a bad key 401,
//   missing USERNAME, REALM or NONCE 400.
// - Allocate (REQUESTED-TRANSPORT UDP only, else 442) answers
//   XOR-MAPPED-ADDRESS, XOR-RELAYED-ADDRESS and LIFETIME (the requested one,
//   at most 10 minutes); another Allocate from the client with another
//   transaction id is 437, and a request with another USERNAME 441. The
//   redirect hook answers Allocate with 300 and ALTERNATE-SERVER.
// - Refresh resets the lifetime (0 deletes the allocation). CreatePermission
//   adds a permission for the peer's IP for 5 minutes (403 for a private
//   address when told to reject them); ChannelBind binds a channel number
//   (0x4000-0x7FFF) to a peer for 10 minutes and refreshes the permission
//   (400 if either is bound to something else, or when told to reject
//   binds). Send indications and ChannelData go out of the external socket
//   to a permitted peer; packets from a peer come back as ChannelData on its
//   channel, else as a Data indication if it has a permission.
// - A binding request is answered with XOR-MAPPED-ADDRESS (a TURN server is
//   also a STUN server).
//
// The library's time model replaces libwebrtc's task queue: the server takes
// `now` from each packet's arrival time, and the timers it posts (allocation,
// permission and channel lifetimes) run in Process(now) at NextProcessTime().
// A TCP or TLS connection that closes stops serving its allocation, which
// stays until its lifetime ends (libwebrtc's too); deleting a TCP or TLS
// allocation closes its connection. Unlike libwebrtc's, packets for an
// allocation whose connection is gone are dropped (libwebrtc would send on
// the deleted socket). Deleted sockets are freed at the next Process, as
// libwebrtc posts their deletion. Not here: ECN.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/async_tcp_socket.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/port.h"
#include "sm/p2p/socket.h"
#include "sm/p2p/stun.h"

namespace sm::sim {

inline constexpr int kMinTurnChannelNumber = 0x4000;
inline constexpr int kMaxTurnChannelNumber = 0x7FFF;
// The default server port for TURN (RFC 5766).
inline constexpr int TURN_SERVER_PORT = 3478;

// Gets HA1 = MD5(username:realm:password) for a user and realm; false if
// they are not valid.
class TurnAuthInterface {
 public:
  virtual ~TurnAuthInterface() = default;
  virtual bool GetKey(std::string_view username, std::string_view realm, std::string* key) = 0;
};

// Lets a test redirect Allocate requests.
class TurnRedirectInterface {
 public:
  virtual ~TurnRedirectInterface() = default;
  virtual bool ShouldRedirect(const p2p::SocketAddress& address, p2p::SocketAddress* out) = 0;
};

class TurnServer {
 public:
  TurnServer() : nonce_key_(p2p::CreateRandomString(kNonceKeySize)) {}
  ~TurnServer() {
    allocations_.clear();
    for (auto& [socket, proto] : server_sockets_) {
      socket->DeregisterReceivedPacketCallback();
      socket->UnsubscribeCloseEvent(this);
    }
    for (auto& l : listen_sockets_) l.socket->UnsubscribeAll(this);
  }
  TurnServer(const TurnServer&) = delete;
  TurnServer& operator=(const TurnServer&) = delete;

  const std::string& realm() const { return realm_; }
  void set_realm(std::string_view realm) { realm_.assign(realm); }
  const std::string& software() const { return software_; }
  void set_software(std::string_view software) { software_.assign(software); }
  void set_auth_hook(TurnAuthInterface* hook) { auth_hook_ = hook; }
  void set_redirect_hook(TurnRedirectInterface* hook) { redirect_hook_ = hook; }
  void set_enable_otu_nonce(bool enable) { enable_otu_nonce_ = enable; }
  // Reject CreatePermission to RFC 1918 addresses.
  void set_reject_private_addresses(bool filter) { reject_private_addresses_ = filter; }
  void set_reject_bind_requests(bool filter) { reject_bind_requests_ = filter; }
  void set_enable_permission_checks(bool enable) { enable_permission_checks_ = enable; }

  // Listens for client packets on `socket`.
  void AddInternalSocket(std::unique_ptr<p2p::AsyncPacketSocket> socket, p2p::ProtocolType protocol) {
    p2p::AsyncPacketSocket* raw = socket.get();
    server_sockets_.emplace_back(std::move(socket), protocol);
    raw->RegisterReceivedPacketCallback(
        [this](p2p::AsyncPacketSocket* s, const p2p::ReceivedPacket& p) { OnInternalPacket(s, p); });
  }
  // Wraps an accepted connection for TLS (libwebrtc's SSLAdapterFactory:
  // an adapter in server role, its handshake started); nullptr refuses it.
  using SslWrapper = std::function<std::unique_ptr<p2p::Socket>(std::unique_ptr<p2p::Socket>)>;
  // Accepts client connections on the listening stream socket `socket`
  // (AddInternalServerSocket); each is framed as STUN/ChannelData
  // (AsyncStunTcpSocket) and served as `protocol`, after `ssl` if given.
  void AddInternalServerSocket(std::unique_ptr<p2p::Socket> socket, p2p::ProtocolType protocol,
                               SslWrapper ssl = nullptr) {
    p2p::Socket* raw = socket.get();
    listen_sockets_.push_back(ListenSocket{std::move(socket), protocol, std::move(ssl)});
    raw->SubscribeReadEvent(this, [this](p2p::Socket* s, Timestamp now) { OnNewInternalConnection(s, now); });
  }
  // External (relayed) sockets come from `factory`, bound to `address`. The
  // factory must outlive the server (libwebrtc takes ownership).
  void SetExternalSocketFactory(p2p::PacketSocketFactory* factory, const p2p::SocketAddress& address) {
    external_socket_factory_ = factory;
    external_addr_ = address;
  }

  size_t allocation_count() const { return allocations_.size(); }
  // The relayed address of the allocation for `client`, or nil.
  p2p::SocketAddress relayed_address(const p2p::SocketAddress& client) const {
    for (const auto& [key, a] : allocations_) {
      if (std::get<0>(key) == client) return a->external_socket_->GetLocalAddress();
    }
    return {};
  }
  // Tests: the next NONCE is made from `timestamp_ms` (SetTimestampForNextNonce).
  std::string SetTimestampForNextNonce(int64_t timestamp_ms) {
    ts_for_next_nonce_ = timestamp_ms;
    return GenerateNonce(timestamp_ms);
  }
  // Tests: sees every TURN message the server receives (StunMessageObserver).
  void SetStunMessageObserver(std::function<void(const p2p::TurnMessage&)> observer) {
    observer_ = std::move(observer);
  }
  // Tests: every TURN message and channel data the server receives.
  std::vector<uint16_t>& received_message_types() { return received_types_; }
  int received_channel_data() const { return received_channel_data_; }

  // Runs the lifetimes that ended by `now`.
  void Process(Timestamp now) {
    now_ = std::max(now_, now);
    deleted_sockets_.clear();
    std::vector<Key> expired;
    for (auto& [key, a] : allocations_) {
      a->ProcessTimers(now);
      if (a->expires_ <= now) expired.push_back(key);
    }
    for (const Key& k : expired) DestroyAllocation(k);
  }
  Timestamp NextProcessTime() const {
    Timestamp next = Timestamp::PlusInfinity();
    if (!deleted_sockets_.empty()) next = now_;
    for (const auto& [key, a] : allocations_) next = std::min(next, a->NextTimer());
    return next;
  }

 private:
  static constexpr TimeDelta kNonceTimeout = TimeDelta::Seconds(60 * 60);
  static constexpr TimeDelta kDefaultAllocationTimeout = TimeDelta::Seconds(10 * 60);
  static constexpr TimeDelta kPermissionTimeout = TimeDelta::Seconds(5 * 60);
  static constexpr TimeDelta kChannelTimeout = TimeDelta::Seconds(10 * 60);
  static constexpr size_t kNonceKeySize = 16;
  static constexpr size_t kNonceSize = 48;
  static constexpr size_t kChannelHeaderSize = 4;
  static constexpr uint32_t kIpProtoUdp = 17;

  // TurnServerConnection: (source, the socket's remote address, protocol).
  using Key = std::tuple<p2p::SocketAddress, p2p::SocketAddress, int>;
  struct Conn {
    p2p::SocketAddress src;
    p2p::AsyncPacketSocket* socket;
    Key key() const { return {src, socket->GetRemoteAddress(), static_cast<int>(proto)}; }
    p2p::ProtocolType proto;
  };

  static bool IsTurnChannelData(uint16_t msg_type) { return (msg_type & 0xC000) == 0x4000; }

  class Allocation {
   public:
    Allocation(TurnServer* server, const Conn& conn, std::unique_ptr<p2p::AsyncPacketSocket> external, std::string key)
        : server_(server), conn_(conn), external_socket_(std::move(external)), key_(std::move(key)) {
      external_socket_->RegisterReceivedPacketCallback(
          [this](p2p::AsyncPacketSocket*, const p2p::ReceivedPacket& p) { OnExternalPacket(p); });
    }
    ~Allocation() { external_socket_->DeregisterReceivedPacketCallback(); }

    void HandleTurnMessage(Timestamp now, const p2p::TurnMessage* msg) {
      switch (msg->type()) {
        case p2p::STUN_ALLOCATE_REQUEST: HandleAllocateRequest(now, msg); break;
        case p2p::TURN_REFRESH_REQUEST: HandleRefreshRequest(now, msg); break;
        case p2p::TURN_SEND_INDICATION: HandleSendIndication(now, msg); break;
        case p2p::TURN_CREATE_PERMISSION_REQUEST: HandleCreatePermissionRequest(now, msg); break;
        case p2p::TURN_CHANNEL_BIND_REQUEST: HandleChannelBindRequest(now, msg); break;
        default: break;  // Eaten.
      }
    }
    void HandleChannelData(Timestamp now, std::span<const uint8_t> payload) {
      const uint16_t channel_id = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
      auto channel = FindChannel(channel_id);
      if (channel != channels_.end()) {
        SendExternal(now, payload.subspan(kChannelHeaderSize), channel->peer);
      }
    }
    void ProcessTimers(Timestamp now) {
      std::erase_if(perms_, [now](const Permission& p) { return p.expires <= now; });
      std::erase_if(channels_, [now](const Channel& c) { return c.expires <= now; });
    }
    Timestamp NextTimer() const {
      Timestamp next = expires_;
      for (const Permission& p : perms_) next = std::min(next, p.expires);
      for (const Channel& c : channels_) next = std::min(next, c.expires);
      return next;
    }

    TurnServer* const server_;
    Conn conn_;
    std::unique_ptr<p2p::AsyncPacketSocket> external_socket_;
    std::string key_;
    std::string transaction_id_;
    std::string username_;
    std::string last_nonce_;
    Timestamp expires_ = Timestamp::PlusInfinity();

   private:
    struct Channel {
      uint16_t id;
      p2p::SocketAddress peer;
      Timestamp expires;
    };
    struct Permission {
      p2p::IpAddress peer;
      Timestamp expires;
    };

    void HandleAllocateRequest(Timestamp now, const p2p::TurnMessage* msg) {
      transaction_id_ = msg->transaction_id();
      const p2p::StunByteStringAttribute* username_attr = msg->GetByteString(p2p::STUN_ATTR_USERNAME);
      username_ = std::string(username_attr->string_view());
      const TimeDelta lifetime = ComputeLifetime(*msg);
      expires_ = now + lifetime;
      p2p::TurnMessage response(static_cast<uint16_t>(p2p::GetStunSuccessResponseType(msg->type())),
                                msg->transaction_id());
      response.AddAttribute(
          std::make_unique<p2p::StunXorAddressAttribute>(p2p::STUN_ATTR_XOR_MAPPED_ADDRESS, conn_.src));
      response.AddAttribute(std::make_unique<p2p::StunXorAddressAttribute>(p2p::STUN_ATTR_XOR_RELAYED_ADDRESS,
                                                                           external_socket_->GetLocalAddress()));
      response.AddAttribute(std::make_unique<p2p::StunUInt32Attribute>(p2p::STUN_ATTR_LIFETIME,
                                                                       static_cast<uint32_t>(lifetime.seconds())));
      SendResponse(now, &response);
    }
    void HandleRefreshRequest(Timestamp now, const p2p::TurnMessage* msg) {
      const TimeDelta lifetime = ComputeLifetime(*msg);
      expires_ = now + lifetime;  // Deleted at the next Process when 0.
      p2p::TurnMessage response(static_cast<uint16_t>(p2p::GetStunSuccessResponseType(msg->type())),
                                msg->transaction_id());
      response.AddAttribute(std::make_unique<p2p::StunUInt32Attribute>(p2p::STUN_ATTR_LIFETIME,
                                                                       static_cast<uint32_t>(lifetime.seconds())));
      SendResponse(now, &response);
    }
    void HandleSendIndication(Timestamp now, const p2p::TurnMessage* msg) {
      const p2p::StunByteStringAttribute* data_attr = msg->GetByteString(p2p::STUN_ATTR_DATA);
      const p2p::StunAddressAttribute* peer_attr = msg->GetAddress(p2p::STUN_ATTR_XOR_PEER_ADDRESS);
      if (!data_attr || !peer_attr) return;
      // Only to a peer with a permission.
      if (HasPermission(peer_attr->GetAddress().ipaddr())) {
        SendExternal(now, data_attr->array_view(), peer_attr->GetAddress());
      }
    }
    void HandleCreatePermissionRequest(Timestamp now, const p2p::TurnMessage* msg) {
      const p2p::StunAddressAttribute* peer_attr = msg->GetAddress(p2p::STUN_ATTR_XOR_PEER_ADDRESS);
      if (!peer_attr) {
        SendBadRequestResponse(now, msg);
        return;
      }
      if (server_->reject_private_addresses_ && p2p::IpIsPrivate(peer_attr->GetAddress().ipaddr())) {
        server_->SendErrorResponse(now, conn_, msg, p2p::STUN_ERROR_FORBIDDEN, p2p::STUN_ERROR_REASON_FORBIDDEN);
        return;
      }
      AddPermission(now, peer_attr->GetAddress().ipaddr());
      p2p::TurnMessage response(static_cast<uint16_t>(p2p::GetStunSuccessResponseType(msg->type())),
                                msg->transaction_id());
      SendResponse(now, &response);
    }
    void HandleChannelBindRequest(Timestamp now, const p2p::TurnMessage* msg) {
      if (server_->reject_bind_requests_) {
        SendBadRequestResponse(now, msg);
        return;
      }
      const p2p::StunUInt32Attribute* channel_attr = msg->GetUInt32(p2p::STUN_ATTR_CHANNEL_NUMBER);
      const p2p::StunAddressAttribute* peer_attr = msg->GetAddress(p2p::STUN_ATTR_XOR_PEER_ADDRESS);
      if (!channel_attr || !peer_attr) {
        SendBadRequestResponse(now, msg);
        return;
      }
      const uint16_t channel_id = static_cast<uint16_t>(channel_attr->value() >> 16);
      if (channel_id < kMinTurnChannelNumber || channel_id > kMaxTurnChannelNumber) {
        SendBadRequestResponse(now, msg);
        return;
      }
      // Neither the channel nor the peer bound to something else.
      auto channel1 = FindChannel(channel_id);
      auto channel2 = FindChannel(peer_attr->GetAddress());
      if (channel1 != channel2) {
        SendBadRequestResponse(now, msg);
        return;
      }
      if (channel1 == channels_.end()) {
        channel1 = channels_.insert(channels_.end(), Channel{channel_id, peer_attr->GetAddress(), now});
      }
      channel1->expires = now + kChannelTimeout;
      // A channel bind refreshes the permission too.
      AddPermission(now, peer_attr->GetAddress().ipaddr());
      p2p::TurnMessage response(static_cast<uint16_t>(p2p::GetStunSuccessResponseType(msg->type())),
                                msg->transaction_id());
      SendResponse(now, &response);
    }
    void OnExternalPacket(const p2p::ReceivedPacket& packet) {
      const Timestamp now = packet.arrival_time;
      auto channel = FindChannel(packet.source_address);
      if (channel != channels_.end()) {
        // A channel is bound to the peer: ChannelData.
        p2p::ByteBufferWriter buf;
        buf.WriteUInt16(channel->id);
        buf.WriteUInt16(static_cast<uint16_t>(packet.payload.size()));
        buf.Write(packet.payload);
        server_->Send(now, conn_, buf);
      } else if (!server_->enable_permission_checks_ || HasPermission(packet.source_address.ipaddr())) {
        // A permission but no channel: a Data indication.
        p2p::TurnMessage msg(p2p::TURN_DATA_INDICATION);
        msg.AddAttribute(
            std::make_unique<p2p::StunXorAddressAttribute>(p2p::STUN_ATTR_XOR_PEER_ADDRESS, packet.source_address));
        msg.AddAttribute(std::make_unique<p2p::StunByteStringAttribute>(p2p::STUN_ATTR_DATA, packet.payload));
        server_->SendStun(now, conn_, &msg);
      }
      // Otherwise dropped: no permission.
    }
    static TimeDelta ComputeLifetime(const p2p::TurnMessage& msg) {
      if (const p2p::StunUInt32Attribute* attr = msg.GetUInt32(p2p::STUN_ATTR_LIFETIME)) {
        return std::min(TimeDelta::Seconds(static_cast<int64_t>(attr->value())), kDefaultAllocationTimeout);
      }
      return kDefaultAllocationTimeout;
    }
    bool HasPermission(const p2p::IpAddress& addr) const {
      return std::any_of(perms_.begin(), perms_.end(), [&](const Permission& p) { return p.peer == addr; });
    }
    void AddPermission(Timestamp now, const p2p::IpAddress& addr) {
      auto it = std::find_if(perms_.begin(), perms_.end(), [&](const Permission& p) { return p.peer == addr; });
      if (it == perms_.end()) it = perms_.insert(perms_.end(), Permission{addr, now});
      it->expires = now + kPermissionTimeout;
    }
    std::list<Channel>::iterator FindChannel(int channel_id) {
      return std::find_if(channels_.begin(), channels_.end(), [&](const Channel& c) { return c.id == channel_id; });
    }
    std::list<Channel>::iterator FindChannel(const p2p::SocketAddress& addr) {
      return std::find_if(channels_.begin(), channels_.end(), [&](const Channel& c) { return c.peer == addr; });
    }
    void SendResponse(Timestamp now, p2p::TurnMessage* msg) {
      // Success responses always have MESSAGE-INTEGRITY.
      msg->AddMessageIntegrity(key_);
      server_->SendStun(now, conn_, msg);
    }
    void SendBadRequestResponse(Timestamp now, const p2p::TurnMessage* req) {
      server_->SendErrorResponse(now, conn_, req, p2p::STUN_ERROR_BAD_REQUEST, p2p::STUN_ERROR_REASON_BAD_REQUEST);
    }
    void SendExternal(Timestamp now, std::span<const uint8_t> data, const p2p::SocketAddress& peer) {
      external_socket_->SendTo(now, data, peer, {});
    }

    std::list<Permission> perms_;
    std::list<Channel> channels_;
  };

  struct ListenSocket {
    std::unique_ptr<p2p::Socket> socket;
    p2p::ProtocolType proto;
    SslWrapper ssl;
  };

  void OnNewInternalConnection(p2p::Socket* socket, Timestamp now) {
    now_ = std::max(now_, now);
    auto it = std::find_if(listen_sockets_.begin(), listen_sockets_.end(),
                           [socket](const ListenSocket& l) { return l.socket.get() == socket; });
    if (it == listen_sockets_.end()) return;
    p2p::SocketAddress accept_addr;
    std::unique_ptr<p2p::Socket> accepted = socket->Accept(now, &accept_addr);
    if (!accepted) return;
    if (it->ssl) {
      accepted = it->ssl(std::move(accepted));
      if (!accepted) return;
    }
    auto tcp_socket = std::make_unique<p2p::AsyncStunTcpSocket>(std::move(accepted));
    tcp_socket->SubscribeCloseEvent(this, [this](p2p::AsyncPacketSocket* s, int) { OnInternalSocketClose(s); });
    // Now the client can talk to the server over it.
    AddInternalSocket(std::move(tcp_socket), it->proto);
  }

  void OnInternalSocketClose(p2p::AsyncPacketSocket* socket) {
    auto it = std::find_if(server_sockets_.begin(), server_sockets_.end(),
                           [socket](const auto& e) { return e.first.get() == socket; });
    if (it != server_sockets_.end()) DestroyInternalSocket(it);
  }

  using ServerSockets = std::vector<std::pair<std::unique_ptr<p2p::AsyncPacketSocket>, p2p::ProtocolType>>;
  void DestroyInternalSocket(ServerSockets::iterator it) {
    std::unique_ptr<p2p::AsyncPacketSocket> socket = std::move(it->first);
    server_sockets_.erase(it);
    socket->UnsubscribeCloseEvent(this);
    socket->DeregisterReceivedPacketCallback();
    // Deleted later: this may run inside the socket's own callback.
    deleted_sockets_.push_back(std::move(socket));
  }

  void DestroyAllocation(const Key& key) {
    auto alloc = allocations_.find(key);
    if (alloc == allocations_.end()) return;
    // A TCP or TLS allocation's connection goes with it; the UDP socket is
    // shared by every allocation. (A closed TCP socket is gone already.)
    p2p::AsyncPacketSocket* socket = alloc->second->conn_.socket;
    auto it = std::find_if(server_sockets_.begin(), server_sockets_.end(),
                           [socket](const auto& e) { return e.first.get() == socket; });
    if (it != server_sockets_.end() && it->second != p2p::PROTO_UDP) DestroyInternalSocket(it);
    allocations_.erase(alloc);
  }

  void OnInternalPacket(p2p::AsyncPacketSocket* socket, const p2p::ReceivedPacket& packet) {
    now_ = std::max(now_, packet.arrival_time);
    const std::span<const uint8_t> payload = packet.payload;
    // Too small for even a channel header.
    if (payload.size() < kChannelHeaderSize) return;
    auto it = std::find_if(server_sockets_.begin(), server_sockets_.end(),
                           [socket](const auto& e) { return e.first.get() == socket; });
    if (it == server_sockets_.end()) return;
    Conn conn{packet.source_address, socket, it->second};
    const uint16_t msg_type = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
    if (!IsTurnChannelData(msg_type)) {
      HandleStunMessage(packet.arrival_time, conn, payload);
    } else {
      if (Allocation* allocation = FindAllocation(conn)) allocation->HandleChannelData(packet.arrival_time, payload);
      ++received_channel_data_;
    }
  }

  void HandleStunMessage(Timestamp now, const Conn& conn, std::span<const uint8_t> payload) {
    p2p::TurnMessage msg;
    p2p::ByteBufferReader buf(payload);
    if (!msg.Read(&buf) || buf.Length() > 0) return;
    received_types_.push_back(msg.type());
    if (observer_) observer_(msg);
    if (msg.type() == p2p::STUN_BINDING_REQUEST) {
      HandleBindingRequest(now, conn, &msg);
      return;
    }
    if (redirect_hook_ != nullptr && msg.type() == p2p::STUN_ALLOCATE_REQUEST) {
      p2p::SocketAddress address;
      if (redirect_hook_->ShouldRedirect(conn.src, &address)) {
        SendErrorResponseWithAlternateServer(now, conn, &msg, address);
        return;
      }
    }
    // The key that checks MESSAGE-INTEGRITY: the allocation's, once there is one.
    Allocation* allocation = FindAllocation(conn);
    std::string key;
    if (!allocation) {
      GetKey(&msg, &key);
    } else {
      key = allocation->key_;
    }
    // Only requests need authorization.
    if (p2p::IsStunRequestType(msg.type())) {
      if (!CheckAuthorization(now, conn, &msg, key)) return;
    }
    if (!allocation && msg.type() == p2p::STUN_ALLOCATE_REQUEST) {
      HandleAllocateRequest(now, conn, &msg, key);
    } else if (allocation &&
               (msg.type() != p2p::STUN_ALLOCATE_REQUEST || msg.transaction_id() == allocation->transaction_id_)) {
      // A non-allocate request, or a retransmitted allocate: the same user.
      if (p2p::IsStunRequestType(msg.type()) &&
          msg.GetByteString(p2p::STUN_ATTR_USERNAME)->string_view() != allocation->username_) {
        SendErrorResponse(now, conn, &msg, p2p::STUN_ERROR_WRONG_CREDENTIALS,
                          p2p::STUN_ERROR_REASON_WRONG_CREDENTIALS);
        return;
      }
      allocation->HandleTurnMessage(now, &msg);
    } else {
      SendErrorResponse(now, conn, &msg, p2p::STUN_ERROR_ALLOCATION_MISMATCH,
                        p2p::STUN_ERROR_REASON_ALLOCATION_MISMATCH);
    }
  }

  bool GetKey(const p2p::StunMessage* msg, std::string* key) {
    const p2p::StunByteStringAttribute* username_attr = msg->GetByteString(p2p::STUN_ATTR_USERNAME);
    if (!username_attr) return false;
    return auth_hook_ != nullptr && auth_hook_->GetKey(username_attr->string_view(), realm_, key);
  }

  bool CheckAuthorization(Timestamp now, const Conn& conn, p2p::StunMessage* msg, std::string_view key) {
    // RFC 5389 10.2.2.
    const p2p::StunByteStringAttribute* mi_attr = msg->GetByteString(p2p::STUN_ATTR_MESSAGE_INTEGRITY);
    const p2p::StunByteStringAttribute* username_attr = msg->GetByteString(p2p::STUN_ATTR_USERNAME);
    const p2p::StunByteStringAttribute* realm_attr = msg->GetByteString(p2p::STUN_ATTR_REALM);
    const p2p::StunByteStringAttribute* nonce_attr = msg->GetByteString(p2p::STUN_ATTR_NONCE);
    if (!mi_attr) {
      SendErrorResponseWithRealmAndNonce(now, conn, msg, p2p::STUN_ERROR_UNAUTHORIZED,
                                         p2p::STUN_ERROR_REASON_UNAUTHORIZED);
      return false;
    }
    if (!username_attr || !realm_attr || !nonce_attr) {
      SendErrorResponse(now, conn, msg, p2p::STUN_ERROR_BAD_REQUEST, p2p::STUN_ERROR_REASON_BAD_REQUEST);
      return false;
    }
    if (!ValidateNonce(now, nonce_attr->string_view())) {
      SendErrorResponseWithRealmAndNonce(now, conn, msg, p2p::STUN_ERROR_STALE_NONCE,
                                         p2p::STUN_ERROR_REASON_STALE_NONCE);
      return false;
    }
    if (key.empty() ||
        msg->ValidateMessageIntegrity(std::string(key)) != p2p::StunMessage::IntegrityStatus::kIntegrityOk) {
      SendErrorResponseWithRealmAndNonce(now, conn, msg, p2p::STUN_ERROR_UNAUTHORIZED,
                                         p2p::STUN_ERROR_REASON_UNAUTHORIZED);
      return false;
    }
    Allocation* allocation = FindAllocation(conn);
    if (enable_otu_nonce_ && allocation && allocation->last_nonce_ == nonce_attr->string_view()) {
      SendErrorResponseWithRealmAndNonce(now, conn, msg, p2p::STUN_ERROR_STALE_NONCE,
                                         p2p::STUN_ERROR_REASON_STALE_NONCE);
      return false;
    }
    if (allocation) allocation->last_nonce_ = std::string(nonce_attr->string_view());
    return true;
  }

  void HandleBindingRequest(Timestamp now, const Conn& conn, const p2p::StunMessage* req) {
    p2p::StunMessage response(static_cast<uint16_t>(p2p::GetStunSuccessResponseType(req->type())),
                              req->transaction_id());
    response.AddAttribute(std::make_unique<p2p::StunXorAddressAttribute>(p2p::STUN_ATTR_XOR_MAPPED_ADDRESS, conn.src));
    SendStun(now, conn, &response);
  }

  void HandleAllocateRequest(Timestamp now, const Conn& conn, const p2p::TurnMessage* msg, std::string_view key) {
    const p2p::StunUInt32Attribute* transport_attr = msg->GetUInt32(p2p::STUN_ATTR_REQUESTED_TRANSPORT);
    if (!transport_attr) {
      SendErrorResponse(now, conn, msg, p2p::STUN_ERROR_BAD_REQUEST, p2p::STUN_ERROR_REASON_BAD_REQUEST);
      return;
    }
    // Only UDP.
    if ((transport_attr->value() >> 24) != kIpProtoUdp) {
      SendErrorResponse(now, conn, msg, p2p::STUN_ERROR_UNSUPPORTED_PROTOCOL,
                        p2p::STUN_ERROR_REASON_UNSUPPORTED_PROTOCOL);
      return;
    }
    std::unique_ptr<p2p::AsyncPacketSocket> external =
        external_socket_factory_ ? external_socket_factory_->CreateUdpSocket(external_addr_, 0, 0) : nullptr;
    if (!external) {
      SendErrorResponse(now, conn, msg, p2p::STUN_ERROR_SERVER_ERROR, "Failed to allocate socket");
      return;
    }
    auto alloc = std::make_unique<Allocation>(this, conn, std::move(external), std::string(key));
    Allocation* raw = alloc.get();
    allocations_[conn.key()] = std::move(alloc);
    raw->HandleTurnMessage(now, msg);
  }

  // hex(now) + hex(HMAC-MD5(nonce_key_, now)), `now` as its 8 native bytes.
  std::string GenerateNonce(int64_t now_ms) const {
    uint8_t input[sizeof(now_ms)];
    std::memcpy(input, &now_ms, sizeof(now_ms));
    return HexEncode(input) + HexEncode(HmacMd5(input));
  }
  bool ValidateNonce(Timestamp now, std::string_view nonce) const {
    if (nonce.size() != kNonceSize) return false;
    uint8_t then_bytes[sizeof(int64_t)];
    for (size_t i = 0; i < sizeof(then_bytes); ++i) {
      const int hi = HexValue(nonce[2 * i]);
      const int lo = HexValue(nonce[2 * i + 1]);
      if (hi < 0 || lo < 0) return false;
      then_bytes[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    if (nonce.substr(sizeof(then_bytes) * 2) != HexEncode(HmacMd5(then_bytes))) return false;
    int64_t then;
    std::memcpy(&then, then_bytes, sizeof(then));
    return TimeDelta::Millis(now.ms() - then) < kNonceTimeout;
  }
  std::vector<uint8_t> HmacMd5(std::span<const uint8_t> data) const {
    // RFC 2104 over the installed MD5 (sm/p2p/stun.h's crypto hooks).
    constexpr size_t kBlock = 64;
    uint8_t k[kBlock] = {};
    std::memcpy(k, nonce_key_.data(), std::min(nonce_key_.size(), kBlock));
    std::vector<uint8_t> inner(kBlock), outer(kBlock);
    for (size_t i = 0; i < kBlock; ++i) {
      inner[i] = k[i] ^ 0x36;
      outer[i] = k[i] ^ 0x5c;
    }
    inner.insert(inner.end(), data.begin(), data.end());
    uint8_t h[16] = {};
    const auto md5 = p2p::GetStunCrypto().md5;
    if (md5 == nullptr || !md5(inner, h)) return {};
    outer.insert(outer.end(), h, h + 16);
    uint8_t out[16] = {};
    if (!md5(outer, out)) return {};
    return std::vector<uint8_t>(out, out + 16);
  }
  static std::string HexEncode(std::span<const uint8_t> bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    for (uint8_t b : bytes) {
      s.push_back(kHex[b >> 4]);
      s.push_back(kHex[b & 0xf]);
    }
    return s;
  }
  static int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  Allocation* FindAllocation(const Conn& conn) {
    auto it = allocations_.find(conn.key());
    return it != allocations_.end() ? it->second.get() : nullptr;
  }

  void SendErrorResponse(Timestamp now, const Conn& conn, const p2p::StunMessage* req, int code,
                         std::string_view reason) {
    p2p::TurnMessage resp(static_cast<uint16_t>(std::max(0, p2p::GetStunErrorResponseType(req->type()))),
                          req->transaction_id());
    InitErrorResponse(code, reason, &resp);
    SendStun(now, conn, &resp);
  }
  void SendErrorResponseWithRealmAndNonce(Timestamp now, const Conn& conn, const p2p::StunMessage* msg, int code,
                                          std::string_view reason) {
    p2p::TurnMessage resp(static_cast<uint16_t>(std::max(0, p2p::GetStunErrorResponseType(msg->type()))),
                          msg->transaction_id());
    InitErrorResponse(code, reason, &resp);
    int64_t timestamp = now.ms();
    if (ts_for_next_nonce_) {
      timestamp = ts_for_next_nonce_;
      ts_for_next_nonce_ = 0;
    }
    resp.AddAttribute(std::make_unique<p2p::StunByteStringAttribute>(p2p::STUN_ATTR_NONCE, GenerateNonce(timestamp)));
    resp.AddAttribute(std::make_unique<p2p::StunByteStringAttribute>(p2p::STUN_ATTR_REALM, realm_));
    SendStun(now, conn, &resp);
  }
  void SendErrorResponseWithAlternateServer(Timestamp now, const Conn& conn, const p2p::StunMessage* msg,
                                            const p2p::SocketAddress& addr) {
    p2p::TurnMessage resp(static_cast<uint16_t>(std::max(0, p2p::GetStunErrorResponseType(msg->type()))),
                          msg->transaction_id());
    InitErrorResponse(p2p::STUN_ERROR_TRY_ALTERNATE, p2p::STUN_ERROR_REASON_TRY_ALTERNATE_SERVER, &resp);
    auto alt = p2p::StunAttribute::CreateAddress(p2p::STUN_ATTR_ALTERNATE_SERVER);
    alt->SetAddress(addr);
    resp.AddAttribute(std::move(alt));
    SendStun(now, conn, &resp);
  }
  static void InitErrorResponse(int code, std::string_view reason, p2p::StunMessage* resp) {
    auto attr = p2p::StunAttribute::CreateErrorCode();
    attr->SetCode(code);
    attr->SetReason(std::string(reason));
    resp->AddAttribute(std::move(attr));
  }
  void SendStun(Timestamp now, const Conn& conn, p2p::StunMessage* msg) {
    p2p::ByteBufferWriter buf;
    msg->Write(&buf);
    Send(now, conn, buf);
  }
  void Send(Timestamp now, const Conn& conn, const p2p::ByteBufferWriter& buf) {
    // Not on a TCP or TLS connection that is gone.
    if (std::none_of(server_sockets_.begin(), server_sockets_.end(),
                     [&](const auto& e) { return e.first.get() == conn.socket; })) {
      return;
    }
    conn.socket->SendTo(now, buf.DataView(), conn.src, {});
  }

  const std::string nonce_key_;
  std::string realm_;
  std::string software_;
  TurnAuthInterface* auth_hook_ = nullptr;
  TurnRedirectInterface* redirect_hook_ = nullptr;
  // One-time-use nonces: the same nonce twice is stale (438).
  bool enable_otu_nonce_ = false;
  bool reject_private_addresses_ = false;
  bool reject_bind_requests_ = false;
  bool enable_permission_checks_ = true;
  ServerSockets server_sockets_;
  std::vector<ListenSocket> listen_sockets_;
  std::vector<std::unique_ptr<p2p::AsyncPacketSocket>> deleted_sockets_;
  p2p::PacketSocketFactory* external_socket_factory_ = nullptr;
  p2p::SocketAddress external_addr_;
  std::map<Key, std::unique_ptr<Allocation>> allocations_;
  int64_t ts_for_next_nonce_ = 0;
  std::function<void(const p2p::TurnMessage&)> observer_;
  std::vector<uint16_t> received_types_;
  int received_channel_data_ = 0;
  Timestamp now_ = Timestamp::MinusInfinity();
};

}  // namespace sm::sim
