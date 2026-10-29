// 6.1 AEC3, libwebrtc's echo canceller, as a whole (`apm v2`, #240 step 5):
// the frame/block plumbing and the processing chain that runs the pieces of
// steps 1-4 on each 64-sample block, ported from
// compare/webrtc/reference/modules/audio_processing/aec3/
// {frame_blocker,block_framer,block_delay_buffer,echo_remover_metrics,
// echo_remover,block_processor,echo_canceller3}.{h,cc} (M157), with
// EchoCanceller3Config's defaults (Aec3Config).
//
//   EchoCanceller3      what APM calls once per 10 ms frame: AnalyzeRender
//                       queues the render frame (split bands, 100 frames
//                       deep), AnalyzeCapture flags a saturated microphone
//                       (any full-band sample at or over 32700), and
//                       ProcessCapture first buffers the queued render, then
//                       cuts the capture frame into 64-sample blocks (two
//                       80-sample sub-frames per frame, a third block every
//                       fourth frame) and puts the processed blocks back.
//   BlockProcessor      per block: waits until render has started, resets
//                       the render buffer and the delay controller when
//                       capture starts and at a render overrun (a buffer
//                       flush) or underrun, aligns the render buffer to the
//                       delay controller's estimate (a new delay is an echo
//                       path change), then runs the echo remover.
//   EchoRemover         per block: the subtractor's refined and coarse
//                       filters (the coarse one's output is used when it is
//                       clearly better, with a 30-sample crossfade), the
//                       sqrt-Hanning spectra of the capture Y and of the
//                       linear output E, the linear echo S2 = |Y - E|^2,
//                       AecState, comfort noise, the residual echo, the
//                       suppression gain and the suppression filter, which
//                       writes the output from E when the linear filter is
//                       usable and from Y otherwise. A gain change holds
//                       further gain changes off for 3 blocks.
//
// Differences that remain (tools/catalog.py 6.1): the render is used as one
// channel (channel 0), as libwebrtc does until its MultiChannelContentDetector
// finds proper stereo; that detector, ConfigSelector and the multichannel
// config aren't ported. The field trials that AdjustConfig reads are read as
// not set (M157's default), so the config is used as given. No neural
// residual echo estimator, no linear AEC output export (off by default), no
// UMA histograms (BlockProcessorMetrics, ApiCallJitterMetrics, and
// EchoRemoverMetrics's reporting, whose values are exported as metrics
// instead), no data dumps. No render transfer queue thread hand-off: the
// queue is a FIFO of the same depth. The render reference isn't high-passed
// (config.filter.high_pass_filter_echo_reference is false in M157).
#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/aec3_delay.h"
#include "sm/audio/aec3_filter.h"
#include "sm/audio/aec3_state.h"
#include "sm/audio/aec3_suppression.h"
#include "sm/core/metrics.h"

namespace sm::audio::aec3 {

// aec3_common.h
constexpr size_t kSubFrameLength = 80;
constexpr size_t kMaxNumBands = 3;
constexpr size_t kFrameSize = 160;  // AudioBuffer::kSplitBandSize.
constexpr size_t kRenderTransferQueueSizeFrames = 100;
constexpr bool ValidFullBandRate(int sample_rate_hz) {
  return sample_rate_hz == 16000 || sample_rate_hz == 32000 || sample_rate_hz == 48000;
}

// [band][channel][sample] views of one sub-frame (80 samples).
using SubFrameView = std::vector<std::vector<std::span<float>>>;
// [band][channel][160 samples]: one 10 ms frame in split bands.
using Frame = std::vector<std::vector<std::vector<float>>>;

// frame_blocker.h: 80-sample sub-frames in, 64-sample blocks out.
class FrameBlocker {
 public:
  FrameBlocker(size_t num_bands, size_t num_channels);
  // Inserts one sub-frame and extracts one block.
  void InsertSubFrameAndExtractBlock(const SubFrameView& sub_frame, Block* block);
  // Whether 64 samples are left over (every fourth frame).
  bool IsBlockAvailable() const;
  void ExtractBlock(Block* block);

 private:
  const size_t num_bands_;
  const size_t num_channels_;
  std::vector<std::vector<std::vector<float>>> buffer_;
};

// block_framer.h: 64-sample blocks in, 80-sample sub-frames out. Starts with
// one block of zeros (the 4 ms of delay the blocking adds).
class BlockFramer {
 public:
  BlockFramer(size_t num_bands, size_t num_channels);
  // Adds a block without extracting a sub-frame (after IsBlockAvailable).
  void InsertBlock(const Block& block);
  void InsertBlockAndExtractSubFrame(const Block& block, SubFrameView* sub_frame);

 private:
  const size_t num_bands_;
  const size_t num_channels_;
  std::vector<std::vector<std::vector<float>>> buffer_;
};

// block_delay_buffer.h: delays the capture by a fixed number of samples
// (config.delay.fixed_capture_delay_samples; 0, so unused, by default).
class BlockDelayBuffer {
 public:
  BlockDelayBuffer(size_t num_channels, size_t num_bands, size_t frame_length, size_t delay_samples);
  // `frame` is [band][channel][frame_length].
  void DelaySignal(Frame* frame);

 private:
  const size_t frame_length_;
  const size_t delay_;
  std::vector<std::vector<std::vector<float>>> buf_;  // [channel][band].
  size_t last_insert_ = 0;
};

// echo_remover_metrics.h: the ERL/ERLE statistics libwebrtc reports to UMA
// every 10 s (kMetricsReportingIntervalBlocks), kept here as metrics.
class EchoRemoverMetrics {
 public:
  struct DbMetric {
    DbMetric() : DbMetric(0.f, 0.f, 0.f) {}
    DbMetric(float sum, float floor, float ceil) : sum_value(sum), floor_value(floor), ceil_value(ceil) {}
    void Update(float value);
    void UpdateInstant(float value);
    float sum_value;
    float floor_value;
    float ceil_value;
  };
  static constexpr int kMetricsReportingIntervalBlocks = 10 * kNumBlocksPerSecond;
  static constexpr int kMetricsCollectionBlocks = kMetricsReportingIntervalBlocks - 3;

  EchoRemoverMetrics() { ResetMetrics(); }
  void Update(const AecState& aec_state);
  // Whether the last Update completed a reporting interval.
  bool MetricsReported() const { return metrics_reported_; }
  // The values libwebrtc would have reported at the last interval.
  int ReportedErl() const { return reported_erl_; }
  int ReportedErle() const { return reported_erle_; }
  bool ReportedUsableLinearEstimate() const { return reported_usable_linear_estimate_; }

 private:
  void ResetMetrics();
  int block_counter_ = 0;
  DbMetric erl_time_domain_;
  DbMetric erle_time_domain_;
  bool saturated_capture_ = false;
  bool metrics_reported_ = false;
  int reported_erl_ = 0;
  int reported_erle_ = 0;
  bool reported_usable_linear_estimate_ = false;
};

// Updates the two half-band averages of `value` in `statistic`.
void UpdateDbMetric(const std::array<float, kFftLengthBy2Plus1>& value,
                    std::array<EchoRemoverMetrics::DbMetric, 2>* statistic);
// 10 log10(value * scaling) + offset, negated if asked, clamped and truncated.
int TransformDbMetricForReporting(bool negate, float min_value, float max_value, float offset, float scaling,
                                  float value);

// EchoControl::Metrics.
struct EchoControlMetrics {
  double echo_return_loss = 0.0;
  double echo_return_loss_enhancement = 0.0;
  int delay_ms = 0;
};

// echo_remover.cc (EchoRemoverImpl).
class EchoRemover : public MetricsSource {
 public:
  EchoRemover(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels,
              size_t num_capture_channels);
  EchoRemover(const EchoRemover&) = delete;
  EchoRemover& operator=(const EchoRemover&) = delete;
  void GetMetrics(EchoControlMetrics* metrics) const;
  // Removes the echo from one capture block (all bands), in place.
  // `linear_output` (band 0 only) may be null.
  void ProcessCapture(EchoPathVariability echo_path_variability, bool capture_signal_saturation,
                      const std::optional<DelayEstimate>& external_delay, RenderBuffer* render_buffer,
                      Block* linear_output, Block* capture);
  void UpdateEchoLeakageStatus(bool leakage_detected) { echo_leakage_detected_ = leakage_detected; }
  // When the capture output is not used, the suppression is skipped.
  void SetCaptureOutputUsage(bool capture_output_used) { capture_output_used_ = capture_output_used; }
  const AecState& aec_state() const { return aec_state_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  const Aec3Config config_;
  const Aec3Fft fft_;
  const int sample_rate_hz_;
  const size_t num_render_channels_;
  const size_t num_capture_channels_;
  const bool use_coarse_filter_output_;
  Subtractor subtractor_;
  SuppressionGain suppression_gain_;
  ComfortNoiseGenerator cng_;
  SuppressionFilter suppression_filter_;
  RenderSignalAnalyzer render_signal_analyzer_;
  ResidualEchoEstimator residual_echo_estimator_;
  bool echo_leakage_detected_ = false;
  bool capture_output_used_ = true;
  AecState aec_state_;
  EchoRemoverMetrics metrics_;
  std::vector<std::array<float, kFftLengthBy2>> e_old_;
  std::vector<std::array<float, kFftLengthBy2>> y_old_;
  size_t block_counter_ = 0;
  int gain_change_hangover_ = 0;
  bool refined_filter_output_last_selected_ = true;
  // Per capture channel scratch (libwebrtc's stack/heap arrays).
  std::vector<std::array<float, kFftLengthBy2>> e_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2_, E2_, R2_, R2_unbounded_, S2_linear_;
  std::vector<FftData> Y_, E_, comfort_noise_, high_band_comfort_noise_;
  std::vector<SubtractorOutput> subtractor_output_;
  uint64_t coarse_output_blocks_ = 0;
  uint64_t gain_changes_ = 0;
  uint64_t delay_changes_ = 0;
};

// block_processor.cc (BlockProcessorImpl), with libwebrtc's own delay
// controller (config.delay.use_external_delay_estimator is false).
class BlockProcessor : public MetricsSource {
 public:
  BlockProcessor(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels,
                 size_t num_capture_channels);
  BlockProcessor(const BlockProcessor&) = delete;
  BlockProcessor& operator=(const BlockProcessor&) = delete;
  void ProcessCapture(bool echo_path_gain_change, bool capture_signal_saturation, Block* linear_output,
                      Block* capture_block);
  void BufferRender(const Block& block);
  void UpdateEchoLeakageStatus(bool leakage_detected) { echo_remover_.UpdateEchoLeakageStatus(leakage_detected); }
  void GetMetrics(EchoControlMetrics* metrics) const;
  void SetAudioBufferDelay(int delay_ms) { render_buffer_.SetAudioBufferDelay(delay_ms); }
  void SetCaptureOutputUsage(bool capture_output_used) { echo_remover_.SetCaptureOutputUsage(capture_output_used); }
  const EchoRemover& echo_remover() const { return echo_remover_; }
  const RenderDelayBuffer& render_buffer() const { return render_buffer_; }
  std::optional<DelayEstimate> estimated_delay() const { return estimated_delay_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  const Aec3Config config_;
  bool capture_properly_started_ = false;
  bool render_properly_started_ = false;
  const int sample_rate_hz_;
  RenderDelayBuffer render_buffer_;
  RenderDelayController delay_controller_;
  EchoRemover echo_remover_;
  RenderDelayBuffer::BufferingEvent render_event_ = RenderDelayBuffer::BufferingEvent::kNone;
  size_t capture_call_counter_ = 0;
  std::optional<DelayEstimate> estimated_delay_;
  uint64_t buffer_flushes_ = 0;
  uint64_t render_underruns_ = 0;
  uint64_t delay_changes_ = 0;
  uint64_t skipped_capture_blocks_ = 0;
};

// echo_canceller3.h: the EchoControl that APM runs.
class EchoCanceller3 : public MetricsSource {
 public:
  // `sample_rate_hz` is the full-band rate: 16000, 32000 or 48000
  // (ValidFullBandRate; libwebrtc DCHECKs it, and APM only ever hands AEC3
  // one of these). At any other rate the canceller is not valid() and every
  // call below leaves the audio untouched (#594).
  EchoCanceller3(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels,
                 size_t num_capture_channels);
  EchoCanceller3(const EchoCanceller3&) = delete;
  EchoCanceller3& operator=(const EchoCanceller3&) = delete;

  // One 10 ms render frame in split bands ([band][channel][160]), in the
  // S16 range (APM's float AudioBuffer).
  void AnalyzeRender(const Frame& render);
  // The full-band capture channels of one 10 ms frame, before band
  // splitting: detects saturation.
  void AnalyzeCapture(std::span<const std::span<const float>> capture_channels);
  // One 10 ms capture frame in split bands, processed in place.
  // `level_change` flags an echo path gain change (APM's volume changes).
  void ProcessCapture(Frame* capture, bool level_change);

  // Mono 16 kHz convenience: 160 samples in the S16 range. AnalyzeCapture
  // and ProcessCapture of one frame. A canceller at another rate or with
  // more capture channels, or a span of another length, leaves the audio
  // untouched.
  void AnalyzeRender(std::span<const float> render);
  void ProcessCapture(std::span<float> capture, bool level_change = false);

  // False when built for a rate ValidFullBandRate rejects.
  bool valid() const { return valid_; }
  EchoControlMetrics GetMetrics() const;
  void SetAudioBufferDelay(int delay_ms) { block_processor_->SetAudioBufferDelay(delay_ms); }
  void SetCaptureOutputUsage(bool capture_output_used) {
    block_processor_->SetCaptureOutputUsage(capture_output_used);
  }
  bool ActiveProcessing() const { return true; }
  const BlockProcessor& block_processor() const { return *block_processor_; }
  // Render frames lost because the queue was full.
  uint64_t render_frames_dropped() const { return render_frames_dropped_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  void EmptyRenderQueue();
  void FillSubFrameView(Frame* frame, size_t sub_frame_index, size_t num_channels, SubFrameView* view) const;

  const Aec3Config config_;
  const bool valid_;
  // The requested rate when valid_, else 16000, so that the state below is
  // consistent and never used.
  const int sample_rate_hz_;
  const size_t num_bands_;
  const size_t num_render_input_channels_;
  const size_t num_capture_channels_;
  // Render channels the AEC runs on: 1 until proper stereo is detected,
  // which isn't ported.
  const size_t num_render_channels_to_aec_ = 1;
  std::unique_ptr<BlockDelayBuffer> block_delay_buffer_;
  BlockFramer output_framer_;
  FrameBlocker capture_blocker_;
  FrameBlocker render_blocker_;
  std::deque<Frame> render_transfer_queue_;
  Frame render_queue_output_frame_;
  Block render_block_;
  Block capture_block_;
  SubFrameView render_sub_frame_view_;
  SubFrameView capture_sub_frame_view_;
  std::unique_ptr<BlockProcessor> block_processor_;
  bool saturated_microphone_signal_ = false;
  // Scratch for the mono convenience calls.
  Frame mono_frame_;
  uint64_t render_frames_dropped_ = 0;
  uint64_t saturated_frames_ = 0;
};

}  // namespace sm::audio::aec3
