/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/timer/fake_timeout.h (compare/webrtc/reference/net/dcsctp/timer/fake_timeout.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_TIMER_FAKE_TIMEOUT_H_
#define SM_DCSCTP_TIMER_FAKE_TIMEOUT_H_

#include <set>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/public/timeout.h"
#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {

// A timeout used in tests.
class FakeTimeout : public Timeout {
 public:
  FakeTimeout(std::function<Timestamp()> get_time,
              std::function<void(FakeTimeout*)> on_delete)
      : get_time_(std::move(get_time)), on_delete_(std::move(on_delete)) {}

  ~FakeTimeout() override { on_delete_(this); }

  void Start(DurationMs duration_ms, TimeoutID timeout_id) override {
    SM_DCSCTP_DCHECK(expiry_.IsPlusInfinity());
    timeout_id_ = timeout_id;
    expiry_ = get_time_() + duration_ms.ToTimeDelta();
  }
  void Stop() override {
    SM_DCSCTP_DCHECK(!expiry_.IsPlusInfinity());
    expiry_ = Timestamp::PlusInfinity();
  }

  bool EvaluateHasExpired(Timestamp now) {
    if (now >= expiry_) {
      expiry_ = Timestamp::PlusInfinity();
      return true;
    }
    return false;
  }

  TimeoutID timeout_id() const { return timeout_id_; }
  Timestamp expiry() const { return expiry_; }

 private:
  const std::function<Timestamp()> get_time_;
  const std::function<void(FakeTimeout*)> on_delete_;

  TimeoutID timeout_id_ = TimeoutID(0);
  Timestamp expiry_ = Timestamp::PlusInfinity();
};

class FakeTimeoutManager {
 public:
  // The `get_time` function must return the current time, relative to any
  // epoch.
  explicit FakeTimeoutManager(std::function<Timestamp()> get_time)
      : get_time_(std::move(get_time)) {}

  std::unique_ptr<FakeTimeout> CreateTimeout() {
    auto timer = std::make_unique<FakeTimeout>(
        get_time_, [this](FakeTimeout* timer) { timers_.erase(timer); });
    timers_.insert(timer.get());
    return timer;
  }
  std::unique_ptr<FakeTimeout> CreateTimeout(
      DelayPrecision /* precision */) {
    // FakeTimeout does not support implement |precision|.
    return CreateTimeout();
  }

  // NOTE: This can't return a vector, as calling EvaluateHasExpired requires
  // calling socket->HandleTimeout directly afterwards, as the owning Timer
  // still believes it's running, and it needs to be updated to set
  // Timer::is_running_ to false before you operate on the Timer or Timeout
  // again.
  std::optional<TimeoutID> GetNextExpiredTimeout() {
    Timestamp now = get_time_();
    std::vector<TimeoutID> expired_timers;
    for (auto& timer : timers_) {
      if (timer->EvaluateHasExpired(now)) {
        return timer->timeout_id();
      }
    }
    return std::nullopt;
  }

  TimeDelta GetTimeToNextTimeout() const {
    Timestamp next_expiry = Timestamp::PlusInfinity();
    for (const FakeTimeout* timer : timers_) {
      if (timer->expiry() < next_expiry) {
        next_expiry = timer->expiry();
      }
    }
    Timestamp now = get_time_();
    return !next_expiry.IsPlusInfinity() && next_expiry >= now
               ? next_expiry - now
               : TimeDelta::PlusInfinity();
  }

 private:
  const std::function<Timestamp()> get_time_;
  std::set<FakeTimeout*> timers_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_TIMER_FAKE_TIMEOUT_H_
