// 6.11 Sample-rate conversion: libwebrtc M157's `PushResampler<T>`
// (common_audio/resampler/push_resampler.cc), the resampler APM and the
// audio send and receive paths use: one PushSincResampler per channel over a
// deinterleaved copy, for int16 or FloatS16 samples, up to 8 channels.
// Matching rates copy. libwebrtc assumes 10 ms blocks throughout: every call
// passes the same block sizes, and a new size or channel count re-creates
// the per-channel resamplers (and their state), as libwebrtc.
//
// Ported from compare/webrtc/reference/common_audio/resampler/
// {include/push_resampler.h,push_resampler.cc}. Differences: spans and a
// channel count in place of InterleavedView/MonoView, and the RTC_DCHECKs
// are asserts.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "sm/audio/sinc_resampler.h"

namespace sm::audio {

template <typename T>
class PushResampler {
 public:
  // Maximum concurrent number of channels.
  static constexpr size_t kMaxNumberOfChannels = 8;
  // 10 ms at 384 kHz, libwebrtc's kMaxSamplesPerChannel10ms.
  static constexpr size_t kMaxSamplesPerChannel10ms = 384000 / 100;

  PushResampler() = default;
  PushResampler(size_t src_samples_per_channel, size_t dst_samples_per_channel, size_t num_channels) {
    EnsureInitialized(src_samples_per_channel, dst_samples_per_channel, num_channels);
  }

  // Interleaved `src` and `dst` of `num_channels` channels.
  void Resample(std::span<const T> src, std::span<T> dst, size_t num_channels) {
    assert(num_channels > 0);
    const size_t src_spc = src.size() / num_channels;
    const size_t dst_spc = dst.size() / num_channels;
    EnsureInitialized(src_spc, dst_spc, num_channels);
    if (src_spc == dst_spc) {
      // The old resampler provides this memcpy facility in the case of
      // matching sample rates, so reproduce it here for the sinc resampler.
      std::copy(src.begin(), src.end(), dst.begin());
      return;
    }
    // Deinterleave.
    for (size_t ch = 0; ch < num_channels; ++ch)
      for (size_t j = 0; j < src_spc; ++j) source_[ch * src_spc + j] = src[j * num_channels + ch];
    for (size_t ch = 0; ch < num_channels; ++ch)
      resamplers_[ch]->Resample(std::span<const T>(&source_[ch * src_spc], src_spc),
                                std::span<T>(&destination_[ch * dst_spc], dst_spc));
    // Interleave.
    for (size_t ch = 0; ch < num_channels; ++ch)
      for (size_t j = 0; j < dst_spc; ++j) dst[j * num_channels + ch] = destination_[ch * dst_spc + j];
  }

  // For a mono channel, which skips the deinterleaving.
  void Resample(std::span<const T> src, std::span<T> dst) {
    EnsureInitialized(src.size(), dst.size(), 1);
    assert(resamplers_.size() == 1);
    if (src.size() == dst.size()) {
      std::copy(src.begin(), src.end(), dst.begin());
    } else {
      resamplers_[0]->Resample(src, dst);
    }
  }

 private:
  void EnsureInitialized(size_t src_samples_per_channel, size_t dst_samples_per_channel, size_t num_channels) {
    assert(src_samples_per_channel > 0);
    assert(dst_samples_per_channel > 0);
    assert(num_channels > 0);
    assert(src_samples_per_channel <= kMaxSamplesPerChannel10ms);
    assert(dst_samples_per_channel <= kMaxSamplesPerChannel10ms);
    assert(num_channels <= kMaxNumberOfChannels);
    if (src_samples_per_channel == src_spc_ && dst_samples_per_channel == dst_spc_ &&
        num_channels == resamplers_.size()) {
      return;  // No-op if settings haven't changed.
    }
    src_spc_ = src_samples_per_channel;
    dst_spc_ = dst_samples_per_channel;
    source_.assign(src_samples_per_channel * num_channels, T{});
    destination_.assign(dst_samples_per_channel * num_channels, T{});
    resamplers_.clear();
    for (size_t i = 0; i < num_channels; ++i)
      resamplers_.push_back(std::make_unique<PushSincResampler>(src_samples_per_channel, dst_samples_per_channel));
  }

  size_t src_spc_ = 0;
  size_t dst_spc_ = 0;
  // Buffers for when a deinterleaving step is necessary.
  std::vector<T> source_;
  std::vector<T> destination_;
  std::vector<std::unique_ptr<PushSincResampler>> resamplers_;
};

}  // namespace sm::audio
