#include <chrono>
#include <cstring>

#include "sm_test.h"

namespace sm::test {

namespace {
int g_failures_in_current = 0;
}

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> r;
  return r;
}

void ReportFailure(const char* file, int line, const std::string& msg) {
  ++g_failures_in_current;
  std::printf("  FAILED %s:%d: %s\n", file, line, msg.c_str());
}

bool CurrentFailed() { return g_failures_in_current > 0; }

}  // namespace sm::test

int main(int argc, char** argv) {
  using namespace sm::test;
  const char* filter = "";
  bool list = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--filter=", 9) == 0) filter = argv[i] + 9;
    if (std::strcmp(argv[i], "--list") == 0) list = true;
  }
  int run = 0, failed = 0;
  std::vector<std::string> failed_names;
  for (const TestCase& t : Registry()) {
    std::string full = std::string(t.suite) + "." + t.name;
    if (*filter && full.find(filter) == std::string::npos) continue;
    if (list) {
      std::printf("%s\n", full.c_str());
      continue;
    }
    std::printf("[ RUN  ] %s\n", full.c_str());
    std::fflush(stdout);
    g_failures_in_current = 0;
    auto start = std::chrono::steady_clock::now();
    t.fn();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ++run;
    if (g_failures_in_current) {
      ++failed;
      failed_names.push_back(full);
      std::printf("[ FAIL ] %s (%.1f ms)\n", full.c_str(), ms);
    } else {
      std::printf("[  OK  ] %s (%.1f ms)\n", full.c_str(), ms);
    }
  }
  if (list) return 0;
  std::printf("\n%d tests run, %d passed, %d failed\n", run, run - failed, failed);
  for (const auto& n : failed_names) std::printf("  failed: %s\n", n.c_str());
  if (run == 0) {
    std::printf("no tests matched filter '%s'\n", filter);
    return 1;
  }
  return failed ? 1 : 0;
}
