// libwebrtc M157's p2p/base/stun_request.cc (see the header).
#include "sm/p2p/stun_request.h"

#include <algorithm>
#include <cassert>
#include <string_view>
#include <vector>

#include "sm/p2p/byte_buffer.h"

namespace sm::p2p {

StunRequestManager::StunRequestManager(SendPacketFn send_packet) : send_packet_(std::move(send_packet)) {}

StunRequestManager::~StunRequestManager() = default;

bool StunRequestManager::Send(Timestamp now, std::unique_ptr<StunRequest> request, TimeDelta delay) {
  assert(&request->manager_ == this);
  // libwebrtc DCHECKs "Sending request w/o integrity!".
  assert(!request->AuthenticationRequired() ||
         request->msg()->integrity() != StunMessage::IntegrityStatus::kNotSet);
  if (GetStunCrypto().random_bytes == nullptr && !allow_predictable_ids_) return false;
  auto [it, inserted] = requests_.emplace(request->id(), std::move(request));
  if (!inserted) return false;  // libwebrtc DCHECKs a fresh id.
  it->second->Start(now, delay);
  return true;
}

void StunRequestManager::Process(Timestamp now) {
  // One request at a time, earliest first, looked up again each round: a
  // callback may add, remove or clear requests.
  for (;;) {
    auto due = requests_.end();
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
      if (it->second->next_ <= now && (due == requests_.end() || it->second->next_ < due->second->next_)) due = it;
    }
    if (due == requests_.end()) return;
    const Timestamp at = due->second->next_;
    if (!due->second->SendNow(at)) {
      // Timed out: OnTimeout, then OnRequestTimedOut erases it. The request
      // leaves the map first so the callback may change the manager freely.
      std::unique_ptr<StunRequest> owned = std::move(due->second);
      requests_.erase(due);
      owned->OnTimeout(at);
    }
  }
}

Timestamp StunRequestManager::NextProcessTime() const {
  Timestamp t = Timestamp::PlusInfinity();
  for (const auto& [id, r] : requests_) t = std::min(t, r->next_);
  return t;
}

void StunRequestManager::FlushForTest(Timestamp now, int msg_type) {
  std::vector<std::string> ids;
  for (const auto& [id, r] : requests_) {
    if (msg_type == kAllRequestsForTest || msg_type == r->type()) ids.push_back(id);
  }
  for (const std::string& id : ids) {
    auto it = requests_.find(id);
    if (it == requests_.end()) continue;
    // ResetTasksForTest, then Send(0).
    it->second->count_ = 0;
    it->second->Start(now, TimeDelta::Zero());
  }
}

bool StunRequestManager::HasRequestForTest(int msg_type) const {
  assert(msg_type != kAllRequestsForTest);
  for (const auto& [id, r] : requests_) {
    if (r->type() == msg_type) return true;
  }
  return false;
}

void StunRequestManager::Clear() { requests_.clear(); }

bool StunRequestManager::CheckResponse(Timestamp now, StunMessage* msg) {
  auto iter = requests_.find(msg->transaction_id());
  if (iter == requests_.end()) return false;
  StunRequest* request = iter->second.get();

  // Some requests turn integrity checks off with SetAuthenticationRequired;
  // a request that needs them but carries none is a test that forgot them
  // (libwebrtc logs and DCHECKs, then skips the check).
  const bool skip_integrity_checking = request->msg()->integrity() == StunMessage::IntegrityStatus::kNotSet;
  if (!request->AuthenticationRequired()) {
    if (request->type() != STUN_BINDING_REQUEST) {
      // Unauthenticated TURN responses: only 401 and 300 errors get through.
      if (msg->type() == GetStunSuccessResponseType(request->type())) return false;
      if (msg->type() == GetStunErrorResponseType(request->type())) {
        const int code = msg->GetErrorCodeValue();
        if (code != STUN_ERROR_UNAUTHORIZED && code != STUN_ERROR_TRY_ALTERNATE) return false;
      }
    }
  } else if (!skip_integrity_checking) {
    if (msg->integrity() == StunMessage::IntegrityStatus::kNotSet) {
      msg->ValidateMessageIntegrity(request->msg()->password());
    } else if (msg->password() != request->msg()->password()) {
      // Checked earlier with another password (ice-pwd changed between the
      // check and its response).
      msg->RevalidateMessageIntegrity(request->msg()->password());
    }
  }

  if (!msg->GetNonComprehendedAttributes().empty()) {
    // RFC 5389 7.3.3 / 7.3.4: discard, and the transaction has failed.
    requests_.erase(iter);
    return false;
  }
  if (msg->type() == GetStunSuccessResponseType(request->type())) {
    if (!msg->IntegrityOk() && !skip_integrity_checking) return false;
    // Out of the map before the callback, which may change the manager.
    std::unique_ptr<StunRequest> owned = std::move(iter->second);
    requests_.erase(iter);
    owned->OnResponse(now, msg);
    return true;
  }
  if (msg->type() == GetStunErrorResponseType(request->type())) {
    std::unique_ptr<StunRequest> owned = std::move(iter->second);
    requests_.erase(iter);
    owned->OnErrorResponse(now, msg);
    return true;
  }
  return false;  // A response of the wrong type.
}

bool StunRequestManager::CheckResponse(Timestamp now, std::span<const uint8_t> payload) {
  if (payload.size() < 20) return false;
  const std::string_view id(reinterpret_cast<const char*>(payload.data()) + kStunTransactionIdOffset,
                            kStunTransactionIdLength);
  auto iter = requests_.find(id);
  if (iter == requests_.end()) return false;
  ByteBufferReader buf(payload);
  std::unique_ptr<StunMessage> response(iter->second->msg_->CreateNew());
  if (!response->Read(&buf)) return false;
  return CheckResponse(now, response.get());
}

StunRequest::StunRequest(StunRequestManager& manager)
    : manager_(manager), msg_(std::make_unique<StunMessage>(STUN_INVALID_MESSAGE_TYPE)) {}

StunRequest::StunRequest(StunRequestManager& manager, std::unique_ptr<StunMessage> message)
    : manager_(manager), msg_(std::move(message)) {
  assert(!msg_->transaction_id().empty());
}

StunRequest::~StunRequest() = default;

void StunRequest::Start(Timestamp now, TimeDelta delay) {
  timeout_ = false;
  next_ = now + delay;
  if (delay.IsZero()) SendNow(now);
}

bool StunRequest::SendNow(Timestamp now) {
  if (timeout_) {
    next_ = Timestamp::PlusInfinity();
    return false;
  }
  tstamp_ = now;
  ByteBufferWriter buf;
  msg_->Write(&buf);
  manager_.SendPacket(now, buf.DataView(), this);
  OnSent(now);
  next_ = now + TimeDelta::Millis(resend_delay());
  return true;
}

void StunRequest::OnSent(Timestamp /*now*/) {
  count_ += 1;
  if (count_ - 1 >= STUN_MAX_RETRANSMISSIONS) timeout_ = true;
}

int StunRequest::resend_delay() const {
  if (count_ == 0) return 0;
  const int retransmissions = count_ - 1;
  return std::min(STUN_INITIAL_RTO << std::min(retransmissions, 16), STUN_MAX_RTO);
}

}  // namespace sm::p2p
