// libwebrtc M157's SincResampler and PushSincResampler: the resampler
// `PushResampler<float>` runs per channel, which AGC2's VAD wrapper uses to
// bring a 10 ms capture frame to the RNN VAD's 24 kHz (#242).
//
// Ported from compare/webrtc/reference/common_audio/resampler/
// {sinc_resampler,push_sinc_resampler}.{h,cc} with their constants: 32-tap
// Blackman-windowed sinc kernels at 32 sub-sample offsets (plus one), cutoff
// 0.9 x the lower Nyquist frequency, linear interpolation between the two
// kernels around the source position. The push wrapper primes the buffer on
// the first call with one chunk of zeros, so the delay is half a kernel.
//
// Difference from libwebrtc: the convolution is Convolve_C, the portable
// path. libwebrtc picks its SSE2, AVX2 or NEON version at run time, which sum
// in another order (results differ in the last bits of a float).
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sm::audio {

class SincResampler {
 public:
  static constexpr size_t kKernelSize = 32;
  static constexpr size_t kKernelOffsetCount = 32;
  static constexpr size_t kKernelStorageSize = kKernelSize * (kKernelOffsetCount + 1);

  // `io_sample_rate_ratio` is input rate / output rate. Each refill reads
  // `request_frames` input samples from the callback.
  SincResampler(double io_sample_rate_ratio, size_t request_frames);
  virtual ~SincResampler() = default;
  SincResampler(const SincResampler&) = delete;
  SincResampler& operator=(const SincResampler&) = delete;

  // Writes `frames` output samples, calling Run() for more input as needed.
  void Resample(size_t frames, float* destination);
  // Output frames produced per Run() call.
  size_t ChunkSize() const;
  size_t request_frames() const { return request_frames_; }
  void Flush();
  std::span<const float> kernel_for_testing() const { return kernel_storage_; }

 protected:
  // Fills `destination` with `frames` input samples.
  virtual void Run(size_t frames, float* destination) = 0;

 private:
  void InitializeKernel();
  void UpdateRegions(bool second_load);
  static float Convolve(const float* input_ptr, const float* k1, const float* k2,
                        double kernel_interpolation_factor);

  double io_sample_rate_ratio_;
  double virtual_source_idx_ = 0;
  bool buffer_primed_ = false;
  const size_t request_frames_;
  size_t block_size_ = 0;
  const size_t input_buffer_size_;
  std::vector<float> kernel_storage_;
  std::vector<float> input_buffer_;
  float* r0_ = nullptr;
  float* r1_ = nullptr;
  float* r2_ = nullptr;
  float* r3_ = nullptr;
  float* r4_ = nullptr;
};

// A push interface: `source_frames` in, `destination_frames` out per call.
class PushSincResampler : private SincResampler {
 public:
  PushSincResampler(size_t source_frames, size_t destination_frames);
  // `source` must hold `source_frames` and `destination` `destination_frames`.
  void Resample(std::span<const float> source, std::span<float> destination);
  // The SincResampler's ChunkSize(); before the first Resample() call,
  // destination_frames minus it is the delay in output samples.
  size_t chunk_size_for_testing() const { return ChunkSize(); }

 private:
  void Run(size_t frames, float* destination) override;

  const float* source_ptr_ = nullptr;
  const size_t destination_frames_;
  bool first_pass_ = true;
  size_t source_available_ = 0;
};

}  // namespace sm::audio
