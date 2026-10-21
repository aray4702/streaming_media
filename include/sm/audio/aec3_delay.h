// 6.2 Echo-path delay estimation as libwebrtc's AEC3 (`apm v2`, #240 step 1),
// ported from compare/webrtc/reference/modules/audio_processing/aec3/
// {aec3_common,decimator,downsampled_render_buffer,matched_filter,
// matched_filter_lag_aggregator,clockdrift_detector,echo_path_delay_estimator,
// render_delay_controller}.{h,cc} and the low-rate half of
// render_delay_buffer.cc (M157), with the defaults of
// EchoCanceller3Config::Delay and ::RenderLevels (api/audio/
// echo_canceller3_config.h of the pinned prebuilt, which is not in reference/).
//
//   Decimator           6th-order elliptic low-pass (factor 4) or a 5x
//                       Chebyshev band-pass (factor 8), then a 1 kHz high-pass
//                       (factor 4 only), then every 4th/8th sample: 64-sample
//                       16 kHz blocks become 16 (or 8) samples at 4 (2) kHz.
//   MatchedFilter       5 NLMS filters of 32 sub-blocks (512 taps at 4 kHz,
//                       128 ms each) at shifts of 24 sub-blocks, so together
//                       they cover 0-552 ms. Step 0.7, updated only when the
//                       render energy over the window exceeds 150^2 per tap and
//                       the capture isn't saturated. A filter's peak is a lag
//                       candidate when it is away from the filter ends and its
//                       error is below 0.2 of the capture energy; the filter
//                       with the lowest error wins. Pre-echo: the accumulated
//                       error per 4 taps finds an earlier part of the echo.
//   MatchedFilterLagAggregator  250-entry histogram of winning lags (minus
//                       the 32-sample headroom, 8 at 4 kHz); reports a coarse
//                       delay once a lag has more than 5 votes and a refined
//                       one once it has more than 20. With pre-echo detection
//                       on (the default) the reported delay is the pre-echo
//                       candidate, in blocks of 16 samples at 4 kHz.
//   EchoPathDelayEstimator  decimates the capture, runs the filters (slow
//                       smoothing, also 0.7, once a refined delay was found),
//                       aggregates, feeds the clock-drift detector, and soft
//                       resets the filters after 125 identical estimates.
//   RenderDelayController  turns the estimate (samples at 16 kHz) into a
//                       render buffer delay in 64-sample blocks with a
//                       hysteresis of 1 block once both estimates are refined.
//   RenderDelayBufferLowRate  the decimated render ring that the filters read:
//                       newest sample first, read index one sub-block ahead of
//                       write after a reset, overrun/underrun and excess-render
//                       detection (250 blocks, more than 8 blocks ahead).
//
// Differences that remain (tools/catalog.py 6.2): mono only (AlignmentMixer is
// fixed to channel 0, as libwebrtc's for one channel); the portable C
// MatchedFilterCore, where x86 libwebrtc runs the SSE2/AVX2 variants (same
// arithmetic, different float summation order); no RenderDelayControllerMetrics
// (UMA histograms only) or data dumps. The blocks, spectra and FFTs of
// RenderDelayBuffer and the delay it applies to them come with the adaptive
// filter (#240's next step); nothing calls this module in a call yet.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/metrics.h"

namespace sm::audio::aec3 {

// aec3_common.h
constexpr int kNumBlocksPerSecond = 250;
constexpr size_t kFftLengthBy2 = 64;
constexpr size_t kFftLengthBy2Log2 = 6;
constexpr size_t kBlockSize = kFftLengthBy2;
constexpr size_t kBlockSizeLog2 = kFftLengthBy2Log2;
constexpr size_t kMatchedFilterWindowSizeSubBlocks = 32;
constexpr size_t kMatchedFilterAlignmentShiftSizeSubBlocks = kMatchedFilterWindowSizeSubBlocks * 3 / 4;

constexpr size_t GetDownSampledBufferSize(size_t down_sampling_factor, size_t num_matched_filters) {
  return kBlockSize / down_sampling_factor *
         (kMatchedFilterAlignmentShiftSizeSubBlocks * num_matched_filters + kMatchedFilterWindowSizeSubBlocks + 1);
}

// The parts of EchoCanceller3Config that delay estimation reads, with M157's
// defaults.
struct DelayConfig {
  size_t default_delay = 5;
  size_t down_sampling_factor = 4;
  size_t num_filters = 5;
  size_t delay_headroom_samples = 32;
  size_t hysteresis_limit_blocks = 1;
  float delay_estimate_smoothing = 0.7f;
  float delay_estimate_smoothing_delay_found = 0.7f;
  float delay_candidate_detection_threshold = 0.2f;
  int delay_selection_threshold_initial = 5;
  int delay_selection_threshold_converged = 20;
  bool detect_pre_echo = true;
  // config.render_levels
  float poor_excitation_render_limit = 150.f;
  float poor_excitation_render_limit_ds8 = 20.f;
  // config.buffering
  size_t excess_render_detection_interval_blocks = 250;
  size_t max_allowed_excess_render_blocks = 8;
};

struct DelayEstimate {
  enum class Quality { kCoarse, kRefined };
  DelayEstimate(Quality q, size_t d) : quality(q), delay(d) {}
  Quality quality;
  size_t delay;
};

// modules/audio_processing/utility/cascaded_biquad_filter (direct form 1).
class CascadedBiQuadFilter {
 public:
  struct BiQuadCoefficients {
    float b[3];
    float a[2];
  };
  explicit CascadedBiQuadFilter(std::span<const BiQuadCoefficients> coefficients);
  void Process(std::span<const float> x, std::span<float> y);
  void Process(std::span<float> y);
  void Reset();

 private:
  struct BiQuad {
    BiQuadCoefficients coefficients;
    float x[2] = {0.f, 0.f};
    float y[2] = {0.f, 0.f};
  };
  static void ApplyBiQuad(std::span<const float> x, std::span<float> y, BiQuad& biquad);
  std::vector<BiQuad> biquads_;
};

class Decimator {
 public:
  explicit Decimator(size_t down_sampling_factor);
  // `in` is one 64-sample block, `out` 64 / factor samples.
  void Decimate(std::span<const float> in, std::span<float> out);

 private:
  const size_t down_sampling_factor_;
  CascadedBiQuadFilter anti_aliasing_filter_;
  CascadedBiQuadFilter noise_reduction_filter_;
};

struct DownsampledRenderBuffer {
  explicit DownsampledRenderBuffer(size_t downsampled_buffer_size)
      : size(static_cast<int>(downsampled_buffer_size)), buffer(downsampled_buffer_size, 0.f) {}
  int OffsetIndex(int index, int offset) const { return (size + index + offset) % size; }
  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
  const int size;
  std::vector<float> buffer;
  int write = 0;
  int read = 0;
};

void MatchedFilterCore(size_t x_start_index, float x2_sum_threshold, float smoothing, std::span<const float> x,
                       std::span<const float> y, std::span<float> h, bool* filters_updated, float* error_sum,
                       bool compute_accumulated_error, std::span<float> accumulated_error);

// Index of the largest h[k]^2 (the first one on ties).
size_t MaxSquarePeakIndex(std::span<const float> h);

class MatchedFilter {
 public:
  struct LagEstimate {
    LagEstimate() = default;
    LagEstimate(size_t l, size_t pre) : lag(l), pre_echo_lag(pre) {}
    size_t lag = 0;
    size_t pre_echo_lag = 0;
  };
  MatchedFilter(size_t sub_block_size, size_t window_size_sub_blocks, int num_matched_filters,
                size_t alignment_shift_sub_blocks, float excitation_limit, float smoothing_fast,
                float smoothing_slow, float matching_filter_threshold, bool detect_pre_echo);
  void Update(const DownsampledRenderBuffer& render_buffer, std::span<const float> capture,
              bool use_slow_smoothing);
  void Reset(bool full_reset);
  std::optional<LagEstimate> GetBestLagEstimate() const { return reported_lag_estimate_; }
  size_t GetMaxFilterLag() const { return filters_.size() * filter_intra_lag_shift_ + filters_[0].size(); }

 private:
  const size_t sub_block_size_;
  const size_t filter_intra_lag_shift_;
  std::vector<std::vector<float>> filters_;
  std::vector<std::vector<float>> accumulated_error_;
  std::vector<float> instantaneous_accumulated_error_;
  std::optional<LagEstimate> reported_lag_estimate_;
  std::optional<size_t> winner_lag_;
  int last_detected_best_lag_filter_ = -1;
  int number_pre_echo_updates_ = 0;
  const float excitation_limit_;
  const float smoothing_fast_;
  const float smoothing_slow_;
  const float matching_filter_threshold_;
  const bool detect_pre_echo_;
};

class MatchedFilterLagAggregator {
 public:
  MatchedFilterLagAggregator(size_t max_filter_lag, const DelayConfig& config);
  void Reset(bool hard_reset);
  std::optional<DelayEstimate> Aggregate(const std::optional<MatchedFilter::LagEstimate>& lag_estimate);
  bool ReliableDelayFound() const { return significant_candidate_found_; }
  int GetDelayAtHighestPeak() const { return highest_peak_aggregator_.candidate(); }

 private:
  class PreEchoLagAggregator {
   public:
    PreEchoLagAggregator(size_t max_filter_lag, size_t down_sampling_factor);
    void Reset();
    void Aggregate(int pre_echo_lag);
    int pre_echo_candidate() const { return pre_echo_candidate_; }

   private:
    const int block_size_log2_;
    std::array<int, 250> histogram_data_;
    std::vector<int> histogram_;
    int histogram_data_index_ = 0;
    int pre_echo_candidate_ = 0;
    int number_updates_ = 0;
  };
  class HighestPeakAggregator {
   public:
    explicit HighestPeakAggregator(size_t max_filter_lag);
    void Reset();
    void Aggregate(int lag);
    int candidate() const { return candidate_; }
    std::span<const int> histogram() const { return histogram_; }

   private:
    std::vector<int> histogram_;
    std::array<int, 250> histogram_data_;
    int histogram_data_index_ = 0;
    int candidate_ = -1;
  };

  bool significant_candidate_found_ = false;
  const int threshold_initial_;
  const int threshold_converged_;
  const int headroom_;
  HighestPeakAggregator highest_peak_aggregator_;
  std::unique_ptr<PreEchoLagAggregator> pre_echo_lag_aggregator_;
};

class ClockdriftDetector {
 public:
  enum class Level { kNone, kProbable, kVerified };
  void Update(int delay_estimate);
  Level ClockdriftLevel() const { return level_; }

 private:
  std::array<int, 3> delay_history_ = {0, 0, 0};
  Level level_ = Level::kNone;
  size_t stability_counter_ = 0;
};

class EchoPathDelayEstimator {
 public:
  explicit EchoPathDelayEstimator(const DelayConfig& config);
  // Resets the estimation. If the delay confidence is reset, the reset
  // behaviour is as if the call is restarted.
  void Reset(bool reset_delay_confidence) { Reset(true, reset_delay_confidence); }
  // `capture` is one 64-sample block (band 0, 16 kHz). Returns the delay in
  // samples at 16 kHz when the aggregator reports one.
  std::optional<DelayEstimate> EstimateDelay(const DownsampledRenderBuffer& render_buffer,
                                             std::span<const float> capture);
  ClockdriftDetector::Level Clockdrift() const { return clockdrift_detector_.ClockdriftLevel(); }

 private:
  void Reset(bool reset_lag_aggregator, bool reset_delay_confidence);
  const size_t down_sampling_factor_;
  const size_t sub_block_size_;
  Decimator capture_decimator_;
  MatchedFilter matched_filter_;
  MatchedFilterLagAggregator matched_filter_lag_aggregator_;
  std::optional<DelayEstimate> old_aggregated_lag_;
  size_t consistent_estimate_counter_ = 0;
  ClockdriftDetector clockdrift_detector_;
};

class RenderDelayController : public MetricsSource {
 public:
  explicit RenderDelayController(const DelayConfig& config = {});
  void Reset(bool reset_delay_confidence);
  // Returns the render buffer delay in 64-sample blocks.
  std::optional<DelayEstimate> GetDelay(const DownsampledRenderBuffer& render_buffer, std::span<const float> capture);
  bool HasClockdrift() const { return delay_estimator_.Clockdrift() != ClockdriftDetector::Level::kNone; }
  // The latest estimate from the delay estimator, in samples at 16 kHz.
  std::optional<DelayEstimate> delay_samples() const { return delay_samples_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  const int hysteresis_limit_blocks_;
  std::optional<DelayEstimate> delay_;
  EchoPathDelayEstimator delay_estimator_;
  std::optional<DelayEstimate> delay_samples_;
  size_t capture_call_counter_ = 0;
  DelayEstimate::Quality last_delay_estimate_quality_ = DelayEstimate::Quality::kCoarse;
  uint64_t delay_changes_ = 0;
};

// The low-rate (decimated) render path of RenderDelayBuffer: Insert per
// render block, PrepareCaptureProcessing per capture block.
class RenderDelayBufferLowRate {
 public:
  enum class BufferingEvent { kNone, kRenderUnderrun, kRenderOverrun };
  explicit RenderDelayBufferLowRate(const DelayConfig& config = {});
  void Reset();
  BufferingEvent Insert(std::span<const float> block);
  BufferingEvent PrepareCaptureProcessing();
  const DownsampledRenderBuffer& GetDownsampledRenderBuffer() const { return low_rate_; }
  // Unread sub-blocks: how far render is ahead of capture, in blocks.
  int BufferLatency() const;

 private:
  bool DetectExcessRenderBlocks();
  const DelayConfig config_;
  const int sub_block_size_;
  DownsampledRenderBuffer low_rate_;
  Decimator render_decimator_;
  std::vector<float> render_ds_;
  size_t min_latency_blocks_ = 0;
  size_t excess_render_detection_counter_ = 0;
};

}  // namespace sm::audio::aec3
