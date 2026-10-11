// 6.7 Discontinuous transmission (DTX) and comfort noise (RFC 3389).
//
// Sender: while the VAD reports silence, DtxController suppresses audio
// packets and instead emits a SID (silence insertion descriptor) on the
// speech->silence transition, whenever the background noise changes by
// more than `update_threshold_db` in level or spectrum (measured on an
// autocorrelation averaged over recent silent frames), and at least every
// `max_sid_interval` as a keep-alive. The SID carries the noise level
// (-dBov) and LPC reflection coefficients (order 10, Levinson-Durbin),
// quantized as in RFC 3389.
//
// Receiver: ComfortNoiseGenerator shapes white noise with the all-pole LPC
// filter at the SID level, interpolating between SIDs, so silence sounds
// like the far room instead of a dead line.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/random.h"

namespace sm::audio {

struct SidFrame {
  uint8_t level = 127;  // -dBov, 0..127.
  std::vector<float> reflection;  // k_1..k_order, each in (-1, 1).
};

std::vector<uint8_t> EncodeSid(const SidFrame& sid);
std::optional<SidFrame> DecodeSid(std::span<const uint8_t> payload);
// Analyses a frame of background noise.
SidFrame AnalyzeNoise(std::span<const float> frame, int order = 10);
// Building blocks: windowed autocorrelation of a frame, and a SID from an
// (optionally averaged) autocorrelation plus mean power per sample.
std::vector<double> NoiseAutocorrelation(std::span<const float> frame, int order = 10);
SidFrame SidFromAutocorrelation(std::vector<double> r, double mean_power);

enum class DtxDecision { kSendSpeech, kSendSid, kSuppress };

struct DtxConfig {
  int max_sid_interval_ms = 400;
  float update_threshold_db = 3.0f;
  int frame_ms = 10;
  int hangover_frames = 0;  // Extra speech frames after the VAD turns off.
};

struct DtxStats {
  uint64_t frames = 0, speech = 0, sid = 0, suppressed = 0;
};

class DtxController : public MetricsSource {
 public:
  explicit DtxController(DtxConfig cfg = {}) : cfg_(cfg) {}
  // Returns what to send for this frame; `sid_out` is filled for kSendSid.
  DtxDecision Process(std::span<const float> frame, bool speech, SidFrame* sid_out);
  const DtxStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool Changed(const SidFrame& a, const SidFrame& b) const;
  DtxConfig cfg_;
  bool in_dtx_ = false;
  int since_sid_ms_ = 0;
  int hang_ = 0;
  SidFrame last_sid_;
  // Background description averaged over recent silent frames (EMA of the
  // autocorrelation), so single noisy frames do not trigger SID updates.
  std::vector<double> r_avg_;
  double power_avg_ = 0;
  DtxStats stats_;
};

class ComfortNoiseGenerator : public MetricsSource {
 public:
  explicit ComfortNoiseGenerator(uint64_t seed = 99) : rng_(seed) {}
  void OnSid(const SidFrame& sid);
  void Generate(std::span<float> out);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Random rng_;
  // Reflection coefficients are interpolated (always a stable filter),
  // then converted to the direct-form polynomial a_[0..p].
  std::vector<float> k_target_, k_cur_;
  std::vector<double> a_;
  double gain_target_ = 0, gain_cur_ = 0;
  std::array<double, 16> state_{};
  uint64_t sids_ = 0, samples_ = 0;
};

}  // namespace sm::audio
