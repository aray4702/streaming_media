#include "sim_time.h"

#include <algorithm>
#include <deque>
#include <utility>

#include "absl/functional/any_invocable.h"

namespace sm::compare {

using webrtc::TaskQueueBase;
using webrtc::TimeDelta;
using webrtc::Timestamp;

// A task queue whose tasks SimTime runs on the simulation thread.
class SimQueue final : public TaskQueueBase {
 public:
  using Task = absl::AnyInvocable<void() &&>;

  SimQueue(SimTime* owner, std::string name) : owner_(owner), name_(std::move(name)) {}

  absl::string_view queue_name() const override { return name_; }

  void Delete() override {
    deleted_ = true;
    owner_->Unregister(this);
    // Destroy pending tasks outside of any lock: their destructors may post
    // to other queues.
    std::deque<Task> ready = std::move(ready_);
    std::map<std::pair<int64_t, uint64_t>, Task> delayed = std::move(delayed_);
    ready.clear();
    delayed.clear();
    if (!running_) delete this;  // Otherwise RunReady deletes it after the running task.
  }

  void PostTaskImpl(Task task, const PostTaskTraits&, const webrtc::Location&) override {
    if (deleted_) return;
    ready_.push_back(std::move(task));
  }

  void PostDelayedTaskImpl(Task task, TimeDelta delay, const PostDelayedTaskTraits&,
                           const webrtc::Location&) override {
    if (deleted_) return;
    Timestamp at = owner_->Now() + std::max(delay, TimeDelta::Zero());
    delayed_.emplace(std::make_pair(at.us(), next_order_++), std::move(task));
  }

  Timestamp NextRunTime() const {
    if (!ready_.empty()) return owner_->Now();
    if (delayed_.empty()) return Timestamp::PlusInfinity();
    return Timestamp::Micros(delayed_.begin()->first.first);
  }

  // Runs every task due at `now`, including tasks they post to this queue.
  // Returns whether any task ran. May delete this queue.
  bool RunReady(Timestamp now) {
    bool ran = false;
    while (true) {
      while (!delayed_.empty() && delayed_.begin()->first.first <= now.us()) {
        ready_.push_back(std::move(delayed_.begin()->second));
        delayed_.erase(delayed_.begin());
      }
      if (ready_.empty()) break;
      Task task = std::move(ready_.front());
      ready_.pop_front();
      running_ = true;
      {
        CurrentTaskQueueSetter current(this);
        std::move(task)();
        task = nullptr;  // Destroy captured state while this queue is current.
      }
      running_ = false;
      ran = true;
      if (deleted_) {
        delete this;
        return true;
      }
    }
    return ran;
  }

 private:
  ~SimQueue() override = default;

  SimTime* owner_;
  std::string name_;
  std::deque<Task> ready_;
  std::map<std::pair<int64_t, uint64_t>, Task> delayed_;  // (time, order) -> task.
  uint64_t next_order_ = 0;
  bool running_ = false;
  bool deleted_ = false;
};

namespace {

// Lets SimTime clear TaskQueueBase::Current() while it yields, so code run
// during the yield does not think it is on the yielding queue.
class NoQueue final : public TaskQueueBase {
 public:
  class Clear {
   public:
    Clear() : setter_(nullptr) {}

   private:
    CurrentTaskQueueSetter setter_;
  };
  void Delete() override {}
  void PostTaskImpl(absl::AnyInvocable<void() &&>, const PostTaskTraits&, const webrtc::Location&) override {}
  void PostDelayedTaskImpl(absl::AnyInvocable<void() &&>, TimeDelta, const PostDelayedTaskTraits&,
                           const webrtc::Location&) override {}
};

}  // namespace

SimTime::SimTime(Timestamp start) : clock_(start), global_clock_(&clock_) {
  previous_global_clock_ = webrtc::SetClockForTesting(&global_clock_);
}

SimTime::~SimTime() { webrtc::SetClockForTesting(previous_global_clock_); }

std::unique_ptr<TaskQueueBase, webrtc::TaskQueueDeleter> SimTime::CreateTaskQueue(absl::string_view name,
                                                                                  Priority) const {
  auto* self = const_cast<SimTime*>(this);
  auto* q = new SimQueue(self, std::string(name));
  self->Register(q);
  return std::unique_ptr<TaskQueueBase, webrtc::TaskQueueDeleter>(q);
}

void SimTime::Register(SimQueue* q) {
  queues_.push_back(q);
  alive_.insert(q);
}

void SimTime::Unregister(SimQueue* q) {
  alive_.erase(q);
  queues_.erase(std::remove(queues_.begin(), queues_.end(), q), queues_.end());
}

Timestamp SimTime::NextRunTime() const {
  Timestamp next = Timestamp::PlusInfinity();
  for (SimQueue* q : queues_) {
    if (yielded_.count(q)) continue;
    next = std::min(next, q->NextRunTime());
  }
  return next;
}

void SimTime::RunReady() {
  const Timestamp now = Now();
  bool any = true;
  while (any) {
    any = false;
    std::vector<SimQueue*> snapshot = queues_;
    for (SimQueue* q : snapshot) {
      if (!alive_.count(q) || yielded_.count(q)) continue;
      any |= q->RunReady(now);
    }
  }
}

void SimTime::RunUntil(Timestamp until) {
  webrtc::ScopedYieldPolicy policy(this);
  while (true) {
    RunReady();
    Timestamp next = NextRunTime();
    if (next > until) break;
    if (next > Now()) clock_.AdvanceTime(next - Now());
  }
  if (until > Now()) clock_.AdvanceTime(until - Now());
  RunReady();
}

void SimTime::YieldExecution() {
  TaskQueueBase* from = TaskQueueBase::Current();
  NoQueue::Clear clear;
  bool inserted = yielded_.insert(from).second;
  RunReady();
  if (inserted) yielded_.erase(from);
}

}  // namespace sm::compare
