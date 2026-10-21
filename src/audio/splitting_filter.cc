#include "sm/audio/splitting_filter.h"

#include <algorithm>
#include <numbers>

namespace sm::audio {

namespace {

// ------------------------------------------------------------- two bands
// common_audio/signal_processing/splitting_filter.c.

constexpr size_t kMaxBandFrameLength = 320;  // 10 ms at 64 kHz.

// QMF filter coefficients.
constexpr float kAllPassFilter1[3] = {0.0979309082f, 0.5643005371f, 0.8737335205f};
constexpr float kAllPassFilter2[3] = {0.32551574707f, 0.74862670898f, 0.96145629882f};

// WebRtcSpl_AllPassQMF: three cascaded first-order all-pass sections,
//         a_3 + q^-1    a_2 + q^-1    a_1 + q^-1
// y[n] =  -----------   -----------   -----------   x[n]
//         1 + a_3q^-1   1 + a_2q^-1   1 + a_1q^-1
// The state holds x[-1], y[-1] of each section. `in_data` is overwritten
// with the second section's output, as in libwebrtc.
void AllPassQmf(float* in_data, size_t data_length, float* out_data, const float* coefficients, float* state) {
  // First section: in_data -> out_data.
  float diff = in_data[0] - state[1];
  out_data[0] = state[0] + coefficients[0] * diff;
  for (size_t k = 1; k < data_length; k++) {
    diff = in_data[k] - out_data[k - 1];
    out_data[k] = in_data[k - 1] + coefficients[0] * diff;
  }
  state[0] = in_data[data_length - 1];
  state[1] = out_data[data_length - 1];

  // Second section: out_data -> in_data.
  diff = out_data[0] - state[3];
  in_data[0] = state[2] + coefficients[1] * diff;
  for (size_t k = 1; k < data_length; k++) {
    diff = out_data[k] - in_data[k - 1];
    in_data[k] = out_data[k - 1] + coefficients[1] * diff;
  }
  state[2] = out_data[data_length - 1];
  state[3] = in_data[data_length - 1];

  // Third section: in_data -> out_data.
  diff = in_data[0] - state[5];
  out_data[0] = state[4] + coefficients[2] * diff;
  for (size_t k = 1; k < data_length; k++) {
    diff = in_data[k] - out_data[k - 1];
    out_data[k] = in_data[k - 1] + coefficients[2] * diff;
  }
  state[4] = in_data[data_length - 1];
  state[5] = out_data[data_length - 1];
}

// WebRtcSpl_AnalysisQMF.
void AnalysisQmf(const float* in_data, size_t in_data_length, float* low_band, float* high_band, float* state1,
                 float* state2) {
  float half_in1[kMaxBandFrameLength];
  float half_in2[kMaxBandFrameLength];
  float filter1[kMaxBandFrameLength];
  float filter2[kMaxBandFrameLength];
  const size_t band_length = in_data_length / 2;

  // Split even and odd samples.
  for (size_t i = 0, k = 0; i < band_length; i++, k += 2) {
    half_in2[i] = in_data[k];
    half_in1[i] = in_data[k + 1];
  }

  // All-pass filter even and odd samples, independently.
  AllPassQmf(half_in1, band_length, filter1, kAllPassFilter1, state1);
  AllPassQmf(half_in2, band_length, filter2, kAllPassFilter2, state2);

  // The sum and difference of the two branches are the low and high band.
  for (size_t i = 0; i < band_length; i++) {
    low_band[i] = (filter1[i] + filter2[i]) * 0.5f;
    high_band[i] = (filter1[i] - filter2[i]) * 0.5f;
  }
}

// libwebrtc saturates to [-32768, 32767] on its S16-scaled floats; this
// library's samples are in [-1, 1).
constexpr float kS16Min = -1.0f;
constexpr float kS16Max = 32767.0f / 32768.0f;

float Saturate(float v) { return v > kS16Min ? (v < kS16Max ? v : kS16Max) : kS16Min; }

// WebRtcSpl_SynthesisQMF.
void SynthesisQmf(const float* low_band, const float* high_band, size_t band_length, float* out_data,
                  float* state1, float* state2) {
  float half_in1[kMaxBandFrameLength];
  float half_in2[kMaxBandFrameLength];
  float filter1[kMaxBandFrameLength];
  float filter2[kMaxBandFrameLength];

  // The sum and difference channels from the two bands.
  for (size_t i = 0; i < band_length; i++) {
    half_in1[i] = low_band[i] + high_band[i];
    half_in2[i] = low_band[i] - high_band[i];
  }

  // All-pass filter the sum and difference channels.
  AllPassQmf(half_in1, band_length, filter1, kAllPassFilter2, state1);
  AllPassQmf(half_in2, band_length, filter2, kAllPassFilter1, state2);

  // The filtered signals are the even and odd output samples.
  for (size_t i = 0, k = 0; i < band_length; i++) {
    out_data[k++] = Saturate(filter2[i]);
    out_data[k++] = Saturate(filter1[i]);
  }
}

// ----------------------------------------------------------- three bands
// modules/audio_processing/three_band_filter_bank.cc.

using Tbfb = ThreeBandFilterBank;
constexpr int kSubSampling = Tbfb::kNumBands;
constexpr int kDctSize = Tbfb::kNumBands;
constexpr int kFilterSize = Tbfb::kFilterSize;
constexpr int kStride = Tbfb::kStride;
constexpr int kStrideLog2 = Tbfb::kStrideLog2;
constexpr int kMemorySize = Tbfb::kMemorySize;
constexpr int kSplitBandSize = Tbfb::kSplitBandSize;
static_assert(kMemorySize == 15, "The memory must hold the shifted filters");
static_assert(Tbfb::kNumBands * Tbfb::kSplitBandSize == Tbfb::kFullBandSize,
              "The full band must be split in equally sized subbands");

// The polyphase low-pass prototype, from Matlab:
//   N = kNumBands * kSparsity * kFilterSize - 1;
//   h = fir1(N, 1 / (2 * kNumBands), kaiser(N + 1, 3.5));
//   reshape(h, kNumBands * kSparsity, kFilterSize);
// with the two all-zero branches left out.
constexpr float kFilterCoeffs[Tbfb::kNumNonZeroFilters][kFilterSize] = {
    {-0.00047749f, -0.00496888f, +0.16547118f, +0.00425496f},
    {-0.00173287f, -0.01585778f, +0.14989004f, +0.00994113f},
    {-0.00304815f, -0.02536082f, +0.12154542f, +0.01157993f},
    {-0.00346946f, -0.02587886f, +0.04760441f, +0.00607594f},
    {-0.00154717f, -0.01136076f, +0.01387458f, +0.00186353f},
    {+0.00186353f, +0.01387458f, -0.01136076f, -0.00154717f},
    {+0.00607594f, +0.04760441f, -0.02587886f, -0.00346946f},
    {+0.00983212f, +0.08543175f, -0.02982767f, -0.00383509f},
    {+0.00994113f, +0.14989004f, -0.01585778f, -0.00173287f},
    {+0.00425496f, +0.16547118f, -0.00496888f, -0.00047749f}};

constexpr int kZeroFilterIndex1 = 3;
constexpr int kZeroFilterIndex2 = 9;

constexpr float kSqrt3 = std::numbers::sqrt3_v<float>;

// The cosine modulation that shifts the half-bandwidth prototype to the
// band centres 1/12, 3/12 and 5/12.
constexpr float kDctModulation[Tbfb::kNumNonZeroFilters][kDctSize] = {
    {2.f, 2.f, 2.f},     {kSqrt3, 0.f, -kSqrt3}, {1.f, -2.f, 1.f},       {-1.f, 2.f, -1.f}, {-kSqrt3, 0.f, kSqrt3},
    {-2.f, -2.f, -2.f}, {-kSqrt3, 0.f, kSqrt3}, {-1.f, 2.f, -1.f},       {1.f, -2.f, 1.f},  {kSqrt3, 0.f, -kSqrt3}};

// Filters `in` with `filter` shifted by `in_shift`, continuing from `state`.
void FilterCore(const float* filter, const float* in, int in_shift, float* out, float* state) {
  std::fill(out, out + kSplitBandSize, 0.f);

  for (int k = 0; k < in_shift; ++k) {
    for (int i = 0, j = kMemorySize + k - in_shift; i < kFilterSize; ++i, j -= kStride) {
      out[k] += state[j] * filter[i];
    }
  }

  for (int k = in_shift, shift = 0; k < kFilterSize * kStride; ++k, ++shift) {
    const int loop_limit = std::min(kFilterSize, 1 + (shift >> kStrideLog2));
    for (int i = 0, j = shift; i < loop_limit; ++i, j -= kStride) {
      out[k] += in[j] * filter[i];
    }
    for (int i = loop_limit, j = kMemorySize + shift - loop_limit * kStride; i < kFilterSize; ++i, j -= kStride) {
      out[k] += state[j] * filter[i];
    }
  }

  for (int k = kFilterSize * kStride, shift = kFilterSize * kStride - in_shift; k < kSplitBandSize; ++k, ++shift) {
    for (int i = 0, j = shift; i < kFilterSize; ++i, j -= kStride) {
      out[k] += in[j] * filter[i];
    }
  }

  std::copy(in + kSplitBandSize - kMemorySize, in + kSplitBandSize, state);
}

// Maps a polyphase branch to its row in kFilterCoeffs, or -1 for the two
// all-zero branches.
int FilterIndex(int index) {
  if (index == kZeroFilterIndex1 || index == kZeroFilterIndex2) return -1;
  return index < kZeroFilterIndex1 ? index : (index < kZeroFilterIndex2 ? index - 1 : index - 2);
}

}  // namespace

ThreeBandFilterBank::ThreeBandFilterBank() {
  for (auto& s : state_analysis_) s.fill(0.f);
  for (auto& s : state_synthesis_) s.fill(0.f);
}

// 1. Serial to parallel downsampling by kNumBands. 2. Filtering of kSparsity
// delayed signals with the polyphase branches of the prototype. 3. Cosine
// modulation and accumulation into each band.
void ThreeBandFilterBank::Analysis(std::span<const float> in, std::span<float> out) {
  if (in.size() != static_cast<size_t>(kFullBandSize) || out.size() != static_cast<size_t>(kFullBandSize)) return;
  std::fill(out.begin(), out.end(), 0.f);

  for (int downsampling_index = 0; downsampling_index < kSubSampling; ++downsampling_index) {
    // Downsample to form the filter input.
    std::array<float, kSplitBandSize> in_subsampled;
    for (int k = 0; k < kSplitBandSize; ++k) {
      in_subsampled[k] = in[(kSubSampling - 1) - downsampling_index + kSubSampling * k];
    }

    for (int in_shift = 0; in_shift < kStride; ++in_shift) {
      const int filter_index = FilterIndex(downsampling_index + in_shift * kSubSampling);
      if (filter_index < 0) continue;

      std::array<float, kSplitBandSize> out_subsampled;
      FilterCore(kFilterCoeffs[filter_index], in_subsampled.data(), in_shift, out_subsampled.data(),
                 state_analysis_[filter_index].data());

      for (int band = 0; band < kNumBands; ++band) {
        float* out_band = out.data() + band * kSplitBandSize;
        for (int n = 0; n < kSplitBandSize; ++n) {
          out_band[n] += kDctModulation[filter_index][band] * out_subsampled[n];
        }
      }
    }
  }
}

// 1. Cosine modulation. 2. Filtering with the polyphase branches and
// accumulating kSparsity signals with different delays. 3. Parallel to
// serial upsampling by kNumBands.
void ThreeBandFilterBank::Synthesis(std::span<const float> in, std::span<float> out) {
  if (in.size() != static_cast<size_t>(kFullBandSize) || out.size() != static_cast<size_t>(kFullBandSize)) return;
  std::fill(out.begin(), out.end(), 0.f);

  for (int upsampling_index = 0; upsampling_index < kSubSampling; ++upsampling_index) {
    for (int in_shift = 0; in_shift < kStride; ++in_shift) {
      const int filter_index = FilterIndex(upsampling_index + in_shift * kSubSampling);
      if (filter_index < 0) continue;

      // Modulate the banded input.
      std::array<float, kSplitBandSize> in_subsampled;
      in_subsampled.fill(0.f);
      for (int band = 0; band < kNumBands; ++band) {
        const float* in_band = in.data() + band * kSplitBandSize;
        for (int n = 0; n < kSplitBandSize; ++n) {
          in_subsampled[n] += kDctModulation[filter_index][band] * in_band[n];
        }
      }

      std::array<float, kSplitBandSize> out_subsampled;
      FilterCore(kFilterCoeffs[filter_index], in_subsampled.data(), in_shift, out_subsampled.data(),
                 state_synthesis_[filter_index].data());

      // Upsample.
      constexpr float kUpsamplingScaling = kSubSampling;
      for (int k = 0; k < kSplitBandSize; ++k) {
        out[upsampling_index + kSubSampling * k] += kUpsamplingScaling * out_subsampled[k];
      }
    }
  }
}

SplittingFilter::SplittingFilter(int sample_rate) : num_bands_(sample_rate >= 48000 ? 3 : 2) {}

void SplittingFilter::Analysis(std::span<const float> fullband, std::span<float> bands) {
  const size_t n = full_band_samples();
  if (fullband.size() != n || bands.size() != n) return;
  if (num_bands_ == 2) {
    AnalysisQmf(fullband.data(), n, bands.data(), bands.data() + kSamplesPerBand, analysis_state1_.data(),
                analysis_state2_.data());
  } else {
    three_band_.Analysis(fullband, bands);
  }
  ++analyzed_frames_;
}

void SplittingFilter::Synthesis(std::span<const float> bands, std::span<float> fullband) {
  const size_t n = full_band_samples();
  if (fullband.size() != n || bands.size() != n) return;
  if (num_bands_ == 2) {
    SynthesisQmf(bands.data(), bands.data() + kSamplesPerBand, kSamplesPerBand, fullband.data(),
                 synthesis_state1_.data(), synthesis_state2_.data());
  } else {
    three_band_.Synthesis(bands, fullband);
  }
  ++synthesized_frames_;
}

void SplittingFilter::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("num_bands", num_bands_);
  w.Counter("analyzed_frames", analyzed_frames_);
  w.Counter("synthesized_frames", synthesized_frames_);
}

}  // namespace sm::audio
