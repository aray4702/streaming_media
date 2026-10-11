// Structured event log for observability, the lightweight counterpart of the
// WebRTC RTC event log. Algorithms emit decision events (NACK sent, overuse
// detected, keyframe requested, target rate changed) through an optional
// EventSink pointer in their config. A null sink costs one branch; building
// with SM_ENABLE_EVENTS=0 removes the calls entirely.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sm/core/units.h"

namespace sm {

struct Event {
  Timestamp time;
  const char* module = "";  // Static string, e.g. "nack".
  const char* name = "";    // Static string, e.g. "request".
  double value = 0;
  int64_t aux = 0;
};

class EventSink {
 public:
  virtual ~EventSink() = default;
  virtual void OnEvent(const Event& e) = 0;
};

// Fixed-capacity ring buffer of events with JSON-lines export and an optional
// forwarding callback (e.g. to platform logging such as os_log or logcat).
class EventLog : public EventSink {
 public:
  explicit EventLog(size_t capacity = 4096);
  void OnEvent(const Event& e) override;
  void SetCallback(std::function<void(const Event&)> cb) { callback_ = std::move(cb); }

  size_t size() const { return count_ < capacity_ ? count_ : capacity_; }
  uint64_t total() const { return count_; }
  // Oldest to newest.
  std::vector<Event> Snapshot() const;
  size_t Count(const char* module, const char* name) const;
  std::string ToJsonLines() const;
  void Clear();

 private:
  size_t capacity_;
  std::vector<Event> ring_;
  uint64_t count_ = 0;
  std::function<void(const Event&)> callback_;
};

}  // namespace sm

#if SM_ENABLE_EVENTS
#define SM_EVENT(sink, t, module, name, value, aux)                     \
  do {                                                                  \
    if (sink) (sink)->OnEvent(::sm::Event{(t), (module), (name), static_cast<double>(value), \
                                          static_cast<int64_t>(aux)});  \
  } while (0)
#else
#define SM_EVENT(sink, t, module, name, value, aux) \
  do {                                              \
  } while (0)
#endif
