// Minimal text logging in the spirit of libwebrtc's RTC_LOG (rtc_base/logging.h,
// not in compare/webrtc/reference/): severities LS_VERBOSE, LS_INFO,
// LS_WARNING, LS_ERROR and LS_NONE, a process-wide minimum severity, and a
// sink. Issue #988 (requirement C8).
//
//   SM_LOG(kWarning, "unroutable datagram, ssrc %u", ssrc);
//
// - Compile time: `SM_LOG_MIN_LEVEL` (CMake `SM_LOG_MIN_LEVEL`, VERBOSE by
//   default) removes every call below that severity; NONE removes them all.
// - Run time: `SetLogSeverity` (warnings by default). A severity below the
//   compiled minimum returns ObsStatus::kNotSupported and changes nothing.
// - Output: stderr unless the app sets a sink (`SetLogSink`), for example to
//   forward to os_log or the browser console. The library reads no clock, so a
//   message carries no time; the sink can add one.
//
// Logging never changes what the library does. Set the severity and the sink
// before other threads log; logging itself only reads them.
#pragma once

#include <functional>

#include "sm/core/observability.h"

namespace sm {

enum class LogSeverity : int {
  kVerbose = 0,
  kInfo = 1,
  kWarning = 2,
  kError = 3,
  kNone = 4,
};

inline constexpr LogSeverity kCompiledMinLogSeverity = static_cast<LogSeverity>(SM_LOG_MIN_LEVEL);
// The runtime default: warnings and errors.
inline constexpr LogSeverity kDefaultLogSeverity = LogSeverity::kWarning;

// Whether a call at this severity is compiled in.
constexpr bool LogCompiled(LogSeverity s) {
  return s != LogSeverity::kNone && static_cast<int>(s) >= SM_LOG_MIN_LEVEL;
}

struct LogMessage {
  LogSeverity severity = LogSeverity::kInfo;
  const char* file = "";  // __FILE__ of the call.
  int line = 0;
  const char* text = "";  // Formatted, without a trailing newline.
};
using LogSink = std::function<void(const LogMessage&)>;

// Messages at `min` and above are logged. kNone turns logging off. A severity
// that is compiled out returns kNotSupported and leaves the setting unchanged.
ObsStatus SetLogSeverity(LogSeverity min);
LogSeverity GetLogSeverity();
// Null restores the stderr sink.
void SetLogSink(LogSink sink);
// Whether a message at `s` would be logged now.
bool LogEnabled(LogSeverity s);
// The SM_LOG back end: printf-style; messages longer than 1 KB are truncated.
void LogPrintf(LogSeverity s, const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

}  // namespace sm

#define SM_LOG(sev, ...)                                                                    \
  do {                                                                                      \
    if constexpr (::sm::LogCompiled(::sm::LogSeverity::sev)) {                              \
      if (::sm::LogEnabled(::sm::LogSeverity::sev))                                         \
        ::sm::LogPrintf(::sm::LogSeverity::sev, __FILE__, __LINE__, __VA_ARGS__);           \
    }                                                                                       \
  } while (0)
