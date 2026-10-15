// sm_compare: runs test bench scenarios through this library and through
// libwebrtc over the same emulated links and prints the user-experience
// metrics side by side.
//
//   sm_compare [--seeds=N] [--json=FILE] [--dump=DIR] scenario.scn...
//
// --dump writes every run's full report, with its 100 ms timeseries, to
// DIR/<scenario>.<seed>.<sm|webrtc>.json (sm_testbench --json format).
//
// Each scenario runs with seeds seed, seed+1, ..., seed+N-1 (default N=3);
// the table shows the mean over seeds. The JSON has the means ("sm", "webrtc"),
// every seed's values for the shown metrics ("per_seed"), and each shown
// metric's direction and tie floor ("shown"), which tools/parity_report.py
// reads to find gaps in both directions.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "call.h"
#include "scenario.h"
#include "webrtc_call.h"

namespace {

using sm::testbench::Report;
using sm::testbench::Scenario;

enum class Better { kLower, kHigher };

struct Shown {
  const char* metric;
  Better better;
  double floor;  // Differences below this are a tie.
};

// The metrics a user notices, in the order the table shows them.
const std::vector<Shown>& ShownMetrics() {
  static const std::vector<Shown> k = {
      {"audio.concealed_pct", Better::kLower, 0.5},
      {"audio.interruptions", Better::kLower, 0.5},
      {"audio.longest_interruption_ms", Better::kLower, 20},
      {"audio.mouth_to_ear_p95_ms", Better::kLower, 10},
      {"audio.jb_delay_p95_ms", Better::kLower, 10},
      {"video.freeze_count", Better::kLower, 0.5},
      {"video.freeze_total_ms", Better::kLower, 100},
      {"video.longest_freeze_ms", Better::kLower, 100},
      {"video.render_fps", Better::kHigher, 0.5},
      {"video.e2e_delay_p95_ms", Better::kLower, 10},
      {"video.time_to_first_frame_ms", Better::kLower, 50},
      {"video.avg_kbps", Better::kHigher, 20},
      {"video.keyframe_requests", Better::kLower, 0.5},
      {"cc.utilization_pct", Better::kHigher, 2},
      {"cc.queue_p95_ms", Better::kLower, 10},
      {"cc.max_settle_ms", Better::kLower, 500},
      {"cc.rtt_p50_ms", Better::kLower, 10},
      {"net.loss_pct", Better::kLower, 0.5},
      {"repair.overhead_pct", Better::kLower, 2},
  };
  return k;
}

using Metrics = std::map<std::string, double>;

// Only the shown metrics of one run.
Metrics ShownOf(const Report& r) {
  Metrics m;
  for (const auto& sh : ShownMetrics()) {
    auto it = r.metrics.find(sh.metric);
    if (it != r.metrics.end()) m[sh.metric] = it->second;
  }
  return m;
}

Metrics MeanOver(const std::vector<Report>& runs) {
  Metrics sum;
  std::map<std::string, int> n;
  for (const auto& r : runs) {
    for (const auto& [k, v] : r.metrics) {
      sum[k] += v;
      ++n[k];
    }
  }
  for (auto& [k, v] : sum) v /= n[k];
  return sum;
}

// "sm", "webrtc" or "=".
const char* Winner(const Shown& sh, double a, double b) {
  double d = a - b;
  double tol = std::max(sh.floor, 0.05 * std::max(std::fabs(a), std::fabs(b)));
  if (std::fabs(d) <= tol) return "=";
  bool a_better = sh.better == Better::kLower ? d < 0 : d > 0;
  return a_better ? "sm" : "webrtc";
}

std::string Num(double v) {
  char buf[32];
  if (!std::isfinite(v)) return "-";
  if (std::fabs(v) >= 100 || v == std::floor(v)) std::snprintf(buf, sizeof(buf), "%.0f", v);
  else std::snprintf(buf, sizeof(buf), "%.1f", v);
  return buf;
}

std::string JsonMetrics(const Metrics& m) {
  std::string o = "{";
  bool first = true;
  for (const auto& [k, v] : m) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    o += std::string(first ? "" : ", ") + "\"" + k + "\": " + (std::isfinite(v) ? buf : "null");
    first = false;
  }
  return o + "}";
}

std::string JsonShown() {
  std::string o = "{";
  bool first = true;
  for (const auto& sh : ShownMetrics()) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "{\"better\": \"%s\", \"floor\": %g}", sh.better == Better::kLower ? "lower" : "higher", sh.floor);
    o += std::string(first ? "" : ", ") + "\"" + sh.metric + "\": " + buf;
    first = false;
  }
  return o + "}";
}

}  // namespace

int main(int argc, char** argv) {
  int seeds = 3;
  std::string json_path, dump_dir;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--seeds=", 0) == 0) seeds = std::max(1, std::atoi(a.c_str() + 8));
    else if (a.rfind("--json=", 0) == 0) json_path = a.substr(7);
    else if (a.rfind("--dump=", 0) == 0) dump_dir = a.substr(7);
    else if (a == "-h" || a == "--help") {
      std::printf("usage: sm_compare [--seeds=N] [--json=FILE] [--dump=DIR] scenario.scn...\n");
      return 0;
    } else files.push_back(a);
  }
  if (files.empty()) {
    std::fprintf(stderr, "usage: sm_compare [--seeds=N] [--json=FILE] [--dump=DIR] scenario.scn...\n");
    return 2;
  }

  std::printf("streaming_media vs libwebrtc %s, mean of %d seed(s)\n", sm::compare::WebrtcVersion().c_str(), seeds);
  std::string json = "{\n  \"webrtc_version\": \"" + sm::compare::WebrtcVersion() + "\",\n  \"seeds\": " +
                     std::to_string(seeds) + ",\n  \"shown\": " + JsonShown() + ",\n  \"scenarios\": [";
  std::map<std::string, int> wins;
  bool first_scn = true;
  for (const auto& f : files) {
    std::ifstream in(f);
    if (!in) {
      std::fprintf(stderr, "cannot read %s\n", f.c_str());
      return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    auto parsed = sm::testbench::ParseScenario(ss.str());
    if (!parsed.scenario) {
      std::fprintf(stderr, "%s: %s\n", f.c_str(), parsed.error.c_str());
      return 2;
    }
    const Scenario base = *parsed.scenario;
    std::vector<Report> ours, theirs;
    sm::testbench::RunOptions opt;
    opt.timeseries = !dump_dir.empty();
    opt.library_metrics = false;
    for (int k = 0; k < seeds; ++k) {
      Scenario s = base;
      s.seed = base.seed + static_cast<uint64_t>(k);
      ours.push_back(sm::testbench::RunCall(s, opt));
      theirs.push_back(sm::compare::RunWebrtcCall(s, opt));
      if (!dump_dir.empty()) {
        std::string stem = dump_dir + "/" + base.name + "." + std::to_string(s.seed);
        std::ofstream(stem + ".sm.json") << sm::testbench::ToJson(ours.back());
        std::ofstream(stem + ".webrtc.json") << sm::testbench::ToJson(theirs.back());
      }
    }
    Metrics a = MeanOver(ours), b = MeanOver(theirs);

    std::printf("\n%s - %s\n", base.name.c_str(), base.description.c_str());
    std::printf("  %-30s %12s %12s  %s\n", "metric", "sm", "webrtc", "better");
    for (const auto& sh : ShownMetrics()) {
      auto ia = a.find(sh.metric), ib = b.find(sh.metric);
      if (ia == a.end() || ib == b.end()) continue;
      const char* w = Winner(sh, ia->second, ib->second);
      ++wins[w];
      std::printf("  %-30s %12s %12s  %s\n", sh.metric, Num(ia->second).c_str(), Num(ib->second).c_str(), w);
    }
    std::string per_seed;
    for (size_t k = 0; k < ours.size(); ++k) {
      per_seed += std::string(k ? ", " : "") + "{\"seed\": " + std::to_string(base.seed + k) + ", \"sm\": " +
                  JsonMetrics(ShownOf(ours[k])) + ", \"webrtc\": " + JsonMetrics(ShownOf(theirs[k])) + "}";
    }
    json += std::string(first_scn ? "\n" : ",\n") + "    {\"scenario\": \"" + base.name + "\", \"sm\": " +
            JsonMetrics(a) + ", \"webrtc\": " + JsonMetrics(b) + ",\n     \"per_seed\": [" + per_seed + "]}";
    first_scn = false;
  }
  std::printf("\nbetter: sm %d, webrtc %d, tie %d (of the metrics shown)\n", wins["sm"], wins["webrtc"], wins["="]);
  std::printf("\nMetrics measured differently for libwebrtc:\n");
  for (const auto& [k, v] : sm::compare::WebrtcMetricNotes()) std::printf("  %s: %s\n", k.c_str(), v.c_str());
  json += "\n  ]\n}\n";
  if (!json_path.empty()) {
    std::ofstream out(json_path);
    out << json;
  }
  return 0;
}
