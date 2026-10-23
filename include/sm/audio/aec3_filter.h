// 6.1 AEC3's linear echo canceller (`apm v2`, #240 step 2), ported from
// compare/webrtc/reference/modules/audio_processing/aec3/
// {aec3_common,fft_data,aec3_fft,block,block_buffer,spectrum_buffer,fft_buffer,
// render_buffer,render_delay_buffer,adaptive_fir_filter,adaptive_fir_filter_erl,
// echo_path_variability,render_signal_analyzer,subtractor_output,
// refined_filter_update_gain,coarse_filter_update_gain,subtractor}.{h,cc}
// (M157), with the defaults of EchoCanceller3Config::Filter, ::RenderLevels
// and ::Delay (api/audio/echo_canceller3_config.h of the pinned prebuilt).
//
//   Aec3Fft             128-point real FFT of two 64-sample blocks (the new
//                       block after the previous one, or zero padding), with
//                       AEC3's packing: 65 bins, re/im.
//   RenderDelayBuffer   the render blocks, their FFTs and power spectra in
//                       rings of 13 + 1 + 5 * 24 + 32 + 1 = 64 / 4 ... blocks
//                       (GetRenderDelayBufferSize), read at the delay the
//                       delay controller (aec3_delay.h) found, plus the
//                       decimated ring the delay estimator reads.
//   AdaptiveFirFilter   partitioned-block frequency-domain filter: 64-sample
//                       partitions, 12 at the start and 13 after the initial
//                       state (about 52 ms), resized over 250 blocks. One
//                       partition per block is constrained to 64 causal taps.
//   RefinedFilterUpdateGain  NLMS gain mu = H_error / (0.5 H_error X2 +
//                       partitions E2), with the filter-error estimate H_error
//                       (from 10000) leaking back up by the ERL times 0.005
//                       (initial) or 0.00005 when the refined filter beats the
//                       coarse one, 0.5 / 0.05 when it doesn't; floor 0.001,
//                       ceiling 2; bins under the noise gate 20075344 don't
//                       adapt.
//   CoarseFilterUpdateGain   mu = 0.9 (initial) or 0.7 over the render power.
//   Subtractor          runs both filters on the capture block, rescales the
//                       refined filter when its error grows beyond the
//                       capture (misadjustment), copies the refined filter
//                       into the coarse one after 5 blocks where the coarse
//                       one is worse, and holds the refined leakage at the
//                       converged rate for 25 blocks after that.
//   RenderSignalAnalyzer  masks the update around narrow-band render peaks
//                       and stops it when the render excitation is poor.
//
// The FFT is libwebrtc's 128-point Ooura FFT (sm/audio/ooura_fft.h, from
// common_audio/third_party/ooura/fft_size_128), as aec3_fft.cc runs it.
// Differences that remain (tools/catalog.py 6.1): the portable C paths,
// where x86 libwebrtc runs SSE2/AVX2 (same arithmetic, different float
// summation order). Several render channels are read as channel 0 for the
// delay estimate (AlignmentMixer's downmix/adaptive selection isn't ported).
// No data dumps or render-jitter logging. Subtractor takes the capture
// saturation flag that AecState holds; AecState itself, the residual echo
// suppression and EchoCanceller3 are #240's next steps, so nothing calls this
// module in a call yet.
#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/aec3_delay.h"
#include "sm/audio/ooura_fft.h"
#include "sm/core/metrics.h"

namespace sm::audio::aec3 {

// aec3_common.h
constexpr size_t kFftLengthBy2Plus1 = kFftLengthBy2 + 1;
constexpr size_t kFftLengthBy2Minus1 = kFftLengthBy2 - 1;
constexpr size_t kFftLength = 2 * kFftLengthBy2;
constexpr size_t kBlockSizeMs = kFftLengthBy2 * 1000 / 16000;

// log2 of in > 0 from its bits, in a fast and approximate manner.
inline float FastApproxLog2f(const float in) {
  float out = static_cast<float>(std::bit_cast<uint32_t>(in));
  out *= 1.1920929e-7f;  // 1/2^23
  out -= 126.942695f;    // Remove bias.
  return out;
}
constexpr size_t NumBandsForRate(int sample_rate_hz) { return static_cast<size_t>(sample_rate_hz / 16000); }
constexpr int GetTimeDomainLength(int filter_length_blocks) {
  return filter_length_blocks * static_cast<int>(kFftLengthBy2);
}
constexpr size_t GetRenderDelayBufferSize(size_t down_sampling_factor, size_t num_matched_filters,
                                          size_t filter_length_blocks) {
  return GetDownSampledBufferSize(down_sampling_factor, num_matched_filters) / (kBlockSize / down_sampling_factor) +
         filter_length_blocks + 1;
}

// EchoCanceller3Config::Filter with M157's defaults.
struct FilterConfig {
  struct RefinedConfiguration {
    size_t length_blocks;
    float leakage_converged;
    float leakage_diverged;
    float error_floor;
    float error_ceil;
    float noise_gate;
  };
  struct CoarseConfiguration {
    size_t length_blocks;
    float rate;
    float noise_gate;
  };
  RefinedConfiguration refined = {13, 0.00005f, 0.05f, 0.001f, 2.f, 20075344.f};
  CoarseConfiguration coarse = {13, 0.7f, 20075344.f};
  RefinedConfiguration refined_initial = {12, 0.005f, 0.5f, 0.001f, 2.f, 20075344.f};
  CoarseConfiguration coarse_initial = {12, 0.9f, 20075344.f};
  size_t config_change_duration_blocks = 250;
  float initial_state_seconds = 2.5f;
  int coarse_reset_hangover_blocks = 25;
  bool conservative_initial_phase = false;
  bool enable_coarse_filter_output_usage = true;
  bool use_linear_filter = true;
};

// EchoCanceller3Config::Erle with M157's defaults.
struct ErleConfig {
  float min = 1.f;
  float max_l = 4.f;
  float max_h = 1.5f;
  bool onset_detection = true;
  size_t num_sections = 1;
  bool clamp_quality_estimate_to_zero = true;
  bool clamp_quality_estimate_to_one = true;
};

// EchoCanceller3Config::EpStrength with M157's defaults.
struct EpStrengthConfig {
  float default_gain = 1.f;
  float default_len = 0.83f;
  float nearend_len = 0.83f;
  bool echo_can_saturate = true;
  bool bounded_erl = false;
  bool erle_onset_compensation_in_dominant_nearend = false;
  bool use_conservative_tail_frequency_response = true;
};

// EchoCanceller3Config::EchoAudibility with M157's defaults.
struct EchoAudibilityConfig {
  float low_render_limit = 4 * 64.f;
  float normal_render_limit = 64.f;
  float floor_power = 2 * 64.f;
  float audibility_threshold_lf = 10;
  float audibility_threshold_mf = 10;
  float audibility_threshold_hf = 10;
  bool use_stationarity_properties = false;
  bool use_stationarity_properties_at_init = false;
};

// EchoCanceller3Config::EchoRemovalControl with M157's defaults.
struct EchoRemovalControlConfig {
  bool has_clock_drift = false;
  bool linear_and_stable_echo_path = false;
};

// EchoCanceller3Config::EchoModel with M157's defaults.
struct EchoModelConfig {
  size_t noise_floor_hold = 50;
  float min_noise_floor_power = 1638400.f;
  float stationary_gate_slope = 10.f;
  float noise_gate_power = 27509.42f;
  float noise_gate_slope = 0.3f;
  size_t render_pre_window_size = 1;
  size_t render_post_window_size = 1;
  bool model_reverb_in_nonlinear_mode = true;
};

// EchoCanceller3Config::ComfortNoise with M157's defaults.
struct ComfortNoiseConfig {
  float noise_floor_dbfs = -96.03406f;
};

// EchoCanceller3Config::Suppressor with M157's defaults.
struct SuppressorConfig {
  struct MaskingThresholds {
    float enr_transparent;
    float enr_suppress;
    float emr_transparent;
  };
  struct Tuning {
    MaskingThresholds mask_lf;
    MaskingThresholds mask_hf;
    float max_inc_factor;
    float max_dec_factor_lf;
  };
  size_t nearend_average_blocks = 4;
  Tuning normal_tuning = {{.3f, .4f, .3f}, {.07f, .1f, .3f}, 2.0f, 0.25f};
  Tuning nearend_tuning = {{1.09f, 1.1f, .3f}, {.1f, .3f, .3f}, 2.0f, 0.25f};
  bool lf_smoothing_during_initial_phase = true;
  int last_permanent_lf_smoothing_band = 0;
  int last_lf_smoothing_band = 5;
  int last_lf_band = 5;
  int first_hf_band = 8;
  struct DominantNearendDetection {
    float enr_threshold = .25f;
    float enr_exit_threshold = 10.f;
    float snr_threshold = 30.f;
    int hold_duration = 50;
    int trigger_threshold = 12;
    bool use_during_initial_phase = true;
    bool use_unbounded_echo_spectrum = true;
  } dominant_nearend_detection;
  struct SubbandNearendDetection {
    size_t nearend_average_blocks = 1;
    struct SubbandRegion {
      size_t low;
      size_t high;
    };
    SubbandRegion subband1 = {1, 1};
    SubbandRegion subband2 = {1, 1};
    float nearend_threshold = 1.f;
    float snr_threshold = 1.f;
  } subband_nearend_detection;
  bool use_subband_nearend_detection = false;
  struct HighBandsSuppression {
    float enr_threshold = 1.f;
    float max_gain_during_echo = 1.f;
    float anti_howling_activation_threshold = 400.f;
    float anti_howling_gain = 1.f;
  } high_bands_suppression;
  struct HighFrequencySuppression {
    int limiting_gain_band = 16;
    int bands_in_limiting_gain = 1;
  } high_frequency_suppression;
  float floor_first_increase = 0.00001f;
  bool conservative_hf_suppression = false;
};

// The parts of EchoCanceller3Config this module reads.
struct Aec3Config {
  DelayConfig delay;
  FilterConfig filter;
  ErleConfig erle;
  EpStrengthConfig ep_strength;
  EchoAudibilityConfig echo_audibility;
  EchoRemovalControlConfig echo_removal_control;
  EchoModelConfig echo_model;
  ComfortNoiseConfig comfort_noise;
  SuppressorConfig suppressor;
  // config.render_levels
  float active_render_limit = 100.f;
  float poor_excitation_render_limit = 150.f;
  float poor_excitation_render_limit_ds8 = 20.f;
  float render_power_gain_db = 0.f;
  // config.delay
  int fixed_capture_delay_samples = 0;
};

// fft_data.h
struct FftData {
  void Assign(const FftData& src) {
    re = src.re;
    im = src.im;
    im[0] = im[kFftLengthBy2] = 0;
  }
  void Clear() {
    re.fill(0.f);
    im.fill(0.f);
  }
  // |X|^2 per bin.
  void Spectrum(std::span<float> power_spectrum) const {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) power_spectrum[k] = re[k] * re[k] + im[k] * im[k];
  }
  void CopyFromPackedArray(const std::array<float, kFftLength>& v);
  void CopyToPackedArray(std::array<float, kFftLength>* v) const;
  std::array<float, kFftLengthBy2Plus1> re{};
  std::array<float, kFftLengthBy2Plus1> im{};
};

// aec3_fft.h
class Aec3Fft {
 public:
  enum class Window { kRectangular, kHanning, kSqrtHanning };
  Aec3Fft();
  Aec3Fft(const Aec3Fft&) = delete;
  Aec3Fft& operator=(const Aec3Fft&) = delete;
  // Forward FFT of x (overwritten).
  void Fft(std::array<float, kFftLength>* x, FftData* X) const;
  // Inverse FFT, unscaled (x is 64 times the signal).
  void Ifft(const FftData& X, std::array<float, kFftLength>* x) const;
  // FFT of 64 zeros followed by x (windowed by kRectangular or kHanning).
  void ZeroPaddedFft(std::span<const float> x, Window window, FftData* X) const;
  // FFT of x_old followed by x (kRectangular or kSqrtHanning).
  void PaddedFft(std::span<const float> x, std::span<const float> x_old, Window window, FftData* X) const;
  void PaddedFft(std::span<const float> x, std::span<const float> x_old, FftData* X) const {
    PaddedFft(x, x_old, Window::kRectangular, X);
  }

 private:
  const ooura::OouraFft128 ooura_fft_{};
};

// block.h: bands x channels x 64 samples.
class Block {
 public:
  Block(int num_bands, int num_channels, float default_value = 0.0f)
      : num_bands_(num_bands),
        num_channels_(num_channels),
        data_(static_cast<size_t>(num_bands * num_channels) * kBlockSize, default_value) {}
  int NumBands() const { return num_bands_; }
  int NumChannels() const { return num_channels_; }
  std::span<float, kBlockSize> View(int band, int channel) {
    return std::span<float, kBlockSize>(&data_[GetIndex(band, channel)], kBlockSize);
  }
  std::span<const float, kBlockSize> View(int band, int channel) const {
    return std::span<const float, kBlockSize>(&data_[GetIndex(band, channel)], kBlockSize);
  }

 private:
  size_t GetIndex(int band, int channel) const {
    return static_cast<size_t>(band * num_channels_ + channel) * kBlockSize;
  }
  int num_bands_;
  int num_channels_;
  std::vector<float> data_;
};

// The rings of block_buffer.h, spectrum_buffer.h and fft_buffer.h.
template <typename T>
struct RingBuffer {
  RingBuffer(size_t n, const T& init) : size(static_cast<int>(n)), buffer(n, init) {}
  int IncIndex(int index) const { return index < size - 1 ? index + 1 : 0; }
  int DecIndex(int index) const { return index > 0 ? index - 1 : size - 1; }
  int OffsetIndex(int index, int offset) const { return (size + index + offset) % size; }
  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void IncWriteIndex() { write = IncIndex(write); }
  void DecWriteIndex() { write = DecIndex(write); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
  void IncReadIndex() { read = IncIndex(read); }
  void DecReadIndex() { read = DecIndex(read); }
  const int size;
  std::vector<T> buffer;
  int write = 0;
  int read = 0;
};
struct BlockBuffer : RingBuffer<Block> {
  BlockBuffer(size_t n, size_t num_bands, size_t num_channels)
      : RingBuffer(n, Block(static_cast<int>(num_bands), static_cast<int>(num_channels))) {}
};
struct SpectrumBuffer : RingBuffer<std::vector<std::array<float, kFftLengthBy2Plus1>>> {
  SpectrumBuffer(size_t n, size_t num_channels)
      : RingBuffer(n, std::vector<std::array<float, kFftLengthBy2Plus1>>(num_channels)) {}
};
struct FftBuffer : RingBuffer<std::vector<FftData>> {
  FftBuffer(size_t n, size_t num_channels) : RingBuffer(n, std::vector<FftData>(num_channels)) {}
};

// render_buffer.h: the delayed view of the render rings that the filters read.
class RenderBuffer {
 public:
  RenderBuffer(BlockBuffer* block_buffer, SpectrumBuffer* spectrum_buffer, FftBuffer* fft_buffer)
      : block_buffer_(block_buffer), spectrum_buffer_(spectrum_buffer), fft_buffer_(fft_buffer) {}
  RenderBuffer(const RenderBuffer&) = delete;
  RenderBuffer& operator=(const RenderBuffer&) = delete;
  // The block buffer_offset_blocks after the read position (negative: older).
  const Block& GetBlock(int buffer_offset_blocks) const {
    return block_buffer_->buffer[block_buffer_->OffsetIndex(block_buffer_->read, buffer_offset_blocks)];
  }
  // The spectra buffer_offset_ffts after the read position (positive: older).
  std::span<const std::array<float, kFftLengthBy2Plus1>> Spectrum(int buffer_offset_ffts) const {
    return spectrum_buffer_->buffer[spectrum_buffer_->OffsetIndex(spectrum_buffer_->read, buffer_offset_ffts)];
  }
  std::span<const std::vector<FftData>> GetFftBuffer() const { return fft_buffer_->buffer; }
  size_t Position() const { return static_cast<size_t>(fft_buffer_->read); }
  // Sum of the num_spectra most recent spectra over the channels.
  void SpectralSum(size_t num_spectra, std::array<float, kFftLengthBy2Plus1>* X2) const;
  void SpectralSums(size_t num_spectra_shorter, size_t num_spectra_longer,
                    std::array<float, kFftLengthBy2Plus1>* X2_shorter,
                    std::array<float, kFftLengthBy2Plus1>* X2_longer) const;
  bool GetRenderActivity() const { return render_activity_; }
  void SetRenderActivity(bool activity) { render_activity_ = activity; }
  // Blocks between the write and read positions.
  int Headroom() const {
    return fft_buffer_->write <= fft_buffer_->read ? fft_buffer_->read - fft_buffer_->write
                                                   : fft_buffer_->size - fft_buffer_->write + fft_buffer_->read;
  }
  const SpectrumBuffer& GetSpectrumBuffer() const { return *spectrum_buffer_; }
  const BlockBuffer& GetBlockBuffer() const { return *block_buffer_; }

 private:
  const BlockBuffer* const block_buffer_;
  const SpectrumBuffer* const spectrum_buffer_;
  const FftBuffer* const fft_buffer_;
  bool render_activity_ = false;
};

// render_delay_buffer.cc (RenderDelayBufferImpl): the whole buffer, of which
// RenderDelayBufferLowRate (aec3_delay.h) is the decimated half.
class RenderDelayBuffer : public MetricsSource {
 public:
  enum class BufferingEvent { kNone, kRenderUnderrun, kRenderOverrun };
  RenderDelayBuffer(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels);
  RenderDelayBuffer(const RenderDelayBuffer&) = delete;
  RenderDelayBuffer& operator=(const RenderDelayBuffer&) = delete;
  void Reset();
  // One render block (all bands, 64 samples each).
  BufferingEvent Insert(const Block& block);
  // Called once per capture block, before the filters run.
  BufferingEvent PrepareCaptureProcessing();
  void HandleSkippedCaptureProcessing() { ++capture_call_counter_; }
  // Sets the delay (in blocks, from RenderDelayController). Returns whether
  // it changed.
  bool AlignFromDelay(size_t delay);
  void AlignFromExternalDelay();
  size_t Delay() const { return static_cast<size_t>(ComputeDelay()); }
  size_t MaxDelay() const { return blocks_.buffer.size() - 1 - static_cast<size_t>(buffer_headroom_); }
  RenderBuffer* GetRenderBuffer() { return &echo_remover_buffer_; }
  const DownsampledRenderBuffer& GetDownsampledRenderBuffer() const { return low_rate_; }
  int BufferLatency() const;
  // The audio buffer delay that APM's set_stream_delay_ms reports.
  void SetAudioBufferDelay(int delay_ms);
  bool HasReceivedBufferDelay() const { return external_audio_buffer_delay_.has_value(); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  int MapDelayToTotalDelay(size_t delay) const { return BufferLatency() + static_cast<int>(delay); }
  int ComputeDelay() const;
  void ApplyTotalDelay(int delay);
  void InsertBlock(const Block& block, int previous_write);
  bool DetectActiveRender(std::span<const float> x) const;
  bool DetectExcessRenderBlocks();
  void IncrementWriteIndices();
  void IncrementLowRateReadIndices() { low_rate_.UpdateReadIndex(-sub_block_size_); }
  void IncrementReadIndices();
  bool RenderOverrun() const { return low_rate_.read == low_rate_.write || blocks_.read == blocks_.write; }
  bool RenderUnderrun() const { return low_rate_.read == low_rate_.write; }

  const Aec3Config config_;
  const float render_linear_amplitude_gain_;
  const size_t down_sampling_factor_;
  const int sub_block_size_;
  BlockBuffer blocks_;
  SpectrumBuffer spectra_;
  FftBuffer ffts_;
  std::optional<size_t> delay_;
  RenderBuffer echo_remover_buffer_;
  DownsampledRenderBuffer low_rate_;
  Decimator render_decimator_;
  const Aec3Fft fft_;
  std::vector<float> render_ds_;
  const int buffer_headroom_;
  bool last_call_was_render_ = false;
  int num_api_calls_in_a_row_ = 0;
  int max_observed_jitter_ = 1;
  int64_t capture_call_counter_ = 0;
  int64_t render_call_counter_ = 0;
  bool render_activity_ = false;
  size_t render_activity_counter_ = 0;
  std::optional<int> external_audio_buffer_delay_;
  bool external_audio_buffer_delay_verified_after_reset_ = false;
  size_t min_latency_blocks_ = 0;
  size_t excess_render_detection_counter_ = 0;
  uint64_t underruns_ = 0;
  uint64_t overruns_ = 0;
};

// adaptive_fir_filter.cc (the portable C paths).
void ComputeFrequencyResponse(size_t num_partitions, const std::vector<std::vector<FftData>>& H,
                              std::vector<std::array<float, kFftLengthBy2Plus1>>* H2);
void AdaptPartitions(const RenderBuffer& render_buffer, const FftData& G, size_t num_partitions,
                     std::vector<std::vector<FftData>>* H);
void ApplyFilter(const RenderBuffer& render_buffer, size_t num_partitions,
                 const std::vector<std::vector<FftData>>& H, FftData* S);

class AdaptiveFirFilter {
 public:
  AdaptiveFirFilter(size_t max_size_partitions, size_t initial_size_partitions, size_t size_change_duration_blocks,
                    size_t num_render_channels);
  AdaptiveFirFilter(const AdaptiveFirFilter&) = delete;
  AdaptiveFirFilter& operator=(const AdaptiveFirFilter&) = delete;
  // Produces the filter output S.
  void Filter(const RenderBuffer& render_buffer, FftData* S) const;
  // Adapts with the gain G and constrains one partition, updating its part of
  // the time-domain impulse response.
  void Adapt(const RenderBuffer& render_buffer, const FftData& G, std::vector<float>* impulse_response);
  void Adapt(const RenderBuffer& render_buffer, const FftData& G);
  // Zeroes the partitions beyond the current size.
  void HandleEchoPathChange();
  size_t SizePartitions() const { return current_size_partitions_; }
  // Moves the size to `size`, at once or over size_change_duration_blocks.
  void SetSizePartitions(size_t size, bool immediate_effect);
  // |H|^2 per partition (the maximum over render channels).
  void ComputeFrequencyResponse(std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) const;
  size_t max_filter_size_partitions() const { return max_size_partitions_; }
  void ScaleFilter(float factor);
  void SetFilter(size_t num_partitions, const std::vector<std::vector<FftData>>& H);
  const std::vector<std::vector<FftData>>& GetFilter() const { return H_; }

 private:
  void AdaptAndUpdateSize(const RenderBuffer& render_buffer, const FftData& G);
  void Constrain();
  void ConstrainAndUpdateImpulseResponse(std::vector<float>* impulse_response);
  void UpdateSize();

  const Aec3Fft fft_;
  const size_t num_render_channels_;
  const size_t max_size_partitions_;
  const int size_change_duration_blocks_;
  float one_by_size_change_duration_blocks_;
  size_t current_size_partitions_;
  size_t target_size_partitions_;
  size_t old_target_size_partitions_;
  int size_change_counter_ = 0;
  std::vector<std::vector<FftData>> H_;
  size_t partition_to_constrain_ = 0;
};

// adaptive_fir_filter_erl.cc: the echo return loss, sum of |H|^2 over the
// partitions.
void ComputeErl(const std::vector<std::array<float, kFftLengthBy2Plus1>>& H2, std::span<float> erl);

// echo_path_variability.h
struct EchoPathVariability {
  enum class DelayAdjustment { kNone, kBufferFlush, kNewDetectedDelay };
  EchoPathVariability(bool gain, DelayAdjustment delay, bool drift)
      : gain_change(gain), delay_change(delay), clock_drift(drift) {}
  bool AudioPathChanged() const { return gain_change || delay_change != DelayAdjustment::kNone; }
  bool gain_change;
  DelayAdjustment delay_change;
  bool clock_drift;
};

// render_signal_analyzer.h
class RenderSignalAnalyzer {
 public:
  explicit RenderSignalAnalyzer(const Aec3Config& config = {});
  void Update(const RenderBuffer& render_buffer, const std::optional<size_t>& delay_partitions);
  // Whether a narrow band has lasted more than 10 blocks.
  bool PoorSignalExcitation() const;
  // Zeroes v around narrow bands that have lasted more than 5 blocks.
  void MaskRegionsAroundNarrowBands(std::array<float, kFftLengthBy2Plus1>* v) const;
  std::optional<int> NarrowPeakBand() const { return narrow_peak_band_; }

 private:
  const int strong_peak_freeze_duration_;
  std::array<size_t, kFftLengthBy2 - 1> narrow_band_counters_{};
  std::optional<int> narrow_peak_band_;
  size_t narrow_peak_counter_ = 0;
};

// subtractor_output.h
struct SubtractorOutput {
  std::array<float, kBlockSize> s_refined{};
  std::array<float, kBlockSize> s_coarse{};
  std::array<float, kBlockSize> e_refined{};
  std::array<float, kBlockSize> e_coarse{};
  FftData E_refined;
  std::array<float, kFftLengthBy2Plus1> E2_refined{};
  std::array<float, kFftLengthBy2Plus1> E2_coarse{};
  float s2_refined = 0.f;
  float s2_coarse = 0.f;
  float e2_refined = 0.f;
  float e2_coarse = 0.f;
  float y2 = 0.f;
  float s_refined_max_abs = 0.f;
  float s_coarse_max_abs = 0.f;
  void Reset();
  void ComputeMetrics(std::span<const float> y);
};

// refined_filter_update_gain.h
class RefinedFilterUpdateGain {
 public:
  RefinedFilterUpdateGain(const FilterConfig::RefinedConfiguration& config, size_t config_change_duration_blocks);
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability);
  void Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
               const RenderSignalAnalyzer& render_signal_analyzer, const SubtractorOutput& subtractor_output,
               std::span<const float> erl, size_t size_partitions, bool saturated_capture_signal,
               bool disallow_leakage_diverged, FftData* gain_fft);
  void SetConfig(const FilterConfig::RefinedConfiguration& config, bool immediate_effect);

 private:
  void UpdateCurrentConfig();
  const int config_change_duration_blocks_;
  float one_by_config_change_duration_blocks_;
  FilterConfig::RefinedConfiguration current_config_;
  FilterConfig::RefinedConfiguration target_config_;
  FilterConfig::RefinedConfiguration old_target_config_;
  std::array<float, kFftLengthBy2Plus1> H_error_;
  size_t poor_excitation_counter_;
  size_t call_counter_ = 0;
  int config_change_counter_ = 0;
};

// coarse_filter_update_gain.h
class CoarseFilterUpdateGain {
 public:
  CoarseFilterUpdateGain(const FilterConfig::CoarseConfiguration& config, size_t config_change_duration_blocks);
  void HandleEchoPathChange();
  void Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
               const RenderSignalAnalyzer& render_signal_analyzer, const FftData& E_coarse, size_t size_partitions,
               bool saturated_capture_signal, FftData* G);
  void SetConfig(const FilterConfig::CoarseConfiguration& config, bool immediate_effect);

 private:
  void UpdateCurrentConfig();
  FilterConfig::CoarseConfiguration current_config_;
  FilterConfig::CoarseConfiguration target_config_;
  FilterConfig::CoarseConfiguration old_target_config_;
  const int config_change_duration_blocks_;
  float one_by_config_change_duration_blocks_;
  size_t poor_signal_excitation_counter_ = 0;
  size_t call_counter_ = 0;
  int config_change_counter_ = 0;
};

// subtractor.h: the refined and coarse adaptive filters of each capture
// channel.
class Subtractor : public MetricsSource {
 public:
  Subtractor(const Aec3Config& config, size_t num_render_channels, size_t num_capture_channels);
  Subtractor(const Subtractor&) = delete;
  Subtractor& operator=(const Subtractor&) = delete;
  // `capture` is band 0 of each capture channel. `saturated_capture` is
  // AecState::SaturatedCapture().
  void Process(const RenderBuffer& render_buffer, const Block& capture,
               const RenderSignalAnalyzer& render_signal_analyzer, bool saturated_capture,
               std::span<SubtractorOutput> outputs);
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability);
  // Moves from the initial filter configuration to the regular one.
  void ExitInitialState();
  const std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>>& FilterFrequencyResponses() const {
    return refined_frequency_responses_;
  }
  const std::vector<std::vector<float>>& FilterImpulseResponses() const { return refined_impulse_responses_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  class FilterMisadjustmentEstimator {
   public:
    void Update(const SubtractorOutput& output);
    float GetMisadjustment() const { return 2.f / std::sqrt(inv_misadjustment_); }
    bool IsAdjustmentNeeded() const { return inv_misadjustment_ > 10.f; }
    void Reset();

   private:
    const int n_blocks_ = 4;
    int n_blocks_acum_ = 0;
    float e2_acum_ = 0.f;
    float y2_acum_ = 0.f;
    float inv_misadjustment_ = 0.f;
    int overhang_ = 0;
  };

  const Aec3Fft fft_;
  const Aec3Config config_;
  const size_t num_capture_channels_;
  std::vector<std::unique_ptr<AdaptiveFirFilter>> refined_filters_;
  std::vector<std::unique_ptr<AdaptiveFirFilter>> coarse_filter_;
  std::vector<std::unique_ptr<RefinedFilterUpdateGain>> refined_gains_;
  std::vector<std::unique_ptr<CoarseFilterUpdateGain>> coarse_gains_;
  std::vector<FilterMisadjustmentEstimator> filter_misadjustment_estimators_;
  std::vector<size_t> poor_coarse_filter_counters_;
  std::vector<int> coarse_filter_reset_hangover_;
  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> refined_frequency_responses_;
  std::vector<std::vector<float>> refined_impulse_responses_;
  uint64_t misadjustment_rescales_ = 0;
  uint64_t coarse_filter_resets_ = 0;
  uint64_t echo_path_resets_ = 0;
};

}  // namespace sm::audio::aec3
