// Observability build switches and their runtime counterparts (issue #988, requirement C8).
//
// Each observability output can be compiled out with a CMake option, and each
// one that is compiled in has a runtime switch:
//
// | Output | CMake option (default) | Runtime switch |
// |---|---|---|
// | Metrics (`MetricsSource`, `MetricsRegistry`) | `SM_ENABLE_METRICS` (ON) | Computed only when the app exports them (`MetricsRegistry::Export`). Compiled out, `Histogram::Add` and `MetricsRegistry::Export` do nothing. |
// | Decision events (`SM_EVENT`, `EventLog`) | `SM_ENABLE_EVENTS` (ON) | The component's `EventSink*` (null: off). Compiled out, `SM_EVENT` expands to nothing, so a sink gets no events. |
// | Stats snapshot (`rtp::Session::GetStats`) | `SM_ENABLE_STATS` (ON) | Computed only when the app asks. Compiled out, `GetStats` returns `ObsStatus::kNotSupported`. |
// | Text log (`SM_LOG`, sm/core/logging.h) | `SM_LOG_MIN_LEVEL` (`VERBOSE`) | `SetLogSeverity`, default warnings. Levels below the compiled minimum are compiled out, and asking for them returns `ObsStatus::kNotSupported`. |
//
// The rule: a runtime switch can't turn on something compiled out. The calls
// that would return `ObsStatus::kNotSupported` instead, with no exceptions.
// None of these outputs changes what the library does with media: a build
// with all of them off gives bit-identical media
// (tools/check_observability_off.py checks it on the scenario suite).
#pragma once

#include <cstdint>

#ifndef SM_ENABLE_STATS
#define SM_ENABLE_STATS 1
#endif
// 0 verbose, 1 info, 2 warning, 3 error, 4 none (sm::LogSeverity).
#ifndef SM_LOG_MIN_LEVEL
#define SM_LOG_MIN_LEVEL 0
#endif

namespace sm {

enum class ObsStatus : uint8_t {
  kOk,
  kNotSupported,  // Compiled out in this build.
};

// What this build compiled in.
struct ObservabilityBuild {
  bool metrics = false;
  bool events = false;
  bool stats = false;
  int log_min_level = 4;  // sm::LogSeverity as an int; 4 (none): no text log.
};

constexpr ObservabilityBuild CompiledObservability() {
  ObservabilityBuild b;
#if SM_ENABLE_METRICS
  b.metrics = true;
#endif
#if SM_ENABLE_EVENTS
  b.events = true;
#endif
#if SM_ENABLE_STATS
  b.stats = true;
#endif
  b.log_min_level = SM_LOG_MIN_LEVEL;
  return b;
}

}  // namespace sm
