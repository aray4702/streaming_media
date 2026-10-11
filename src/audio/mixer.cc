#include "sm/audio/mixer.h"

#include <algorithm>
#include <functional>

#include "sm/core/dsp.h"

namespace sm::audio {

namespace {
LimiterConfig WithRate(LimiterConfig c, int rate) {
  c.sample_rate = rate;
  return c;
}
}  // namespace

AudioMixer::AudioMixer(MixerConfig cfg) : cfg_(cfg), limiter_(WithRate(cfg.limiter, cfg.sample_rate)) {}

void AudioMixer::Mix(const std::map<uint32_t, std::span<const float>>& sources, std::span<float> out) {
  ++frames_;
  for (const auto& [id, frame] : sources) {
    double e = dsp::Energy(frame) / static_cast<double>(std::max<size_t>(1, frame.size()));
    double& s = energy_[id];
    s += cfg_.energy_smoothing * (e - s);
  }
  // Forget sources that disappeared.
  std::erase_if(mixed_, [&](uint32_t id) { return !sources.count(id); });
  for (auto it = energy_.begin(); it != energy_.end();) {
    it = sources.count(it->first) ? std::next(it) : energy_.erase(it);
  }
  // Rank and pick, with hysteresis for sources already in the mix.
  std::vector<std::pair<double, uint32_t>> ranked;
  for (const auto& [id, e] : energy_) {
    bool in = std::find(mixed_.begin(), mixed_.end(), id) != mixed_.end();
    ranked.emplace_back(in ? e * cfg_.hysteresis : e, id);
  }
  std::sort(ranked.begin(), ranked.end(), std::greater<>());
  std::vector<uint32_t> next;
  for (size_t i = 0; i < ranked.size() && next.size() < cfg_.max_mixed; ++i) {
    if (ranked[i].first > 1e-9) next.push_back(ranked[i].second);
  }
  for (uint32_t id : next) {
    if (std::find(mixed_.begin(), mixed_.end(), id) == mixed_.end()) ++swaps_;
  }
  mixed_ = next;

  std::fill(out.begin(), out.end(), 0.0f);
  for (const auto& [id, frame] : sources) {
    bool in = std::find(mixed_.begin(), mixed_.end(), id) != mixed_.end();
    float& g = gain_[id];
    float target = in ? 1.0f : 0.0f;
    if (g == 0.0f && target == 0.0f) continue;
    const size_t n = std::min(out.size(), frame.size());
    for (size_t i = 0; i < n; ++i) {
      float w = static_cast<float>(i + 1) / static_cast<float>(n);
      out[i] += frame[i] * (g + (target - g) * w);  // Ramp over one frame.
    }
    g = target;
  }
  limiter_.Process(out);
}

void AudioMixer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", frames_);
  w.Counter("swaps", swaps_);
  w.Gauge("mixed_sources", static_cast<double>(mixed_.size()));
  w.Gauge("limiter_gain_db", limiter_.current_gain_db());
}

}  // namespace sm::audio
