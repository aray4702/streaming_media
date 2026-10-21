// 6.4 Automatic gain control as libwebrtc M157's AGC1 (GainController1):
// the digital compression stage. `apm v2` (#242).
//
// Every WebRTC call runs AGC1's digital stage: with the analog gain
// controller on (its default, `AnalogGainController::enabled`),
// `AudioProcessingImpl::InitializeGainController1` creates an
// `AgcManagerDirect`, whose `SetupDigitalGainControl` puts `GainControlImpl`
// in `kFixedDigital` mode with a 2 dB target, 7 dB compression gain and the
// limiter on; `AgcManagerDirect` then moves the compression gain between 2
// and 12 dB. The desktop (`kAdaptiveAnalog`) and mobile (`kFixedDigital`)
// call configurations differ only in that analog volume, not here.
//
// Ported from compare/webrtc/reference/modules/audio_processing/ with
// libwebrtc's constants and defaults:
//  - agc/legacy/digital_agc.cc: `WebRtcAgc_CalculateGainTable` (the
//    32-entry compressor gain table, compression ratio 3, limiter),
//    `WebRtcAgc_InitDigital`, `WebRtcAgc_ComputeDigitalGains` (fast and
//    slow envelope followers, the gain table lookup, the gate that lowers
//    the gain without speech, the overload limit, gain reductions 1 ms
//    early) and the AGC's own VAD (`WebRtcAgc_InitVad`,
//    `WebRtcAgc_ProcessVad`: 4 kHz energy, short- and long-term statistics,
//    log likelihood ratio).
//  - agc/legacy/analog_agc.cc: the fixed-digital parts of `WebRtcAgc_Init`,
//    `WebRtcAgc_set_config` (the compression gain is offset by the target
//    level, and the gain table's analog target is that gain) and
//    `WebRtcAgc_Analyze`, and `WebRtcAgc_AddFarend`.
//  - gain_control_impl.cc: `GainControlImpl` in `kFixedDigital` mode
//    (`ProcessCaptureAudio`, the float `ApplyDigitalGain` with 16 linear
//    steps per millisecond on every band, `ProcessRenderAudio`, the
//    setters and their ranges) and the configuration
//    `AgcManagerDirect::SetupDigitalGainControl` gives it.
//  - common_audio/signal_processing: the fixed-point helpers these call
//    (`WebRtcSpl_DownsampleBy2`, `WebRtcSpl_Sqrt`, `WebRtcSpl_NormU32`,
//    `WebRtcSpl_NormW32`, `WebRtcSpl_DivW32W16`, `WebRtcSpl_DivW32W16ResW16`,
//    `WebRtcSpl_AddSatW16`).
//
// The gains and the output are bit-exact with libwebrtc's own files
// compiled from reference/ (golden values in `Agc1.*MatchesLibwebrtc*`).
//
// Signals are FloatS16 inside (libwebrtc's scale); Process() takes and
// returns our [-1, 1] floats.
//
// What is not here yet (catalog.py, #242):
//  - `AgcManagerDirect` / `MonoAgc`, which picks the compression gain and
//    the analog volume from `Agc`'s loudness histogram (`Agc` and its
//    `VoiceActivityDetector` are ported in agc1_vad.h); APM's analog mic
//    gain emulation is `CaptureLevelsAdjuster`. Until then the compression
//    gain stays at what the caller sets (7 dB by default, as
//    `SetupDigitalGainControl`).
//  - The adaptive modes' analog part (`WebRtcAgc_ProcessAnalog`,
//    `WebRtcAgc_AddMic`, `WebRtcAgc_VirtualMic`): a call never runs it,
//    because `AgcManagerDirect` always sets `kFixedDigital`.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"

namespace sm::audio::agc1 {

// agc/legacy/digital_agc.h: AgcVad.
struct AgcVad {
  std::array<int32_t, 8> down_state{};
  int16_t hp_state = 0;
  int16_t counter = 0;
  int16_t log_ratio = 0;            // log(P(active) / P(inactive)), Q10.
  int16_t mean_long_term = 0;       // Q10.
  int32_t variance_long_term = 0;   // Q8.
  int16_t std_long_term = 0;        // Q10.
  int16_t mean_short_term = 0;      // Q10.
  int32_t variance_short_term = 0;  // Q8.
  int16_t std_short_term = 0;       // Q10.
};

// `WebRtcAgc_InitVad`.
void InitVad(AgcVad& state);
// `WebRtcAgc_ProcessVad`: one 10 ms frame of 80 (8 kHz) or 160 samples.
// Returns the log likelihood ratio in Q10.
int16_t ProcessVad(AgcVad& state, const int16_t* in, size_t num_samples);

// `WebRtcAgc_CalculateGainTable`: the compressor gain table (Q16) for
// `compression_gain_db` (Q0), `target_level_dbfs` (positive, Q0), the
// limiter and the analog target. Returns false on an out-of-range gain.
bool CalculateGainTable(std::array<int32_t, 32>& gain_table, int16_t compression_gain_db, int16_t target_level_dbfs,
                        bool limiter_enable, int16_t analog_target);

// agc/legacy/digital_agc.h: DigitalAgc, in fixed-digital mode.
class DigitalAgc {
 public:
  DigitalAgc() { Init(); }
  // `WebRtcAgc_InitDigital(kAgcModeFixedDigital)`.
  void Init();
  std::array<int32_t, 32>& gain_table() { return gain_table_; }
  // `WebRtcAgc_AddFarendToDigital`.
  void AddFarend(const int16_t* in_far, size_t num_samples);
  // `WebRtcAgc_ComputeDigitalGains`: 11 gains (Q16) for the 10 ms frame,
  // one per millisecond boundary, from the lowest band (`samples_per_ms` 8
  // at 8 kHz, else 16).
  void ComputeDigitalGains(const int16_t* in_near, size_t samples_per_ms, std::array<int32_t, 11>& gains);
  const AgcVad& vad_nearend() const { return vad_nearend_; }
  int32_t capacitor_slow() const { return capacitor_slow_; }
  int32_t capacitor_fast() const { return capacitor_fast_; }

 private:
  int32_t capacitor_slow_ = 0;
  int32_t capacitor_fast_ = 0;
  int32_t gain_ = 65536;
  std::array<int32_t, 32> gain_table_{};
  int16_t gate_previous_ = 0;
  AgcVad vad_nearend_;
  AgcVad vad_farend_;
};

struct GainControlConfig {
  int sample_rate = 16000;  // 8000, 16000, 32000 or 48000.
  // `AgcManagerDirect::SetupDigitalGainControl` (enable_digital_adaptive
  // on, the default): 2 dB target, 7 dB compression, limiter on.
  int target_level_dbfs = 2;    // [0, 31]; the peak target is -target dBFS.
  int compression_gain_db = 7;  // [0, 90].
  bool enable_limiter = true;
  EventSink* events = nullptr;  // "agc1" / "compression_gain" on a change.
};

struct GainControlStats {
  uint64_t frames = 0;
  uint64_t saturated_frames = 0;  // Never set in fixed-digital mode, as libwebrtc.
  float gain_db = 0;              // Gain at the end of the last frame.
  int16_t vad_log_ratio = 0;      // Q10.
};

// `GainControlImpl` in `kFixedDigital` mode, one mono channel.
class GainControl : public MetricsSource {
 public:
  explicit GainControl(GainControlConfig cfg = {});
  // The setters as `GainControlImpl`'s: they reject values outside the
  // ranges above and recompute the gain table, keeping the state.
  bool set_target_level_dbfs(int level);
  bool set_compression_gain_db(int gain);
  void enable_limiter(bool enable);
  int compression_gain_db() const { return cfg_.compression_gain_db; }
  // `ProcessRenderAudio`: the lowest band of a 10 ms render frame, S16. APM
  // feeds it only without `AgcManagerDirect`, so not in a call.
  void ProcessRenderAudio(std::span<const int16_t> band0);
  // `ProcessCaptureAudio` on split bands in FloatS16 (one band below
  // 32 kHz; 160 samples each, 80 at 8 kHz).
  void ProcessBands(std::span<const std::span<float>> bands);
  // One 10 ms full-band frame in [-1, 1] at 8 or 16 kHz.
  void Process(std::span<float> frame);
  const std::array<int32_t, 11>& last_gains() const { return gains_; }
  const DigitalAgc& digital() const { return digital_; }
  const GainControlStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void Configure();

  GainControlConfig cfg_;
  DigitalAgc digital_;
  std::array<int32_t, 11> gains_{};
  std::vector<int16_t> band0_s16_;
  std::vector<float> s16_;
  GainControlStats stats_;
};

}  // namespace sm::audio::agc1
