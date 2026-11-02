// dcsctp's timeouts in the library's time model (#1060); see
// sm/dcsctp/timer/process_timeout.h. Follows libwebrtc M157's
// net/dcsctp/timer/task_queue_timeout.cc for Start/Stop and expiry.
#include "sm/dcsctp/timer/process_timeout.h"

#include <algorithm>
#include <utility>

namespace sm::dcsctp {

class ProcessTimeoutFactory::ProcessTimeout : public Timeout {
 public:
  ProcessTimeout(ProcessTimeoutFactory& parent, uint64_t sequence) : parent_(parent), sequence_(sequence) {
    parent_.timeouts_.push_back(this);
  }
  ~ProcessTimeout() override {
    auto& v = parent_.timeouts_;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }

  // task_queue_timeout.cc: TaskQueueTimeout::Start.
  void Start(DurationMs duration_ms, TimeoutID timeout_id) override {
    SM_DCSCTP_DCHECK(expiration_.IsPlusInfinity());
    expiration_ = parent_.now_ + duration_ms.ToTimeDelta();
    timeout_id_ = timeout_id;
  }
  // task_queue_timeout.cc: TaskQueueTimeout::Stop.
  void Stop() override { expiration_ = Timestamp::PlusInfinity(); }

  Timestamp expiration() const { return expiration_; }
  uint64_t sequence() const { return sequence_; }
  TimeoutID Expire() {
    expiration_ = Timestamp::PlusInfinity();
    return timeout_id_;
  }

 private:
  ProcessTimeoutFactory& parent_;
  const uint64_t sequence_;
  Timestamp expiration_ = Timestamp::PlusInfinity();
  TimeoutID timeout_id_ = TimeoutID(0);
};

ProcessTimeoutFactory::ProcessTimeoutFactory(std::function<void(TimeoutID)> on_expired, Timestamp now)
    : now_(now), on_expired_(std::move(on_expired)) {}

ProcessTimeoutFactory::~ProcessTimeoutFactory() { SM_DCSCTP_DCHECK(timeouts_.empty()); }

std::unique_ptr<Timeout> ProcessTimeoutFactory::CreateTimeout(DelayPrecision /* precision */) {
  return std::make_unique<ProcessTimeout>(*this, next_sequence_++);
}

Timestamp ProcessTimeoutFactory::NextProcessTime() const {
  Timestamp next = Timestamp::PlusInfinity();
  for (const ProcessTimeout* t : timeouts_) next = std::min(next, t->expiration());
  return next;
}

void ProcessTimeoutFactory::Process(Timestamp now) {
  now_ = now;
  // Each callback may start, stop, create or delete timeouts, so look again
  // after every one.
  for (;;) {
    ProcessTimeout* due = nullptr;
    for (ProcessTimeout* t : timeouts_) {
      if (t->expiration() > now_) continue;
      if (due == nullptr || t->expiration() < due->expiration() ||
          (t->expiration() == due->expiration() && t->sequence() < due->sequence())) {
        due = t;
      }
    }
    if (due == nullptr) return;
    const TimeoutID id = due->Expire();
    SM_DCSCTP_DLOG << "Timeout triggered: " << id.value();
    on_expired_(id);
  }
}

}  // namespace sm::dcsctp
