// 6.2 Echo-path delay estimation.
//
// The far-end (render) signal reaches the microphone after the playout
// buffer, DAC, speaker, air and ADC: tens to hundreds of ms, and it changes
// when the audio route changes (speaker <-> Bluetooth). The estimator
// cross-correlates the last `window` of capture (Hann-windowed) against the
// last `max_delay + window` of render (tapered ends) using GCC-PHAT with a
// partial phase transform (the cross-spectrum is divided by |C|^0.8, so
// the peak is sharp even for coloured speech without amplifying empty
// bins), averages the cross-spectrum over successive updates,
// and reports a delay only after it has been the peak for `stable_updates`
// consecutive updates with a peak-to-mean confidence above a threshold.
#pragma once

#include <optional>
#include <span>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/fft.h"
#include "sm/core/metrics.h"

namespace sm::audio {

struct EchoDelayConfig {
  int sample_rate = 16000;
  int max_delay_ms = 500;
  int window_ms = 128;
  int update_interval_ms = 100;
  float smoothing = 0.6f;          // Weight of the previous cross-spectrum.
  int stable_updates = 3;
  float min_confidence = 6.0f;     // Peak / mean |correlation|.
  float min_render_dbfs = -60.0f;  // Skip updates when the far end is silent.
  EventSink* events = nullptr;
};

class EchoDelayEstimator : public MetricsSource {
 public:
  explicit EchoDelayEstimator(EchoDelayConfig cfg = {});
  void AnalyzeRender(std::span<const float> far);
  void AnalyzeCapture(std::span<const float> near);
  std::optional<int> delay_samples() const { return delay_; }
  float confidence() const { return confidence_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Update();
  EchoDelayConfig cfg_;
  size_t max_delay_, window_, interval_;
  std::vector<float> far_, near_;  // Ring-less sliding buffers (oldest first).
  size_t since_update_ = 0;
  Fft fft_;
  std::vector<Complex> x_, y_, cross_;
  std::vector<float> near_win_;
  bool have_cross_ = false;
  std::optional<int> delay_;
  int candidate_ = -1, candidate_count_ = 0;
  float confidence_ = 0;
  uint64_t updates_ = 0, changes_ = 0;
};

}  // namespace sm::audio
