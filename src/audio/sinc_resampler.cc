// Ported from compare/webrtc/reference/common_audio/resampler/
// sinc_resampler.cc and push_sinc_resampler.cc (libwebrtc M157), same
// float/double types and operation order; see the header.
#include "sm/audio/sinc_resampler.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>

namespace sm::audio {

namespace {

double SincScaleFactor(double io_ratio) {
  // The normalized cutoff frequency of the low-pass filter, lowered slightly
  // because the windowed sinc's transition is not a brick wall.
  double sinc_scale_factor = io_ratio > 1.0 ? 1.0 / io_ratio : 1.0;
  sinc_scale_factor *= 0.9;
  return sinc_scale_factor;
}

}  // namespace

SincResampler::SincResampler(double io_sample_rate_ratio, size_t request_frames)
    : io_sample_rate_ratio_(io_sample_rate_ratio),
      request_frames_(request_frames),
      input_buffer_size_(request_frames_ + kKernelSize),
      kernel_storage_(kKernelStorageSize, 0.0f),
      input_buffer_(input_buffer_size_, 0.0f) {
  assert(request_frames_ > 0);
  r1_ = input_buffer_.data();
  r2_ = input_buffer_.data() + kKernelSize / 2;
  Flush();
  assert(block_size_ > kKernelSize);
  InitializeKernel();
}

void SincResampler::UpdateRegions(bool second_load) {
  // On the second load r0_ slides right by kKernelSize / 2.
  r0_ = input_buffer_.data() + (second_load ? kKernelSize : kKernelSize / 2);
  r3_ = r0_ + request_frames_ - kKernelSize;
  r4_ = r0_ + request_frames_ - kKernelSize / 2;
  block_size_ = static_cast<size_t>(r4_ - r2_);
}

void SincResampler::InitializeKernel() {
  // Blackman window parameters.
  constexpr double kAlpha = 0.16;
  constexpr double kA0 = 0.5 * (1.0 - kAlpha);
  constexpr double kA1 = 0.5;
  constexpr double kA2 = 0.5 * kAlpha;
  // Windowed sinc() kernels for sub-sample offsets from 0.0 to 1.0.
  const double sinc_scale_factor = SincScaleFactor(io_sample_rate_ratio_);
  for (size_t offset_idx = 0; offset_idx <= kKernelOffsetCount; ++offset_idx) {
    const float subsample_offset = static_cast<float>(offset_idx) / kKernelOffsetCount;
    for (size_t i = 0; i < kKernelSize; ++i) {
      const size_t idx = i + offset_idx * kKernelSize;
      const float pre_sinc = static_cast<float>(
          std::numbers::pi * (static_cast<int>(i) - static_cast<int>(kKernelSize / 2) - subsample_offset));
      // Blackman window, matching the offset of the sinc().
      const float x = (static_cast<float>(i) - subsample_offset) / kKernelSize;
      const float window = static_cast<float>(kA0 - kA1 * std::cos(2.0 * std::numbers::pi * x) +
                                              kA2 * std::cos(4.0 * std::numbers::pi * x));
      kernel_storage_[idx] = static_cast<float>(
          window * ((pre_sinc == 0) ? sinc_scale_factor : (std::sin(sinc_scale_factor * pre_sinc) / pre_sinc)));
    }
  }
}

void SincResampler::Resample(size_t frames, float* destination) {
  size_t remaining_frames = frames;
  // (1) Prime the input buffer at the start of the input stream.
  if (!buffer_primed_ && remaining_frames) {
    Run(request_frames_, r0_);
    buffer_primed_ = true;
  }
  // (2) Resample.
  const double current_io_ratio = io_sample_rate_ratio_;
  const float* const kernel_ptr = kernel_storage_.data();
  while (remaining_frames) {
    // `i` may be negative if the last call ended on an iteration that put
    // `virtual_source_idx_` over the limit.
    for (int i = static_cast<int>(std::ceil((static_cast<double>(block_size_) - virtual_source_idx_) /
                                            current_io_ratio));
         i > 0; --i) {
      const int source_idx = static_cast<int>(virtual_source_idx_);
      const double subsample_remainder = virtual_source_idx_ - source_idx;
      const double virtual_offset_idx = subsample_remainder * kKernelOffsetCount;
      const int offset_idx = static_cast<int>(virtual_offset_idx);
      // The two kernels which straddle `virtual_source_idx_`.
      const float* const k1 = kernel_ptr + offset_idx * kKernelSize;
      const float* const k2 = k1 + kKernelSize;
      const float* const input_ptr = r1_ + source_idx;
      const double kernel_interpolation_factor = virtual_offset_idx - offset_idx;
      *destination++ = Convolve(input_ptr, k1, k2, kernel_interpolation_factor);
      virtual_source_idx_ += current_io_ratio;
      if (!--remaining_frames) return;
    }
    // Wrap back around to the start.
    virtual_source_idx_ -= static_cast<double>(block_size_);
    // (3) Copy r3_, r4_ to r1_, r2_: the last input frames go back to the
    // start of the buffer.
    std::memcpy(r1_, r3_, sizeof(float) * kKernelSize);
    // (4) Reinitialize regions if necessary.
    if (r0_ == r2_) UpdateRegions(true);
    // (5) Refresh the buffer with more input.
    Run(request_frames_, r0_);
  }
}

size_t SincResampler::ChunkSize() const {
  return static_cast<size_t>(static_cast<double>(block_size_) / io_sample_rate_ratio_);
}

void SincResampler::Flush() {
  virtual_source_idx_ = 0;
  buffer_primed_ = false;
  std::fill(input_buffer_.begin(), input_buffer_.end(), 0.0f);
  UpdateRegions(false);
}

float SincResampler::Convolve(const float* input_ptr, const float* k1, const float* k2,
                              double kernel_interpolation_factor) {
  float sum1 = 0;
  float sum2 = 0;
  size_t n = kKernelSize;
  while (n--) {
    sum1 += *input_ptr * *k1++;
    sum2 += *input_ptr++ * *k2++;
  }
  // Linearly interpolate the two "convolutions".
  return static_cast<float>((1.0 - kernel_interpolation_factor) * sum1 + kernel_interpolation_factor * sum2);
}

PushSincResampler::PushSincResampler(size_t source_frames, size_t destination_frames)
    : SincResampler(static_cast<double>(source_frames) * 1.0 / static_cast<double>(destination_frames),
                    source_frames),
      destination_frames_(destination_frames) {}

void PushSincResampler::Resample(std::span<const float> source, std::span<float> destination) {
  assert(source.size() == request_frames());
  assert(destination.size() >= destination_frames_);
  source_ptr_ = source.data();
  source_available_ = source.size();
  // On the first pass Resample() runs twice: the first call reads dummy input
  // and its output is discarded. That primes the buffer with the correct delay
  // (half the kernel), so every later call reads its input with one Run().
  // ChunkSize() is exactly the output that takes one Run() of `source_frames`.
  if (first_pass_) SincResampler::Resample(ChunkSize(), destination.data());
  SincResampler::Resample(destination_frames_, destination.data());
  source_ptr_ = nullptr;
}

void PushSincResampler::Run(size_t frames, float* destination) {
  assert(source_available_ == frames);
  if (first_pass_) {
    // Dummy input on the first pass, whose output is discarded.
    std::memset(destination, 0, frames * sizeof(*destination));
    first_pass_ = false;
    return;
  }
  std::memcpy(destination, source_ptr_, frames * sizeof(*destination));
  source_available_ -= frames;
}

}  // namespace sm::audio
