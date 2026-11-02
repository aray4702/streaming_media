// sm/p2p/connection.h: libwebrtc M157's p2p/base/connection.cc.
#include "sm/p2p/connection.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/port.h"

namespace sm::p2p {
namespace {

// Determines whether we have seen at least the given maximum number of pings fail to have a response.
bool TooManyFailures(const std::vector<Connection::SentPing>& pings_since_last_response, uint32_t maximum_failures,
                     TimeDelta rtt_estimate, Timestamp now) {
  // If we haven't sent that many pings, then we can't have failed that many.
  if (pings_since_last_response.size() < maximum_failures) return false;
  // Check if the window in which we would expect a response to the ping has already elapsed.
  const Timestamp expected_response_time = pings_since_last_response[maximum_failures - 1].sent_time + rtt_estimate;
  return now > expected_response_time;
}

// Determines whether we have gone too long without seeing any response.
bool TooLongWithoutResponse(const std::vector<Connection::SentPing>& pings_since_last_response,
                            TimeDelta maximum_time, Timestamp now) {
  if (pings_since_last_response.empty()) return false;
  const auto& first = pings_since_last_response[0];
  return now > (first.sent_time + maximum_time);
}

// When we don't have any RTT data, we have to pick something reasonable. We use a large value just in case the
// connection is really slow.
constexpr TimeDelta kDefaultRtt = TimeDelta::Seconds(3);

// We will restrict RTT estimates (when used for determining state) to be within a reasonable range.
constexpr TimeDelta kMinimumRtt = TimeDelta::Millis(100);
constexpr TimeDelta kMaximumRtt = TimeDelta::Seconds(60);

constexpr int kDefaultRttEstimateHalfTimeMs = 500;
// Weighting of the old rtt value to new data.
constexpr int kRttRatio = 3;  // 3 : 1

constexpr TimeDelta kMinExtraPingDelay = TimeDelta::Millis(100);

// Default field trials.
const IceFieldTrials kDefaultFieldTrials;

constexpr int kSupportGoogPingVersionRequestIndex =
    static_cast<int>(IceGoogMiscInfoBindingRequestAttributeIndex::SUPPORT_GOOG_PING_VERSION);
constexpr int kSupportGoogPingVersionResponseIndex =
    static_cast<int>(IceGoogMiscInfoBindingResponseAttributeIndex::SUPPORT_GOOG_PING_VERSION);

std::string HexEncode(std::string_view s) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(s.size() * 2);
  for (unsigned char c : s) {
    out.push_back(kHex[c >> 4]);
    out.push_back(kHex[c & 0xF]);
  }
  return out;
}

}  // namespace

// A ConnectionRequest is a STUN binding used to determine writability.
class Connection::ConnectionRequest : public StunRequest {
 public:
  ConnectionRequest(StunRequestManager& manager, Connection* connection, std::unique_ptr<IceMessage> message)
      : StunRequest(manager, std::move(message)), connection_(connection) {}

 protected:
  void OnResponse(Timestamp now, StunMessage* response) override {
    connection_->OnConnectionRequestResponse(now, this, response);
  }
  void OnErrorResponse(Timestamp now, StunMessage* response) override {
    connection_->OnConnectionRequestErrorResponse(now, this, response);
  }
  void OnTimeout(Timestamp now) override { connection_->OnConnectionRequestTimeout(now, this); }
  void OnSent(Timestamp now) override {
    connection_->OnConnectionRequestSent(now, this);
    // Each request is sent only once. After a single delay, the request will time out.
    set_timed_out();
  }
  int resend_delay() const override { return static_cast<int>(kConnectionResponseTimeout.ms()); }

 private:
  Connection* const connection_;
};

Connection::Connection(Timestamp now, Port* port, size_t index, const Candidate& remote_candidate)
    : id_(CreateRandomId()),
      port_(port),
      local_candidate_(port->Candidates()[index]),
      remote_candidate_(remote_candidate),
      requests_([this](Timestamp t, std::span<const uint8_t> data, StunRequest* request) {
        OnSendStunPacket(t, data, request);
      }),
      rtt_(kDefaultRtt),
      time_created_(now),
      field_trials_(&kDefaultFieldTrials),
      rtt_estimate_(kDefaultRttEstimateHalfTimeMs) {}

Connection::~Connection() = default;

const Network* Connection::network() const { return port_->network(); }

int Connection::generation() const { return static_cast<int>(port_->generation()); }

uint64_t Connection::priority() const {
  if (!port_) return 0;
  uint64_t priority = 0;
  // RFC 5245 - 5.7.2. Computing Pair Priority and Ordering Pairs
  // Let G be the priority for the candidate provided by the controlling agent. Let D be the priority for the
  // candidate provided by the controlled agent.
  // pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
  const IceRole role = port_->GetIceRole();
  if (role != ICEROLE_UNKNOWN) {
    uint32_t g = 0;
    uint32_t d = 0;
    if (role == ICEROLE_CONTROLLING) {
      g = local_candidate().priority();
      d = remote_candidate_.priority();
    } else {
      g = remote_candidate_.priority();
      d = local_candidate().priority();
    }
    priority = std::min(g, d);
    priority = priority << 32;
    priority += 2 * static_cast<uint64_t>(std::max(g, d)) + (g > d ? 1 : 0);
  }
  return priority;
}

void Connection::set_write_state(WriteState value) {
  const WriteState old_value = write_state_;
  write_state_ = value;
  if (value != old_value) Notify(state_change_callbacks_);
}

void Connection::UpdateReceiving(Timestamp now) {
  bool receiving;
  if (LastPingSent() < LastPingResponseReceived()) {
    // We consider any candidate pair that has its last connectivity check acknowledged by a response as receiving,
    // particularly for backup candidate pairs that send checks at a much slower pace than the selected one.
    // Otherwise, a backup candidate pair constantly becomes not receiving as a side effect of a long ping interval,
    // since we do not have a separate receiving timeout for backup candidate pairs.
    receiving = true;
  } else {
    receiving = LastReceived() > Timestamp::Millis(0) && now <= LastReceived() + ReceivingTimeout();
  }
  if (receiving_ == receiving) return;
  receiving_ = receiving;
  receiving_unchanged_since_ = now;
  Notify(state_change_callbacks_);
}

void Connection::set_state(IceCandidatePairState state) { state_ = state; }

void Connection::set_connected(bool value) {
  const bool old_value = connected_;
  connected_ = value;
  if (value != old_value) Notify(state_change_callbacks_);
}

void Connection::SetIceFieldTrials(const IceFieldTrials* field_trials) {
  field_trials_ = field_trials;
  rtt_estimate_.SetHalfTime(field_trials->rtt_estimate_halftime_ms);
}

void Connection::OnSendStunPacket(Timestamp now, std::span<const uint8_t> data, StunRequest* /*req*/) {
  PacketOptions options;
  options.dscp = port_->StunDscpValue();
  // libwebrtc logs a failed send; the check then goes unanswered.
  port_->SendTo(now, data, remote_candidate_.address(), options, false);
}

void Connection::RegisterReceivedPacketCallback(std::function<void(Connection*, const ReceivedPacket&)> cb) {
  // libwebrtc RTC_CHECKs that no receiver is registered yet.
  received_packet_callback_ = std::move(cb);
}

void Connection::OnReadPacket(const ReceivedPacket& packet) {
  const Timestamp now = packet.arrival_time;
  std::unique_ptr<IceMessage> msg;
  std::string remote_ufrag;
  const SocketAddress& addr(remote_candidate_.address());
  if (!port_->GetStunMessage(now, packet.payload, addr, &msg, &remote_ufrag)) {
    // The packet did not parse as a valid STUN message. This is a data packet, pass it along.
    last_data_received_ = now;
    UpdateReceiving(last_data_received_);
    recv_rate_tracker_.Update(static_cast<int64_t>(packet.payload.size()), last_data_received_.ms());
    stats_.recv_total_bytes += packet.payload.size();
    stats_.packets_received++;
    if (received_packet_callback_) received_packet_callback_(this, packet);
    // If timed out sending writability checks, start up again.
    if (!pruned_ && (write_state_ == STATE_WRITE_TIMEOUT)) set_write_state(STATE_WRITE_INIT);
    return;
  } else if (!msg) {
    // The packet was STUN, but failed a check and was handled internally.
    return;
  }

  // The packet is STUN and passed the Port checks. Perform our own checks to ensure this packet is valid.
  // REQUESTs have msg->integrity() already checked in Port.
  // RESPONSEs have msg->integrity() checked below.
  // INDICATION does not have any integrity.
  if (IsStunRequestType(msg->type())) {
    if (msg->integrity() != StunMessage::IntegrityStatus::kIntegrityOk) {
      return;  // "silently" discard the request.
    }
  } else if (IsStunSuccessResponseType(msg->type()) || IsStunErrorResponseType(msg->type())) {
    if (msg->ValidateMessageIntegrity(remote_candidate().password()) != StunMessage::IntegrityStatus::kIntegrityOk) {
      return;  // "silently" discard the response.
    }
  }
  // Otherwise an indication: no message integrity.

  switch (msg->type()) {
    case STUN_BINDING_REQUEST:
      if (remote_ufrag == remote_candidate_.username()) {
        HandleStunBindingOrGoogPingRequest(now, msg.get());
      } else {
        // The packet had the right local username, but the remote username was not the right one for the remote
        // address.
        port_->SendBindingErrorResponse(now, msg.get(), addr, STUN_ERROR_UNAUTHORIZED,
                                        STUN_ERROR_REASON_UNAUTHORIZED);
      }
      break;
    // Response from remote peer. Does it match request sent? This doesn't just check, it makes callbacks if
    // transaction id's match.
    case STUN_BINDING_RESPONSE:
    case STUN_BINDING_ERROR_RESPONSE:
      requests_.CheckResponse(now, msg.get());
      break;
    // Remote end point sent an STUN indication instead of regular binding request. In this case
    // `last_ping_received_` will be updated but no response will be sent.
    case STUN_BINDING_INDICATION:
      ReceivedPing(now, msg->transaction_id());
      break;
    case GOOG_PING_REQUEST:
      // Checked in Port::GetStunMessage.
      HandleStunBindingOrGoogPingRequest(now, msg.get());
      break;
    case GOOG_PING_RESPONSE:
    case GOOG_PING_ERROR_RESPONSE:
      requests_.CheckResponse(now, msg.get());
      break;
    default:
      break;  // libwebrtc: RTC_DCHECK_NOTREACHED.
  }
}

void Connection::HandleStunBindingOrGoogPingRequest(Timestamp now, IceMessage* msg) {
  // This connection should now be receiving.
  ReceivedPing(now, msg->transaction_id());
  if (field_trials_->extra_ice_ping && last_ping_response_received_ <= Timestamp::Millis(0)) {
    if (local_candidate().is_relay() || local_candidate().is_prflx() || remote_candidate().is_relay() ||
        remote_candidate().is_prflx()) {
      if (last_ping_sent_ + kMinExtraPingDelay <= now) Ping(now);
    }
  }

  const SocketAddress& remote_addr = remote_candidate_.address();
  if (msg->type() == STUN_BINDING_REQUEST) {
    // Check for role conflicts.
    const std::string& remote_ufrag = remote_candidate_.username();
    if (!port_->MaybeIceRoleConflict(now, remote_addr, msg, remote_ufrag)) {
      return;  // Received conflicting role from the peer.
    }
  }

  stats_.recv_ping_requests++;

  // This is a validated stun request from remote peer.
  if (msg->type() == STUN_BINDING_REQUEST) {
    SendStunBindingResponse(now, msg);
  } else {
    SendGoogPingResponse(now, msg);  // GOOG_PING_REQUEST.
  }

  // If it timed out on writing check, start up again.
  if (!pruned_ && write_state_ == STATE_WRITE_TIMEOUT) set_write_state(STATE_WRITE_INIT);

  if (port_->GetIceRole() == ICEROLE_CONTROLLED) {
    const StunUInt32Attribute* nomination_attr = msg->GetUInt32(STUN_ATTR_NOMINATION);
    uint32_t nomination = 0;
    if (nomination_attr) {
      nomination = nomination_attr->value();
      // libwebrtc logs an invalid nomination of 0.
    } else {
      const StunByteStringAttribute* use_candidate_attr = msg->GetByteString(STUN_ATTR_USE_CANDIDATE);
      if (use_candidate_attr) nomination = 1;
    }
    // We don't un-nominate a connection, so we only keep a larger nomination.
    if (nomination > remote_nomination_) {
      set_remote_nomination(nomination);
      Notify(nominated_callbacks_);
    }
  }
  // Set the remote cost if the network_info attribute is available. Note: If packets are re-ordered, we may get
  // incorrect network cost temporarily, but it should get the correct value shortly after that.
  const StunUInt32Attribute* network_attr = msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
  if (network_attr) {
    const uint32_t network_info = network_attr->value();
    const uint16_t network_cost = static_cast<uint16_t>(network_info);
    if (network_cost != remote_candidate_.network_cost()) {
      remote_candidate_.set_network_cost(network_cost);
      // Network cost change will affect the connection ranking, so signal state change to force a re-sort in
      // P2PTransportChannel.
      Notify(state_change_callbacks_);
    }
  }

  if (field_trials_->piggyback_ice_check_acknowledgement) HandlePiggybackCheckAcknowledgementIfAny(now, msg);
}

void Connection::SendStunBindingResponse(Timestamp now, const StunMessage* message) {
  // Retrieve the username from the `message`.
  const StunByteStringAttribute* username_attr = message->GetByteString(STUN_ATTR_USERNAME);
  if (username_attr == nullptr) return;  // No valid username, skip the response (libwebrtc DCHECKs).

  // Fill in the response.
  StunMessage response(STUN_BINDING_RESPONSE, message->transaction_id());
  const StunUInt32Attribute* retransmit_attr = message->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT);
  if (retransmit_attr) {
    // Inherit the incoming retransmit value in the response so the other side can see our view of lost pings.
    response.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_RETRANSMIT_COUNT, retransmit_attr->value()));
  }

  response.AddAttribute(
      std::make_unique<StunXorAddressAttribute>(STUN_ATTR_XOR_MAPPED_ADDRESS, remote_candidate_.address()));

  if (field_trials_->announce_goog_ping) {
    // Check if message contains a announce-request.
    auto goog_misc = message->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO);
    if (goog_misc != nullptr && goog_misc->Size() > kSupportGoogPingVersionRequestIndex &&
        // Which version can we handle...currently any >= 1
        goog_misc->GetType(kSupportGoogPingVersionRequestIndex) >= 1) {
      auto list = StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
      list->AddTypeAtIndex(kSupportGoogPingVersionResponseIndex, kGoogPingVersion);
      response.AddAttribute(std::move(list));
    }
  }

  const StunByteStringAttribute* delta = message->GetByteString(STUN_ATTR_GOOG_DELTA);
  if (delta) {
    if (field_trials_->answer_goog_delta && goog_delta_consumer_) {
      auto ack = goog_delta_consumer_(delta);
      if (ack) response.AddAttribute(std::move(ack));
      // libwebrtc logs a consumer that returns no ack.
    }
    // Otherwise libwebrtc logs and ignores the GOOG_DELTA.
  }

  response.AddMessageIntegrity(local_candidate().password());
  response.AddFingerprint();

  SendResponseMessage(now, response);
}

void Connection::SendGoogPingResponse(Timestamp now, const StunMessage* message) {
  // Fill in the response.
  StunMessage response(GOOG_PING_RESPONSE, message->transaction_id());
  response.AddMessageIntegrity32(local_candidate().password());
  SendResponseMessage(now, response);
}

void Connection::SendResponseMessage(Timestamp now, const StunMessage& response) {
  // Where I send the response.
  const SocketAddress& addr = remote_candidate_.address();

  // Send the response.
  ByteBufferWriter buf;
  response.Write(&buf);
  PacketOptions options;
  options.dscp = port_->StunDscpValue();
  const int err = port_->SendTo(now, buf.DataView(), addr, options, false);
  if (err >= 0) stats_.sent_ping_responses++;
  // libwebrtc logs a failed send.
}

void Connection::Prune() {
  if (!pruned_ || active()) {
    pruned_ = true;
    requests_.Clear();
    set_write_state(STATE_WRITE_TIMEOUT);
  }
}

void Connection::Destroy(Timestamp now) {
  // libwebrtc DCHECKs the port is still there.
  if (port_) port_->DestroyConnection(now, this);
}

bool Connection::Shutdown() {
  if (!port_) return false;  // already shut down.

  // Fire the 'destroyed' event before deleting the object. This is done intentionally to avoid a situation whereby
  // the signal might have dangling pointers to objects that have been deleted by the time the connection is
  // deleted.
  Notify(destroyed_callbacks_);

  // Reset the `port_` after firing the destroyed signal since information required for logging needs access to it.
  port_ = nullptr;

  // Clear any pending requests (or responses).
  requests_.Clear();
  return true;
}

void Connection::FailAndPrune() {
  // libwebrtc DCHECKs the port is still there.
  if (!port_) return;
  set_state(IceCandidatePairState::FAILED);
  Prune();
}

void Connection::PrintPingsSinceLastResponse(std::string* s, size_t max) const {
  std::string out;
  if (pings_since_last_response_.size() > max) {
    for (size_t i = 0; i < max; i++) out += HexEncode(pings_since_last_response_[i].id) + " ";
    out += "... " + std::to_string(pings_since_last_response_.size() - max) + " more";
  } else {
    for (const SentPing& ping : pings_since_last_response_) out += HexEncode(ping.id) + " ";
  }
  *s = std::move(out);
}

void Connection::UpdateState(Timestamp now) {
  // libwebrtc DCHECKs the port is still there.
  if (!port_) return;

  // Computes our estimate of the RTT given the current estimate.
  const TimeDelta rtt = std::clamp(2 * rtt_, kMinimumRtt, kMaximumRtt);

  // Check the writable state. (The order of these checks is important.)
  //
  // Before becoming unwritable, we allow for a fixed number of pings to fail (i.e., receive no response). We also
  // have to give the response time to get back, so we include a conservative estimate of this.
  //
  // Before timing out writability, we give a fixed amount of time. This is to allow for changes in network
  // conditions.
  if ((write_state_ == STATE_WRITABLE) &&
      TooManyFailures(pings_since_last_response_, static_cast<uint32_t>(unwritable_min_checks()), rtt, now) &&
      TooLongWithoutResponse(pings_since_last_response_, UnwritableTimeout(), now)) {
    set_write_state(STATE_WRITE_UNRELIABLE);
  }
  if ((write_state_ == STATE_WRITE_UNRELIABLE || write_state_ == STATE_WRITE_INIT) &&
      TooLongWithoutResponse(pings_since_last_response_, InactiveTimeout(), now)) {
    set_write_state(STATE_WRITE_TIMEOUT);
  }

  // Update the receiving state.
  UpdateReceiving(now);
  if (dead(now)) port_->DestroyConnectionAsync(now, this);
}

void Connection::UpdateLocalIceParameters(int component, std::string_view username_fragment,
                                          std::string_view password) {
  local_candidate_.set_component(component);
  local_candidate_.set_username(username_fragment);
  local_candidate_.set_password(password);
}

void Connection::Ping(Timestamp now, std::unique_ptr<StunByteStringAttribute> delta) {
  // libwebrtc DCHECKs the port is still there.
  if (!port_) return;

  last_ping_sent_ = now;

  // If not using renomination, we use "1" to mean "nominated" and "0" to mean "not nominated". If using
  // renomination, values greater than 1 are used for re-nominated pairs.
  uint32_t nomination = use_candidate_attr_ ? 1 : 0;
  if (nomination_ > 0) nomination = nomination_;

  const bool has_delta = delta != nullptr;
  auto req = std::make_unique<ConnectionRequest>(requests_, this, BuildPingRequest(std::move(delta)));

  if (!has_delta && ShouldSendGoogPing(req->msg())) {
    auto message = std::make_unique<IceMessage>(GOOG_PING_REQUEST, req->id());
    message->AddMessageIntegrity32(remote_candidate_.password());
    req = std::make_unique<ConnectionRequest>(requests_, this, std::move(message));
  }

  pings_since_last_response_.push_back(SentPing(req->id(), now, nomination));
  requests_.Send(now, std::move(req));
  state_ = IceCandidatePairState::IN_PROGRESS;
  num_pings_sent_++;
}

std::unique_ptr<IceMessage> Connection::BuildPingRequest(std::unique_ptr<StunByteStringAttribute> delta) {
  auto message = std::make_unique<IceMessage>(STUN_BINDING_REQUEST);
  // Note that the order of attributes does not impact the parsing on the receiver side. The attribute is retrieved
  // then by iterating and matching over all parsed attributes. See StunMessage::GetAttribute.
  message->AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_USERNAME, port_->CreateStunUsername(remote_candidate_.username())));
  message->AddAttribute(std::make_unique<StunUInt32Attribute>(
      STUN_ATTR_GOOG_NETWORK_INFO,
      (static_cast<uint32_t>(port_->network()->id()) << 16) | static_cast<uint16_t>(port_->network_cost())));

  if (field_trials_->piggyback_ice_check_acknowledgement && last_ping_id_received_) {
    message->AddAttribute(
        std::make_unique<StunByteStringAttribute>(STUN_ATTR_GOOG_LAST_ICE_CHECK_RECEIVED, *last_ping_id_received_));
  }

  // Adding ICE_CONTROLLED or ICE_CONTROLLING attribute based on the role (libwebrtc DCHECKs one is set).
  const IceRole ice_role = port_->GetIceRole();
  message->AddAttribute(std::make_unique<StunUInt64Attribute>(
      ice_role == ICEROLE_CONTROLLING ? STUN_ATTR_ICE_CONTROLLING : STUN_ATTR_ICE_CONTROLLED, port_->IceTiebreaker()));

  if (ice_role == ICEROLE_CONTROLLING) {
    // We should have either USE_CANDIDATE attribute or ICE_NOMINATION attribute but not both. That was enforced in
    // p2ptransportchannel.
    if (use_candidate_attr()) {
      message->AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USE_CANDIDATE));
    }
    if (nomination_ && nomination_ != acked_nomination()) {
      message->AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_NOMINATION, nomination_));
    }
  }

  message->AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY, prflx_priority()));

  if (port_->send_retransmit_count_attribute()) {
    message->AddAttribute(std::make_unique<StunUInt32Attribute>(
        STUN_ATTR_RETRANSMIT_COUNT, static_cast<uint32_t>(pings_since_last_response_.size())));
  }
  if (field_trials_->enable_goog_ping && !remote_support_goog_ping_.has_value()) {
    // Check if remote supports GOOG PING by announcing which version we support. This is sent on all
    // STUN_BINDING_REQUEST until we get a STUN_BINDING_RESPONSE.
    auto list = StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
    list->AddTypeAtIndex(kSupportGoogPingVersionRequestIndex, kGoogPingVersion);
    message->AddAttribute(std::move(list));
  }

  if (delta) {
    // libwebrtc DCHECKs delta->type() == STUN_ATTR_GOOG_DELTA.
    const size_t msg_length = message->length();
    if (msg_length + kStunAttributeHeaderSize + delta->length() < static_cast<size_t>(kMaxStunBindingLength)) {
      message->AddAttribute(std::move(delta));
    }
    // Otherwise libwebrtc logs "Not sending GOOG_DELTA, request full".
  }

  message->AddMessageIntegrity(remote_candidate_.password());
  message->AddFingerprint();
  return message;
}

void Connection::ReceivedPing(Timestamp now, const std::optional<std::string>& request_id) {
  last_ping_received_ = now;
  last_ping_id_received_ = request_id;
  UpdateReceiving(last_ping_received_);
}

void Connection::HandlePiggybackCheckAcknowledgementIfAny(Timestamp now, StunMessage* msg) {
  const StunByteStringAttribute* last_ice_check_received_attr =
      msg->GetByteString(STUN_ATTR_GOOG_LAST_ICE_CHECK_RECEIVED);
  if (last_ice_check_received_attr) {
    const std::string_view request_id = last_ice_check_received_attr->string_view();
    auto iter = std::find_if(pings_since_last_response_.begin(), pings_since_last_response_.end(),
                             [&request_id](const SentPing& ping) { return ping.id == request_id; });
    if (iter != pings_since_last_response_.end()) {
      const TimeDelta rtt = now - iter->sent_time;
      const uint32_t nomination = iter->nomination;
      ReceivedPingResponse(now, rtt, request_id, nomination);
    }
  }
}

void Connection::ReceivedPingResponse(Timestamp now, TimeDelta rtt, std::string_view /*request_id*/,
                                      const std::optional<uint32_t>& nomination) {
  // libwebrtc DCHECKs rtt >= 0.
  // We've already validated that this is a STUN binding response with the correct local and remote username for
  // this connection. So if we're not already, become writable. We may be bringing a pruned connection back to life,
  // but if we don't really want it, we can always prune it again.
  if (nomination && nomination.value() > acked_nomination_) acked_nomination_ = nomination.value();

  total_round_trip_time_ += rtt;
  current_round_trip_time_ = rtt;
  rtt_estimate_.AddSample(now.ms(), static_cast<int>(rtt.ms()));

  pings_since_last_response_.clear();
  last_ping_response_received_ = now;
  UpdateReceiving(last_ping_response_received_);
  set_write_state(STATE_WRITABLE);
  set_state(IceCandidatePairState::SUCCEEDED);

  // Smooth the RTT estimate using a moving average.
  if (rtt_samples_ > 0) {
    rtt_ = (kRttRatio * rtt_ + rtt) / (kRttRatio + 1);
  } else {
    rtt_ = rtt;
  }
  rtt_samples_++;
}

bool Connection::dead(Timestamp now) const {
  if (LastReceived() > Timestamp::Millis(0)) {
    // If it has ever received anything, we keep it alive
    // - if it has received in the last DEAD_CONNECTION_RECEIVE_TIMEOUT (30s)
    // - if it has a ping outstanding shorter than DEAD_CONNECTION_RECEIVE_TIMEOUT (30s)
    // - else if IDLE let it live field_trials_->dead_connection_timeout_ms
    //
    // This covers the normal case of a successfully used connection that stops working. This also allows a remote
    // peer to continue pinging over a locally inactive (pruned) connection. This also allows the local agent to ping
    // with longer interval than 30s as long as it shorter than `dead_connection_timeout_ms`.
    if (now <= (LastReceived() + kDeadConnectionReceiveTimeout)) {
      return false;  // Not dead since we have received the last 30s.
    }
    if (!pings_since_last_response_.empty()) {
      // Outstanding pings: let it live until the ping is unreplied for DEAD_CONNECTION_RECEIVE_TIMEOUT.
      return now > (pings_since_last_response_[0].sent_time + kDeadConnectionReceiveTimeout);
    }
    // No outstanding pings: let it live until field_trials_->dead_connection_timeout_ms has passed.
    return now > LastReceived() + TimeDelta::Millis(field_trials_->dead_connection_timeout_ms);
  }

  if (active()) {
    // If it has never received anything, keep it alive as long as it is actively pinging and not pruned. Otherwise,
    // the connection might be deleted before it has a chance to ping. This is the normal case for a new connection
    // that is pinging but hasn't received anything yet.
    return false;
  }

  // If it has never received anything and is not actively pinging (pruned), we keep it around for at least
  // MIN_CONNECTION_LIFETIME to prevent connections from being pruned too quickly during a network change event when
  // two networks would be up simultaneously but only for a brief period.
  return now > (time_created_ + kMinConnectionLifetime);
}

std::string Connection::ToDebugId() const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(this));
  return buf;
}

uint32_t Connection::ComputeNetworkCost() const {
  // libwebrtc DCHECKs the port is still there.
  return static_cast<uint32_t>(static_cast<uint16_t>(port_->network_cost())) + remote_candidate_.network_cost();
}

std::string Connection::ToString() const {
  static constexpr const char* kConnectState[2] = {"-", "C"};
  static constexpr const char* kReceiveState[2] = {"-", "R"};
  static constexpr const char* kWriteState[4] = {"W", "w", "-", "x"};
  static constexpr const char* kIceState[4] = {"W", "I", "S", "F"};
  static constexpr const char* kSelectedState[2] = {"-", "S"};
  std::string ss = "Conn[" + ToDebugId();
  if (!port_) {
    // No content or network names for pending delete. Temporarily substitute the names with a hash (rhyming with
    // trash).
    ss += ":#:#:";
  } else {
    ss += ":" + port_->content_name() + ":" + port_->network()->ToString() + ":";
  }
  const Candidate& local = local_candidate();
  const Candidate& remote = remote_candidate();
  ss += local.id() + ":" + std::to_string(local.component()) + ":" + std::to_string(local.generation()) + ":" +
        std::string(local.type_name()) + ":" + local.protocol() + ":" + local.address().ToSensitiveString() + "->" +
        remote.id() + ":" + std::to_string(remote.component()) + ":" + std::to_string(remote.priority()) + ":" +
        std::string(remote.type_name()) + ":" + remote.protocol() + ":" + remote.address().ToSensitiveString() + "|";
  ss += std::string(kConnectState[connected_]) + kReceiveState[receiving_] + kWriteState[write_state_] +
        kIceState[static_cast<int>(state_)] + "|" + kSelectedState[selected_] + "|" +
        std::to_string(remote_nomination_) + "|" + std::to_string(nomination_) + "|";
  if (port_) ss += std::to_string(priority()) + "|";
  if (rtt_ < kDefaultRtt) {
    ss += std::to_string(rtt_.ms()) + "]";
  } else {
    ss += "-]";
  }
  return ss;
}

void Connection::OnConnectionRequestResponse(Timestamp now, StunRequest* request, StunMessage* response) {
  const TimeDelta rtt = request->Elapsed(now);

  std::optional<uint32_t> nomination;
  const std::string request_id = request->id();
  auto iter = std::find_if(pings_since_last_response_.begin(), pings_since_last_response_.end(),
                           [&request_id](const SentPing& ping) { return ping.id == request_id; });
  if (iter != pings_since_last_response_.end()) nomination.emplace(iter->nomination);
  ReceivedPingResponse(now, rtt, request_id, nomination);

  stats_.recv_ping_responses++;

  if (request->msg()->type() == STUN_BINDING_REQUEST) {
    if (!remote_support_goog_ping_.has_value()) {
      auto goog_misc = response->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO);
      if (goog_misc != nullptr && goog_misc->Size() > kSupportGoogPingVersionResponseIndex) {
        // The remote peer has indicated that it {does/does not} supports GOOG_PING.
        remote_support_goog_ping_ = goog_misc->GetType(kSupportGoogPingVersionResponseIndex) >= kGoogPingVersion;
      } else {
        remote_support_goog_ping_ = false;
      }
    }

    MaybeUpdateLocalCandidate(request, response);

    if (field_trials_->enable_goog_ping && remote_support_goog_ping_ == true) {
      cached_stun_binding_ = request->msg()->Clone();
    }
  }

  // Did we send a delta ?
  const bool sent_goog_delta = request->msg()->GetByteString(STUN_ATTR_GOOG_DELTA) != nullptr;
  // Did we get a GOOG_DELTA_ACK ?
  const StunUInt64Attribute* delta_ack = response->GetUInt64(STUN_ATTR_GOOG_DELTA_ACK);

  if (goog_delta_ack_consumer_) {
    if (sent_goog_delta && delta_ack) {
      goog_delta_ack_consumer_(delta_ack, GoogDeltaAckError::kNone);
    } else if (sent_goog_delta) {
      // We sent DELTA but did not get a DELTA_ACK. This means that remote does not support GOOG_DELTA.
      goog_delta_ack_consumer_(nullptr, GoogDeltaAckError::kUnsupported);
    } else if (delta_ack) {
      // We did NOT send DELTA but got a DELTA_ACK. That is internal error.
      goog_delta_ack_consumer_(nullptr, GoogDeltaAckError::kInternal);
    }
  }
  // Otherwise libwebrtc logs and discards a GOOG_DELTA_ACK.
}

void Connection::OnConnectionRequestErrorResponse(Timestamp now, ConnectionRequest* request,
                                                  StunMessage* response) {
  // libwebrtc DCHECKs the port is still there.
  if (!port_) return;

  const int error_code = response->GetErrorCodeValue();
  cached_stun_binding_.reset();
  if (error_code == STUN_ERROR_UNKNOWN_ATTRIBUTE || error_code == STUN_ERROR_SERVER_ERROR ||
      error_code == STUN_ERROR_UNAUTHORIZED) {
    // Recoverable error, retry.
  } else if (error_code == STUN_ERROR_ROLE_CONFLICT) {
    port_->NotifyRoleConflict();
  } else if (request->msg()->type() == GOOG_PING_REQUEST) {
    // Race, retry.
  } else {
    // This is not a valid connection.
    set_state(IceCandidatePairState::FAILED);
    port_->DestroyConnectionAsync(now, this);
  }
}

void Connection::OnConnectionRequestTimeout(Timestamp /*now*/, ConnectionRequest* /*request*/) {
  // libwebrtc only logs: the outstanding ping stays in pings_since_last_response_ for UpdateState.
}

void Connection::OnConnectionRequestSent(Timestamp /*now*/, ConnectionRequest* /*request*/) {
  stats_.sent_ping_requests_total++;
  if (stats_.recv_ping_responses == 0) stats_.sent_ping_requests_before_first_response++;
}

void Connection::MaybeSetRemoteIceParametersAndGeneration(const IceParameters& ice_params, int generation) {
  if (remote_candidate_.username() == ice_params.ufrag && remote_candidate_.password().empty()) {
    remote_candidate_.set_password(ice_params.pwd);
  }
  // TODO(deadbeef): A value of '0' for the generation is used for both generation 0 and "generation unknown". It
  // should be changed to an std::optional to fix this.
  if (remote_candidate_.username() == ice_params.ufrag && remote_candidate_.password() == ice_params.pwd &&
      remote_candidate_.generation() == 0) {
    remote_candidate_.set_generation(static_cast<uint32_t>(generation));
  }
}

void Connection::MaybeUpdatePeerReflexiveCandidate(const Candidate& new_candidate) {
  if (remote_candidate_.is_prflx() && !new_candidate.is_prflx() &&
      remote_candidate_.protocol() == new_candidate.protocol() &&
      remote_candidate_.address() == new_candidate.address() &&
      remote_candidate_.username() == new_candidate.username() &&
      remote_candidate_.password() == new_candidate.password() &&
      remote_candidate_.generation() == new_candidate.generation()) {
    remote_candidate_ = new_candidate;
  }
}

Timestamp Connection::LastReceived() const {
  return std::max({last_data_received_, last_ping_received_, last_ping_response_received_});
}

uint32_t Connection::prflx_priority() const {
  // PRIORITY Attribute. Changing the type preference to Peer Reflexive and local preference and component id
  // information is unchanged from the original priority.
  // priority = (2^24)*(type preference) + (2^8)*(local preference) + (2^0)*(256 - component ID)
  const uint32_t type_preference =
      (local_candidate_.protocol() == TCP_PROTOCOL_NAME) ? ICE_TYPE_PREFERENCE_PRFLX_TCP : ICE_TYPE_PREFERENCE_PRFLX;
  return type_preference << 24 | (local_candidate_.priority() & 0x00FFFFFF);
}

ConnectionInfo Connection::stats(Timestamp now) {
  stats_.recv_bytes_second = static_cast<size_t>(recv_rate_tracker_.Rate(now.ms()).value_or(0));
  stats_.sent_bytes_second = static_cast<size_t>(send_rate_tracker_.Rate(now.ms()).value_or(0));
  stats_.receiving = receiving_;
  stats_.writable = write_state_ == STATE_WRITABLE;
  stats_.timeout = write_state_ == STATE_WRITE_TIMEOUT;
  stats_.rtt = static_cast<size_t>(rtt_.ms());
  stats_.key = this;
  stats_.state = state_;
  if (port_) {
    stats_.priority = priority();
    stats_.local_candidate = local_candidate();
  }
  stats_.nominated = nominated();
  stats_.total_round_trip_time_ms = static_cast<uint64_t>(total_round_trip_time_.ms());
  stats_.current_round_trip_time_ms =
      current_round_trip_time_.has_value() ? std::optional(static_cast<uint32_t>(current_round_trip_time_->ms()))
                                           : std::nullopt;
  stats_.remote_candidate = remote_candidate();
  if (last_data_received_ > Timestamp::Millis(0)) stats_.last_data_received = last_data_received_;
  if (last_send_data_ > Timestamp::Millis(0)) stats_.last_data_sent = last_send_data_;
  return stats_;
}

void Connection::MaybeUpdateLocalCandidate(StunRequest* request, StunMessage* response) {
  // libwebrtc DCHECKs the port is still there.
  if (!port_) return;

  // RFC 5245
  // The agent checks the mapped address from the STUN response. If the transport address does not match any of the
  // local candidates that the agent knows about, the mapped address represents a new candidate -- a peer reflexive
  // candidate.
  const StunAddressAttribute* addr = response->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  if (!addr) return;  // libwebrtc logs "No MAPPED-ADDRESS or XOR-MAPPED-ADDRESS found".

  for (const Candidate& candidate : port_->Candidates()) {
    if (candidate.address() == addr->GetAddress()) {
      if (local_candidate_ != candidate) {
        local_candidate_ = candidate;
        // Force a re-sort in P2PTransportChannel as this Connection's local candidate has changed.
        Notify(state_change_callbacks_);
      }
      return;
    }
  }

  // RFC 5245
  // Its priority is set equal to the value of the PRIORITY attribute in the Binding request.
  const StunUInt32Attribute* priority_attr = request->msg()->GetUInt32(STUN_ATTR_PRIORITY);
  if (!priority_attr) return;  // libwebrtc logs "No STUN_ATTR_PRIORITY found".
  const uint32_t priority = priority_attr->value();

  // Create a peer-reflexive candidate based on the local candidate.
  local_candidate_.generate_id();
  local_candidate_.set_type(IceCandidateType::kPrflx);
  // Set the related address and foundation attributes before changing the address.
  local_candidate_.set_related_address(local_candidate_.address());
  local_candidate_.ComputeFoundation(local_candidate_.address(), port_->IceTiebreaker());
  local_candidate_.set_priority(priority);
  local_candidate_.set_address(addr->GetAddress());

  // Change the local candidate of this Connection to the new prflx candidate.
  port_->AddPrflxCandidate(local_candidate_);

  // Force a re-sort in P2PTransportChannel as this Connection's local candidate has changed.
  Notify(state_change_callbacks_);
}

bool Connection::rtt_converged() const { return rtt_samples_ > (kRttRatio + 1); }

bool Connection::missing_responses(Timestamp now) const {
  if (pings_since_last_response_.empty()) return false;
  const TimeDelta waiting = now - pings_since_last_response_[0].sent_time;
  return waiting > 2 * Rtt();
}

bool Connection::TooManyOutstandingPings(const std::optional<int>& max_outstanding_pings) const {
  if (!max_outstanding_pings.has_value()) return false;
  if (static_cast<int>(pings_since_last_response_.size()) < *max_outstanding_pings) return false;
  return true;
}

void Connection::SetLocalCandidateNetworkCost(uint16_t cost) {
  if (cost == local_candidate_.network_cost()) return;
  local_candidate_.set_network_cost(cost);
  // Network cost change will affect the connection selection criteria. Signal the connection state change to force
  // a re-sort in P2PTransportChannel.
  Notify(state_change_callbacks_);
}

bool Connection::ShouldSendGoogPing(const StunMessage* message) const {
  if (remote_support_goog_ping_ == true && cached_stun_binding_ &&
      cached_stun_binding_->EqualAttributes(message, [](int type) {
        // Ignore these attributes.
        // NOTE: Consider what to do if adding more content to STUN_ATTR_GOOG_MISC_INFO
        return type != STUN_ATTR_FINGERPRINT && type != STUN_ATTR_MESSAGE_INTEGRITY &&
               type != STUN_ATTR_RETRANSMIT_COUNT && type != STUN_ATTR_GOOG_MISC_INFO;
      })) {
    return true;
  }
  return false;
}

void Connection::ForgetLearnedState() {
  requests_.Clear();
  receiving_ = false;
  write_state_ = STATE_WRITE_INIT;
  rtt_estimate_.Reset();
  pings_since_last_response_.clear();
}

bool Connection::set_writable_for_fake_ice_lite(Timestamp now) {
  if (write_state() != STATE_WRITABLE) {
    UpdateReceiving(now);
    set_write_state(STATE_WRITABLE);
    set_state(IceCandidatePairState::SUCCEEDED);
    return true;
  }
  return false;
}

ProxyConnection::ProxyConnection(Timestamp now, Port* port, size_t index, const Candidate& remote_candidate)
    : Connection(now, port, index, remote_candidate) {}

int ProxyConnection::Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) {
  // libwebrtc DCHECKs the port is still there.
  if (port() == nullptr) return -1;  // SOCKET_ERROR

  mutable_stats().sent_total_packets++;
  const int sent = port()->SendTo(now, data, remote_candidate().address(), options, true);
  if (sent <= 0) {
    error_ = port()->GetError();
    mutable_stats().sent_discarded_packets++;
    mutable_stats().sent_discarded_bytes += data.size();
  } else {
    AddSentBytesToStats(sent, now);
  }
  set_last_send_data(now);
  return sent;
}

}  // namespace sm::p2p
