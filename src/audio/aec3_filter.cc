// AEC3's linear echo canceller, ported from libwebrtc M157's
// modules/audio_processing/aec3/ (see sm/audio/aec3_filter.h for the files).
#include "sm/audio/aec3_filter.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>


namespace sm::audio::aec3 {

// --- fft_data.h --------------------------------------------------------------

void FftData::CopyFromPackedArray(const std::array<float, kFftLength>& v) {
  re[0] = v[0];
  re[kFftLengthBy2] = v[1];
  im[0] = im[kFftLengthBy2] = 0;
  for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
    re[k] = v[j++];
    im[k] = v[j++];
  }
}

void FftData::CopyToPackedArray(std::array<float, kFftLength>* v) const {
  (*v)[0] = re[0];
  (*v)[1] = re[kFftLengthBy2];
  for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
    (*v)[j++] = re[k];
    (*v)[j++] = im[k];
  }
}

// --- aec3_fft.cc -------------------------------------------------------------

namespace {

const float kHanning64[kFftLengthBy2] = {
    0.f, 0.00248461f, 0.00991376f, 0.0222136f, 0.03926189f, 0.06088921f, 0.08688061f, 0.11697778f,
    0.15088159f, 0.1882551f, 0.22872687f, 0.27189467f, 0.31732949f, 0.36457977f, 0.41317591f, 0.46263495f,
    0.51246535f, 0.56217185f, 0.61126047f, 0.65924333f, 0.70564355f, 0.75f, 0.79187184f, 0.83084292f,
    0.86652594f, 0.89856625f, 0.92664544f, 0.95048443f, 0.96984631f, 0.98453864f, 0.99441541f, 0.99937846f,
    0.99937846f, 0.99441541f, 0.98453864f, 0.96984631f, 0.95048443f, 0.92664544f, 0.89856625f, 0.86652594f,
    0.83084292f, 0.79187184f, 0.75f, 0.70564355f, 0.65924333f, 0.61126047f, 0.56217185f, 0.51246535f,
    0.46263495f, 0.41317591f, 0.36457977f, 0.31732949f, 0.27189467f, 0.22872687f, 0.1882551f, 0.15088159f,
    0.11697778f, 0.08688061f, 0.06088921f, 0.03926189f, 0.0222136f, 0.00991376f, 0.00248461f, 0.f};

const float kSqrtHanning128[kFftLength] = {
    0.00000000000000f, 0.02454122852291f, 0.04906767432742f, 0.07356456359967f, 0.09801714032956f,
    0.12241067519922f, 0.14673047445536f, 0.17096188876030f, 0.19509032201613f, 0.21910124015687f,
    0.24298017990326f, 0.26671275747490f, 0.29028467725446f, 0.31368174039889f, 0.33688985339222f,
    0.35989503653499f, 0.38268343236509f, 0.40524131400499f, 0.42755509343028f, 0.44961132965461f,
    0.47139673682600f, 0.49289819222978f, 0.51410274419322f, 0.53499761988710f, 0.55557023301960f,
    0.57580819141785f, 0.59569930449243f, 0.61523159058063f, 0.63439328416365f, 0.65317284295378f,
    0.67155895484702f, 0.68954054473707f, 0.70710678118655f, 0.72424708295147f, 0.74095112535496f,
    0.75720884650648f, 0.77301045336274f, 0.78834642762661f, 0.80320753148064f, 0.81758481315158f,
    0.83146961230255f, 0.84485356524971f, 0.85772861000027f, 0.87008699110871f, 0.88192126434835f,
    0.89322430119552f, 0.90398929312344f, 0.91420975570353f, 0.92387953251129f, 0.93299279883474f,
    0.94154406518302f, 0.94952818059304f, 0.95694033573221f, 0.96377606579544f, 0.97003125319454f,
    0.97570213003853f, 0.98078528040323f, 0.98527764238894f, 0.98917650996478f, 0.99247953459871f,
    0.99518472667220f, 0.99729045667869f, 0.99879545620517f, 0.99969881869620f, 1.00000000000000f,
    0.99969881869620f, 0.99879545620517f, 0.99729045667869f, 0.99518472667220f, 0.99247953459871f,
    0.98917650996478f, 0.98527764238894f, 0.98078528040323f, 0.97570213003853f, 0.97003125319454f,
    0.96377606579544f, 0.95694033573221f, 0.94952818059304f, 0.94154406518302f, 0.93299279883474f,
    0.92387953251129f, 0.91420975570353f, 0.90398929312344f, 0.89322430119552f, 0.88192126434835f,
    0.87008699110871f, 0.85772861000027f, 0.84485356524971f, 0.83146961230255f, 0.81758481315158f,
    0.80320753148064f, 0.78834642762661f, 0.77301045336274f, 0.75720884650648f, 0.74095112535496f,
    0.72424708295147f, 0.70710678118655f, 0.68954054473707f, 0.67155895484702f, 0.65317284295378f,
    0.63439328416365f, 0.61523159058063f, 0.59569930449243f, 0.57580819141785f, 0.55557023301960f,
    0.53499761988710f, 0.51410274419322f, 0.49289819222978f, 0.47139673682600f, 0.44961132965461f,
    0.42755509343028f, 0.40524131400499f, 0.38268343236509f, 0.35989503653499f, 0.33688985339222f,
    0.31368174039889f, 0.29028467725446f, 0.26671275747490f, 0.24298017990326f, 0.21910124015687f,
    0.19509032201613f, 0.17096188876030f, 0.14673047445536f, 0.12241067519922f, 0.09801714032956f,
    0.07356456359967f, 0.04906767432742f, 0.02454122852291f};

}  // namespace

Aec3Fft::Aec3Fft() = default;

void Aec3Fft::Fft(std::array<float, kFftLength>* x, FftData* X) const {
  ooura_fft_.Fft(x->data());
  X->CopyFromPackedArray(*x);
}

void Aec3Fft::Ifft(const FftData& X, std::array<float, kFftLength>* x) const {
  X.CopyToPackedArray(x);
  ooura_fft_.InverseFft(x->data());
}

void Aec3Fft::ZeroPaddedFft(std::span<const float> x, Window window, FftData* X) const {
  std::array<float, kFftLength> fft;
  std::fill(fft.begin(), fft.begin() + kFftLengthBy2, 0.f);
  switch (window) {
    case Window::kRectangular:
      std::copy(x.begin(), x.end(), fft.begin() + kFftLengthBy2);
      break;
    case Window::kHanning:
      std::transform(x.begin(), x.end(), std::begin(kHanning64), fft.begin() + kFftLengthBy2,
                     [](float a, float b) { return a * b; });
      break;
    case Window::kSqrtHanning:
      // Not used by AEC3 (RTC_DCHECK_NOTREACHED there).
      std::fill(fft.begin() + kFftLengthBy2, fft.end(), 0.f);
      break;
  }
  Fft(&fft, X);
}

void Aec3Fft::PaddedFft(std::span<const float> x, std::span<const float> x_old, Window window, FftData* X) const {
  std::array<float, kFftLength> fft;
  switch (window) {
    case Window::kRectangular:
      std::copy(x_old.begin(), x_old.end(), fft.begin());
      std::copy(x.begin(), x.end(), fft.begin() + x_old.size());
      break;
    case Window::kHanning:
      // Not used by AEC3 (RTC_DCHECK_NOTREACHED there).
      fft.fill(0.f);
      break;
    case Window::kSqrtHanning:
      std::transform(x_old.begin(), x_old.end(), std::begin(kSqrtHanning128), fft.begin(), std::multiplies<float>());
      std::transform(x.begin(), x.end(), std::begin(kSqrtHanning128) + x_old.size(), fft.begin() + x_old.size(),
                     std::multiplies<float>());
      break;
  }
  Fft(&fft, X);
}

// --- render_buffer.cc --------------------------------------------------------

void RenderBuffer::SpectralSum(size_t num_spectra, std::array<float, kFftLengthBy2Plus1>* X2) const {
  X2->fill(0.f);
  int position = spectrum_buffer_->read;
  for (size_t j = 0; j < num_spectra; ++j) {
    for (const auto& channel_spectrum : spectrum_buffer_->buffer[position]) {
      for (size_t k = 0; k < X2->size(); ++k) (*X2)[k] += channel_spectrum[k];
    }
    position = spectrum_buffer_->IncIndex(position);
  }
}

void RenderBuffer::SpectralSums(size_t num_spectra_shorter, size_t num_spectra_longer,
                                std::array<float, kFftLengthBy2Plus1>* X2_shorter,
                                std::array<float, kFftLengthBy2Plus1>* X2_longer) const {
  X2_shorter->fill(0.f);
  int position = spectrum_buffer_->read;
  size_t j = 0;
  for (; j < num_spectra_shorter; ++j) {
    for (const auto& channel_spectrum : spectrum_buffer_->buffer[position]) {
      for (size_t k = 0; k < X2_shorter->size(); ++k) (*X2_shorter)[k] += channel_spectrum[k];
    }
    position = spectrum_buffer_->IncIndex(position);
  }
  std::copy(X2_shorter->begin(), X2_shorter->end(), X2_longer->begin());
  for (; j < num_spectra_longer; ++j) {
    for (const auto& channel_spectrum : spectrum_buffer_->buffer[position]) {
      for (size_t k = 0; k < X2_longer->size(); ++k) (*X2_longer)[k] += channel_spectrum[k];
    }
    position = spectrum_buffer_->IncIndex(position);
  }
}

// --- render_delay_buffer.cc --------------------------------------------------

RenderDelayBuffer::RenderDelayBuffer(const Aec3Config& config, int sample_rate_hz, size_t num_render_channels)
    : config_(config),
      render_linear_amplitude_gain_(std::pow(10.0f, config_.render_power_gain_db / 20.f)),
      down_sampling_factor_(config.delay.down_sampling_factor),
      sub_block_size_(static_cast<int>(down_sampling_factor_ > 0 ? kBlockSize / down_sampling_factor_ : kBlockSize)),
      blocks_(GetRenderDelayBufferSize(down_sampling_factor_, config.delay.num_filters,
                                       config.filter.refined.length_blocks),
              NumBandsForRate(sample_rate_hz), num_render_channels),
      spectra_(blocks_.buffer.size(), num_render_channels),
      ffts_(blocks_.buffer.size(), num_render_channels),
      delay_(config_.delay.default_delay),
      echo_remover_buffer_(&blocks_, &spectra_, &ffts_),
      low_rate_(GetDownSampledBufferSize(down_sampling_factor_, config.delay.num_filters)),
      render_mixer_(num_render_channels, config.delay.render_alignment_mixing),
      render_decimator_(down_sampling_factor_),
      render_ds_(static_cast<size_t>(sub_block_size_), 0.f),
      buffer_headroom_(static_cast<int>(config.filter.refined.length_blocks)) {
  Reset();
}

void RenderDelayBuffer::Reset() {
  last_call_was_render_ = false;
  num_api_calls_in_a_row_ = 1;
  min_latency_blocks_ = 0;
  excess_render_detection_counter_ = 0;
  // Initialize the read index to one sub-block before the write index.
  low_rate_.read = low_rate_.OffsetIndex(low_rate_.write, sub_block_size_);
  // Check for any external audio buffer delay and whether it is feasible.
  if (external_audio_buffer_delay_) {
    const int headroom = 2;
    size_t audio_buffer_delay_to_set;
    // Minimum delay is 1 (like the low-rate render buffer).
    if (*external_audio_buffer_delay_ <= headroom) {
      audio_buffer_delay_to_set = 1;
    } else {
      audio_buffer_delay_to_set = static_cast<size_t>(*external_audio_buffer_delay_ - headroom);
    }
    audio_buffer_delay_to_set = std::min(audio_buffer_delay_to_set, MaxDelay());
    // When an external delay estimate is available, use that delay as the
    // initial render buffer delay.
    ApplyTotalDelay(static_cast<int>(audio_buffer_delay_to_set));
    delay_ = static_cast<size_t>(ComputeDelay());
    external_audio_buffer_delay_verified_after_reset_ = false;
  } else {
    // If an external delay estimate is not available, use that delay as the
    // initial delay. Set the render buffer delays to the default delay.
    ApplyTotalDelay(static_cast<int>(config_.delay.default_delay));
    // Unset the delays which are set by AlignFromDelay.
    delay_ = std::nullopt;
  }
}

RenderDelayBuffer::BufferingEvent RenderDelayBuffer::Insert(const Block& block) {
  ++render_call_counter_;
  if (delay_) {
    if (!last_call_was_render_) {
      last_call_was_render_ = true;
      num_api_calls_in_a_row_ = 1;
    } else if (++num_api_calls_in_a_row_ > max_observed_jitter_) {
      max_observed_jitter_ = num_api_calls_in_a_row_;
    }
  }
  // Increase the write indices to where the new blocks should be written.
  const int previous_write = blocks_.write;
  IncrementWriteIndices();
  // Allow overrun and do a reset when render overrun occurs due to more render
  // data being inserted than capture data is received.
  const BufferingEvent event = RenderOverrun() ? BufferingEvent::kRenderOverrun : BufferingEvent::kNone;
  // Detect and update render activity.
  if (!render_activity_) {
    render_activity_counter_ += DetectActiveRender(block.View(/*band=*/0, /*channel=*/0)) ? 1 : 0;
    render_activity_ = render_activity_counter_ >= 20;
  }
  // Insert the new render block into the specified position.
  InsertBlock(block, previous_write);
  if (event != BufferingEvent::kNone) {
    ++overruns_;
    Reset();
  }
  return event;
}

RenderDelayBuffer::BufferingEvent RenderDelayBuffer::PrepareCaptureProcessing() {
  BufferingEvent event = BufferingEvent::kNone;
  ++capture_call_counter_;
  if (delay_) {
    if (last_call_was_render_) {
      last_call_was_render_ = false;
      num_api_calls_in_a_row_ = 1;
    } else if (++num_api_calls_in_a_row_ > max_observed_jitter_) {
      max_observed_jitter_ = num_api_calls_in_a_row_;
    }
  }
  if (DetectExcessRenderBlocks()) {
    // Too many render blocks compared to capture blocks. Risk of delay ending
    // up before the filter used by the delay estimator.
    Reset();
    event = BufferingEvent::kRenderOverrun;
    ++overruns_;
  } else if (RenderUnderrun()) {
    // Don't increment the read indices of the low rate buffer if there is a
    // render underrun.
    IncrementReadIndices();
    // Incrementing the buffer index without increasing the low rate buffer
    // index means that the delay is reduced by one.
    if (delay_ && *delay_ > 0) delay_ = *delay_ - 1;
    event = BufferingEvent::kRenderUnderrun;
    ++underruns_;
  } else {
    // Increment the read indices in the render buffers to point to the most
    // recent block to use in the capture processing.
    IncrementLowRateReadIndices();
    IncrementReadIndices();
  }
  echo_remover_buffer_.SetRenderActivity(render_activity_);
  if (render_activity_) {
    render_activity_counter_ = 0;
    render_activity_ = false;
  }
  return event;
}

bool RenderDelayBuffer::AlignFromDelay(size_t delay) {
  if (!external_audio_buffer_delay_verified_after_reset_ && external_audio_buffer_delay_ && delay_) {
    // libwebrtc only logs the mismatch with the external delay here.
    external_audio_buffer_delay_verified_after_reset_ = true;
  }
  if (delay_ && *delay_ == delay) return false;
  delay_ = delay;
  // Compute the total delay and limit the delay to the allowed range.
  int total_delay = MapDelayToTotalDelay(*delay_);
  total_delay = static_cast<int>(std::min(MaxDelay(), static_cast<size_t>(std::max(total_delay, 0))));
  // Apply the delay to the buffers.
  ApplyTotalDelay(total_delay);
  return true;
}

void RenderDelayBuffer::SetAudioBufferDelay(int delay_ms) {
  // Convert delay from milliseconds to blocks (rounded down).
  constexpr int kSampleRateForFixedCaptureDelay = 16000;
  constexpr int kNumSamplesPerMs = kSampleRateForFixedCaptureDelay / 1000;
  external_audio_buffer_delay_ = (delay_ms * kNumSamplesPerMs + config_.fixed_capture_delay_samples) /
                                 (static_cast<int>(kBlockSizeMs) * kNumSamplesPerMs);
}

int RenderDelayBuffer::ComputeDelay() const {
  const int latency_blocks = BufferLatency();
  const int internal_delay =
      spectra_.read >= spectra_.write ? spectra_.read - spectra_.write : spectra_.size + spectra_.read - spectra_.write;
  return internal_delay - latency_blocks;
}

void RenderDelayBuffer::ApplyTotalDelay(int delay) {
  blocks_.read = blocks_.OffsetIndex(blocks_.write, -delay);
  spectra_.read = spectra_.OffsetIndex(spectra_.write, delay);
  ffts_.read = ffts_.OffsetIndex(ffts_.write, delay);
}

void RenderDelayBuffer::AlignFromExternalDelay() {
  if (external_audio_buffer_delay_) {
    const int64_t delay = render_call_counter_ - capture_call_counter_ + *external_audio_buffer_delay_;
    const int64_t delay_with_headroom = delay - static_cast<int64_t>(config_.delay.delay_headroom_samples / kBlockSize);
    ApplyTotalDelay(static_cast<int>(delay_with_headroom));
  }
}

void RenderDelayBuffer::InsertBlock(const Block& block, int previous_write) {
  auto& b = blocks_;
  auto& lr = low_rate_;
  auto& ds = render_ds_;
  auto& f = ffts_;
  auto& s = spectra_;
  const int num_bands = b.buffer[b.write].NumBands();
  const int num_render_channels = b.buffer[b.write].NumChannels();
  for (int band = 0; band < num_bands; ++band) {
    for (int ch = 0; ch < num_render_channels; ++ch) {
      const auto src = block.View(band, ch);
      std::copy(src.begin(), src.end(), b.buffer[b.write].View(band, ch).begin());
    }
  }
  if (render_linear_amplitude_gain_ != 1.f) {
    for (int band = 0; band < num_bands; ++band) {
      for (int ch = 0; ch < num_render_channels; ++ch) {
        for (float& sample : b.buffer[b.write].View(band, ch)) sample *= render_linear_amplitude_gain_;
      }
    }
  }
  std::array<float, kBlockSize> downmixed_render;
  render_mixer_.ProduceOutput(b.buffer[b.write], downmixed_render);
  render_decimator_.Decimate(downmixed_render, ds);
  std::copy(ds.rbegin(), ds.rend(), lr.buffer.begin() + lr.write);
  for (int channel = 0; channel < num_render_channels; ++channel) {
    fft_.PaddedFft(b.buffer[b.write].View(/*band=*/0, channel), b.buffer[previous_write].View(/*band=*/0, channel),
                   &f.buffer[f.write][channel]);
    f.buffer[f.write][channel].Spectrum(s.buffer[s.write][channel]);
  }
}

bool RenderDelayBuffer::DetectActiveRender(std::span<const float> x) const {
  const float x_energy = std::inner_product(x.begin(), x.end(), x.begin(), 0.f);
  return x_energy > (config_.active_render_limit * config_.active_render_limit) * kFftLengthBy2;
}

bool RenderDelayBuffer::DetectExcessRenderBlocks() {
  bool excess_render_detected = false;
  const size_t latency_blocks = static_cast<size_t>(BufferLatency());
  // The recently seen minimum latency in blocks. Should be close to 0.
  min_latency_blocks_ = std::min(min_latency_blocks_, latency_blocks);
  // After processing a configurable number of blocks the minimum latency is
  // checked.
  if (++excess_render_detection_counter_ >= config_.delay.excess_render_detection_interval_blocks) {
    // If the minimum latency is not lower than the threshold there have been
    // more render than capture frames.
    excess_render_detected = min_latency_blocks_ > config_.delay.max_allowed_excess_render_blocks;
    // Reset the counter and let the minimum latency be the current latency.
    min_latency_blocks_ = latency_blocks;
    excess_render_detection_counter_ = 0;
  }
  return excess_render_detected;
}

int RenderDelayBuffer::BufferLatency() const {
  const DownsampledRenderBuffer& l = low_rate_;
  const int latency_samples = (l.size + l.read - l.write) % l.size;
  return latency_samples / sub_block_size_;
}

void RenderDelayBuffer::IncrementWriteIndices() {
  low_rate_.UpdateWriteIndex(-sub_block_size_);
  blocks_.IncWriteIndex();
  spectra_.DecWriteIndex();
  ffts_.DecWriteIndex();
}

void RenderDelayBuffer::IncrementReadIndices() {
  if (blocks_.read != blocks_.write) {
    blocks_.IncReadIndex();
    spectra_.DecReadIndex();
    ffts_.DecReadIndex();
  }
}

void RenderDelayBuffer::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("delay_blocks", delay_ ? static_cast<double>(*delay_) : -1.0);
  w.Gauge("latency_blocks", static_cast<double>(BufferLatency()));
  w.Gauge("max_api_jitter_blocks", static_cast<double>(max_observed_jitter_));
  w.Counter("render_underruns", underruns_);
  w.Counter("render_overruns", overruns_);
}

// --- adaptive_fir_filter.cc --------------------------------------------------

void ComputeFrequencyResponse(size_t num_partitions, const std::vector<std::vector<FftData>>& H,
                              std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) {
  for (auto& H2_ch : *H2) H2_ch.fill(0.f);
  const size_t num_render_channels = H[0].size();
  for (size_t p = 0; p < num_partitions; ++p) {
    for (size_t ch = 0; ch < num_render_channels; ++ch) {
      for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) {
        const float tmp = H[p][ch].re[j] * H[p][ch].re[j] + H[p][ch].im[j] * H[p][ch].im[j];
        (*H2)[p][j] = std::max((*H2)[p][j], tmp);
      }
    }
  }
}

void AdaptPartitions(const RenderBuffer& render_buffer, const FftData& G, size_t num_partitions,
                     std::vector<std::vector<FftData>>* H) {
  const std::span<const std::vector<FftData>> render_buffer_data = render_buffer.GetFftBuffer();
  size_t index = render_buffer.Position();
  const size_t num_render_channels = render_buffer_data[index].size();
  for (size_t p = 0; p < num_partitions; ++p) {
    for (size_t ch = 0; ch < num_render_channels; ++ch) {
      const FftData& X_p_ch = render_buffer_data[index][ch];
      FftData& H_p_ch = (*H)[p][ch];
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        H_p_ch.re[k] += X_p_ch.re[k] * G.re[k] + X_p_ch.im[k] * G.im[k];
        H_p_ch.im[k] += X_p_ch.re[k] * G.im[k] - X_p_ch.im[k] * G.re[k];
      }
    }
    index = index < (render_buffer_data.size() - 1) ? index + 1 : 0;
  }
}

void ApplyFilter(const RenderBuffer& render_buffer, size_t num_partitions,
                 const std::vector<std::vector<FftData>>& H, FftData* S) {
  S->re.fill(0.f);
  S->im.fill(0.f);
  const std::span<const std::vector<FftData>> render_buffer_data = render_buffer.GetFftBuffer();
  size_t index = render_buffer.Position();
  const size_t num_render_channels = render_buffer_data[index].size();
  for (size_t p = 0; p < num_partitions; ++p) {
    for (size_t ch = 0; ch < num_render_channels; ++ch) {
      const FftData& X_p_ch = render_buffer_data[index][ch];
      const FftData& H_p_ch = H[p][ch];
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        S->re[k] += X_p_ch.re[k] * H_p_ch.re[k] - X_p_ch.im[k] * H_p_ch.im[k];
        S->im[k] += X_p_ch.re[k] * H_p_ch.im[k] + X_p_ch.im[k] * H_p_ch.re[k];
      }
    }
    index = index < (render_buffer_data.size() - 1) ? index + 1 : 0;
  }
}

namespace {

// Zeroes the partitions from old_size to new_size.
void ZeroFilter(size_t old_size, size_t new_size, std::vector<std::vector<FftData>>* H) {
  for (size_t p = old_size; p < new_size; ++p) {
    for (size_t ch = 0; ch < (*H)[0].size(); ++ch) (*H)[p][ch].Clear();
  }
}

}  // namespace

AdaptiveFirFilter::AdaptiveFirFilter(size_t max_size_partitions, size_t initial_size_partitions,
                                     size_t size_change_duration_blocks, size_t num_render_channels)
    : num_render_channels_(num_render_channels),
      max_size_partitions_(max_size_partitions),
      size_change_duration_blocks_(static_cast<int>(size_change_duration_blocks)),
      current_size_partitions_(initial_size_partitions),
      target_size_partitions_(initial_size_partitions),
      old_target_size_partitions_(initial_size_partitions),
      H_(max_size_partitions_, std::vector<FftData>(num_render_channels_)) {
  one_by_size_change_duration_blocks_ = 1.f / static_cast<float>(size_change_duration_blocks_);
  ZeroFilter(0, max_size_partitions_, &H_);
  SetSizePartitions(current_size_partitions_, true);
}

void AdaptiveFirFilter::HandleEchoPathChange() {
  // TODO(peah): Check the value and purpose of the code below.
  ZeroFilter(current_size_partitions_, max_size_partitions_, &H_);
}

void AdaptiveFirFilter::SetSizePartitions(size_t size, bool immediate_effect) {
  target_size_partitions_ = std::min(max_size_partitions_, size);
  if (immediate_effect) {
    const size_t old_size_partitions = current_size_partitions_;
    current_size_partitions_ = old_target_size_partitions_ = target_size_partitions_;
    ZeroFilter(old_size_partitions, current_size_partitions_, &H_);
    partition_to_constrain_ = std::min(partition_to_constrain_, current_size_partitions_ - 1);
    size_change_counter_ = 0;
  } else {
    size_change_counter_ = size_change_duration_blocks_;
  }
}

void AdaptiveFirFilter::UpdateSize() {
  const size_t old_size_partitions = current_size_partitions_;
  if (size_change_counter_ > 0) {
    --size_change_counter_;
    auto average = [](float from, float to, float from_weight) { return from * from_weight + to * (1.f - from_weight); };
    const float change_factor = static_cast<float>(size_change_counter_) * one_by_size_change_duration_blocks_;
    current_size_partitions_ = static_cast<size_t>(average(static_cast<float>(old_target_size_partitions_),
                                                           static_cast<float>(target_size_partitions_),
                                                           change_factor));
    partition_to_constrain_ = std::min(partition_to_constrain_, current_size_partitions_ - 1);
  } else {
    current_size_partitions_ = old_target_size_partitions_ = target_size_partitions_;
  }
  ZeroFilter(old_size_partitions, current_size_partitions_, &H_);
}

void AdaptiveFirFilter::Filter(const RenderBuffer& render_buffer, FftData* S) const {
  ApplyFilter(render_buffer, current_size_partitions_, H_, S);
}

void AdaptiveFirFilter::Adapt(const RenderBuffer& render_buffer, const FftData& G) {
  AdaptAndUpdateSize(render_buffer, G);
  Constrain();
}

void AdaptiveFirFilter::Adapt(const RenderBuffer& render_buffer, const FftData& G,
                              std::vector<float>* impulse_response) {
  AdaptAndUpdateSize(render_buffer, G);
  ConstrainAndUpdateImpulseResponse(impulse_response);
}

void AdaptiveFirFilter::ComputeFrequencyResponse(std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) const {
  H2->resize(current_size_partitions_);
  aec3::ComputeFrequencyResponse(current_size_partitions_, H_, H2);
}

void AdaptiveFirFilter::AdaptAndUpdateSize(const RenderBuffer& render_buffer, const FftData& G) {
  UpdateSize();
  AdaptPartitions(render_buffer, G, current_size_partitions_, &H_);
}

void AdaptiveFirFilter::ConstrainAndUpdateImpulseResponse(std::vector<float>* impulse_response) {
  impulse_response->resize(static_cast<size_t>(GetTimeDomainLength(static_cast<int>(current_size_partitions_))));
  std::array<float, kFftLength> h;
  std::fill(impulse_response->begin() + static_cast<std::ptrdiff_t>(partition_to_constrain_ * kFftLengthBy2),
            impulse_response->begin() + static_cast<std::ptrdiff_t>((partition_to_constrain_ + 1) * kFftLengthBy2),
            0.f);
  for (size_t ch = 0; ch < num_render_channels_; ++ch) {
    fft_.Ifft(H_[partition_to_constrain_][ch], &h);
    static constexpr float kScale = 1.0f / kFftLengthBy2;
    std::for_each(h.begin(), h.begin() + kFftLengthBy2, [](float& a) { a *= kScale; });
    std::fill(h.begin() + kFftLengthBy2, h.end(), 0.f);
    if (ch == 0) {
      std::copy(h.begin(), h.begin() + kFftLengthBy2,
                impulse_response->begin() + static_cast<std::ptrdiff_t>(partition_to_constrain_ * kFftLengthBy2));
    } else {
      for (size_t k = 0, j = partition_to_constrain_ * kFftLengthBy2; k < kFftLengthBy2; ++k, ++j) {
        if (std::fabs((*impulse_response)[j]) < std::fabs(h[k])) (*impulse_response)[j] = h[k];
      }
    }
    fft_.Fft(&h, &H_[partition_to_constrain_][ch]);
  }
  partition_to_constrain_ = partition_to_constrain_ < (current_size_partitions_ - 1) ? partition_to_constrain_ + 1 : 0;
}

void AdaptiveFirFilter::Constrain() {
  std::array<float, kFftLength> h;
  for (size_t ch = 0; ch < num_render_channels_; ++ch) {
    fft_.Ifft(H_[partition_to_constrain_][ch], &h);
    static constexpr float kScale = 1.0f / kFftLengthBy2;
    std::for_each(h.begin(), h.begin() + kFftLengthBy2, [](float& a) { a *= kScale; });
    std::fill(h.begin() + kFftLengthBy2, h.end(), 0.f);
    fft_.Fft(&h, &H_[partition_to_constrain_][ch]);
  }
  partition_to_constrain_ = partition_to_constrain_ < (current_size_partitions_ - 1) ? partition_to_constrain_ + 1 : 0;
}

void AdaptiveFirFilter::ScaleFilter(float factor) {
  for (auto& H_p : H_) {
    for (auto& H_p_ch : H_p) {
      for (auto& re : H_p_ch.re) re *= factor;
      for (auto& im : H_p_ch.im) im *= factor;
    }
  }
}

void AdaptiveFirFilter::SetFilter(size_t num_partitions, const std::vector<std::vector<FftData>>& H) {
  const size_t min_num_partitions = std::min(current_size_partitions_, num_partitions);
  for (size_t p = 0; p < min_num_partitions; ++p) {
    for (size_t ch = 0; ch < num_render_channels_; ++ch) {
      H_[p][ch].re = H[p][ch].re;
      H_[p][ch].im = H[p][ch].im;
    }
  }
}

// --- adaptive_fir_filter_erl.cc ----------------------------------------------

void ComputeErl(const std::vector<std::array<float, kFftLengthBy2Plus1>>& H2, std::span<float> erl) {
  std::fill(erl.begin(), erl.end(), 0.f);
  for (const auto& H2_j : H2) std::transform(H2_j.begin(), H2_j.end(), erl.begin(), erl.begin(), std::plus<float>());
}

// --- render_signal_analyzer.cc -----------------------------------------------

namespace {

constexpr size_t kCounterThreshold = 5;

// Identifies local bands with narrow characteristics.
void IdentifySmallNarrowBandRegions(const RenderBuffer& render_buffer, const std::optional<size_t>& delay_partitions,
                                    std::array<size_t, kFftLengthBy2 - 1>* narrow_band_counters) {
  if (!delay_partitions) {
    narrow_band_counters->fill(0);
    return;
  }
  std::array<size_t, kFftLengthBy2 - 1> channel_counters;
  channel_counters.fill(0);
  const auto X2 = render_buffer.Spectrum(static_cast<int>(*delay_partitions));
  for (size_t ch = 0; ch < X2.size(); ++ch) {
    for (size_t k = 1; k < kFftLengthBy2; ++k) {
      if (X2[ch][k] > 3 * std::max(X2[ch][k - 1], X2[ch][k + 1])) ++channel_counters[k - 1];
    }
  }
  for (size_t k = 1; k < kFftLengthBy2; ++k) {
    (*narrow_band_counters)[k - 1] = channel_counters[k - 1] > 0 ? (*narrow_band_counters)[k - 1] + 1 : 0;
  }
}

// Identifies whether the signal has a single strong narrow-band component.
void IdentifyStrongNarrowBandComponent(const RenderBuffer& render_buffer, int strong_peak_freeze_duration,
                                       std::optional<int>* narrow_peak_band, size_t* narrow_peak_counter) {
  if (*narrow_peak_band && ++(*narrow_peak_counter) > static_cast<size_t>(strong_peak_freeze_duration)) {
    *narrow_peak_band = std::nullopt;
  }
  const Block& x_latest = render_buffer.GetBlock(0);
  float max_peak_level = 0.f;
  for (int channel = 0; channel < x_latest.NumChannels(); ++channel) {
    const auto& X2_latest = render_buffer.Spectrum(0)[static_cast<size_t>(channel)];
    // Identify the spectral peak.
    const int peak_bin = static_cast<int>(std::max_element(X2_latest.begin(), X2_latest.end()) - X2_latest.begin());
    // Compute the level around the peak.
    float non_peak_power = 0.f;
    for (int k = std::max(0, peak_bin - 14); k < peak_bin - 4; ++k) non_peak_power = std::max(X2_latest[k], non_peak_power);
    for (int k = peak_bin + 5; k < std::min(peak_bin + 15, static_cast<int>(kFftLengthBy2Plus1)); ++k) {
      non_peak_power = std::max(X2_latest[k], non_peak_power);
    }
    // Assess the render signal strength.
    const auto band0 = x_latest.View(/*band=*/0, channel);
    const auto result0 = std::minmax_element(band0.begin(), band0.end());
    float max_abs = std::max(std::fabs(*result0.first), std::fabs(*result0.second));
    if (x_latest.NumBands() > 1) {
      const auto band1 = x_latest.View(/*band=*/1, channel);
      const auto result1 = std::minmax_element(band1.begin(), band1.end());
      max_abs = std::max(max_abs, std::max(std::fabs(*result1.first), std::fabs(*result1.second)));
    }
    // Detect whether the spectral peak has a strong narrowband nature.
    const float peak_level = X2_latest[static_cast<size_t>(peak_bin)];
    if (peak_bin > 0 && max_abs > 100 && peak_level > 100 * non_peak_power) {
      // Store the strongest peak across channels.
      if (peak_level > max_peak_level) {
        max_peak_level = peak_level;
        *narrow_peak_band = peak_bin;
        *narrow_peak_counter = 0;
      }
    }
  }
}

}  // namespace

RenderSignalAnalyzer::RenderSignalAnalyzer(const Aec3Config& config)
    : strong_peak_freeze_duration_(static_cast<int>(config.filter.refined.length_blocks)) {}

void RenderSignalAnalyzer::Update(const RenderBuffer& render_buffer, const std::optional<size_t>& delay_partitions) {
  // Identify bands of narrow nature.
  IdentifySmallNarrowBandRegions(render_buffer, delay_partitions, &narrow_band_counters_);
  // Identify the presence of a strong narrow band.
  IdentifyStrongNarrowBandComponent(render_buffer, strong_peak_freeze_duration_, &narrow_peak_band_,
                                    &narrow_peak_counter_);
}

bool RenderSignalAnalyzer::PoorSignalExcitation() const {
  return std::any_of(narrow_band_counters_.begin(), narrow_band_counters_.end(), [](size_t a) { return a > 10; });
}

void RenderSignalAnalyzer::MaskRegionsAroundNarrowBands(std::array<float, kFftLengthBy2Plus1>* v) const {
  // Set v to zero around narrow band signal regions.
  if (narrow_band_counters_[0] > kCounterThreshold) (*v)[1] = (*v)[0] = 0.f;
  for (size_t k = 2; k < kFftLengthBy2 - 1; ++k) {
    if (narrow_band_counters_[k - 1] > kCounterThreshold) {
      (*v)[k - 2] = (*v)[k - 1] = (*v)[k] = (*v)[k + 1] = (*v)[k + 2] = 0.f;
    }
  }
  if (narrow_band_counters_[kFftLengthBy2 - 2] > kCounterThreshold) {
    (*v)[kFftLengthBy2] = (*v)[kFftLengthBy2 - 1] = 0.f;
  }
}

// --- subtractor_output.cc ----------------------------------------------------

void SubtractorOutput::Reset() {
  s_refined.fill(0.f);
  s_coarse.fill(0.f);
  e_refined.fill(0.f);
  e_coarse.fill(0.f);
  E_refined.Clear();
  E2_refined.fill(0.f);
  E2_coarse.fill(0.f);
  e2_refined = 0.f;
  e2_coarse = 0.f;
  s2_refined = 0.f;
  s2_coarse = 0.f;
  y2 = 0.f;
}

void SubtractorOutput::ComputeMetrics(std::span<const float> y) {
  const auto sum_of_squares = [](float a, float b) { return a + b * b; };
  y2 = std::accumulate(y.begin(), y.end(), 0.f, sum_of_squares);
  e2_refined = std::accumulate(e_refined.begin(), e_refined.end(), 0.f, sum_of_squares);
  e2_coarse = std::accumulate(e_coarse.begin(), e_coarse.end(), 0.f, sum_of_squares);
  s2_refined = std::accumulate(s_refined.begin(), s_refined.end(), 0.f, sum_of_squares);
  s2_coarse = std::accumulate(s_coarse.begin(), s_coarse.end(), 0.f, sum_of_squares);
  s_refined_max_abs = *std::max_element(s_refined.begin(), s_refined.end());
  s_refined_max_abs = std::max(s_refined_max_abs, -(*std::min_element(s_refined.begin(), s_refined.end())));
  s_coarse_max_abs = *std::max_element(s_coarse.begin(), s_coarse.end());
  s_coarse_max_abs = std::max(s_coarse_max_abs, -(*std::min_element(s_coarse.begin(), s_coarse.end())));
}

// --- refined_filter_update_gain.cc -------------------------------------------

namespace {
constexpr float kHErrorInitial = 10000.f;
constexpr int kPoorExcitationCounterInitial = 1000;

float Average(float from, float to, float from_weight) { return from * from_weight + to * (1.f - from_weight); }
}  // namespace

RefinedFilterUpdateGain::RefinedFilterUpdateGain(const FilterConfig::RefinedConfiguration& config,
                                                 size_t config_change_duration_blocks)
    : config_change_duration_blocks_(static_cast<int>(config_change_duration_blocks)),
      poor_excitation_counter_(kPoorExcitationCounterInitial) {
  SetConfig(config, true);
  H_error_.fill(kHErrorInitial);
  one_by_config_change_duration_blocks_ = 1.f / static_cast<float>(config_change_duration_blocks_);
}

void RefinedFilterUpdateGain::SetConfig(const FilterConfig::RefinedConfiguration& config, bool immediate_effect) {
  if (immediate_effect) {
    old_target_config_ = current_config_ = target_config_ = config;
    config_change_counter_ = 0;
  } else {
    old_target_config_ = current_config_;
    target_config_ = config;
    config_change_counter_ = config_change_duration_blocks_;
  }
}

void RefinedFilterUpdateGain::HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
  if (echo_path_variability.delay_change != EchoPathVariability::DelayAdjustment::kNone) {
    H_error_.fill(kHErrorInitial);
  }
  if (!echo_path_variability.gain_change) {
    poor_excitation_counter_ = kPoorExcitationCounterInitial;
    call_counter_ = 0;
  }
}

void RefinedFilterUpdateGain::Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
                                      const RenderSignalAnalyzer& render_signal_analyzer,
                                      const SubtractorOutput& subtractor_output, std::span<const float> erl,
                                      size_t size_partitions, bool saturated_capture_signal,
                                      bool disallow_leakage_diverged, FftData* gain_fft) {
  const FftData& E_refined = subtractor_output.E_refined;
  const auto& E2_refined = subtractor_output.E2_refined;
  const auto& E2_coarse = subtractor_output.E2_coarse;
  FftData* G = gain_fft;
  const auto& X2 = render_power;

  ++call_counter_;
  UpdateCurrentConfig();
  if (render_signal_analyzer.PoorSignalExcitation()) poor_excitation_counter_ = 0;

  // Do not update the filter if the render is not sufficiently excited.
  if (++poor_excitation_counter_ < size_partitions || saturated_capture_signal || call_counter_ <= size_partitions) {
    G->re.fill(0.f);
    G->im.fill(0.f);
  } else {
    // Corresponds to WGN of power -39 dBFS.
    std::array<float, kFftLengthBy2Plus1> mu;
    // mu = H_error / (0.5* H_error* X2 + n * E2).
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      if (X2[k] >= current_config_.noise_gate) {
        mu[k] = H_error_[k] / (0.5f * H_error_[k] * X2[k] + static_cast<float>(size_partitions) * E2_refined[k]);
      } else {
        mu[k] = 0.f;
      }
    }
    // Avoid updating the filter close to narrow bands in the render signals.
    render_signal_analyzer.MaskRegionsAroundNarrowBands(&mu);
    // H_error = H_error - 0.5 * mu * X2 * H_error.
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) H_error_[k] -= 0.5f * mu[k] * X2[k] * H_error_[k];
    // G = mu * E.
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      G->re[k] = mu[k] * E_refined.re[k];
      G->im[k] = mu[k] * E_refined.im[k];
    }
  }

  // H_error = H_error + factor * erl.
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    if (E2_refined[k] <= E2_coarse[k] || disallow_leakage_diverged) {
      H_error_[k] += current_config_.leakage_converged * erl[k];
    } else {
      H_error_[k] += current_config_.leakage_diverged * erl[k];
    }
    H_error_[k] = std::max(H_error_[k], current_config_.error_floor);
    H_error_[k] = std::min(H_error_[k], current_config_.error_ceil);
  }
}

void RefinedFilterUpdateGain::UpdateCurrentConfig() {
  if (config_change_counter_ > 0) {
    if (--config_change_counter_ > 0) {
      const float change_factor = static_cast<float>(config_change_counter_) * one_by_config_change_duration_blocks_;
      current_config_.leakage_converged =
          Average(old_target_config_.leakage_converged, target_config_.leakage_converged, change_factor);
      current_config_.leakage_diverged =
          Average(old_target_config_.leakage_diverged, target_config_.leakage_diverged, change_factor);
      current_config_.error_floor = Average(old_target_config_.error_floor, target_config_.error_floor, change_factor);
      current_config_.error_ceil = Average(old_target_config_.error_ceil, target_config_.error_ceil, change_factor);
      current_config_.noise_gate = Average(old_target_config_.noise_gate, target_config_.noise_gate, change_factor);
    } else {
      current_config_ = old_target_config_ = target_config_;
    }
  }
}

// --- coarse_filter_update_gain.cc --------------------------------------------

CoarseFilterUpdateGain::CoarseFilterUpdateGain(const FilterConfig::CoarseConfiguration& config,
                                               size_t config_change_duration_blocks)
    : config_change_duration_blocks_(static_cast<int>(config_change_duration_blocks)) {
  SetConfig(config, true);
  one_by_config_change_duration_blocks_ = 1.f / static_cast<float>(config_change_duration_blocks_);
}

void CoarseFilterUpdateGain::SetConfig(const FilterConfig::CoarseConfiguration& config, bool immediate_effect) {
  if (immediate_effect) {
    old_target_config_ = current_config_ = target_config_ = config;
    config_change_counter_ = 0;
  } else {
    old_target_config_ = current_config_;
    target_config_ = config;
    config_change_counter_ = config_change_duration_blocks_;
  }
}

void CoarseFilterUpdateGain::HandleEchoPathChange() {
  poor_signal_excitation_counter_ = 0;
  call_counter_ = 0;
}

void CoarseFilterUpdateGain::Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
                                     const RenderSignalAnalyzer& render_signal_analyzer, const FftData& E_coarse,
                                     size_t size_partitions, bool saturated_capture_signal, FftData* G) {
  ++call_counter_;
  UpdateCurrentConfig();
  if (render_signal_analyzer.PoorSignalExcitation()) poor_signal_excitation_counter_ = 0;

  // Do not update the filter if the render is not sufficiently excited.
  if (++poor_signal_excitation_counter_ < size_partitions || saturated_capture_signal ||
      call_counter_ <= size_partitions) {
    G->re.fill(0.f);
    G->im.fill(0.f);
    return;
  }

  // Compute mu.
  std::array<float, kFftLengthBy2Plus1> mu;
  const auto& X2 = render_power;
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    mu[k] = X2[k] > current_config_.noise_gate ? current_config_.rate / X2[k] : 0.f;
  }
  // Avoid updating the filter close to narrow bands in the render signals.
  render_signal_analyzer.MaskRegionsAroundNarrowBands(&mu);
  // G = mu * E.
  for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
    G->re[k] = mu[k] * E_coarse.re[k];
    G->im[k] = mu[k] * E_coarse.im[k];
  }
}

void CoarseFilterUpdateGain::UpdateCurrentConfig() {
  if (config_change_counter_ > 0) {
    if (--config_change_counter_ > 0) {
      const float change_factor = static_cast<float>(config_change_counter_) * one_by_config_change_duration_blocks_;
      current_config_.rate = Average(old_target_config_.rate, target_config_.rate, change_factor);
      current_config_.noise_gate = Average(old_target_config_.noise_gate, target_config_.noise_gate, change_factor);
    } else {
      current_config_ = old_target_config_ = target_config_;
    }
  }
}

// --- subtractor.cc -----------------------------------------------------------

namespace {

void PredictionError(const Aec3Fft& fft, const FftData& S, std::span<const float> y,
                     std::array<float, kBlockSize>* e, std::array<float, kBlockSize>* s) {
  std::array<float, kFftLength> tmp;
  fft.Ifft(S, &tmp);
  constexpr float kScale = 1.0f / kFftLengthBy2;
  std::transform(y.begin(), y.end(), tmp.begin() + kFftLengthBy2, e->begin(),
                 [&](float a, float b) { return a - b * kScale; });
  if (s) {
    for (size_t k = 0; k < s->size(); ++k) (*s)[k] = kScale * tmp[k + kFftLengthBy2];
  }
}

void ScaleFilterOutput(std::span<const float> y, float factor, std::span<float> e, std::span<float> s) {
  for (size_t k = 0; k < y.size(); ++k) {
    s[k] *= factor;
    e[k] = y[k] - s[k];
  }
}

}  // namespace

Subtractor::Subtractor(const Aec3Config& config, size_t num_render_channels, size_t num_capture_channels)
    : config_(config),
      num_capture_channels_(num_capture_channels),
      refined_filters_(num_capture_channels_),
      coarse_filter_(num_capture_channels_),
      refined_gains_(num_capture_channels_),
      coarse_gains_(num_capture_channels_),
      filter_misadjustment_estimators_(num_capture_channels_),
      poor_coarse_filter_counters_(num_capture_channels_, 0),
      coarse_filter_reset_hangover_(num_capture_channels_, 0),
      refined_frequency_responses_(
          num_capture_channels_,
          std::vector<std::array<float, kFftLengthBy2Plus1>>(
              std::max(config_.filter.refined_initial.length_blocks, config_.filter.refined.length_blocks),
              std::array<float, kFftLengthBy2Plus1>())),
      refined_impulse_responses_(
          num_capture_channels_,
          std::vector<float>(static_cast<size_t>(GetTimeDomainLength(static_cast<int>(
                                 std::max(config_.filter.refined_initial.length_blocks,
                                          config_.filter.refined.length_blocks)))),
                             0.f)) {
  const auto& f = config_.filter;
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    refined_filters_[ch] = std::make_unique<AdaptiveFirFilter>(
        f.refined.length_blocks, f.refined_initial.length_blocks, f.config_change_duration_blocks, num_render_channels);
    coarse_filter_[ch] = std::make_unique<AdaptiveFirFilter>(
        f.coarse.length_blocks, f.coarse_initial.length_blocks, f.config_change_duration_blocks, num_render_channels);
    refined_gains_[ch] = std::make_unique<RefinedFilterUpdateGain>(f.refined_initial, f.config_change_duration_blocks);
    coarse_gains_[ch] = std::make_unique<CoarseFilterUpdateGain>(f.coarse_initial, f.config_change_duration_blocks);
  }
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    for (auto& H2_k : refined_frequency_responses_[ch]) H2_k.fill(0.f);
  }
}

void Subtractor::HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
  const auto full_reset = [&]() {
    ++echo_path_resets_;
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      refined_filters_[ch]->HandleEchoPathChange();
      coarse_filter_[ch]->HandleEchoPathChange();
      refined_gains_[ch]->HandleEchoPathChange(echo_path_variability);
      coarse_gains_[ch]->HandleEchoPathChange();
      refined_gains_[ch]->SetConfig(config_.filter.refined_initial, true);
      coarse_gains_[ch]->SetConfig(config_.filter.coarse_initial, true);
      refined_filters_[ch]->SetSizePartitions(config_.filter.refined_initial.length_blocks, true);
      coarse_filter_[ch]->SetSizePartitions(config_.filter.coarse_initial.length_blocks, true);
    }
  };
  if (echo_path_variability.delay_change != EchoPathVariability::DelayAdjustment::kNone) full_reset();
  if (echo_path_variability.gain_change) {
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      refined_gains_[ch]->HandleEchoPathChange(echo_path_variability);
    }
  }
}

void Subtractor::ExitInitialState() {
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    refined_gains_[ch]->SetConfig(config_.filter.refined, false);
    coarse_gains_[ch]->SetConfig(config_.filter.coarse, false);
    refined_filters_[ch]->SetSizePartitions(config_.filter.refined.length_blocks, false);
    coarse_filter_[ch]->SetSizePartitions(config_.filter.coarse.length_blocks, false);
  }
}

void Subtractor::Process(const RenderBuffer& render_buffer, const Block& capture,
                         const RenderSignalAnalyzer& render_signal_analyzer, bool saturated_capture,
                         std::span<SubtractorOutput> outputs) {
  // Compute the render powers.
  const bool same_filter_sizes = refined_filters_[0]->SizePartitions() == coarse_filter_[0]->SizePartitions();
  std::array<float, kFftLengthBy2Plus1> X2_refined;
  std::array<float, kFftLengthBy2Plus1> X2_coarse_data;
  auto& X2_coarse = same_filter_sizes ? X2_refined : X2_coarse_data;
  if (same_filter_sizes) {
    render_buffer.SpectralSum(refined_filters_[0]->SizePartitions(), &X2_refined);
  } else if (refined_filters_[0]->SizePartitions() > coarse_filter_[0]->SizePartitions()) {
    render_buffer.SpectralSums(coarse_filter_[0]->SizePartitions(), refined_filters_[0]->SizePartitions(), &X2_coarse,
                               &X2_refined);
  } else {
    render_buffer.SpectralSums(refined_filters_[0]->SizePartitions(), coarse_filter_[0]->SizePartitions(), &X2_refined,
                               &X2_coarse);
  }

  // Process all capture channels.
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    SubtractorOutput& output = outputs[ch];
    const std::span<const float> y = capture.View(/*band=*/0, static_cast<int>(ch));
    FftData& E_refined = output.E_refined;
    FftData E_coarse;
    std::array<float, kBlockSize>& e_refined = output.e_refined;
    std::array<float, kBlockSize>& e_coarse = output.e_coarse;

    FftData S;
    FftData& G = S;

    // Form the outputs of the refined and coarse filters.
    refined_filters_[ch]->Filter(render_buffer, &S);
    PredictionError(fft_, S, y, &e_refined, &output.s_refined);
    coarse_filter_[ch]->Filter(render_buffer, &S);
    PredictionError(fft_, S, y, &e_coarse, &output.s_coarse);

    // Compute the signal powers in the subtractor output.
    output.ComputeMetrics(y);

    // Adjust the filter if needed.
    bool refined_filters_adjusted = false;
    filter_misadjustment_estimators_[ch].Update(output);
    if (filter_misadjustment_estimators_[ch].IsAdjustmentNeeded()) {
      const float scale = filter_misadjustment_estimators_[ch].GetMisadjustment();
      refined_filters_[ch]->ScaleFilter(scale);
      for (auto& h_k : refined_impulse_responses_[ch]) h_k *= scale;
      ScaleFilterOutput(y, scale, e_refined, output.s_refined);
      filter_misadjustment_estimators_[ch].Reset();
      refined_filters_adjusted = true;
      ++misadjustment_rescales_;
    }

    // Compute the FFts of the refined and coarse filter outputs.
    fft_.ZeroPaddedFft(e_refined, Aec3Fft::Window::kHanning, &E_refined);
    fft_.ZeroPaddedFft(e_coarse, Aec3Fft::Window::kHanning, &E_coarse);

    // Compute spectra for future use.
    E_coarse.Spectrum(output.E2_coarse);
    E_refined.Spectrum(output.E2_refined);

    // Update the refined filter.
    if (!refined_filters_adjusted) {
      // Do not allow the performance of the coarse filter to affect the
      // adaptation speed of the refined filter just after the coarse filter
      // has been reset.
      const bool disallow_leakage_diverged = coarse_filter_reset_hangover_[ch] > 0;
      std::array<float, kFftLengthBy2Plus1> erl;
      ComputeErl(refined_frequency_responses_[ch], erl);
      refined_gains_[ch]->Compute(X2_refined, render_signal_analyzer, output, erl,
                                  refined_filters_[ch]->SizePartitions(), saturated_capture,
                                  disallow_leakage_diverged, &G);
    } else {
      G.re.fill(0.f);
      G.im.fill(0.f);
    }
    refined_filters_[ch]->Adapt(render_buffer, G, &refined_impulse_responses_[ch]);
    refined_filters_[ch]->ComputeFrequencyResponse(&refined_frequency_responses_[ch]);

    // Update the coarse filter.
    poor_coarse_filter_counters_[ch] =
        output.e2_refined < output.e2_coarse ? poor_coarse_filter_counters_[ch] + 1 : 0;
    if (poor_coarse_filter_counters_[ch] < 5) {
      coarse_gains_[ch]->Compute(X2_coarse, render_signal_analyzer, E_coarse, coarse_filter_[ch]->SizePartitions(),
                                 saturated_capture, &G);
      coarse_filter_reset_hangover_[ch] = std::max(coarse_filter_reset_hangover_[ch] - 1, 0);
    } else {
      poor_coarse_filter_counters_[ch] = 0;
      coarse_filter_[ch]->SetFilter(refined_filters_[ch]->SizePartitions(), refined_filters_[ch]->GetFilter());
      coarse_gains_[ch]->Compute(X2_coarse, render_signal_analyzer, E_refined, coarse_filter_[ch]->SizePartitions(),
                                 saturated_capture, &G);
      coarse_filter_reset_hangover_[ch] = config_.filter.coarse_reset_hangover_blocks;
      ++coarse_filter_resets_;
    }
    // libwebrtc keeps the coarse impulse response only for its data dumps,
    // which a release build doesn't have.
    coarse_filter_[ch]->Adapt(render_buffer, G);

    for (float& a : e_refined) a = std::clamp(a, -32768.f, 32767.f);
  }
}

void Subtractor::FilterMisadjustmentEstimator::Update(const SubtractorOutput& output) {
  e2_acum_ += output.e2_refined;
  y2_acum_ += output.y2;
  if (++n_blocks_acum_ == n_blocks_) {
    if (y2_acum_ > static_cast<float>(n_blocks_) * 200.f * 200.f * kBlockSize) {
      const float update = (e2_acum_ / y2_acum_);
      if (e2_acum_ > static_cast<float>(n_blocks_) * 7500.f * 7500.f * kBlockSize) {
        // Duration equal to blockSizeMs * n_blocks_ * 4.
        overhang_ = 4;
      } else {
        overhang_ = std::max(overhang_ - 1, 0);
      }
      if ((update < inv_misadjustment_) || (overhang_ > 0)) {
        inv_misadjustment_ += 0.1f * (update - inv_misadjustment_);
      }
    }
    e2_acum_ = 0.f;
    y2_acum_ = 0.f;
    n_blocks_acum_ = 0;
  }
}

void Subtractor::FilterMisadjustmentEstimator::Reset() {
  e2_acum_ = 0.f;
  y2_acum_ = 0.f;
  n_blocks_acum_ = 0;
  inv_misadjustment_ = 0.f;
  overhang_ = 0;
}

void Subtractor::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("refined_partitions", static_cast<double>(refined_filters_[0]->SizePartitions()));
  w.Gauge("coarse_partitions", static_cast<double>(coarse_filter_[0]->SizePartitions()));
  w.Counter("misadjustment_rescales", misadjustment_rescales_);
  w.Counter("coarse_filter_resets", coarse_filter_resets_);
  w.Counter("echo_path_resets", echo_path_resets_);
}

}  // namespace sm::audio::aec3
