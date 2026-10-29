// AEC3 as a whole: compare/webrtc/reference/modules/audio_processing/aec3/
// {frame_blocker,block_framer,block_delay_buffer,echo_remover_metrics,
// echo_remover,block_processor,echo_canceller3}.cc (M157). See
// sm/audio/aec3.h.
#include "sm/audio/aec3.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sm::audio::aec3 {

// --- frame_blocker.cc ---------------------------------------------------------

FrameBlocker::FrameBlocker(size_t num_bands, size_t num_channels)
    : num_bands_(num_bands), num_channels_(num_channels), buffer_(num_bands_, std::vector<std::vector<float>>(num_channels)) {
  for (auto& band : buffer_) {
    for (auto& channel : band) channel.reserve(kBlockSize);
  }
}

void FrameBlocker::InsertSubFrameAndExtractBlock(const SubFrameView& sub_frame, Block* block) {
  for (size_t band = 0; band < num_bands_; ++band) {
    for (size_t channel = 0; channel < num_channels_; ++channel) {
      auto& buf = buffer_[band][channel];
      const auto& sub = sub_frame[band][channel];
      auto out = block->View(static_cast<int>(band), static_cast<int>(channel));
      const size_t samples_to_block = kBlockSize - buf.size();
      std::copy(buf.begin(), buf.end(), out.begin());
      std::copy(sub.begin(), sub.begin() + static_cast<std::ptrdiff_t>(samples_to_block),
                out.begin() + static_cast<std::ptrdiff_t>(kBlockSize - samples_to_block));
      buf.clear();
      buf.insert(buf.begin(), sub.begin() + static_cast<std::ptrdiff_t>(samples_to_block), sub.end());
    }
  }
}

bool FrameBlocker::IsBlockAvailable() const { return kBlockSize == buffer_[0][0].size(); }

void FrameBlocker::ExtractBlock(Block* block) {
  for (size_t band = 0; band < num_bands_; ++band) {
    for (size_t channel = 0; channel < num_channels_; ++channel) {
      auto& buf = buffer_[band][channel];
      std::copy(buf.begin(), buf.end(), block->View(static_cast<int>(band), static_cast<int>(channel)).begin());
      buf.clear();
    }
  }
}

// --- block_framer.cc ----------------------------------------------------------

BlockFramer::BlockFramer(size_t num_bands, size_t num_channels)
    : num_bands_(num_bands),
      num_channels_(num_channels),
      buffer_(num_bands_, std::vector<std::vector<float>>(num_channels, std::vector<float>(kBlockSize, 0.f))) {}

// All the constants are chosen so that the buffer is either empty or has
// enough samples for InsertBlockAndExtractSubFrame to produce a frame. In
// order to achieve this, the InsertBlockAndExtractSubFrame and InsertBlock
// methods need to be called in the correct order.
void BlockFramer::InsertBlock(const Block& block) {
  for (size_t band = 0; band < num_bands_; ++band) {
    for (size_t channel = 0; channel < num_channels_; ++channel) {
      auto in = block.View(static_cast<int>(band), static_cast<int>(channel));
      auto& buf = buffer_[band][channel];
      buf.insert(buf.begin(), in.begin(), in.end());
    }
  }
}

void BlockFramer::InsertBlockAndExtractSubFrame(const Block& block, SubFrameView* sub_frame) {
  for (size_t band = 0; band < num_bands_; ++band) {
    for (size_t channel = 0; channel < num_channels_; ++channel) {
      auto in = block.View(static_cast<int>(band), static_cast<int>(channel));
      auto& buf = buffer_[band][channel];
      auto& out = (*sub_frame)[band][channel];
      const size_t samples_to_frame = kSubFrameLength - buf.size();
      std::copy(buf.begin(), buf.end(), out.begin());
      std::copy(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(samples_to_frame),
                out.begin() + static_cast<std::ptrdiff_t>(buf.size()));
      buf.clear();
      buf.insert(buf.begin(), in.begin() + static_cast<std::ptrdiff_t>(samples_to_frame), in.end());
    }
  }
}

// --- block_delay_buffer.cc ----------------------------------------------------

BlockDelayBuffer::BlockDelayBuffer(size_t num_channels, size_t num_bands, size_t frame_length, size_t delay_samples)
    : frame_length_(frame_length),
      delay_(delay_samples),
      buf_(num_channels, std::vector<std::vector<float>>(num_bands, std::vector<float>(delay_, 0.f))) {}

void BlockDelayBuffer::DelaySignal(Frame* frame) {
  if (delay_ == 0) return;
  const size_t num_bands = buf_[0].size();
  const size_t num_channels = buf_.size();
  const size_t i_start = last_insert_;
  size_t i = 0;
  for (size_t ch = 0; ch < num_channels; ++ch) {
    for (size_t band = 0; band < num_bands; ++band) {
      i = i_start;
      float* buf_ch_band = buf_[ch][band].data();
      float* frame_ch_band = (*frame)[band][ch].data();
      for (size_t k = 0; k < frame_length_; ++k) {
        const float tmp = buf_ch_band[i];
        buf_ch_band[i] = frame_ch_band[k];
        frame_ch_band[k] = tmp;
        i = i < delay_ - 1 ? i + 1 : 0;
      }
    }
  }
  last_insert_ = i;
}

// --- echo_remover_metrics.cc --------------------------------------------------

void EchoRemoverMetrics::DbMetric::Update(float value) {
  sum_value += value;
  floor_value = std::min(floor_value, value);
  ceil_value = std::max(ceil_value, value);
}

void EchoRemoverMetrics::DbMetric::UpdateInstant(float value) {
  sum_value = value;
  floor_value = std::min(floor_value, value);
  ceil_value = std::max(ceil_value, value);
}

void EchoRemoverMetrics::ResetMetrics() {
  erl_time_domain_ = DbMetric(0.f, 10000.f, 0.000f);
  erle_time_domain_ = DbMetric(0.f, 1000.f, 0.f);
  saturated_capture_ = false;
}

void EchoRemoverMetrics::Update(const AecState& aec_state) {
  metrics_reported_ = false;
  if (++block_counter_ <= kMetricsCollectionBlocks) {
    erl_time_domain_.UpdateInstant(aec_state.ErlTimeDomain());
    erle_time_domain_.UpdateInstant(aec_state.FullBandErleLog2());
    saturated_capture_ = saturated_capture_ || aec_state.SaturatedCapture();
  } else {
    // Report the metrics over several frames in order to lower the impact of
    // the logarithms involved on the computational complexity.
    switch (block_counter_) {
      case kMetricsCollectionBlocks + 1:
        reported_usable_linear_estimate_ = aec_state.UsableLinearEstimate();
        break;
      case kMetricsCollectionBlocks + 2:
        reported_erl_ = TransformDbMetricForReporting(true, 0.f, 59.f, 30.f, 1.f, erl_time_domain_.sum_value);
        break;
      case kMetricsCollectionBlocks + 3:
        reported_erle_ = TransformDbMetricForReporting(false, 0.f, 19.f, 0.f, 1.f, erle_time_domain_.sum_value);
        metrics_reported_ = true;
        block_counter_ = 0;
        ResetMetrics();
        break;
      default:
        break;
    }
  }
}

void UpdateDbMetric(const std::array<float, kFftLengthBy2Plus1>& value,
                    std::array<EchoRemoverMetrics::DbMetric, 2>* statistic) {
  // Truncation is intended in the band width computation.
  constexpr int kNumBands = 2;
  constexpr int kBandWidth = 65 / kNumBands;
  constexpr float kOneByBandWidth = 1.f / kBandWidth;
  for (size_t k = 0; k < statistic->size(); ++k) {
    float average_band = std::accumulate(value.begin() + kBandWidth * static_cast<int>(k),
                                         value.begin() + kBandWidth * static_cast<int>(k + 1), 0.f) *
                         kOneByBandWidth;
    (*statistic)[k].Update(average_band);
  }
}

int TransformDbMetricForReporting(bool negate, float min_value, float max_value, float offset, float scaling,
                                  float value) {
  float new_value = 10.f * std::log10(value * scaling + 1e-10f) + offset;
  if (negate) new_value = -new_value;
  return static_cast<int>(std::clamp(new_value, min_value, max_value));
}

// --- echo_remover.cc ----------------------------------------------------------

namespace {

// Computes the linear echo power S2 = |Y - E|^2 from the capture and the
// linear filter output spectra.
void LinearEchoPower(const FftData& E, const FftData& Y, std::array<float, kFftLengthBy2Plus1>* S2) {
  for (size_t k = 0; k < E.re.size(); ++k) {
    (*S2)[k] = (Y.re[k] - E.re[k]) * (Y.re[k] - E.re[k]) + (Y.im[k] - E.im[k]) * (Y.im[k] - E.im[k]);
  }
}

// Fades between two input signals using a fix-sized transition.
void SignalTransition(std::span<const float> from, std::span<const float> to, std::span<float> out) {
  if (from.data() == to.data()) {
    std::copy(to.begin(), to.end(), out.begin());
  } else {
    constexpr size_t kTransitionSize = 30;
    constexpr float kOneByTransitionSizePlusOne = 1.f / (kTransitionSize + 1);
    for (size_t k = 0; k < kTransitionSize; ++k) {
      float a = static_cast<float>(k + 1) * kOneByTransitionSizePlusOne;
      out[k] = a * to[k] + (1.f - a) * from[k];
    }
    std::copy(to.begin() + kTransitionSize, to.end(), out.begin() + kTransitionSize);
  }
}

// Computes a windowed (square root Hanning) padded FFT and updates the related
// memory.
void WindowedPaddedFft(const Aec3Fft& fft, std::span<const float> v, std::span<float> v_old, FftData* V) {
  fft.PaddedFft(v, v_old, Aec3Fft::Window::kSqrtHanning, V);
  std::copy(v.begin(), v.end(), v_old.begin());
}

// Whether the refined filter output should be used (otherwise the coarse
// one's).
bool UseRefinedOutput(const SubtractorOutput& subtractor_output) {
  if (subtractor_output.e2_coarse < 0.9f * subtractor_output.e2_refined &&
      subtractor_output.y2 > 30.f * 30.f * kBlockSize &&
      (subtractor_output.s2_refined > 60.f * 60.f * kBlockSize ||
       subtractor_output.s2_coarse > 60.f * 60.f * kBlockSize)) {
    return false;
  }
  if (subtractor_output.e2_coarse < subtractor_output.e2_refined &&
      subtractor_output.y2 < subtractor_output.e2_refined) {
    return false;
  }
  return true;
}

// Forms the linear filter output by smoothly transitioning between the
// refined and coarse filter outputs.
void FormLinearFilterOutput(bool refined_filter_output_last_selected, bool use_refined_output,
                            const SubtractorOutput& subtractor_output, std::span<float> output) {
  SignalTransition(refined_filter_output_last_selected ? subtractor_output.e_refined : subtractor_output.e_coarse,
                   use_refined_output ? subtractor_output.e_refined : subtractor_output.e_coarse, output);
}

}  // namespace

EchoRemover::EchoRemover(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels,
                         size_t num_capture_channels)
    : config_(config),
      sample_rate_hz_(sample_rate_hz),
      num_render_channels_(num_render_channels),
      num_capture_channels_(num_capture_channels),
      use_coarse_filter_output_(config_.filter.enable_coarse_filter_output_usage),
      subtractor_(config, num_render_channels_, num_capture_channels_),
      suppression_gain_(config_, sample_rate_hz, num_capture_channels),
      cng_(config_, num_capture_channels_),
      suppression_filter_(sample_rate_hz_, num_capture_channels_),
      render_signal_analyzer_(config_),
      residual_echo_estimator_(config_, num_render_channels),
      aec_state_(config_, num_capture_channels_),
      e_old_(num_capture_channels_, std::array<float, kFftLengthBy2>{}),
      y_old_(num_capture_channels_, std::array<float, kFftLengthBy2>{}),
      e_(num_capture_channels_),
      Y2_(num_capture_channels_),
      E2_(num_capture_channels_),
      R2_(num_capture_channels_),
      R2_unbounded_(num_capture_channels_),
      S2_linear_(num_capture_channels_),
      Y_(num_capture_channels_),
      E_(num_capture_channels_),
      comfort_noise_(num_capture_channels_),
      high_band_comfort_noise_(num_capture_channels_),
      subtractor_output_(num_capture_channels_) {}

void EchoRemover::GetMetrics(EchoControlMetrics* metrics) const {
  // Echo return loss (ERL) is inverted to go from gain to attenuation.
  metrics->echo_return_loss = -10.0 * std::log10(aec_state_.ErlTimeDomain());
  metrics->echo_return_loss_enhancement = Log2TodB(aec_state_.FullBandErleLog2());
}

void EchoRemover::ProcessCapture(EchoPathVariability echo_path_variability, bool capture_signal_saturation,
                                 const std::optional<DelayEstimate>& external_delay, RenderBuffer* render_buffer,
                                 Block* linear_output, Block* capture) {
  ++block_counter_;
  const Block& x = render_buffer->GetBlock(0);
  Block* y = capture;

  std::span<std::array<float, kFftLengthBy2>> e(e_);
  std::span<std::array<float, kFftLengthBy2Plus1>> Y2(Y2_), E2(E2_), R2(R2_), R2_unbounded(R2_unbounded_),
      S2_linear(S2_linear_);
  std::span<FftData> Y(Y_), E(E_), comfort_noise(comfort_noise_), high_band_comfort_noise(high_band_comfort_noise_);
  std::span<SubtractorOutput> subtractor_output(subtractor_output_);

  aec_state_.UpdateCaptureSaturation(capture_signal_saturation);

  if (echo_path_variability.AudioPathChanged()) {
    // Ensure that the gain change is only acted on once per frame.
    if (echo_path_variability.gain_change) {
      if (gain_change_hangover_ == 0) {
        constexpr int kMaxBlocksPerFrame = 3;
        gain_change_hangover_ = kMaxBlocksPerFrame;
        ++gain_changes_;
      } else {
        echo_path_variability.gain_change = false;
      }
    }

    subtractor_.HandleEchoPathChange(echo_path_variability);
    aec_state_.HandleEchoPathChange(echo_path_variability);

    if (echo_path_variability.delay_change != EchoPathVariability::DelayAdjustment::kNone) {
      suppression_gain_.SetInitialState(true);
      ++delay_changes_;
    }
  }
  if (gain_change_hangover_ > 0) --gain_change_hangover_;

  // Analyze the render signal.
  render_signal_analyzer_.Update(*render_buffer, static_cast<size_t>(aec_state_.MinDirectPathFilterDelay()));

  // State transition.
  if (aec_state_.TransitionTriggered()) {
    subtractor_.ExitInitialState();
    suppression_gain_.SetInitialState(false);
  }

  // Perform linear echo cancellation.
  subtractor_.Process(*render_buffer, *y, render_signal_analyzer_, aec_state_.SaturatedCapture(),
                      subtractor_output);

  // Compute spectra.
  bool use_refined_output;
  if (use_coarse_filter_output_) {
    use_refined_output = false;
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      if (UseRefinedOutput(subtractor_output[ch])) {
        use_refined_output = true;
        break;
      }
    }
  } else {
    use_refined_output = true;
  }
  if (!use_refined_output) ++coarse_output_blocks_;
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    FormLinearFilterOutput(refined_filter_output_last_selected_, use_refined_output, subtractor_output[ch], e[ch]);
    WindowedPaddedFft(fft_, y->View(/*band=*/0, static_cast<int>(ch)), y_old_[ch], &Y[ch]);
    WindowedPaddedFft(fft_, e[ch], e_old_[ch], &E[ch]);
    LinearEchoPower(E[ch], Y[ch], &S2_linear[ch]);
    Y[ch].Spectrum(Y2[ch]);
    E[ch].Spectrum(E2[ch]);
  }
  refined_filter_output_last_selected_ = use_refined_output;

  // Compute the nearend spectrum the suppressor reads. libwebrtc binds it
  // here, before the AecState update below, so the choice follows the
  // previous block's state.
  const bool nearend_is_linear_output = aec_state_.UsableLinearEstimate();
  const std::span<std::array<float, kFftLengthBy2Plus1>> nearend_spectrum = nearend_is_linear_output ? E2 : Y2;

  // Optionally return the linear filter output.
  if (linear_output) {
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      std::copy(e[ch].begin(), e[ch].end(), linear_output->View(/*band=*/0, static_cast<int>(ch)).begin());
    }
  }

  // Update the AEC state information.
  aec_state_.Update(external_delay, subtractor_.FilterFrequencyResponses(), subtractor_.FilterImpulseResponses(),
                    *render_buffer, E2, Y2, subtractor_output);

  // Choose the linear output.
  const std::span<const FftData> Y_fft = aec_state_.UseLinearFilterOutput() ? E : Y;

  const bool usable_linear_estimate = aec_state_.UsableLinearEstimate();

  // Estimate the comfort noise.
  cng_.Compute(aec_state_.SaturatedCapture(), nearend_spectrum, comfort_noise, high_band_comfort_noise);

  // Only do the below processing if the output of the audio processing module
  // is used.
  std::array<float, kFftLengthBy2Plus1> G;
  if (capture_output_used_) {
    // Estimate the residual echo power.
    residual_echo_estimator_.Estimate(aec_state_, *render_buffer, S2_linear, Y2, E2,
                                      suppression_gain_.IsDominantNearend(), R2, R2_unbounded);

    // Suppressor nearend estimate.
    if (usable_linear_estimate) {
      // E2 is bound by Y2.
      for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
        std::transform(E2[ch].begin(), E2[ch].end(), Y2[ch].begin(), E2[ch].begin(),
                       [](float a, float b) { return std::min(a, b); });
      }
    }
    // Suppressor echo estimate.
    const auto& echo_spectrum = usable_linear_estimate ? S2_linear : R2;

    // Determine if the suppressor should assume clock drift.
    const bool clock_drift = config_.echo_removal_control.has_clock_drift || echo_path_variability.clock_drift;

    // Compute the preferred gain.
    float high_bands_gain;
    suppression_gain_.GetGain(config_.suppressor, /*config_changed=*/false, nearend_spectrum, echo_spectrum, R2,
                              R2_unbounded, cng_.NoiseSpectrum(), render_signal_analyzer_, aec_state_, x,
                              clock_drift, &high_bands_gain, &G);

    suppression_filter_.ApplyGain(comfort_noise, high_band_comfort_noise, G, high_bands_gain, Y_fft, y);
  } else {
    G.fill(0.f);
  }

  // Update the metrics.
  metrics_.Update(aec_state_);
}

void EchoRemover::ExportMetrics(MetricsWriter& w) const {
  w.Counter("blocks", block_counter_);
  w.Counter("coarse_output_blocks", coarse_output_blocks_);
  w.Counter("gain_changes", gain_changes_);
  w.Counter("delay_changes", delay_changes_);
  w.Gauge("reported_erl_db", metrics_.ReportedErl());
  w.Gauge("reported_erle_db", metrics_.ReportedErle());
  aec_state_.ExportMetrics(w);
  subtractor_.ExportMetrics(w);
  residual_echo_estimator_.ExportMetrics(w);
  suppression_gain_.ExportMetrics(w);
}

// --- block_processor.cc -------------------------------------------------------

BlockProcessor::BlockProcessor(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels,
                               size_t num_capture_channels)
    : config_(config),
      sample_rate_hz_(sample_rate_hz),
      render_buffer_(config, sample_rate_hz, num_render_channels),
      delay_controller_(config.delay),
      echo_remover_(config, sample_rate_hz, num_render_channels, num_capture_channels) {}

void BlockProcessor::ProcessCapture(bool echo_path_gain_change, bool capture_signal_saturation, Block* linear_output,
                                    Block* capture_block) {
  capture_call_counter_++;

  if (render_properly_started_) {
    if (!capture_properly_started_) {
      capture_properly_started_ = true;
      render_buffer_.Reset();
      delay_controller_.Reset(true);
    }
  } else {
    // If no render data has yet arrived, do not process the capture signal.
    render_buffer_.HandleSkippedCaptureProcessing();
    ++skipped_capture_blocks_;
    return;
  }

  EchoPathVariability echo_path_variability(echo_path_gain_change, EchoPathVariability::DelayAdjustment::kNone,
                                            false);

  if (render_event_ == RenderDelayBuffer::BufferingEvent::kRenderOverrun && render_properly_started_) {
    echo_path_variability.delay_change = EchoPathVariability::DelayAdjustment::kBufferFlush;
    delay_controller_.Reset(true);
    ++buffer_flushes_;
  }
  render_event_ = RenderDelayBuffer::BufferingEvent::kNone;

  // Update the render buffers with any newly arrived render blocks and prepare
  // the render buffers for reading the render data corresponding to the
  // current capture block.
  RenderDelayBuffer::BufferingEvent buffer_event = render_buffer_.PrepareCaptureProcessing();
  // Reset the delay controller at render buffer underrun.
  if (buffer_event == RenderDelayBuffer::BufferingEvent::kRenderUnderrun) {
    delay_controller_.Reset(false);
    ++render_underruns_;
  }

  // Compute and apply the render delay required to achieve proper signal
  // alignment.
  estimated_delay_ =
      delay_controller_.GetDelay(render_buffer_.GetDownsampledRenderBuffer(), capture_block->View(0, 0));

  if (estimated_delay_) {
    bool delay_change = render_buffer_.AlignFromDelay(estimated_delay_->delay);
    if (delay_change) {
      echo_path_variability.delay_change = EchoPathVariability::DelayAdjustment::kNewDetectedDelay;
      ++delay_changes_;
    }
  }

  echo_path_variability.clock_drift = delay_controller_.HasClockdrift();

  // Remove the echo from the capture signal.
  echo_remover_.ProcessCapture(echo_path_variability, capture_signal_saturation, estimated_delay_,
                               render_buffer_.GetRenderBuffer(), linear_output, capture_block);
}

void BlockProcessor::BufferRender(const Block& block) {
  render_event_ = render_buffer_.Insert(block);
  render_properly_started_ = true;
}

void BlockProcessor::GetMetrics(EchoControlMetrics* metrics) const {
  echo_remover_.GetMetrics(metrics);
  constexpr int block_size_ms = 4;
  metrics->delay_ms = static_cast<int>(render_buffer_.Delay()) * block_size_ms;
}

void BlockProcessor::ExportMetrics(MetricsWriter& w) const {
  w.Counter("capture_blocks", capture_call_counter_);
  w.Counter("skipped_capture_blocks", skipped_capture_blocks_);
  w.Counter("buffer_flushes", buffer_flushes_);
  w.Counter("render_underruns", render_underruns_);
  w.Counter("delay_changes_blocks", delay_changes_);
  w.Gauge("delay_blocks", estimated_delay_ ? static_cast<double>(estimated_delay_->delay) : -1.0);
  render_buffer_.ExportMetrics(w);
  delay_controller_.ExportMetrics(w);
  echo_remover_.ExportMetrics(w);
}

// --- echo_canceller3.cc -------------------------------------------------------

namespace {

bool DetectSaturation(std::span<const float> y) {
  for (float v : y) {
    if (v >= 32700.0f || v <= -32700.0f) return true;
  }
  return false;
}

Frame MakeFrame(size_t num_bands, size_t num_channels) {
  return Frame(num_bands, std::vector<std::vector<float>>(num_channels, std::vector<float>(kFrameSize, 0.f)));
}

}  // namespace

EchoCanceller3::EchoCanceller3(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels,
                               size_t num_capture_channels)
    : config_(config),
      valid_(ValidFullBandRate(sample_rate_hz)),
      sample_rate_hz_(valid_ ? sample_rate_hz : 16000),
      num_bands_(NumBandsForRate(sample_rate_hz_)),
      num_render_input_channels_(num_render_channels),
      num_capture_channels_(num_capture_channels),
      output_framer_(num_bands_, num_capture_channels_),
      capture_blocker_(num_bands_, num_capture_channels_),
      render_blocker_(num_bands_, num_render_channels_to_aec_),
      render_queue_output_frame_(MakeFrame(num_bands_, num_render_input_channels_)),
      render_block_(static_cast<int>(num_bands_), static_cast<int>(num_render_channels_to_aec_)),
      capture_block_(static_cast<int>(num_bands_), static_cast<int>(num_capture_channels_)),
      render_sub_frame_view_(num_bands_, std::vector<std::span<float>>(num_render_channels_to_aec_)),
      capture_sub_frame_view_(num_bands_, std::vector<std::span<float>>(num_capture_channels_)),
      block_processor_(std::make_unique<BlockProcessor>(config_, sample_rate_hz_, num_render_channels_to_aec_,
                                                        num_capture_channels_)),
      mono_frame_(MakeFrame(1, 1)) {
  if (config_.fixed_capture_delay_samples > 0) {
    block_delay_buffer_ = std::make_unique<BlockDelayBuffer>(
        num_capture_channels_, num_bands_, kFrameSize, static_cast<size_t>(config_.fixed_capture_delay_samples));
  }
}

void EchoCanceller3::FillSubFrameView(Frame* frame, size_t sub_frame_index, size_t num_channels,
                                      SubFrameView* view) const {
  // With more frame channels than view channels (the render before stereo is
  // detected), channel 0 is used: libwebrtc downmixes only while temporary
  // stereo content is detected, which needs the content detector.
  for (size_t band = 0; band < view->size(); ++band) {
    for (size_t channel = 0; channel < num_channels; ++channel) {
      (*view)[band][channel] =
          std::span<float>(&(*frame)[band][channel][sub_frame_index * kSubFrameLength], kSubFrameLength);
    }
  }
}

void EchoCanceller3::AnalyzeRender(const Frame& render) {
  if (!valid_) return;
  // SwapQueue::Insert fails (and the frame is lost) when the queue is full.
  if (render_transfer_queue_.size() >= kRenderTransferQueueSizeFrames) {
    ++render_frames_dropped_;
    return;
  }
  render_transfer_queue_.push_back(render);
}

void EchoCanceller3::AnalyzeCapture(std::span<const std::span<const float>> capture_channels) {
  if (!valid_) return;
  saturated_microphone_signal_ = false;
  for (const auto& channel : capture_channels) {
    saturated_microphone_signal_ |= DetectSaturation(channel);
    if (saturated_microphone_signal_) break;
  }
  if (saturated_microphone_signal_) ++saturated_frames_;
}

void EchoCanceller3::ProcessCapture(Frame* capture, bool level_change) {
  if (!valid_) return;
  if (block_delay_buffer_) block_delay_buffer_->DelaySignal(capture);

  EmptyRenderQueue();

  // The blocks of the two 80-sample sub-frames, then the block left over
  // every fourth frame.
  for (size_t sub_frame_index = 0; sub_frame_index < 2; ++sub_frame_index) {
    FillSubFrameView(capture, sub_frame_index, num_capture_channels_, &capture_sub_frame_view_);
    capture_blocker_.InsertSubFrameAndExtractBlock(capture_sub_frame_view_, &capture_block_);
    block_processor_->ProcessCapture(/*echo_path_gain_change=*/level_change, saturated_microphone_signal_,
                                     /*linear_output=*/nullptr, &capture_block_);
    output_framer_.InsertBlockAndExtractSubFrame(capture_block_, &capture_sub_frame_view_);
  }
  if (capture_blocker_.IsBlockAvailable()) {
    capture_blocker_.ExtractBlock(&capture_block_);
    block_processor_->ProcessCapture(/*echo_path_gain_change=*/level_change, saturated_microphone_signal_,
                                     /*linear_output=*/nullptr, &capture_block_);
    output_framer_.InsertBlock(capture_block_);
  }
}

void EchoCanceller3::EmptyRenderQueue() {
  while (!render_transfer_queue_.empty()) {
    render_queue_output_frame_.swap(render_transfer_queue_.front());
    render_transfer_queue_.pop_front();
    for (size_t sub_frame_index = 0; sub_frame_index < 2; ++sub_frame_index) {
      FillSubFrameView(&render_queue_output_frame_, sub_frame_index, num_render_channels_to_aec_,
                       &render_sub_frame_view_);
      render_blocker_.InsertSubFrameAndExtractBlock(render_sub_frame_view_, &render_block_);
      block_processor_->BufferRender(render_block_);
    }
    if (render_blocker_.IsBlockAvailable()) {
      render_blocker_.ExtractBlock(&render_block_);
      block_processor_->BufferRender(render_block_);
    }
  }
}

void EchoCanceller3::AnalyzeRender(std::span<const float> render) {
  if (!valid_ || num_bands_ != 1 || render.size() != kFrameSize) return;
  std::copy(render.begin(), render.end(), mono_frame_[0][0].begin());
  AnalyzeRender(mono_frame_);
}

void EchoCanceller3::ProcessCapture(std::span<float> capture, bool level_change) {
  if (!valid_ || num_bands_ != 1 || num_capture_channels_ != 1 || capture.size() != kFrameSize) return;
  const std::span<const float> channels[1] = {capture};
  AnalyzeCapture(channels);
  std::copy(capture.begin(), capture.end(), mono_frame_[0][0].begin());
  ProcessCapture(&mono_frame_, level_change);
  std::copy(mono_frame_[0][0].begin(), mono_frame_[0][0].end(), capture.begin());
}

EchoControlMetrics EchoCanceller3::GetMetrics() const {
  EchoControlMetrics metrics;
  block_processor_->GetMetrics(&metrics);
  return metrics;
}

void EchoCanceller3::ExportMetrics(MetricsWriter& w) const {
  w.Counter("render_frames_dropped", render_frames_dropped_);
  w.Counter("saturated_capture_frames", saturated_frames_);
  const EchoControlMetrics m = GetMetrics();
  w.Gauge("erl_db", m.echo_return_loss);
  w.Gauge("erle_db", m.echo_return_loss_enhancement);
  w.Gauge("delay_ms", m.delay_ms);
  block_processor_->ExportMetrics(w);
}

}  // namespace sm::audio::aec3
