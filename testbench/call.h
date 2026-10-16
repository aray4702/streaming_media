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

// Steps shared with other call implementations (compare/), so every
// implementation reports the same metrics computed the same way.
// Adds cc.target_avg_kbps, cc.goodput_kbps, cc.utilization_pct,
// cc.max_settle_ms and cc.unsettled from rep.timeseries. `demand_kbps` is
// the media's maximum wire rate (what the call would send on an unlimited link).
void AddCcMetrics(const Scenario& s, double demand_kbps, Report& rep);
// Also adds cc.target_max_drop_pct and cc.send_over_target_p95_pct.
// Checks rep.scenario's expectations against rep.metrics.
void EvaluateExpectations(Report& rep);

// Times of one video frame on its way from capture to render (issue #60).
// An unset time is infinite or minus infinity and the frame is not counted.
struct VideoFrameTimes {
  Timestamp capture = Timestamp::MinusInfinity();
  Timestamp submitted = Timestamp::MinusInfinity();   // Handed to the sender (after encoding).
  Timestamp last_sent = Timestamp::MinusInfinity();   // Last media packet left the pacer.
  Timestamp complete = Timestamp::MinusInfinity();    // Last media or RTX packet of the frame arrived.
  Timestamp rendered = Timestamp::MinusInfinity();
};

// Adds video.delay_encode_ms (mean), video.delay_pacer_p95_ms,
// video.delay_network_p95_ms and video.delay_jitter_buffer_p95_ms from the
// frames that were rendered. Adds nothing when no frame was.
void AddVideoDelayMetrics(const std::vector<VideoFrameTimes>& frames, Report& rep);

std::string ToJson(const Report& r, bool include_timeseries = true, bool include_library = false);
std::string ToText(const Report& r);

// Names and one-line meanings of every metric in Report::metrics.
const std::vector<std::pair<std::string, std::string>>& MetricCatalog();

}  // namespace sm::testbench
