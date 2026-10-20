// 3.4 Codec-agnostic audio packet loss concealment (PCM domain).
//
// For each lost frame the concealer extends the signal by repeating the last
// pitch period (found by normalized autocorrelation), mixes in matched-level
// noise when the history is unvoiced, and fades out after `fade_start` so
// long outages decay to silence. When real audio resumes it crossfades from
// the synthetic continuation into the new frame to avoid a click.
//
// Codecs with built-in recovery (Opus LBRR in-band FEC, DRED, NetEq's
// codec-internal PLC) should be preferred when available; this module is
// the fallback that works for any PCM source (G.711, raw PCM, decoder
// without PLC) and the "expand" operation used by the audio jitter buffer.
#pragma once

#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/random.h"

namespace sm::loss {

struct PlcConfig {
  int sample_rate = 16000;
  int min_pitch_hz = 60;
  int max_pitch_hz = 400;
  float fade_start_ms = 10;   // Full-level concealment before fading.
  float fade_length_ms = 50;  // Linear fade to silence after that.
  float merge_ms = 5;         // Crossfade length into resumed audio.
  float voiced_threshold = 0.5f;
};

struct PlcStats {
  uint64_t good_frames = 0;
  uint64_t concealed_frames = 0;
  uint64_t concealed_samples = 0;
  uint64_t concealment_events = 0;  // Bursts of consecutive concealed frames.
  uint64_t merges = 0;
  Histogram burst_length_frames;
  int last_pitch_lag = 0;
  float last_correlation = 0;
};

class PacketLossConcealer : public MetricsSource {
 public:
  explicit PacketLossConcealer(PlcConfig cfg = {});

  // Call with every decoded frame. Modified in place when it follows a
  // concealed frame (crossfade).
  void OnGoodFrame(std::span<float> frame);
  // Produces `out.size()` samples of concealment.
  void Conceal(std::span<float> out);

  bool in_concealment() const { return concealing_; }
  // Gain of the next concealed sample (1 outside a concealment): the fade
  // that NetEq's Expand::MuteFactor tracks.
  float gain() const;
  const PlcStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void PushHistory(std::span<const float> x);
  float NextSample();

  PlcConfig cfg_;
  Random rng_{7};
  std::vector<float> history_;  // Last `history_len_` samples, oldest first.
  size_t history_len_;
  int min_lag_, max_lag_, window_;
  bool concealing_ = false;
  size_t concealed_in_burst_ = 0;
  uint64_t burst_frames_ = 0;
  // Concealment state.
  std::vector<float> period_;  // Waveform being repeated (1-3 pitch periods).
  size_t period_pos_ = 0;
  float voiced_mix_ = 1;
  float noise_rms_ = 0;
  PlcStats stats_;
};

}  // namespace sm::loss
