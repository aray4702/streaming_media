// 6.12 Band splitting
//
// libwebrtc's `SplittingFilter`, which APM uses at 32 and 48 kHz so that
// AEC3 and NS can run on the lowest 0-8 kHz band (issue #241;
// modules/audio_processing/splitting_filter.cc). One 10 ms frame of
// full-band audio becomes `num_bands()` bands of 160 samples each (16 kHz
// per band), and back. 16 kHz has one band; APM does not split it.
//
// 32 kHz, two bands: the all-pass QMF of
// common_audio/signal_processing/splitting_filter.c (`WebRtcSpl_AnalysisQMF`,
// `WebRtcSpl_SynthesisQMF`). Even and odd samples each pass a cascade of
// three first-order all-pass sections (coefficients 0.0979, 0.5643, 0.8737
// and 0.3255, 0.7486, 0.9615); their half sum is the low band and their half
// difference the high band. Synthesis clamps to the S16 range, as libwebrtc
// does; on this library's [-1, 1) float scale that is [-1, 32767/32768].
//
// 48 kHz, three bands: `ThreeBandFilterBank`
// (modules/audio_processing/three_band_filter_bank.cc), a DCT-modulated FIR
// filter bank with a 4-tap polyphase branch of the low-pass prototype per
// delay (Kaiser window, alpha 3.5, 40 dB stop band, 24 samples of delay at
// 48 kHz). It does not reconstruct perfectly, as in libwebrtc.
//
// Bands are passed as one span of num_bands() * 160 samples, band b at
// [b * 160, (b + 1) * 160). Mono, as the rest of this library's APM.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sm/core/metrics.h"

namespace sm::audio {

// libwebrtc's ThreeBandFilterBank: 480 samples (10 ms at 48 kHz) <-> three
// bands of 160.
class ThreeBandFilterBank {
 public:
  static constexpr int kNumBands = 3;
  static constexpr int kFullBandSize = 480;
  static constexpr int kSplitBandSize = kFullBandSize / kNumBands;
  static constexpr int kSparsity = 4;
  static constexpr int kStrideLog2 = 2;
  static constexpr int kStride = 1 << kStrideLog2;
  static constexpr int kNumZeroFilters = 2;
  static constexpr int kFilterSize = 4;
  static constexpr int kMemorySize = kFilterSize * kStride - 1;
  static constexpr int kNumNonZeroFilters = kSparsity * kNumBands - kNumZeroFilters;

  ThreeBandFilterBank();
  // `in` has kFullBandSize samples; `out` has kNumBands * kSplitBandSize.
  void Analysis(std::span<const float> in, std::span<float> out);
  // `in` has kNumBands * kSplitBandSize samples; `out` has kFullBandSize.
  void Synthesis(std::span<const float> in, std::span<float> out);

 private:
  std::array<std::array<float, kMemorySize>, kNumNonZeroFilters> state_analysis_;
  std::array<std::array<float, kMemorySize>, kNumNonZeroFilters> state_synthesis_;
};

class SplittingFilter : public MetricsSource {
 public:
  static constexpr size_t kSamplesPerBand = 160;

  // `sample_rate` is 32000 (two bands) or 48000 (three bands).
  explicit SplittingFilter(int sample_rate);
  int num_bands() const { return num_bands_; }
  size_t full_band_samples() const { return kSamplesPerBand * static_cast<size_t>(num_bands_); }

  // `fullband` and `bands` both have full_band_samples() samples.
  void Analysis(std::span<const float> fullband, std::span<float> bands);
  void Synthesis(std::span<const float> bands, std::span<float> fullband);

  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // WebRtcSpl_AllPassQMF's state: x[-1] and y[-1] of each of the three
  // cascaded sections.
  using QmfState = std::array<float, 6>;

  int num_bands_;
  QmfState analysis_state1_{}, analysis_state2_{};
  QmfState synthesis_state1_{}, synthesis_state2_{};
  ThreeBandFilterBank three_band_;
  uint64_t analyzed_frames_ = 0, synthesized_frames_ = 0;
};

}  // namespace sm::audio
