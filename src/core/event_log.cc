#include "sm/core/event_log.h"

#include <cstdio>
#include <cstring>

namespace sm {

EventLog::EventLog(size_t capacity) : capacity_(capacity ? capacity : 1), ring_(capacity_) {}

void EventLog::OnEvent(const Event& e) {
  ring_[count_ % capacity_] = e;
  ++count_;
  if (callback_) callback_(e);
}

std::vector<Event> EventLog::Snapshot() const {
  std::vector<Event> out;
  size_t n = size();
  out.reserve(n);
  uint64_t start = count_ - n;
  for (uint64_t i = start; i < count_; ++i) out.push_back(ring_[i % capacity_]);
  return out;
}

size_t EventLog::Count(const char* module, const char* name) const {
  size_t c = 0;
  for (const Event& e : Snapshot()) {
    if (std::strcmp(e.module, module) == 0 && std::strcmp(e.name, name) == 0) ++c;
  }
  return c;
}

std::string EventLog::ToJsonLines() const {
  std::string out;
  char buf[256];
  for (const Event& e : Snapshot()) {
    std::snprintf(buf, sizeof(buf), "{\"t_us\":%lld,\"module\":\"%s\",\"name\":\"%s\",\"value\":%.6g,\"aux\":%lld}\n",
                  static_cast<long long>(e.time.us()), e.module, e.name, e.value,
                  static_cast<long long>(e.aux));
    out += buf;
  }
  return out;
}

void EventLog::Clear() { count_ = 0; }

}  // namespace sm
