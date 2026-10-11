#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sm_bench.h"

namespace sm::bench {

std::vector<BenchCase>& Registry() {
  static std::vector<BenchCase> r;
  return r;
}

void State::Require(const std::string& name, double value, const std::string& op, double threshold) {
  bool pass = false;
  if (op == ">=") pass = value >= threshold;
  else if (op == "<=") pass = value <= threshold;
  else if (op == ">") pass = value > threshold;
  else if (op == "<") pass = value < threshold;
  reqs_.push_back({name, value, op, threshold, pass});
}

}  // namespace sm::bench

namespace {

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace sm::bench;
  const char* filter = "";
  const char* json_path = nullptr;
  double min_time = 0.25;
  double scale = 1.0;
  if (const char* env = std::getenv("SM_BENCH_BUDGET_SCALE")) scale = std::atof(env);
  for (int i = 1; i < argc; ++i) {
    if (!std::strncmp(argv[i], "--filter=", 9)) filter = argv[i] + 9;
    else if (!std::strncmp(argv[i], "--json=", 7)) json_path = argv[i] + 7;
    else if (!std::strncmp(argv[i], "--min-time=", 11)) min_time = std::atof(argv[i] + 11);
    else if (!std::strncmp(argv[i], "--budget-scale=", 15)) scale = std::atof(argv[i] + 15);
    else if (!std::strcmp(argv[i], "--list")) {
      for (const auto& b : Registry()) std::printf("%s\n", b.name);
      return 0;
    }
  }

  std::string json = "{\"budget_scale\":" + std::to_string(scale) + ",\"benchmarks\":[";
  int run = 0, failed = 0;
  std::printf("%-44s %12s %14s  %s\n", "benchmark", "ns/item", "items/s", "result");
  for (const BenchCase& b : Registry()) {
    if (*filter && std::string(b.name).find(filter) == std::string::npos) continue;
    int64_t iters = 1;
    State* final_state = nullptr;
    State st(iters, scale);
    for (;;) {
      st = State(iters, scale);
      auto t0 = std::chrono::steady_clock::now();
      b.fn(st);
      if (b.quality) {
        // Quality benchmarks run once; report the whole run's wall time.
        st.SetElapsedNs(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
      }
      if (b.quality || st.elapsed_ns() >= min_time * 1e9 || iters >= (int64_t{1} << 30)) break;
      double per = st.elapsed_ns() / static_cast<double>(iters);
      int64_t next = per > 0 ? static_cast<int64_t>(min_time * 1e9 / per * 1.2) : iters * 10;
      iters = std::max(iters * 2, std::min(next, iters * 100));
    }
    final_state = &st;
    const State& s = *final_state;
    double items = static_cast<double>(s.iterations()) * s.items_per_iter();
    double ns_per_item = items > 0 ? s.elapsed_ns() / items : 0;
    double items_per_s = ns_per_item > 0 ? 1e9 / ns_per_item : 0;

    std::vector<Requirement> reqs = s.requirements();
    if (s.budget_ns() > 0) {
      double limit = s.budget_ns() * scale;
      reqs.push_back({"ns_per_item", ns_per_item, "<=", limit, ns_per_item <= limit});
    }
    double rt_factor = 0;
    if (s.media_seconds_per_iter() > 0) {
      double secs = s.elapsed_ns() / 1e9 / static_cast<double>(s.iterations());
      rt_factor = secs > 0 ? s.media_seconds_per_iter() / secs : 0;
      double limit = s.min_rt_factor() / scale;
      reqs.push_back({"realtime_factor", rt_factor, ">=", limit, rt_factor >= limit});
    }
    bool pass = true;
    for (const auto& r : reqs) pass = pass && r.pass;
    ++run;
    if (!pass) ++failed;

    if (b.quality) {
      std::printf("%-44s %12s %11.1f ms  %s\n", b.name, "quality", s.elapsed_ns() / 1e6, pass ? "PASS" : "FAIL");
    } else {
      std::printf("%-44s %12.1f %14.0f  %s\n", b.name, ns_per_item, items_per_s, pass ? "PASS" : "FAIL");
    }
    for (const auto& [k, v] : s.counters()) std::printf("    %-40s %g\n", k.c_str(), v);
    if (rt_factor > 0) std::printf("    %-40s %.1fx\n", "realtime_factor", rt_factor);
    for (const auto& r : reqs) {
      std::printf("    require %-32s %g %s %g  %s\n", r.name.c_str(), r.value, r.op.c_str(), r.threshold,
                  r.pass ? "ok" : "VIOLATED");
    }

    if (run > 1) json += ",";
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\"name\":\"%s\",\"quality\":%s,\"iterations\":%lld,\"ns_per_item\":%.3f,"
                  "\"items_per_second\":%.1f,\"realtime_factor\":%.2f,\"pass\":%s,\"counters\":{",
                  b.name, b.quality ? "true" : "false", static_cast<long long>(s.iterations()), ns_per_item,
                  items_per_s, rt_factor, pass ? "true" : "false");
    json += buf;
    bool first = true;
    for (const auto& [k, v] : s.counters()) {
      std::snprintf(buf, sizeof(buf), "%s\"%s\":%.6g", first ? "" : ",", JsonEscape(k).c_str(), v);
      json += buf;
      first = false;
    }
    json += "},\"requirements\":[";
    first = true;
    for (const auto& r : reqs) {
      std::snprintf(buf, sizeof(buf), "%s{\"name\":\"%s\",\"value\":%.6g,\"op\":\"%s\",\"threshold\":%.6g,\"pass\":%s}",
                    first ? "" : ",", JsonEscape(r.name).c_str(), r.value, r.op.c_str(), r.threshold,
                    r.pass ? "true" : "false");
      json += buf;
      first = false;
    }
    json += "]}";
  }
  json += "]}\n";
  std::printf("\n%d benchmarks run, %d passed, %d failed\n", run, run - failed, failed);
  if (json_path) {
    if (FILE* f = std::fopen(json_path, "w")) {
      std::fputs(json.c_str(), f);
      std::fclose(f);
    }
  }
  if (run == 0) return 1;
  return failed ? 1 : 0;
}
