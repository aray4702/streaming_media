#include "sm/core/logging.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <utility>

namespace sm {
namespace {

constexpr LogSeverity InitialSeverity() {
  return static_cast<int>(kDefaultLogSeverity) >= SM_LOG_MIN_LEVEL ? kDefaultLogSeverity : kCompiledMinLogSeverity;
}

std::atomic<int> g_min_severity{static_cast<int>(InitialSeverity())};

LogSink& Sink() {
  static LogSink sink;
  return sink;
}

const char* Tag(LogSeverity s) {
  switch (s) {
    case LogSeverity::kVerbose: return "V";
    case LogSeverity::kInfo: return "I";
    case LogSeverity::kWarning: return "W";
    case LogSeverity::kError: return "E";
    case LogSeverity::kNone: break;
  }
  return "?";
}

}  // namespace

ObsStatus SetLogSeverity(LogSeverity min) {
  if (min != LogSeverity::kNone && !LogCompiled(min)) return ObsStatus::kNotSupported;
  g_min_severity.store(static_cast<int>(min), std::memory_order_relaxed);
  return ObsStatus::kOk;
}

LogSeverity GetLogSeverity() { return static_cast<LogSeverity>(g_min_severity.load(std::memory_order_relaxed)); }

void SetLogSink(LogSink sink) { Sink() = std::move(sink); }

bool LogEnabled(LogSeverity s) {
  return LogCompiled(s) && static_cast<int>(s) >= g_min_severity.load(std::memory_order_relaxed);
}

void LogPrintf(LogSeverity s, const char* file, int line, const char* fmt, ...) {
  if (!LogEnabled(s)) return;
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  LogMessage m{s, file, line, buf};
  if (const LogSink& sink = Sink()) {
    sink(m);
  } else {
    std::fprintf(stderr, "[sm %s] %s:%d: %s\n", Tag(s), file, line, buf);
  }
}

}  // namespace sm
