// ICE candidate pairs (issue #1000, part of ICE #990, milestone 3): libwebrtc M157's p2p/base/connection.{h,cc} and
// connection_info.{h,cc} (compare/webrtc/reference/p2p/base/).
//
// A Connection is one candidate pair: a local candidate of its Port and a remote candidate. It runs the pair's
// connectivity checks (RFC 5245 7, RFC 8445) and keeps its state as libwebrtc does:
// - Ping() sends a STUN binding request with USERNAME "RFRAG:LFRAG", GOOG-NETWORK-INFO, ICE-CONTROLLING or
//   ICE-CONTROLLED with the tie-breaker, USE-CANDIDATE or NOMINATION (controlling side), PRIORITY (the local
//   candidate's priority with the peer-reflexive type preference), MESSAGE-INTEGRITY and FINGERPRINT. Each check is
//   sent once (no STUN retransmissions) and its transaction is dropped after kConnectionResponseTimeout (60 s).
//   With GOOG-PING announced by both sides (enable_goog_ping, off by default) an unchanged request goes as a short
//   GOOG-PING instead.
// - An incoming check (already checked by the Port: username, MESSAGE-INTEGRITY) marks the pair receiving and is
//   answered with XOR-MAPPED-ADDRESS; the ICE role conflict rule (RFC 5245 7.2.1.1) answers 487 or raises the role
//   conflict signal. On the controlled side USE-CANDIDATE or NOMINATION nominates the pair.
// - A response (MESSAGE-INTEGRITY checked with the remote password) makes the pair writable, updates the RTT (a 3:1
//   moving average, 3 s before the first sample) and, if its XOR-MAPPED-ADDRESS is not a known local candidate,
//   turns the local candidate into a peer-reflexive one (RFC 5245 7.1.3.2.1). A 487 raises the role conflict signal;
//   400 and other non-recoverable errors fail the pair and destroy it.
// - Write states: writable after a response; unreliable after kConnectionWriteConnectFailures (5) unanswered checks
//   and kConnectionWriteConnectTimeout (5 s) without a response; timed out after kConnectionWriteTimeout (15 s)
//   without one. This is consent freshness (RFC 7675) as libwebrtc implements it: a pair whose checks go unanswered
//   stops being writable. Receiving: something arrived within the receiving timeout (2.5 s), or the last check was
//   answered.
// - Dead: received nothing for kDeadConnectionReceiveTimeout (30 s) with a check outstanding as long, or for the
//   dead_connection_timeout field trial (30 s) with none; never received anything and pruned for
//   kMinConnectionLifetime (10 s). UpdateState then destroys it.
//
// The library's time model replaces libwebrtc's network thread and clock: calls that read the clock there take
// `now` here (as there, times at or before Timestamp 0 mean "never", so the caller's clock starts after 0) (a received packet's arrival time), nothing posts a task, and the Port runs the pair's STUN transactions
// in Port::Process. libwebrtc's DestroyConnectionAsync posts the delete; here the Port deletes the connection at its
// next Process (NextProcessTime says when).
//
// Not here: the DTLS-in-STUN piggyback (M157 has it behind a field trial that is off; see dtls_transport.h), the RTC
// event log (ice_logger), and the Unix-epoch conversion of the stats' timestamps (they stay in the caller's time
// base). The RTT estimate is libwebrtc's EventBasedExponentialMovingAverage (rtc_base/numerics/), and the rate
// trackers are BitrateTracker's RateStatistics (rtc_base/bitrate_tracker.cc: a 1 s window, kBpsScale 8000) used directly.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/core/event_based_exponential_moving_average.h"
#include "sm/core/rate_statistics.h"
#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/connection_info.h"
#include "sm/p2p/network.h"
#include "sm/p2p/p2p_constants.h"
#include "sm/p2p/p2p_transport_channel_ice_field_trials.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_request.h"
#include "sm/p2p/transport_description.h"

namespace sm::p2p {

class Port;

// Version number for GOOG_PING, this is added to have the option of adding new pings later.
inline constexpr int kGoogPingVersion = 1;
// 1200 is the "commonly used" MTU. Subtract the IP and UDP header (24 + 8 bytes).
inline constexpr int kMaxStunBindingLength = 1200 - 24 - 8;

// How a GOOG-DELTA-ACK consumer is told that the delta went unacknowledged: the remote does not support it
// (RTCErrorType::UNSUPPORTED_OPERATION), or an ack came for no delta (INTERNAL_ERROR).
enum class GoogDeltaAckError { kNone, kUnsupported, kInternal };

class Connection {
 public:
  struct SentPing {
    SentPing(std::string_view ping_id, Timestamp ping_sent_time, uint32_t ping_nomination)
        : id(ping_id), sent_time(ping_sent_time), nomination(ping_nomination) {}
    std::string id;
    Timestamp sent_time;
    uint32_t nomination;
  };

  enum WriteState {
    STATE_WRITABLE = 0,          // we have received ping responses recently
    STATE_WRITE_UNRELIABLE = 1,  // we have had a few ping failures
    STATE_WRITE_INIT = 2,        // we have yet to receive a ping response
    STATE_WRITE_TIMEOUT = 3,     // we have had a large number of ping failures
  };

  virtual ~Connection();
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // A unique ID assigned when the connection is created.
  uint32_t id() const { return id_; }
  const Candidate& local_candidate() const { return local_candidate_; }
  const Candidate& remote_candidate() const { return remote_candidate_; }
  // The network of the port; must not be called after Shutdown.
  const Network* network() const;
  int generation() const;
  // The pair priority (RFC 5245 5.7.2); 0 without a role or after Shutdown.
  uint64_t priority() const;

  WriteState write_state() const { return write_state_; }
  bool writable() const { return write_state_ == STATE_WRITABLE; }
  bool receiving() const { return receiving_; }
  const Port* port() const { return port_; }
  Port* PortForTest() { return port_; }
  // Can only be false for TCP connections.
  bool connected() const { return connected_; }
  bool weak() const { return !(writable() && receiving() && connected()); }
  bool active() const { return write_state_ != STATE_WRITE_TIMEOUT; }
  // True once Shutdown has run (the Port deletes the object next).
  bool pending_delete() const { return port_ == nullptr; }
  // A connection that is dead can be deleted.
  bool dead(Timestamp now) const;

  // The smoothed RTT (3 s before the first response).
  TimeDelta Rtt() const { return rtt_; }

  TimeDelta UnwritableTimeout() const { return unwritable_timeout_.value_or(kConnectionWriteConnectTimeout); }
  void SetUnwritableTimeout(std::optional<TimeDelta> value) { unwritable_timeout_ = value; }
  int unwritable_min_checks() const { return unwritable_min_checks_.value_or(kConnectionWriteConnectFailures); }
  void set_unwritable_min_checks(const std::optional<int>& value) { unwritable_min_checks_ = value; }
  TimeDelta InactiveTimeout() const { return inactive_timeout_.value_or(kConnectionWriteTimeout); }
  void SetInactiveTimeout(std::optional<TimeDelta> value) { inactive_timeout_ = value; }

  // The statistics, with the send and receive rates at `now`.
  ConnectionInfo stats(Timestamp now);

  // Signals, removed by tag.
  void SubscribeStateChange(const void* tag, std::function<void(Connection*)> cb) {
    state_change_callbacks_.emplace_back(tag, std::move(cb));
  }
  void SubscribeDestroyed(const void* tag, std::function<void(Connection*)> cb) {
    destroyed_callbacks_.emplace_back(tag, std::move(cb));
  }
  void UnsubscribeDestroyed(const void* tag) { Remove(destroyed_callbacks_, tag); }
  void SubscribeReadyToSend(const void* tag, std::function<void(Connection*)> cb) {
    ready_to_send_callbacks_.emplace_back(tag, std::move(cb));
  }
  void SubscribeNominated(const void* tag, std::function<void(Connection*)> cb) {
    nominated_callbacks_.emplace_back(tag, std::move(cb));
  }

  // Sends a data packet to the remote candidate. Returns the bytes sent, or -1 (GetError()).
  virtual int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) = 0;
  virtual int GetError() = 0;

  // One receiver for the data packets (libwebrtc RTC_CHECKs there is only one).
  void RegisterReceivedPacketCallback(std::function<void(Connection*, const ReceivedPacket&)> cb);
  void DeregisterReceivedPacketCallback() { received_packet_callback_ = nullptr; }

  // A packet from the remote candidate's address (the Port hands it over); `packet.arrival_time` is now.
  void OnReadPacket(const ReceivedPacket& packet);
  // The socket can send again.
  void OnReadyToSend() { Notify(ready_to_send_callbacks_); }

  // A pruned connection stops pinging; it is timed out for writing.
  bool pruned() const { return pruned_; }
  void Prune();

  bool use_candidate_attr() const { return use_candidate_attr_; }
  void set_use_candidate_attr(bool enable) { use_candidate_attr_ = enable; }
  void set_nomination(uint32_t value) { nomination_ = value; }
  uint32_t remote_nomination() const { return remote_nomination_; }
  // One side nominates and the other acknowledges, or the remote side nominated.
  bool nominated() const { return acked_nomination_ != 0 || remote_nomination_ != 0; }

  TimeDelta ReceivingTimeout() const { return receiving_timeout_.value_or(kWeakConnectionReceiveTimeout); }
  void SetReceivingTimeout(std::optional<TimeDelta> receiving_timeout) { receiving_timeout_ = receiving_timeout; }

  // Removes the connection from its Port and deletes it, now (Port::DestroyConnection).
  void Destroy(Timestamp now);
  // Signals destroyed and drops the port; the Port calls it before deleting. False if done already.
  bool Shutdown();
  // Fails and prunes the connection.
  void FailAndPrune();

  // Checks the write and receive state and destroys the connection (at the Port's next Process) when it is dead.
  void UpdateState(Timestamp now);

  void UpdateLocalIceParameters(int component, std::string_view username_fragment, std::string_view password);

  Timestamp LastPingSent() const { return last_ping_sent_; }
  // Sends a connectivity check; `delta` is a GOOG-DELTA for the STUN dictionary.
  void Ping(Timestamp now, std::unique_ptr<StunByteStringAttribute> delta = nullptr);
  // A response to `request_id`, after `rtt`.
  void ReceivedPingResponse(Timestamp now, TimeDelta rtt, std::string_view request_id,
                            const std::optional<uint32_t>& nomination = std::nullopt);
  std::unique_ptr<IceMessage> BuildPingRequestForTest() { return BuildPingRequest(nullptr); }
  Timestamp LastPingResponseReceived() const { return last_ping_response_received_; }
  const std::optional<std::string>& last_ping_id_received() const { return last_ping_id_received_; }
  // Whether any ping response has been received.
  int rtt_samples() const { return rtt_samples_; }

  Timestamp LastPingReceived() const { return last_ping_received_; }
  // A valid check (or indication) arrived.
  void ReceivedPing(Timestamp now, const std::optional<std::string>& request_id = std::nullopt);
  // A validated binding request or GOOG-PING from the remote side: answers it.
  void HandleStunBindingOrGoogPingRequest(Timestamp now, IceMessage* msg);
  // A check carrying GOOG-LAST-ICE-CHECK-RECEIVED acknowledges one of ours (field trial, off).
  void HandlePiggybackCheckAcknowledgementIfAny(Timestamp now, StunMessage* msg);

  Timestamp LastSendData() const { return last_send_data_; }
  Timestamp LastDataReceived() const { return last_data_received_; }

  // A hex string identifying the object, for logs.
  std::string ToDebugId() const;
  // "Conn[id:content:network:local->remote|CRWS|S|remote_nom|nom|priority|rtt]".
  std::string ToString() const;

  void PrintPingsSinceLastResponse(std::string* pings, size_t max) const;

  // Whether the ICE transport selected this connection.
  bool selected() const { return selected_; }
  void set_selected(bool selected) { selected_ = selected; }

  IceCandidatePairState state() const { return state_; }
  int num_pings_sent() const { return num_pings_sent_; }
  uint32_t ComputeNetworkCost() const;

  // Sets the remote candidate's password and generation when the ufrag matches.
  void MaybeSetRemoteIceParametersAndGeneration(const IceParameters& params, int generation);
  // A signalled candidate replaces the peer-reflexive one with the same address and credentials.
  void MaybeUpdatePeerReflexiveCandidate(const Candidate& new_candidate);

  // When anything (data, a check, a response) last arrived.
  Timestamp LastReceived() const;
  Timestamp ReceivingUnchangedSince() const { return receiving_unchanged_since_; }

  // The PRIORITY attribute of our checks.
  uint32_t prflx_priority() const;

  // The RTT has converged and no response is missing.
  bool stable(Timestamp now) const { return rtt_converged() && !missing_responses(now); }

  bool TooManyOutstandingPings(const std::optional<int>& max_outstanding_pings) const;

  void SetLocalCandidateNetworkCost(uint16_t cost);

  // `field_trials` must outlive the connection.
  void SetIceFieldTrials(const IceFieldTrials* field_trials);
  const EventBasedExponentialMovingAverage& GetRttEstimate() const { return rtt_estimate_; }

  // Resets the learned state (writable, receiving, the RTT estimate, outstanding checks) without a state change
  // signal; a network change makes P2PTransportChannel call it.
  void ForgetLearnedState();

  void SendStunBindingResponse(Timestamp now, const StunMessage* message);
  void SendGoogPingResponse(Timestamp now, const StunMessage* message);
  void SendResponseMessage(Timestamp now, const StunMessage& response);

  uint32_t acked_nomination() const { return acked_nomination_; }
  void set_remote_nomination(uint32_t remote_nomination) { remote_nomination_ = remote_nomination; }
  const std::string& remote_password_for_test() const { return remote_candidate_.password(); }
  void set_remote_password_for_test(std::string_view pwd) { remote_candidate_.set_password(pwd); }

  // The STUN dictionary's GOOG-DELTA handlers (StunDictionaryView/Writer live in P2PTransportChannel, #1002).
  using GoogDeltaConsumer = std::function<std::unique_ptr<StunAttribute>(const StunByteStringAttribute*)>;
  using GoogDeltaAckConsumer = std::function<void(const StunUInt64Attribute* ack, GoogDeltaAckError error)>;
  void SetStunDictConsumer(GoogDeltaConsumer goog_delta_consumer, GoogDeltaAckConsumer goog_delta_ack_consumer) {
    goog_delta_consumer_ = std::move(goog_delta_consumer);
    goog_delta_ack_consumer_ = std::move(goog_delta_ack_consumer);
  }
  void ClearStunDictConsumer() {
    goog_delta_consumer_ = nullptr;
    goog_delta_ack_consumer_ = nullptr;
  }

  void NotifyNominatedForTesting(Connection* connection) { Notify(nominated_callbacks_, connection); }

  // FakeIceLiteAgent: pretend the connection is writable. True if it was not.
  bool set_writable_for_fake_ice_lite(Timestamp now);

  // The pair's STUN transactions; the Port runs them.
  void ProcessRequests(Timestamp now) { requests_.Process(now); }
  Timestamp NextRequestTime() const { return requests_.NextProcessTime(); }
  // Tests: transaction ids from the fixed-seed generator when no crypto is installed.
  void set_allow_predictable_ids_for_testing(bool allow) { requests_.set_allow_predictable_ids_for_testing(allow); }

 protected:
  class ConnectionRequest;

  // The local candidate is `port`'s candidate `index`.
  Connection(Timestamp now, Port* port, size_t index, const Candidate& remote_candidate);

  // Called back when the StunRequestManager has a STUN packet to send.
  void OnSendStunPacket(Timestamp now, std::span<const uint8_t> data, StunRequest* req);

  // Callbacks from ConnectionRequest.
  virtual void OnConnectionRequestResponse(Timestamp now, StunRequest* req, StunMessage* response);
  void OnConnectionRequestErrorResponse(Timestamp now, ConnectionRequest* req, StunMessage* response);
  void OnConnectionRequestTimeout(Timestamp now, ConnectionRequest* req);
  void OnConnectionRequestSent(Timestamp now, ConnectionRequest* req);

  bool rtt_converged() const;
  // If the response is not received within 2 * RTT, the response is assumed to be missing.
  bool missing_responses(Timestamp now) const;

  void set_write_state(WriteState value);
  void UpdateReceiving(Timestamp now);
  void set_state(IceCandidatePairState state);
  void set_connected(bool value);

  Port* port() { return port_; }
  ConnectionInfo& mutable_stats() { return stats_; }
  void AddSentBytesToStats(int size, Timestamp now) {
    send_rate_tracker_.Update(size, now.ms());
    stats_.sent_total_bytes += static_cast<size_t>(size);
  }
  void set_last_send_data(Timestamp now) { last_send_data_ = now; }

 private:
  using Callbacks = std::vector<std::pair<const void*, std::function<void(Connection*)>>>;
  static void Remove(Callbacks& cbs, const void* tag) {
    std::erase_if(cbs, [tag](const auto& e) { return e.first == tag; });
  }
  void Notify(const Callbacks& cbs) { Notify(cbs, this); }
  static void Notify(const Callbacks& cbs, Connection* connection) {
    // Copy, so a receiver may subscribe or unsubscribe while it runs.
    auto copy = cbs;
    for (auto& [tag, cb] : copy) cb(connection);
  }

  std::unique_ptr<IceMessage> BuildPingRequest(std::unique_ptr<StunByteStringAttribute> delta);
  // A response's XOR-MAPPED-ADDRESS that is no local candidate makes a peer-reflexive one.
  void MaybeUpdateLocalCandidate(StunRequest* request, StunMessage* response);
  bool ShouldSendGoogPing(const StunMessage* message) const;

  const uint32_t id_;
  Port* port_;
  Candidate local_candidate_;
  Candidate remote_candidate_;

  ConnectionInfo stats_;
  // BitrateTracker: RateStatistics with scale 8000 over a 1 s window.
  RateStatistics recv_rate_tracker_{1000, 8000.0f};
  RateStatistics send_rate_tracker_{1000, 8000.0f};
  Timestamp last_send_data_ = Timestamp::Millis(0);

  WriteState write_state_ = STATE_WRITE_INIT;
  bool receiving_ = false;
  bool connected_ = true;
  bool pruned_ = false;
  bool selected_ = false;
  // By default the controlling side doesn't nominate; P2PTransportChannel sets this when it does (or tests).
  bool use_candidate_attr_ = false;
  // Used by the controlling side to indicate that this connection will be selected for transmission if the peer
  // supports ICE-renomination when this value is positive. A larger-value indicates that a connection is nominated
  // later and should be selected by the controlled side with higher precedence. A zero-value indicates not
  // nominating this connection.
  uint32_t nomination_ = 0;
  // The last nomination that has been acknowledged.
  uint32_t acked_nomination_ = 0;
  // Used by the controlled side to remember the nomination value received from the controlling side. When the
  // peer does not support ICE re-nomination, its value will be 1 if the connection has been nominated.
  uint32_t remote_nomination_ = 0;

  StunRequestManager requests_;
  TimeDelta rtt_;
  int rtt_samples_ = 0;
  // https://w3c.github.io/webrtc-stats/#dom-rtcicecandidatepairstats-totalroundtriptime
  TimeDelta total_round_trip_time_ = TimeDelta::Zero();
  // https://w3c.github.io/webrtc-stats/#dom-rtcicecandidatepairstats-currentroundtriptime
  std::optional<TimeDelta> current_round_trip_time_;
  Timestamp last_ping_sent_ = Timestamp::Millis(0);       // last time we sent a ping to the other side
  Timestamp last_ping_received_ = Timestamp::Millis(0);   // last time we received a ping from the other side
  Timestamp last_data_received_ = Timestamp::Millis(0);
  Timestamp last_ping_response_received_ = Timestamp::Millis(0);
  Timestamp receiving_unchanged_since_ = Timestamp::Millis(0);
  std::vector<SentPing> pings_since_last_response_;
  // Transaction ID of the last connectivity check received. Null if having not received a ping yet.
  std::optional<std::string> last_ping_id_received_;

  std::optional<TimeDelta> unwritable_timeout_;
  std::optional<int> unwritable_min_checks_;
  std::optional<TimeDelta> inactive_timeout_;

  IceCandidatePairState state_ = IceCandidatePairState::WAITING;
  // Time duration to switch from receiving to not receiving.
  std::optional<TimeDelta> receiving_timeout_;
  const Timestamp time_created_;
  int num_pings_sent_ = 0;

  // GOOG_PING_REQUEST is sent in place of STUN_BINDING_REQUEST if the remote supports it (from a
  // STUN_BINDING_RESPONSE) and the request equals the last one (from the last successful STUN_BINDING_REQUEST).
  std::optional<bool> remote_support_goog_ping_;
  std::unique_ptr<StunMessage> cached_stun_binding_;

  const IceFieldTrials* field_trials_;
  EventBasedExponentialMovingAverage rtt_estimate_;

  GoogDeltaConsumer goog_delta_consumer_;
  GoogDeltaAckConsumer goog_delta_ack_consumer_;
  std::function<void(Connection*, const ReceivedPacket&)> received_packet_callback_;

  Callbacks state_change_callbacks_;
  Callbacks destroyed_callbacks_;
  Callbacks ready_to_send_callbacks_;
  Callbacks nominated_callbacks_;
};

// ProxyConnection defers all the interesting work to the port.
class ProxyConnection : public Connection {
 public:
  ProxyConnection(Timestamp now, Port* port, size_t index, const Candidate& remote_candidate);

  int Send(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options) override;
  int GetError() override { return error_; }

 private:
  int error_ = 0;
};

}  // namespace sm::p2p
