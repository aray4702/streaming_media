// 6.8 Voice activity detection, as libwebrtc M157's `webrtc::Vad`.
//
// Ported from compare/webrtc/reference/common_audio/vad/: include/vad.h and
// vad.cc (`Vad`, `CreateVad`, `VadImpl`) over the `WebRtcVad` fixed-point
// GMM VAD of webrtc_vad.c and vad_core.c, which is ported bit-exact in
// `agc1_vad.h` (#242: AGC1's `VoiceActivityDetector` runs it in mode 3).
// Per 10, 20 or 30 ms frame: six sub-band log energies (vad_filterbank.c),
// a two-Gaussian speech and noise model per band (vad_gmm.c) whose noise
// side adapts to the minimum of the last 100 frames (vad_sp.c), a
// log-likelihood ratio test with the aggressiveness mode's thresholds, and
// a hangover after speech (vad_core.c `kOverHangMax*`).
// At 48 kHz (#901) it runs `WebRtcVad_CalcVad48khz` with SPL's 48-to-8 kHz
// resampler, including libwebrtc's reuse of the frame's first 10 ms for every
// 10 ms block of a 20 or 30 ms frame (see `agc1_vad.h`).
//
// Where libwebrtc M157 runs a VAD in a call: its `AudioProcessingImpl`
// reports no VAD decision (`VoiceDetection` was removed; the statistics it
// fills are the echo metrics, audio_processing_impl.cc:1434-1449). AGC1's
// `Agc` runs `VoiceActivityDetector` (agc.cc:96, `agc1::Agc`), AGC2 its RNN
// VAD (gain_controller2.cc:204, `agc2_rnn_vad.h`), and the Opus encoder its
// own. `webrtc::Vad` (aggressiveness `kVadNormal`) is libwebrtc's
// standalone VAD, the one `sm_apm_compare` measures this unit against, so it
// is the catalog unit and the capture chain's per-frame VAD output. The
// transient suppressor (an opt-in, #243) reads `VoiceActivityDetector`'s
// pitch-based probability instead, so a broadband click is not speech.
//
// Differences from libwebrtc (scope, not behaviour):
//  - A constructor in place of `CreateVad`; `Reset()` re-initializes the
//    instance as `VadImpl::Reset` (free, create, init, set mode).
//  - A float overload for frames in [-1, 1), scaled to S16 by rounding
//    (as `sm_apm_compare` feeds libwebrtc), and metrics.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sm/audio/agc1_vad.h"
#include "sm/core/metrics.h"

namespace sm::audio {

struct VadStats {
  uint64_t frames = 0;
  uint64_t speech_frames = 0;
  uint64_t errors = 0;
};

class Vad : public MetricsSource {
 public:
  enum Aggressiveness { kVadNormal = 0, kVadLowBitrate = 1, kVadAggressive = 2, kVadVeryAggressive = 3 };
  enum Activity { kPassive = 0, kActive = 1, kError = -1 };

  explicit Vad(Aggressiveness aggressiveness = kVadNormal);

  // A VAD decision for one frame. Valid sample rates are 8000, 16000,
  // 32000 and 48000 Hz, and the frame must be 10, 20 or 30 ms long.
  Activity VoiceActivity(const int16_t* audio, size_t num_samples, int sample_rate_hz);
  // The same for a float frame in [-1, 1).
  Activity VoiceActivity(std::span<const float> frame, int sample_rate_hz);
  // Resets the VAD state.
  void Reset();

  Aggressiveness aggressiveness() const { return aggressiveness_; }
  const VadStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Aggressiveness aggressiveness_;
  agc1::detail::WebRtcVad vad_;
  std::vector<int16_t> pcm_;
  VadStats stats_;
};

}  // namespace sm::audio
