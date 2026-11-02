// A STUN server for tests (issue #997): libwebrtc M157's
// p2p/test/stun_server.{h,cc} (compare/webrtc/reference/p2p/test/). It
// answers binding requests on one packet socket with the address each came
// from: XOR-MAPPED-ADDRESS, or MAPPED-ADDRESS for an RFC 3489 request
// without the magic cookie. Other requests get error 600 "Operation Not
// Supported", and datagrams that don't parse are dropped. On the emulated
// network it runs on an `EmulatedSocketServer` socket; ICE's STUN ports
// learn their server-reflexive address from it.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/stun.h"

namespace sm::sim {

class StunServer {
 public:
  explicit StunServer(std::unique_ptr<p2p::AsyncPacketSocket> socket) : socket_(std::move(socket)) {
    socket_->RegisterReceivedPacketCallback(
        [this](p2p::AsyncPacketSocket*, const p2p::ReceivedPacket& p) { OnPacket(p); });
  }
  ~StunServer() { socket_->DeregisterReceivedPacketCallback(); }
  StunServer(const StunServer&) = delete;
  StunServer& operator=(const StunServer&) = delete;

  p2p::SocketAddress address() const { return socket_->GetLocalAddress(); }
  int binding_requests() const { return binding_requests_; }

  // GetStunBindResponse: the mapped address of `remote` for `request`.
  static void GetStunBindResponse(const p2p::StunMessage& request, const p2p::SocketAddress& remote,
                                  p2p::StunMessage* response) {
    std::unique_ptr<p2p::StunAddressAttribute> mapped;
    if (request.IsLegacy()) {
      mapped = p2p::StunAttribute::CreateAddress(p2p::STUN_ATTR_MAPPED_ADDRESS);
    } else {
      mapped = p2p::StunAttribute::CreateXorAddress(p2p::STUN_ATTR_XOR_MAPPED_ADDRESS);
    }
    mapped->SetAddress(remote);
    response->AddAttribute(std::move(mapped));
  }

 private:
  void OnPacket(const p2p::ReceivedPacket& packet) {
    p2p::ByteBufferReader buf(packet.payload);
    p2p::StunMessage msg;
    if (!msg.Read(&buf)) return;
    if (msg.type() == p2p::STUN_BINDING_REQUEST) {
      ++binding_requests_;
      p2p::StunMessage response(p2p::STUN_BINDING_RESPONSE, msg.transaction_id());
      GetStunBindResponse(msg, packet.source_address, &response);
      Send(packet.arrival_time, response, packet.source_address);
    } else {
      p2p::StunMessage err(static_cast<uint16_t>(p2p::GetStunErrorResponseType(msg.type())), msg.transaction_id());
      auto code = p2p::StunAttribute::CreateErrorCode();
      code->SetCode(600);
      code->SetReason("Operation Not Supported");
      err.AddAttribute(std::move(code));
      Send(packet.arrival_time, err, packet.source_address);
    }
  }

  void Send(Timestamp now, const p2p::StunMessage& msg, const p2p::SocketAddress& to) {
    p2p::ByteBufferWriter out;
    msg.Write(&out);
    socket_->SendTo(now, out.DataView(), to, {});
  }

  std::unique_ptr<p2p::AsyncPacketSocket> socket_;
  int binding_requests_ = 0;
};

}  // namespace sm::sim
