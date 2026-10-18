// sm_testbench: runs call scenarios over emulated links and reports
// user-experience metrics.
//
//   sm_testbench scenarios/wifi_handover.scn          text report, exit 1 on failed expectations
//   sm_testbench --json=out.json a.scn b.scn          JSON (one object per scenario, in an array)
//   sm_testbench --timeseries --json=- a.scn          include 100 ms samples
//   sm_testbench --library a.scn                      include every module's own metrics
//   sm_testbench --check a.scn                        parse only
//   sm_testbench --link-trace=DIR a.scn               write DIR/<scenario>.<seed>.sm.trace.csv: every packet offered to the forward link
//   sm_testbench --metrics                            list metric names
//   echo "at 0s loss=5%" | sm_testbench -             scenario from stdin
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "call.h"

namespace {

bool ReadFile(const std::string& path, std::string* out) {
  if (path == "-") {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    *out = ss.str();
    return true;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

int Usage() {
  std::fprintf(stderr,
               "usage: sm_testbench [--json=<file|->] [--timeseries] [--library] [--check] [--quiet]\n"
               "                    [--link-trace=<dir>] <scenario.scn|->...\n"
               "       sm_testbench --metrics\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string json_path, trace_dir;
  bool timeseries = false, library = false, check_only = false, quiet = false;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--json=", 0) == 0) json_path = a.substr(7);
    else if (a.rfind("--link-trace=", 0) == 0) trace_dir = a.substr(13);
    else if (a == "--timeseries") timeseries = true;
    else if (a == "--library") library = true;
    else if (a == "--check") check_only = true;
    else if (a == "--quiet") quiet = true;
    else if (a == "--metrics") {
      for (const auto& [name, meaning] : sm::testbench::MetricCatalog()) std::printf("%-30s %s\n", name.c_str(), meaning.c_str());
      return 0;
    } else if (a == "--help" || a == "-h") return Usage();
    else if (a.size() > 1 && a[0] == '-' && a != "-") {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return Usage();
    } else files.push_back(a);
  }
  if (files.empty()) return Usage();

  int failed = 0;
  std::string json = "[\n";
  for (size_t i = 0; i < files.size(); ++i) {
    std::string text;
    if (!ReadFile(files[i], &text)) {
      std::fprintf(stderr, "%s: cannot read\n", files[i].c_str());
      return 2;
    }
    auto parsed = sm::testbench::ParseScenario(text);
    if (!parsed.scenario) {
      std::fprintf(stderr, "%s: %s\n", files[i].c_str(), parsed.error.c_str());
      return 2;
    }
    if (check_only) {
      std::printf("%s: ok (%s)\n", files[i].c_str(), parsed.scenario->name.c_str());
      continue;
    }
    sm::testbench::RunOptions opt;
    opt.timeseries = timeseries;
    opt.library_metrics = library;
    opt.link_trace = !trace_dir.empty();
    auto t0 = std::chrono::steady_clock::now();
    auto report = sm::testbench::RunCall(*parsed.scenario, opt);
    double wall_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!report.passed) ++failed;
    if (opt.link_trace) {
      // Every packet offered to the forward link, as CSV (see tools/link_trace.py).
      std::ofstream(trace_dir + "/" + report.scenario.name + "." + std::to_string(report.scenario.seed) + ".sm.trace.csv",
                    std::ios::binary)
          << sm::testbench::LinkTraceCsv(report.link_trace);
    }
    if (!quiet) {
      std::printf("%s", sm::testbench::ToText(report).c_str());
      std::printf("  (simulated %.0f s in %.0f ms)\n\n", parsed.scenario->duration.seconds(), wall_ms);
    } else {
      std::printf("%s %s\n", report.passed ? "PASS" : "FAIL", report.scenario.name.c_str());
    }
    std::string one = sm::testbench::ToJson(report, timeseries, library);
    json += (i ? ",\n" : "") + one.substr(0, one.size() - 1);
  }
  json += "\n]\n";
  if (!json_path.empty() && !check_only) {
    if (json_path == "-") {
      std::fwrite(json.data(), 1, json.size(), stdout);
    } else {
      std::ofstream f(json_path, std::ios::binary);
      f << json;
    }
  }
  return failed ? 1 : 0;
}
