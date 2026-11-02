// STUN transactions (issue #997, part of ICE #990, milestone 3): libwebrtc
// M157's p2p/base/stun_request.{h,cc} (compare/webrtc/reference/p2p/base/).
//
// A StunRequest is one outstanding request; the StunRequestManager keeps them
// by transaction id, retransmits them on libwebrtc's schedule and matches
// responses to them:
// - first send after the given delay, then resends 250, 500, 1000, 2000,
//   4000, 8000, 8000 and 8000 ms apart (STUN_INITIAL_RTO = 250 ms doubling,
//   capped at STUN_MAX_RTO = 8000 ms), 9 sends in all
//   (STUN_MAX_RETRANSMISSIONS = 8), and the timeout 8000 ms after the last
//   send: 39750 ms after the first (STUN_TOTAL_TIMEOUT);
// - a response is checked as CheckResponse does: by transaction id, then
//   MESSAGE-INTEGRITY with the request's password when the request requires
//   authentication (revalidated if it was checked with another password),
//   unauthenticated TURN responses refused except 401 and 300 errors, and a
//   response with unknown comprehension-required attributes discarded and the
//   transaction failed (RFC 5389 7.3.3 and 7.3.4).
//
// The library's time model replaces libwebrtc's task queue: nothing reads a
// clock or posts a task. The manager's owner passes `now` in and calls
// Process(now) at NextProcessTime(); the request callbacks get `now`.
//
// Transaction ids guard against spoofed responses (RFC 5389 section 6), so
// they must be unpredictable on a real call. The manager refuses to send a
// request while STUN's random-bytes hook is not installed (sm/p2p/stun.h,
// SetStunCrypto; InstallStunCryptoFromTls() in sm/p2p/stun_crypto_tls.h
// installs BoringSSL's), unless a test allows the fixed-seed fallback.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "sm/core/units.h"
#include "sm/p2p/stun.h"

namespace sm::p2p {

class StunRequest;

inline constexpr int kAllRequestsForTest = 0;
inline constexpr int STUN_TOTAL_TIMEOUT = 39750;  // ms
// stun_request.cc's schedule.
inline constexpr int STUN_INITIAL_RTO = 250;        // ms
inline constexpr int STUN_MAX_RETRANSMISSIONS = 8;  // 9 sends in all.
inline constexpr int STUN_MAX_RTO = 8000;           // ms

// The send callback must not add or remove requests (it runs inside a send).
class StunRequestManager {
 public:
  using SendPacketFn = std::function<void(Timestamp now, std::span<const uint8_t>, StunRequest*)>;

  explicit StunRequestManager(SendPacketFn send_packet);
  ~StunRequestManager();
  StunRequestManager(const StunRequestManager&) = delete;
  StunRequestManager& operator=(const StunRequestManager&) = delete;

  // Starts `request`: sent at `now + delay` (now if zero), then resent until a
  // response or the timeout. Returns false, and drops the request, when
  // transaction ids would be predictable (see the top of the file).
  bool Send(Timestamp now, std::unique_ptr<StunRequest> request, TimeDelta delay = TimeDelta::Zero());

  // Sends, resends and times out what is due by `now`.
  void Process(Timestamp now);
  // When Process next has something to do; PlusInfinity when idle.
  Timestamp NextProcessTime() const;

  // Sends every request of `msg_type` (or all) again now and restarts its
  // schedule.
  void FlushForTest(Timestamp now, int msg_type);
  bool HasRequestForTest(int msg_type) const;

  // Drops every request without calling it back.
  void Clear();

  // Matches a response to its request and calls the request back. True if it
  // was a response to one of ours that was accepted.
  bool CheckResponse(Timestamp now, StunMessage* msg);
  // The same from the bytes: looks the transaction id up first, then parses.
  bool CheckResponse(Timestamp now, std::span<const uint8_t> payload);

  bool empty() const { return requests_.empty(); }

  // Allows the fixed-seed transaction ids of sm/p2p/stun.h when no crypto is
  // installed. Tests only.
  void set_allow_predictable_ids_for_testing(bool allow) { allow_predictable_ids_ = allow; }

 private:
  friend class StunRequest;
  void SendPacket(Timestamp now, std::span<const uint8_t> data, StunRequest* request) {
    send_packet_(now, data, request);
  }

  std::map<std::string, std::unique_ptr<StunRequest>, std::less<>> requests_;
  const SendPacketFn send_packet_;
  bool allow_predictable_ids_ = false;
};

class StunRequest {
 public:
  // A binding-less request with an invalid type and a generated id, as
  // libwebrtc's first constructor; subclasses fill mutable_msg().
  explicit StunRequest(StunRequestManager& manager);
  StunRequest(StunRequestManager& manager, std::unique_ptr<StunMessage> message);
  virtual ~StunRequest();
  StunRequest(const StunRequest&) = delete;
  StunRequest& operator=(const StunRequest&) = delete;

  StunRequestManager* manager() { return &manager_; }
  const std::string& id() const { return msg_->transaction_id(); }
  uint32_t reduced_transaction_id() const { return msg_->reduced_transaction_id(); }
  int type() const { return msg_->type(); }
  const StunMessage* msg() const { return msg_.get(); }
  // Time since the latest send.
  TimeDelta Elapsed(Timestamp now) const { return now - tstamp_; }
  void SetAuthenticationRequired(bool val) { authentication_required_ = val; }
  bool AuthenticationRequired() const { return authentication_required_; }

 protected:
  friend class StunRequestManager;
  StunMessage* mutable_msg() { return msg_.get(); }

  virtual void OnResponse(Timestamp /*now*/, StunMessage* /*response*/) {}
  virtual void OnErrorResponse(Timestamp /*now*/, StunMessage* /*response*/) {}
  virtual void OnTimeout(Timestamp /*now*/) {}
  // Called after each send; the base counts sends and decides the timeout.
  virtual void OnSent(Timestamp now);
  // Delay to the next send: 0 before the first, then the backoff.
  virtual int resend_delay() const;
  void set_timed_out() { timeout_ = true; }

 private:
  void Start(Timestamp now, TimeDelta delay);
  // SendInternal; false when the request timed out instead (the manager then
  // removes it and calls OnTimeout).
  bool SendNow(Timestamp now);

  StunRequestManager& manager_;
  const std::unique_ptr<StunMessage> msg_;
  Timestamp tstamp_ = Timestamp::Millis(0);
  Timestamp next_ = Timestamp::PlusInfinity();  // Next send (or timeout), if scheduled.
  int count_ = 0;
  bool timeout_ = false;
  bool authentication_required_ = true;
};

}  // namespace sm::p2p
