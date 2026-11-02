// dcsctp's timeouts in the library's time model (#1060, C7 part 2), in place
// of libwebrtc M157's net/dcsctp/timer/task_queue_timeout.{h,cc}
// (compare/webrtc/reference/net/dcsctp/timer/task_queue_timeout.h).
//
// libwebrtc's TaskQueueTimeoutFactory posts a delayed task on a task queue
// for each started Timeout, reads the time through `get_time`, and calls
// `on_expired(timeout_id)` when the task runs and the timeout is still due;
// the host then passes the id to DcSctpSocketInterface::HandleTimeout. The
// library starts no threads and reads no clocks, so here the host drives it:
//
//   ProcessTimeoutFactory timeouts([&](TimeoutID id) { socket->HandleTimeout(id); });
//   // DcSctpSocketCallbacks::CreateTimeout returns timeouts.CreateTimeout(precision),
//   // and DcSctpSocketCallbacks::Now returns timeouts.now().
//   timeouts.Process(now);                // at timeouts.NextProcessTime(), or any time
//
// Process(now) sets the current time and calls `on_expired` for every
// started timeout whose expiry (start time + duration) is at or before
// `now`, earliest expiry first (ties in creation order), as the task queue
// would run the delayed tasks. A timeout that is stopped or restarted before
// it is due does not fire, as in libwebrtc, where the stale task finds it
// stopped or re-arms with the remaining time. DelayPrecision is accepted and
// not used: the host decides how precisely it calls Process.
//
// As in libwebrtc: each DcSctpSocket needs its own factory (TimeoutIDs are
// per socket), the factory must outlive the timeouts it created (so the
// socket), and it is not thread safe.
#ifndef SM_DCSCTP_TIMER_PROCESS_TIMEOUT_H_
#define SM_DCSCTP_TIMER_PROCESS_TIMEOUT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/public/timeout.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {

class ProcessTimeoutFactory {
 public:
  explicit ProcessTimeoutFactory(std::function<void(TimeoutID timeout_id)> on_expired,
                                 Timestamp now = Timestamp::Micros(0));
  ~ProcessTimeoutFactory();
  ProcessTimeoutFactory(const ProcessTimeoutFactory&) = delete;
  ProcessTimeoutFactory& operator=(const ProcessTimeoutFactory&) = delete;

  // Creates an implementation of `Timeout`.
  std::unique_ptr<Timeout> CreateTimeout(DelayPrecision precision = DelayPrecision::kLow);

  // The current time: the last value given to Process or set_now. Timeouts
  // started now expire at now() + duration.
  Timestamp now() const { return now_; }
  void set_now(Timestamp now) { now_ = now; }

  // The earliest expiry of a started timeout; PlusInfinity when none runs.
  Timestamp NextProcessTime() const;

  // Sets now() to `now`, then fires every timeout due by `now`.
  void Process(Timestamp now);

 private:
  class ProcessTimeout;

  Timestamp now_;
  uint64_t next_sequence_ = 0;
  std::vector<ProcessTimeout*> timeouts_;
  const std::function<void(TimeoutID)> on_expired_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_TIMER_PROCESS_TIMEOUT_H_
