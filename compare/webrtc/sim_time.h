// Simulated time for libwebrtc: a clock and task queues that run on one
// thread and jump from one scheduled task to the next, so a 30 s call runs
// in well under a second and the same seed gives the same result.
//
// libwebrtc has this in test/time_controller (GlobalSimulatedTimeController),
// but prebuilt release binaries leave test code out. This is a smaller
// version of the same design: every task queue libwebrtc creates comes from
// SimTime, which runs whichever queue has a task due, then advances the
// clock to the next due task.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/timestamp.h"
#include "rtc_base/synchronization/yield_policy.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/clock.h"

namespace sm::compare {

class SimQueue;

class SimTime final : public webrtc::TaskQueueFactory, public webrtc::YieldInterface {
 public:
  explicit SimTime(webrtc::Timestamp start);
  ~SimTime() override;
  SimTime(const SimTime&) = delete;
  SimTime& operator=(const SimTime&) = delete;

  webrtc::Clock* clock() { return &clock_; }
  webrtc::Timestamp Now() const { return clock_.CurrentTime(); }

  // TaskQueueFactory.
  std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> CreateTaskQueue(
      absl::string_view name, Priority priority) const override;

  // Runs every task due up to and including `until`, advancing the clock.
  void RunUntil(webrtc::Timestamp until);
  // Runs every task due now, without advancing the clock.
  void RunReady();

  // YieldInterface: rtc::Event::Wait calls this before blocking, so a task
  // that waits for another queue lets that queue run first.
  void YieldExecution() override;

 private:
  friend class SimQueue;
  // libwebrtc code that reads rtc::TimeMicros() instead of the Environment
  // clock.
  class GlobalClock final : public webrtc::ClockInterface {
   public:
    explicit GlobalClock(webrtc::SimulatedClock* c) : clock_(c) {}
    int64_t TimeNanos() const override { return clock_->CurrentTime().us() * 1000; }

   private:
    webrtc::SimulatedClock* clock_;
  };

  void Register(SimQueue* q);
  void Unregister(SimQueue* q);
  webrtc::Timestamp NextRunTime() const;

  mutable webrtc::SimulatedClock clock_;
  GlobalClock global_clock_;
  webrtc::ClockInterface* previous_global_clock_ = nullptr;
  std::vector<SimQueue*> queues_;  // In creation order, for a fixed run order.
  std::set<SimQueue*> alive_;
  std::set<webrtc::TaskQueueBase*> yielded_;
};

}  // namespace sm::compare
