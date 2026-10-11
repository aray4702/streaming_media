#include "sm/audio/comfort_noise.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "sm/core/dsp.h"

namespace sm::audio {

namespace {
// Reflection coefficients -> direct-form LPC polynomial a[0..p] (a[0] = 1).
std::vector<double> ReflectionToLpc(const std::vector<float>& k) {
  std::vector<double> a{1.0};
  for (float ki : k) {
    std::vector<double> next(a.size() + 1, 0.0);
    for (size_t j = 0; j < a.size(); ++j) next[j] = a[j];
    for (size_t j = 1; j < next.size(); ++j) next[j] += ki * a[next.size() - 1 - j];
    a = next;
  }
  return a;
}
float LevelToRms(uint8_t level) { return dsp::DbToLinear(-static_cast<float>(level)); }
}  // namespace

std::vector<uint8_t> EncodeSid(const SidFrame& sid) {
  std::vector<uint8_t> out{static_cast<uint8_t>(std::min<int>(sid.level, 127))};
  for (float k : sid.reflection) {
    // RFC 3389: uniform 8-bit quantization of (-1, 1); 0 -> -1, 254 -> +1.
    int q = static_cast<int>(std::lround((std::clamp(k, -0.999f, 0.999f) + 1.0f) * 127.0f));
    out.push_back(static_cast<uint8_t>(std::clamp(q, 0, 254)));
  }
  return out;
}

std::optional<SidFrame> DecodeSid(std::span<const uint8_t> p) {
  if (p.empty() || p[0] > 127) return std::nullopt;
  SidFrame s;
  s.level = p[0];
  for (size_t i = 1; i < p.size(); ++i) s.reflection.push_back(static_cast<float>(p[i]) / 127.0f - 1.0f);
  return s;
}

std::vector<double> NoiseAutocorrelation(std::span<const float> frame, int order) {
  std::vector<float> w(frame.begin(), frame.end());
  auto win = dsp::HannWindow(w.size());
  for (size_t i = 0; i < w.size(); ++i) w[i] *= win[i];
  std::vector<double> r(static_cast<size_t>(order) + 1);
  dsp::Autocorrelation(w, order, r.data());
  return r;
}

SidFrame SidFromAutocorrelation(std::vector<double> r, double mean_power) {
  SidFrame s;
  const int order = static_cast<int>(r.size()) - 1;
  float rms = static_cast<float>(std::sqrt(std::max(mean_power, 0.0)));
  s.level = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(-dsp::LinearToDb(rms))), 0, 127));
  std::vector<double> a(static_cast<size_t>(order) + 1), k(static_cast<size_t>(order));
  r[0] *= 1.0001;  // White-noise correction for conditioning.
  for (int i = 1; i <= order; ++i) r[static_cast<size_t>(i)] *= std::exp(-0.5 * std::pow(0.01 * i, 2));  // Lag window.
  dsp::LevinsonDurbin(r.data(), order, a.data(), k.data());
  for (double ki : k) s.reflection.push_back(static_cast<float>(std::clamp(ki, -0.99, 0.99)));
  return s;
}

SidFrame AnalyzeNoise(std::span<const float> frame, int order) {
  double power = frame.empty() ? 0.0 : dsp::Energy(frame) / static_cast<double>(frame.size());
  return SidFromAutocorrelation(NoiseAutocorrelation(frame, order), power);
}

bool DtxController::Changed(const SidFrame& a, const SidFrame& b) const {
  if (std::abs(static_cast<int>(a.level) - static_cast<int>(b.level)) > cfg_.update_threshold_db) return true;
  // Spectral change: compare the first two reflection coefficients
  // (dominant spectral tilt and resonance).
  for (size_t i = 0; i < std::min<size_t>(2, std::min(a.reflection.size(), b.reflection.size())); ++i) {
    if (std::fabs(a.reflection[i] - b.reflection[i]) > 0.2f) return true;
  }
  return false;
}

DtxDecision DtxController::Process(std::span<const float> frame, bool speech, SidFrame* sid_out) {
  ++stats_.frames;
  if (speech) {
    hang_ = cfg_.hangover_frames;
  } else if (hang_ > 0) {
    --hang_;
    speech = true;
  }
  if (speech) {
    in_dtx_ = false;
    ++stats_.speech;
    return DtxDecision::kSendSpeech;
  }
  // Average the background description over recent silent frames.
  auto r = NoiseAutocorrelation(frame, 10);
  double power = frame.empty() ? 0.0 : dsp::Energy(frame) / static_cast<double>(frame.size());
  const double a = in_dtx_ ? 0.2 : 1.0;  // Restart averaging at each pause.
  if (r_avg_.size() != r.size()) r_avg_.assign(r.size(), 0.0);
  for (size_t i = 0; i < r.size(); ++i) r_avg_[i] = (1 - a) * r_avg_[i] + a * r[i];
  power_avg_ = (1 - a) * power_avg_ + a * power;
  SidFrame cur = SidFromAutocorrelation(r_avg_, power_avg_);
  since_sid_ms_ += cfg_.frame_ms;
  if (!in_dtx_ || since_sid_ms_ >= cfg_.max_sid_interval_ms || Changed(cur, last_sid_)) {
    in_dtx_ = true;
    since_sid_ms_ = 0;
    last_sid_ = cur;
    if (sid_out) *sid_out = cur;
    ++stats_.sid;
    return DtxDecision::kSendSid;
  }
  ++stats_.suppressed;
  return DtxDecision::kSuppress;
}

void DtxController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Counter("speech_frames", stats_.speech);
  w.Counter("sid_frames", stats_.sid);
  w.Counter("suppressed_frames", stats_.suppressed);
  w.Gauge("tx_ratio", stats_.frames ? static_cast<double>(stats_.speech + stats_.sid) / static_cast<double>(stats_.frames) : 1.0);
}

void ComfortNoiseGenerator::OnSid(const SidFrame& sid) {
  ++sids_;
  k_target_ = sid.reflection;
  // Prediction gain: residual energy = prod(1 - k^2); scale the excitation
  // so the filtered output has the SID's RMS.
  double pred = 1.0;
  for (float k : sid.reflection) pred *= (1.0 - static_cast<double>(k) * k);
  gain_target_ = LevelToRms(sid.level) * std::sqrt(std::max(pred, 1e-6));
  if (k_cur_.size() != k_target_.size()) {
    k_cur_ = k_target_;
    gain_cur_ = gain_target_;
  }
}

void ComfortNoiseGenerator::Generate(std::span<float> out) {
  if (sids_ == 0) {
    std::fill(out.begin(), out.end(), 0.0f);
    return;
  }
  // Interpolate toward the latest SID (per frame).
  for (size_t i = 0; i < k_cur_.size(); ++i) k_cur_[i] += 0.3f * (k_target_[i] - k_cur_[i]);
  gain_cur_ += 0.3 * (gain_target_ - gain_cur_);
  a_ = ReflectionToLpc(k_cur_);
  const size_t p = std::min(a_.size() - 1, state_.size());
  for (float& o : out) {
    double y = gain_cur_ * rng_.Gaussian();
    for (size_t j = 1; j <= p; ++j) y -= a_[j] * state_[j - 1];
    for (size_t j = p; j > 1; --j) state_[j - 1] = state_[j - 2];
    if (p > 0) state_[0] = y;
    o = static_cast<float>(y);
  }
  samples_ += out.size();
}

void ComfortNoiseGenerator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("sids", sids_);
  w.Counter("samples", samples_);
  w.Gauge("gain", gain_cur_);
}

}  // namespace sm::audio
