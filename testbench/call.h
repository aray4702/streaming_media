// Runs one scenario: a complete one-way audio + video call through the
// library over the scenario's emulated links, measured the way a user
// experiences it (audio gaps, video freezes, delay, quality, recovery time).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "scenario.h"

namespace sm::testbench {

// One 100 ms sample of the call, for plots.
struct Sample {
  double t_s = 0;
  double capacity_kbps = -1;   // -1 = unlimited, 0 = link down.
  double target_kbps = 0;      // GCC target.
  double send_kbps = 0;        // Everything the sender put on the wire.
  double goodput_kbps = 0;     // Media + repair delivered to the receiver.
  double queue_ms = 0;         // Bottleneck queue delay.
  double rtt_ms = 0;
  double audio_buffer_ms = 0;  // Jitter buffer level.
  double audio_concealed_pct = 0;
  double video_fps = 0;        // Rendered frames in the window, as fps.
  double video_delay_ms = 0;   // Capture -> render of the last rendered frame.
  double fec_pct = 0;          // FEC protection factor.
};

struct CheckResult {
  Expectation expectation;
  double actual = 0;
  bool found = false;
  bool passed = false;
};

struct Report {
  Scenario scenario;
  std::map<std::string, double> metrics;  // User-experience metrics (see README).
  std::vector<CheckResult> checks;
  bool passed = true;
  std::vector<Sample> timeseries;
  std::map<std::string, double> library;  // Every module's own metrics.
  std::map<std::string, int> events;      // Event counts by module/name.
};

struct RunOptions {
  bool timeseries = true;
  bool library_metrics = true;
};

Report RunCall(const Scenario& s, const RunOptions& opt = {});

std::string ToJson(const Report& r, bool include_timeseries = true, bool include_library = false);
std::string ToText(const Report& r);

// Names and one-line meanings of every metric in Report::metrics.
const std::vector<std::pair<std::string, std::string>>& MetricCatalog();

}  // namespace sm::testbench
