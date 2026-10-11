#include "sm/loss/plc.h"

#include <algorithm>
#include <cmath>

#include "sm/core/dsp.h"

namespace sm::loss {

PacketLossConcealer::PacketLossConcealer(PlcConfig cfg) : cfg_(cfg) {
  min_lag_ = cfg_.sample_rate / cfg_.max_pitch_hz;
  max_lag_ = cfg_.sample_rate / cfg_.min_pitch_hz;
  window_ = std::max(cfg_.sample_rate / 100, min_lag_ * 2);  // 10 ms.
  history_len_ = static_cast<size_t>(window_ + max_lag_ * 3);
  history_.assign(history_len_, 0.0f);
}

void PacketLossConcealer::PushHistory(std::span<const float> x) {
  if (x.size() >= history_len_) {
    std::copy(x.end() - static_cast<std::ptrdiff_t>(history_len_), x.end(), history_.begin());
    return;
  }
  std::copy(history_.begin() + static_cast<std::ptrdiff_t>(x.size()), history_.end(), history_.begin());
  std::copy(x.begin(), x.end(), history_.end() - static_cast<std::ptrdiff_t>(x.size()));
}

float PacketLossConcealer::NextSample() {
  float v = period_[period_pos_];
  period_pos_ = (period_pos_ + 1) % period_.size();
  if (voiced_mix_ < 1) v = voiced_mix_ * v + (1 - voiced_mix_) * noise_rms_ * static_cast<float>(rng_.Gaussian());
  return v;
}

void PacketLossConcealer::Conceal(std::span<float> out) {
  if (!concealing_) {
    concealing_ = true;
    concealed_in_burst_ = 0;
    burst_frames_ = 0;
    ++stats_.concealment_events;
    auto pitch = dsp::EstimatePitch(history_, min_lag_, max_lag_, window_);
    stats_.last_pitch_lag = pitch.lag;
    stats_.last_correlation = pitch.correlation;
    // Repeat the last pitch period; when voicing is weak, mix in noise of
    // the same level so unvoiced sounds do not turn into a buzz.
    size_t lag = static_cast<size_t>(std::max(pitch.lag, min_lag_));
    period_.assign(history_.end() - static_cast<std::ptrdiff_t>(lag), history_.end());
    period_pos_ = 0;
    voiced_mix_ = std::clamp((pitch.correlation - 0.2f) / (cfg_.voiced_threshold + 0.3f - 0.2f), 0.0f, 1.0f);
    noise_rms_ = dsp::Rms(std::span<const float>(history_).last(static_cast<size_t>(window_)));
  }
  const size_t fade_start = static_cast<size_t>(cfg_.fade_start_ms * cfg_.sample_rate / 1000);
  const size_t fade_len = std::max<size_t>(1, static_cast<size_t>(cfg_.fade_length_ms * cfg_.sample_rate / 1000));
  for (float& s : out) {
    float gain = 1.0f;
    if (concealed_in_burst_ > fade_start) {
      gain = std::max(0.0f, 1.0f - static_cast<float>(concealed_in_burst_ - fade_start) / static_cast<float>(fade_len));
    }
    s = gain * NextSample();
    ++concealed_in_burst_;
  }
  ++burst_frames_;
  ++stats_.concealed_frames;
  stats_.concealed_samples += out.size();
  PushHistory(out);
}

void PacketLossConcealer::OnGoodFrame(std::span<float> frame) {
  ++stats_.good_frames;
  if (concealing_) {
    // Crossfade from the synthetic continuation into the real signal.
    const size_t fade_start = static_cast<size_t>(cfg_.fade_start_ms * cfg_.sample_rate / 1000);
    const size_t fade_len = std::max<size_t>(1, static_cast<size_t>(cfg_.fade_length_ms * cfg_.sample_rate / 1000));
    size_t n = std::min(frame.size(), static_cast<size_t>(cfg_.merge_ms * cfg_.sample_rate / 1000));
    for (size_t i = 0; i < n; ++i) {
      float gain = 1.0f;
      if (concealed_in_burst_ > fade_start) {
        gain = std::max(0.0f, 1.0f - static_cast<float>(concealed_in_burst_ - fade_start) / static_cast<float>(fade_len));
      }
      float synth = gain * NextSample();
      ++concealed_in_burst_;
      float w = static_cast<float>(i + 1) / static_cast<float>(n + 1);
      frame[i] = (1 - w) * synth + w * frame[i];
    }
    concealing_ = false;
    ++stats_.merges;
    stats_.burst_length_frames.Add(static_cast<double>(burst_frames_));
  }
  PushHistory(frame);
}

void PacketLossConcealer::ExportMetrics(MetricsWriter& w) const {
  w.Counter("good_frames", stats_.good_frames);
  w.Counter("concealed_frames", stats_.concealed_frames);
  w.Counter("concealed_samples", stats_.concealed_samples);
  w.Counter("concealment_events", stats_.concealment_events);
  w.Counter("merges", stats_.merges);
  w.Gauge("last_pitch_lag", stats_.last_pitch_lag);
  w.Gauge("last_correlation", stats_.last_correlation);
  w.Hist("burst_length_frames", stats_.burst_length_frames);
}

}  // namespace sm::loss
