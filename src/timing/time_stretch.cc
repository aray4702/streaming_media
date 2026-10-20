#include "sm/timing/time_stretch.h"

#include <algorithm>

#include "sm/core/dsp.h"

namespace sm::timing {

namespace {
constexpr size_t kStartMs = 0;  // Segment pair starts at the block start.
}

TimeStretcher::TimeStretcher(TimeStretchConfig cfg) : cfg_(cfg) {}

int TimeStretcher::FindPeriod(std::span<const float> in, size_t start, float* corr) const {
  int min_l = cfg_.sample_rate / cfg_.min_period_hz;
  int max_l = std::min<int>(cfg_.sample_rate / cfg_.max_period_hz, static_cast<int>((in.size() - start) / 2));
  int best = min_l;
  float best_c = -2;
  // Coarse search on every other lag, then refine around the peak.
  for (int l = min_l; l <= max_l; l += 2) {
    float c = dsp::NormalizedCorrelation(in.data() + start, in.data() + start + l, static_cast<size_t>(l));
    if (c > best_c) {
      best_c = c;
      best = l;
    }
  }
  for (int l = std::max(min_l, best - 1); l <= std::min(max_l, best + 1); ++l) {
    float c = dsp::NormalizedCorrelation(in.data() + start, in.data() + start + l, static_cast<size_t>(l));
    if (c > best_c) {
      best_c = c;
      best = l;
    }
  }
  *corr = best_c;
  return best;
}

size_t TimeStretcher::Accelerate(std::span<const float> in, std::vector<float>& out) {
  out.assign(in.begin(), in.end());
  if (in.size() < min_input()) return 0;
  float corr = 0;
  const size_t s = kStartMs;
  int l = FindPeriod(in, s, &corr);
  bool silent = dsp::RmsDbfs(in) < cfg_.silence_dbfs;
  if (!silent && corr < cfg_.correlation_threshold) {
    ++stats_.accelerate_rejected;
    return 0;
  }
  const size_t L = static_cast<size_t>(l);
  out.clear();
  out.insert(out.end(), in.begin(), in.begin() + static_cast<std::ptrdiff_t>(s));
  for (size_t i = 0; i < L; ++i) {
    float w = static_cast<float>(i) / static_cast<float>(L);
    out.push_back((1 - w) * in[s + i] + w * in[s + L + i]);
  }
  out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(s + 2 * L), in.end());
  ++stats_.accelerate_ok;
  stats_.samples_removed += L;
  return L;
}

size_t TimeStretcher::PreemptiveExpand(std::span<const float> in, std::vector<float>& out, size_t old_samples) {
  out.assign(in.begin(), in.end());
  if (in.size() < min_input()) return 0;
  float corr = 0;
  const size_t s = kStartMs + old_samples;
  int l = FindPeriod(in, s, &corr);
  bool silent = dsp::RmsDbfs(in) < cfg_.silence_dbfs;
  if (!silent && corr < cfg_.correlation_threshold) {
    ++stats_.expand_rejected;
    return 0;
  }
  const size_t L = static_cast<size_t>(l);
  out.clear();
  out.insert(out.end(), in.begin(), in.begin() + static_cast<std::ptrdiff_t>(s + L));
  for (size_t i = 0; i < L; ++i) {
    float w = static_cast<float>(i) / static_cast<float>(L);
    out.push_back((1 - w) * in[s + L + i] + w * in[s + i]);
  }
  out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(s + L), in.end());
  ++stats_.expand_ok;
  stats_.samples_inserted += L;
  return L;
}

void TimeStretcher::ExportMetrics(MetricsWriter& w) const {
  w.Counter("accelerate_ok", stats_.accelerate_ok);
  w.Counter("accelerate_rejected", stats_.accelerate_rejected);
  w.Counter("expand_ok", stats_.expand_ok);
  w.Counter("expand_rejected", stats_.expand_rejected);
  w.Counter("samples_removed", stats_.samples_removed);
  w.Counter("samples_inserted", stats_.samples_inserted);
}

}  // namespace sm::timing
