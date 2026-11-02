// sm/p2p/port.h: libwebrtc M157's p2p/base/port.cc.
#include "sm/p2p/port.h"

#include <cstdio>

#include "sm/p2p/byte_buffer.h"

namespace sm::p2p {

const char* ProtoToString(ProtocolType proto) {
  switch (proto) {
    case PROTO_UDP: return UDP_PROTOCOL_NAME;
    case PROTO_TCP: return TCP_PROTOCOL_NAME;
    case PROTO_SSLTCP: return SSLTCP_PROTOCOL_NAME;
    case PROTO_TLS: return TLS_PROTOCOL_NAME;
  }
  return UDP_PROTOCOL_NAME;
}

Port::Port(const Parameters& args, IceCandidateType type, Timestamp now) : Port(args, type, 0, 0, true, now) {}

Port::Port(const Parameters& args, IceCandidateType type, uint16_t min_port, uint16_t max_port, bool shared_socket,
           Timestamp now)
    : factory_(args.socket_factory),
      type_(type),
      network_(args.network),
      increase_host_srflx_priority_(args.increase_host_srflx_priority),
      min_port_(min_port),
      max_port_(max_port),
      content_name_(args.content_name),
      ice_username_fragment_(args.ice_username_fragment),
      password_(args.ice_password),
      ice_tiebreaker_(args.ice_tiebreaker),
      shared_socket_(shared_socket),
      network_cost_(static_cast<int16_t>(args.network->GetCost())) {
  // libwebrtc's old behaviour, kept there: empty credentials get random ones.
  if (ice_username_fragment_.empty()) {
    ice_username_fragment_ = CreateRandomString(ICE_UFRAG_LENGTH);
    password_ = CreateRandomString(ICE_PWD_LENGTH);
  }
  PostDestroyIfDead(now, /*delayed=*/true);
}

Port::~Port() = default;

void Port::SetIceParameters(int component, std::string_view username_fragment, std::string_view password) {
  component_ = component;
  ice_username_fragment_.assign(username_fragment);
  password_.assign(password);
  for (Candidate& c : candidates_) {
    c.set_component(component);
    c.set_username(username_fragment);
    c.set_password(password);
  }
  // Connections get them too (#1000).
}

void Port::AddAddress(const SocketAddress& address, const SocketAddress& base_address,
                      const SocketAddress& related_address, std::string_view protocol,
                      std::string_view relay_protocol, std::string_view tcptype, IceCandidateType type,
                      uint32_t type_preference, uint32_t relay_preference, std::string_view url, bool is_final) {
  Candidate c(component_, protocol, address, 0U, ice_username_fragment_, password_, type, generation_, "",
              network_->id(), static_cast<uint16_t>(network_cost_));
  // The relay protocol goes in before the foundation is computed.
  c.set_relay_protocol(relay_protocol);
  c.ComputeFoundation(base_address, ice_tiebreaker_);
  c.set_priority(c.GetPriority(type_preference, network_->preference(), static_cast<int>(relay_preference),
                               increase_host_srflx_priority_));
  c.set_tcptype(tcptype);
  c.set_network_name(network_->name());
  c.set_network_type(network_->type());
  c.set_underlying_type_for_vpn(network_->underlying_type_for_vpn());
  c.set_url(url);
  c.set_related_address(related_address);
  // MaybeObfuscateAddress: no mDNS responder, so never pending.
  FinishAddingAddress(c, is_final);
}

void Port::FinishAddingAddress(const Candidate& c, bool is_final) {
  candidates_.push_back(c);
  candidate_ready_.Send(this, c);
  PostAddAddress(is_final);
}

void Port::PostAddAddress(bool is_final) {
  if (is_final) NotifyPortComplete();
}

void Port::OnReadPacket(const ReceivedPacket& packet, ProtocolType proto) {
  const Timestamp now = packet.arrival_time;
  const SocketAddress& addr = packet.source_address;
  if (enable_port_packets_) {
    read_packet_.Send(this, packet.payload, addr);
    return;
  }
  std::unique_ptr<IceMessage> msg;
  std::string remote_username;
  if (!GetStunMessage(now, packet.payload, addr, &msg, &remote_username)) {
    // A non-STUN packet from an unknown address: dropped.
  } else if (!msg) {
    // Answered already.
  } else if (msg->type() == STUN_BINDING_REQUEST) {
    // Without a role the port has not been taken over by an ICE agent, and
    // the request can't be handled (MaybeIceRoleConflict): dropped.
    if (ice_role_ == ICEROLE_UNKNOWN) return;
    // The unknown address goes first, so a candidate pair exists to send a
    // role-conflict error on.
    unknown_address_.Send(now, this, addr, proto, msg.get(), remote_username, false);
    MaybeIceRoleConflict(now, addr, msg.get(), remote_username);
  } else if (msg->type() == GOOG_PING_REQUEST) {
    // A GOOG-PING to a connection that was destroyed: ask for a full binding.
    SendBindingErrorResponse(now, msg.get(), addr, STUN_ERROR_BAD_REQUEST, STUN_ERROR_REASON_BAD_REQUEST);
  }
  // Other types (responses to requests of pruned connections) are ignored.
}

bool Port::GetStunMessage(Timestamp now, std::span<const uint8_t> data, const SocketAddress& addr,
                          std::unique_ptr<IceMessage>* out_msg, std::string* out_username) {
  out_username->clear();
  // In ICE every STUN packet has a FINGERPRINT, except GOOG-PING.
  const int types[] = {GOOG_PING_REQUEST, GOOG_PING_RESPONSE, GOOG_PING_ERROR_RESPONSE};
  if (!StunMessage::IsStunMethod(types, data) && !StunMessage::ValidateFingerprint(data)) return false;

  auto stun_msg = std::make_unique<IceMessage>();
  ByteBufferReader buf(data);
  if (!stun_msg->Read(&buf) || buf.Length() > 0) return false;

  const std::vector<uint16_t> unknown_attributes = stun_msg->GetNonComprehendedAttributes();
  const int type = stun_msg->type();
  if (type == STUN_BINDING_REQUEST) {
    if (!stun_msg->GetByteString(STUN_ATTR_USERNAME) || !stun_msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY)) {
      SendBindingErrorResponse(now, stun_msg.get(), addr, STUN_ERROR_BAD_REQUEST, STUN_ERROR_REASON_BAD_REQUEST);
      return true;
    }
    std::string local_ufrag;
    std::string remote_ufrag;
    if (!ParseStunUsername(stun_msg.get(), &local_ufrag, &remote_ufrag) || local_ufrag != ice_username_fragment_) {
      SendBindingErrorResponse(now, stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED, STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }
    if (stun_msg->ValidateMessageIntegrity(password_) != StunMessage::IntegrityStatus::kIntegrityOk) {
      SendBindingErrorResponse(now, stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED, STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }
    // RFC 5389 7.3.1.
    if (!unknown_attributes.empty()) {
      SendUnknownAttributesErrorResponse(now, stun_msg.get(), addr, unknown_attributes);
      return true;
    }
    out_username->assign(remote_ufrag);
  } else if (type == STUN_BINDING_RESPONSE || type == STUN_BINDING_ERROR_RESPONSE) {
    if (type == STUN_BINDING_ERROR_RESPONSE && stun_msg->GetErrorCode() == nullptr) return true;
    // RFC 5389 7.3.3 and 7.3.4: discarded, the transaction failed.
    if (!unknown_attributes.empty()) return true;
    out_username->clear();
  } else if (type == STUN_BINDING_INDICATION) {
    out_username->clear();
    // RFC 5389 7.3.2.
    if (!unknown_attributes.empty()) return true;
  } else if (type == GOOG_PING_REQUEST) {
    if (stun_msg->ValidateMessageIntegrity(password_) != StunMessage::IntegrityStatus::kIntegrityOk) {
      SendBindingErrorResponse(now, stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED, STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }
    out_username->clear();
  } else if (type == GOOG_PING_RESPONSE || type == GOOG_PING_ERROR_RESPONSE) {
    // GOOG-MESSAGE-INTEGRITY-32 is checked by the Connection.
    out_username->clear();
  } else {
    return true;  // An invalid type.
  }
  *out_msg = std::move(stun_msg);
  return true;
}

bool Port::IsCompatibleAddress(const SocketAddress& addr) const {
  const IpAddress ip = network_->GetBestIP();
  // Single-stack sockets: the families must match.
  if (addr.family() != ip.family()) return false;
  // Link-local IPv6 ports reach only link-local IPv6 ports.
  if (ip.family() == IpFamily::kV6 && IpIsLinkLocal(ip) != IpIsLinkLocal(addr.ipaddr())) return false;
  return true;
}

bool Port::ParseStunUsername(const StunMessage* stun_msg, std::string* local_ufrag, std::string* remote_ufrag) const {
  local_ufrag->clear();
  remote_ufrag->clear();
  const StunByteStringAttribute* username_attr = stun_msg->GetByteString(STUN_ATTR_USERNAME);
  if (username_attr == nullptr) return false;
  const std::string_view username = username_attr->string_view();
  const size_t colon_pos = username.find(':');
  if (colon_pos == std::string_view::npos) return false;
  local_ufrag->assign(username.substr(0, colon_pos));
  remote_ufrag->assign(username.substr(colon_pos + 1));
  return true;
}

bool Port::MaybeIceRoleConflict(Timestamp now, const SocketAddress& addr, IceMessage* stun_msg,
                                std::string_view remote_ufrag) {
  bool ret = true;
  IceRole remote_ice_role = ICEROLE_UNKNOWN;
  uint64_t remote_tiebreaker = 0;
  const StunUInt64Attribute* stun_attr = stun_msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  if (stun_attr) {
    remote_ice_role = ICEROLE_CONTROLLING;
    remote_tiebreaker = stun_attr->value();
  }
  // Our own ufrag and tie-breaker: a loopback call, which is fine.
  if (remote_ice_role == ICEROLE_CONTROLLING && ice_username_fragment_ == remote_ufrag &&
      remote_tiebreaker == ice_tiebreaker_) {
    return true;
  }
  stun_attr = stun_msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED);
  if (stun_attr) {
    remote_ice_role = ICEROLE_CONTROLLED;
    remote_tiebreaker = stun_attr->value();
  }
  switch (ice_role_) {
    case ICEROLE_CONTROLLING:
      if (remote_ice_role == ICEROLE_CONTROLLING) {
        if (remote_tiebreaker >= ice_tiebreaker_) {
          if (role_conflict_) role_conflict_();
        } else {
          SendBindingErrorResponse(now, stun_msg, addr, STUN_ERROR_ROLE_CONFLICT, STUN_ERROR_REASON_ROLE_CONFLICT);
          ret = false;
        }
      }
      break;
    case ICEROLE_CONTROLLED:
      if (remote_ice_role == ICEROLE_CONTROLLED) {
        if (remote_tiebreaker < ice_tiebreaker_) {
          if (role_conflict_) role_conflict_();
        } else {
          SendBindingErrorResponse(now, stun_msg, addr, STUN_ERROR_ROLE_CONFLICT, STUN_ERROR_REASON_ROLE_CONFLICT);
          ret = false;
        }
      }
      break;
    default:
      break;  // libwebrtc: RTC_DCHECK_NOTREACHED.
  }
  return ret;
}

std::string Port::CreateStunUsername(std::string_view remote_username) const {
  return std::string(remote_username) + ":" + ice_username_fragment_;
}

bool Port::HandleIncomingPacket(AsyncPacketSocket*, const ReceivedPacket&) { return false; }

bool Port::CanHandleIncomingPacketsFrom(const SocketAddress&) const { return false; }

void Port::SendBindingErrorResponse(Timestamp now, StunMessage* message, const SocketAddress& addr, int error_code,
                                    std::string_view reason) {
  const bool binding = message->type() == STUN_BINDING_REQUEST;
  StunMessage response(binding ? STUN_BINDING_ERROR_RESPONSE : GOOG_PING_ERROR_RESPONSE, message->transaction_id());
  auto error_attr = StunAttribute::CreateErrorCode();
  error_attr->SetCode(error_code);
  error_attr->SetReason(std::string(reason));
  response.AddAttribute(std::move(error_attr));
  // RFC 5245 10.1.2: 400 and 401 carry no MESSAGE-INTEGRITY (no shared
  // secret to sign with), nor does an error to a GOOG-PING.
  if (error_code != STUN_ERROR_BAD_REQUEST && error_code != STUN_ERROR_UNAUTHORIZED &&
      message->type() != GOOG_PING_REQUEST) {
    if (binding) {
      response.AddMessageIntegrity(password_);
    } else {
      response.AddMessageIntegrity32(password_);
    }
  }
  if (binding) response.AddFingerprint();
  ByteBufferWriter buf;
  response.Write(&buf);
  PacketOptions options;
  options.dscp = StunDscpValue();
  SendTo(now, buf.DataView(), addr, options, false);
}

void Port::SendUnknownAttributesErrorResponse(Timestamp now, StunMessage* message, const SocketAddress& addr,
                                              const std::vector<uint16_t>& unknown_types) {
  StunMessage response(STUN_BINDING_ERROR_RESPONSE, message->transaction_id());
  auto error_attr = StunAttribute::CreateErrorCode();
  error_attr->SetCode(STUN_ERROR_UNKNOWN_ATTRIBUTE);
  error_attr->SetReason(STUN_ERROR_REASON_UNKNOWN_ATTRIBUTE);
  response.AddAttribute(std::move(error_attr));
  auto unknown_attr = StunAttribute::CreateUnknownAttributes();
  for (uint16_t type : unknown_types) unknown_attr->AddType(type);
  response.AddAttribute(std::move(unknown_attr));
  response.AddMessageIntegrity(password_);
  response.AddFingerprint();
  ByteBufferWriter buf;
  response.Write(&buf);
  PacketOptions options;
  options.dscp = StunDscpValue();
  SendTo(now, buf.DataView(), addr, options, false);
}

void Port::KeepAliveUntilPruned() {
  if (state_ == State::kInit) state_ = State::kKeepAliveUntilPruned;
}

void Port::Prune(Timestamp now) {
  state_ = State::kPruned;
  PostDestroyIfDead(now, /*delayed=*/false);
}

void Port::CancelPendingTasks() { destroy_checks_.clear(); }

void Port::PostDestroyIfDead(Timestamp now, bool delayed) {
  destroy_checks_.insert(delayed ? now + TimeDelta::Millis(timeout_delay_) : now);
}

void Port::Process(Timestamp now) {
  while (!dead_ && !destroy_checks_.empty() && *destroy_checks_.begin() <= now) {
    const Timestamp at = *destroy_checks_.begin();
    destroy_checks_.erase(destroy_checks_.begin());
    DestroyIfDead(at);
  }
}

Timestamp Port::NextProcessTime() const {
  if (dead_ || destroy_checks_.empty()) return Timestamp::PlusInfinity();
  return *destroy_checks_.begin();
}

void Port::DestroyIfDead(Timestamp now) {
  // No connections until #1000, so only the state and the timer decide.
  const bool dead = (state_ == State::kInit || state_ == State::kPruned) &&
                    now - last_time_all_connections_removed_ >= TimeDelta::Millis(timeout_delay_);
  if (dead) Destroy();
}

void Port::Destroy() {
  dead_ = true;
  CancelPendingTasks();
  port_destroyed_.Send(this);
}

void Port::UpdateNetworkCost() {
  const int16_t new_cost = static_cast<int16_t>(network_->GetCost());
  if (network_cost_ == new_cost) return;
  network_cost_ = new_cost;
  for (Candidate& candidate : candidates_) candidate.set_network_cost(static_cast<uint16_t>(network_cost_));
}

std::string Port::ToString() const {
  char ptr[32];
  std::snprintf(ptr, sizeof(ptr), "%p", static_cast<const void*>(this));
  return std::string("Port[") + ptr + ":" + content_name_ + ":" + std::to_string(component_) + ":" +
         std::to_string(generation_) + ":" + std::string(IceCandidateTypeToString(type_)) + ":" +
         network_->ToString() + "]";
}

}  // namespace sm::p2p
