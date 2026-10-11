// Minimal benchmark harness with pass/fail criteria.
//
// Two kinds of benchmark:
//   SM_BENCH(Name)          - throughput: the body loops `for (auto _ : st)`;
//                             the harness grows the iteration count until the
//                             loop runs for --min-time, then reports ns/item.
//   SM_BENCH_QUALITY(Name)  - runs once; typically a network simulation that
//                             reports quality metrics (recovery rate, SNR...).
//
// Both can declare requirements; a benchmark "passes" only if every
// requirement holds. Speed budgets are multiplied by --budget-scale (or the
// SM_BENCH_BUDGET_SCALE environment variable) so the same suite can gate a
// desktop (1x), a phone (3-4x), a wearable or Wasm (5-10x).
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace sm::bench {

struct Requirement {
  std::string name;
  double value;
  std::string op;
  double threshold;
  bool pass;
};

class State {
 public:
  explicit State(int64_t iterations, double budget_scale)
      : iterations_(iterations), budget_scale_(budget_scale) {}

  struct Iterator {
    State* s;
    int64_t left;
    bool operator!=(const Iterator&) {
      if (left > 0) return true;
      s->StopTimer();
      return false;
    }
    void operator++() { --left; }
    int operator*() const { return 0; }
  };
  Iterator begin() {
    StartTimer();
    return Iterator{this, iterations_};
  }
  Iterator end() { return Iterator{this, 0}; }

  int64_t iterations() const { return iterations_; }
  // How many logical items (packets, samples, frames) one iteration processes.
  void SetItemsPerIteration(double n) { items_per_iter_ = n; }
  void PauseTiming() { StopTimer(); }
  void ResumeTiming() { StartTimer(); }

  // Reported metric (quality or auxiliary).
  void Counter(const std::string& name, double value) { counters_.emplace_back(name, value); }
  // Requirement on a reported metric; op is one of ">=", "<=", ">", "<".
  void Require(const std::string& name, double value, const std::string& op, double threshold);
  // Speed budget on ns per item, scaled by the budget scale.
  void BudgetNsPerItem(double ns) { budget_ns_ = ns; }
  // Real-time factor budget for media processing: processing must be at
  // least `factor` times faster than real time (after scaling).
  void RequireRealtimeFactor(double media_seconds_per_iteration, double factor) {
    media_seconds_per_iter_ = media_seconds_per_iteration;
    min_rt_factor_ = factor;
  }

  // Internal.
  void SetElapsedNs(double ns) { elapsed_ns_ = ns; }
  double elapsed_ns() const { return elapsed_ns_; }
  double items_per_iter() const { return items_per_iter_; }
  double budget_ns() const { return budget_ns_; }
  double budget_scale() const { return budget_scale_; }
  double media_seconds_per_iter() const { return media_seconds_per_iter_; }
  double min_rt_factor() const { return min_rt_factor_; }
  const std::vector<std::pair<std::string, double>>& counters() const { return counters_; }
  const std::vector<Requirement>& requirements() const { return reqs_; }

 private:
  void StartTimer() {
    if (!running_) {
      running_ = true;
      start_ = std::chrono::steady_clock::now();
    }
  }
  void StopTimer() {
    if (running_) {
      running_ = false;
      elapsed_ns_ += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start_).count();
    }
  }
  int64_t iterations_;
  double budget_scale_;
  double items_per_iter_ = 1;
  double budget_ns_ = 0;
  double media_seconds_per_iter_ = 0;
  double min_rt_factor_ = 0;
  bool running_ = false;
  double elapsed_ns_ = 0;
  std::chrono::steady_clock::time_point start_;
  std::vector<std::pair<std::string, double>> counters_;
  std::vector<Requirement> reqs_;
};

struct BenchCase {
  const char* name;
  void (*fn)(State&);
  bool quality;
};
std::vector<BenchCase>& Registry();
struct Registrar {
  Registrar(const char* name, void (*fn)(State&), bool quality) { Registry().push_back({name, fn, quality}); }
};

// Prevents the optimizer from discarding a computed value.
template <class T>
inline void DoNotOptimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "r,m"(value) : "memory");
#else
  static volatile const T* sink;
  sink = &value;
#endif
}

}  // namespace sm::bench

// Registered as "section/Name" so --filter=section/ selects a section.
#define SM_BENCH_IMPL(section, name, quality)                                       \
  static void section##_##name(::sm::bench::State&);                                \
  static ::sm::bench::Registrar section##_##name##_reg(#section "/" #name,          \
                                                       &section##_##name, quality); \
  static void section##_##name(::sm::bench::State& st)

#define SM_BENCH(section, name) SM_BENCH_IMPL(section, name, false)
#define SM_BENCH_QUALITY(section, name) SM_BENCH_IMPL(section, name, true)
