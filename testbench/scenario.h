// Test bench scenarios: a call configuration plus a timeline of link
// impairments and the user-experience expectations the call must meet.
//
// Scenarios are plain text so they can be pasted into bug reports:
//
//   name     wifi-handover
//   describe Wi-Fi to LTE handover: 2 s outage, then a slower, lossier link
//   duration 40s
//   seed     7
//   # forward (sender -> receiver) link; every key is optional
//   at 0s    capacity=2500kbps delay=25ms jitter=4ms loss=0.5% queue=200ms
//   at 15s   down
//   at 17s   up capacity=800kbps delay=60ms loss=2%
//   # reverse (feedback) link
//   reverse at 0s delay=25ms
//   expect   audio.concealed_pct < 5
//   expect   video.longest_freeze_ms <= 2500
//
// See testbench/README.md for every directive and link key.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"

namespace sm::testbench {

// One change to a link at `at`. Unset fields keep their previous value.
struct LinkChange {
  TimeDelta at = TimeDelta::Zero();
  std::optional<DataRate> capacity;     // PlusInfinity() = unlimited.
  std::optional<TimeDelta> delay;
  std::optional<TimeDelta> jitter;
  std::optional<double> loss;           // Random (Bernoulli) loss, 0..1.
  std::optional<double> burst_len;      // Mean loss burst length in packets (>1 = Gilbert-Elliott).
  std::optional<double> duplicate;
  std::optional<bool> reorder;
  std::optional<TimeDelta> queue;       // Bottleneck buffer, as time at capacity. Zero = unlimited.
  std::optional<DataRate> cross;        // Competing constant-rate traffic on the bottleneck.
  std::optional<bool> down;
};

enum class CompareOp { kLt, kLe, kGt, kGe, kEq };

struct Expectation {
  std::string metric;
  CompareOp op = CompareOp::kLt;
  double value = 0;
  std::string text;  // As written, for reports.
};

struct Scenario {
  std::string name = "unnamed";
  std::string description;
  TimeDelta duration = TimeDelta::Seconds(30);
  uint64_t seed = 1;

  // Media.
  bool audio = true;
  bool video = true;
  bool red = true;                  // Audio redundancy (RFC 2198).
  bool nack = true;                 // Video NACK/RTX.
  bool fec = true;                  // Adaptive video FEC (protection controller).
  int audio_sample_rate = 16000;
  DataRate audio_bitrate = DataRate::KilobitsPerSec(32);
  int video_fps = 30;
  DataRate video_max = DataRate::KilobitsPerSec(2500);
  DataRate video_min = DataRate::KilobitsPerSec(50);
  TimeDelta keyframe_interval = TimeDelta::Zero();  // Zero = only on request.
  bool bitrate_adjuster = true;  // `bitrate_adjuster on|off`: libwebrtc's EncoderBitrateAdjuster (default on as in M157, issue #5).
  bool keyframe_v2 = false;  // `keyframe v2`: libwebrtc's receiver keyframe-request policy (issue #23; default v1).
  bool pacer_v2 = false;  // `pacer v2`: libwebrtc's PacingController (default v1, the library's own pacer).

  // Congestion control.
  DataRate start_rate = DataRate::KilobitsPerSec(300);
  DataRate min_rate = DataRate::KilobitsPerSec(30);
  DataRate max_rate = DataRate::KilobitsPerSec(5000);

  std::vector<LinkChange> forward;
  std::vector<LinkChange> reverse;
  std::vector<Expectation> expectations;
};

struct ParseResult {
  std::optional<Scenario> scenario;
  std::string error;  // "line N: ..." when scenario is empty.
};

ParseResult ParseScenario(const std::string& text);

// Canonical text form (round-trips through ParseScenario).
std::string FormatScenario(const Scenario& s);

const char* ToString(CompareOp op);
bool Evaluate(CompareOp op, double lhs, double rhs);

}  // namespace sm::testbench
