// sm_compare: runs test bench scenarios through this library and through
// libwebrtc over the same emulated links and prints the user-experience
// metrics side by side.
//
//   sm_compare [--seeds=N] [--json=FILE] [--dump=DIR] [--trace=NAME,...] [--trace-dir=DIR] scenario.scn...
//
// --dump writes every run's full report, with its 100 ms timeseries, to
// DIR/<scenario>.<seed>.<sm|webrtc>.json (sm_testbench --json format).
//
// Each scenario runs with seeds seed, seed+1, ..., seed+N-1 (default N=3, or the
// scenario's own `compare_seeds N` directive);
// the table shows the mean over seeds. The JSON has the means ("sm", "webrtc"),
// every seed's values for the shown metrics ("per_seed"), and each shown
// metric's direction and tie floor ("shown"), which tools/parity_report.py
// reads to find gaps in both directions.
//
// --trace=NAME,... records every packet offered to the forward link, for this
// library and for libwebrtc, on the first seed of the named scenarios (issue
// #125; default very_low_capacity_allocator_v2, issue_124_clean_link_v2,
// issue_124_bufferbloat_v2 and issue_124_burst_loss_v2, "none" disables). The CSVs go
// into the JSON's "traces" and, with --trace-dir=DIR, to DIR. Summarise or
// diff them with tools/link_trace.py. The same runs also write the sender's
// per-second video decisions (issue #131; "sm_video", "webrtc_video") and the
// GoogCC estimates per update (issue #149; "sm_bwe", "webrtc_bwe").
// tools/bwe_trace.py lays the two side by side.
//
// --trace-all-dir=DIR traces the first seed of every scenario the same way and
// writes only the CSV files, <scenario>.<seed>.{sm,webrtc}.{trace,video,bwe}.csv,
// to DIR; the JSON keeps only the --trace scenarios. CI runs it on main and
// publishes DIR as traces/ on the parity-data branch, so libwebrtc's trace of
// any scenario is there without a PR that adds it to --trace.
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
      {"video.pause_count", Better::kLower, 0.5},
      {"video.pause_total_ms", Better::kLower, 100},
      {"video.render_fps", Better::kHigher, 0.5},
      {"video.e2e_delay_p95_ms", Better::kLower, 10},
      {"video.time_to_first_frame_ms", Better::kLower, 50},
      {"video.avg_kbps", Better::kHigher, 20},
      {"video.keyframe_requests", Better::kLower, 0.5},
      {"sync.av_offset_p95_ms", Better::kLower, 10},  // Issue #63.
      {"cc.utilization_pct", Better::kHigher, 2},
      {"cc.queue_p95_ms", Better::kLower, 10},
      {"cc.max_settle_ms", Better::kLower, 500},
      {"cc.rtt_p50_ms", Better::kLower, 10},
      {"net.loss_pct", Better::kLower, 0.5},
      {"repair.overhead_pct", Better::kLower, 2},
  };
  return k;
}

// Information only (issue #117): what the forward link carried and lost per
// kind. Shown after the table, never counted as better or worse and not part
// of the parity gaps.
const std::vector<const char*>& InfoMetrics() {
  static const std::vector<const char*> k = {
      "net.audio_kbps",  "net.video_kbps",  "net.rtx_kbps",       "net.padding_kbps",    "net.fec_kbps",
      "net.audio_loss_pct", "net.video_loss_pct", "net.rtx_loss_pct", "net.padding_loss_pct",
      // Issue #119: packet sizes offered to the link, and queue drops, per kind.
      "net.audio_size_p50_b", "net.audio_size_p90_b", "net.audio_size_max_b", "net.audio_queue_drops",
      "net.video_size_p50_b", "net.video_size_p90_b", "net.video_size_max_b", "net.video_queue_drops",
      "net.rtx_size_p50_b", "net.rtx_size_p90_b", "net.rtx_size_max_b", "net.rtx_queue_drops",
      "net.padding_size_p50_b", "net.padding_size_p90_b", "net.padding_size_max_b", "net.padding_queue_drops",
  };
  return k;
}

// Scenarios whose first seed is traced packet by packet on the forward link
// (issue #125), for both stacks, when --trace is not given. The traces go into
// the JSON ("traces"), which CI publishes; tools/link_trace.py reads them.
const char* const kDefaultTraced =
    "very_low_capacity,very_low_capacity_allocator_v2,issue_124_clean_link_v2,issue_124_bufferbloat_v2,"
    "issue_124_burst_loss_v2,issue_61_googcc_glue_congestive,probe_v2_cross_traffic,high_jitter,issue_453_high_jitter_seed7";

std::string JsonString(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (c == '\n') o += "\\n";
    else o += c;
  }
  return o + "\"";
}

bool Traced(const std::string& names, const std::string& scenario) {
  size_t pos = 0;
  while (pos <= names.size()) {
    size_t end = names.find(',', pos);
    if (end == std::string::npos) end = names.size();
    if (names.compare(pos, end - pos, scenario) == 0 && end - pos == scenario.size()) return true;
    pos = end + 1;
  }
  return false;
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
  std::string json_path, dump_dir, trace_names = kDefaultTraced, trace_dir, trace_all_dir, traces_json;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--seeds=", 0) == 0) seeds = std::max(1, std::atoi(a.c_str() + 8));
    else if (a.rfind("--json=", 0) == 0) json_path = a.substr(7);
    else if (a.rfind("--dump=", 0) == 0) dump_dir = a.substr(7);
    else if (a.rfind("--trace=", 0) == 0) trace_names = a.substr(8);
    else if (a.rfind("--trace-dir=", 0) == 0) trace_dir = a.substr(12);
    else if (a.rfind("--trace-all-dir=", 0) == 0) trace_all_dir = a.substr(16);
    else if (a == "-h" || a == "--help") {
      std::printf("usage: sm_compare [--seeds=N] [--json=FILE] [--dump=DIR] [--trace=NAME,...] [--trace-dir=DIR] scenario.scn...\n"
                  "  --trace   scenarios whose first seed is traced packet by packet (default %s; none to disable)\n"
                  "  --trace-dir  also write <scenario>.<seed>.{sm,webrtc}.trace.csv there\n"
                  "  --trace-all-dir  trace the first seed of every scenario, CSV files to this directory only\n",
                  kDefaultTraced);
      return 0;
    } else files.push_back(a);
  }
  if (files.empty()) {
    std::fprintf(stderr, "usage: sm_compare [--seeds=N] [--json=FILE] [--dump=DIR] [--trace=NAME,...] [--trace-dir=DIR] scenario.scn...\n");
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
    const int scn_seeds = base.compare_seeds > 0 ? base.compare_seeds : seeds;  // `compare_seeds N` in the .scn
    for (int k = 0; k < scn_seeds; ++k) {
      Scenario s = base;
      s.seed = base.seed + static_cast<uint64_t>(k);
      const bool named = k == 0 && Traced(trace_names, base.name);
      opt.link_trace = named || (k == 0 && !trace_all_dir.empty());
      opt.video_trace = opt.link_trace;
      opt.bwe_trace = opt.link_trace;
      opt.audio_trace = opt.link_trace;
      ours.push_back(sm::testbench::RunCall(s, opt));
      theirs.push_back(sm::compare::RunWebrtcCall(s, opt));
      if (opt.link_trace) {
        const std::string sm_csv = sm::testbench::LinkTraceCsv(ours.back().link_trace);
        const std::string rtc_csv = sm::testbench::LinkTraceCsv(theirs.back().link_trace);
        // The per-second video send decisions (issue #131), same scenario and seed.
        const std::string sm_video = sm::testbench::VideoTraceCsv(ours.back().video_trace);
        const std::string rtc_video = sm::testbench::VideoTraceCsv(theirs.back().video_trace);
        // The GoogCC estimates per update (issue #149), same scenario and seed.
        const std::string sm_bwe = sm::testbench::BweTraceCsv(ours.back().bwe_trace);
        const std::string rtc_bwe = sm::testbench::BweTraceCsv(theirs.back().bwe_trace);
        // The jitter buffer per 10 ms audio pull (issue #62), same scenario and seed.
        const std::string sm_audio = sm::testbench::AudioTraceCsv(ours.back().audio_trace);
        const std::string rtc_audio = sm::testbench::AudioTraceCsv(theirs.back().audio_trace);
        if (named) {
          traces_json += std::string(traces_json.empty() ? "\n" : ",\n") + "    {\"scenario\": \"" + base.name +
                         "\", \"seed\": " + std::to_string(s.seed) + ", \"sm\": " + JsonString(sm_csv) +
                         ", \"webrtc\": " + JsonString(rtc_csv) + ", \"sm_video\": " + JsonString(sm_video) +
                         ", \"webrtc_video\": " + JsonString(rtc_video) + ", \"sm_bwe\": " + JsonString(sm_bwe) +
                         ", \"webrtc_bwe\": " + JsonString(rtc_bwe) + ", \"sm_audio\": " + JsonString(sm_audio) +
                         ", \"webrtc_audio\": " + JsonString(rtc_audio) + "}";
        }
        for (const std::string& dir : {named ? trace_dir : std::string(), trace_all_dir}) {
          if (dir.empty()) continue;
          const std::string stem = dir + "/" + base.name + "." + std::to_string(s.seed);
          std::ofstream(stem + ".sm.trace.csv") << sm_csv;
          std::ofstream(stem + ".webrtc.trace.csv") << rtc_csv;
          std::ofstream(stem + ".sm.video.csv") << sm_video;
          std::ofstream(stem + ".webrtc.video.csv") << rtc_video;
          std::ofstream(stem + ".sm.bwe.csv") << sm_bwe;
          std::ofstream(stem + ".webrtc.bwe.csv") << rtc_bwe;
          std::ofstream(stem + ".sm.audio.csv") << sm_audio;
          std::ofstream(stem + ".webrtc.audio.csv") << rtc_audio;
        }
      }
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
    std::printf("  forward link per kind (information only)\n");
    for (const char* name : InfoMetrics()) {
      auto ia = a.find(name), ib = b.find(name);
      if (ia == a.end() || ib == b.end()) continue;
      std::printf("  %-30s %12s %12s\n", name, Num(ia->second).c_str(), Num(ib->second).c_str());
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
  json += "\n  ]";
  if (!traces_json.empty()) json += ",\n  \"traces\": [" + traces_json + "\n  ]";
  json += "\n}\n";
  if (!json_path.empty()) {
    std::ofstream out(json_path);
    out << json;
  }
  return 0;
}
