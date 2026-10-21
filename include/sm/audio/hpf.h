// 6.5 High-pass filter as libwebrtc M157's HighPassFilter. `apm v2` (#239).
//
// Ported from compare/webrtc/reference/modules/audio_processing/
// high_pass_filter.* with libwebrtc's coefficient tables: three cascaded
// biquads per rate (kHighPassFilterCoefficients16kHz/32kHz/48kHz,
// utility/cascaded_biquad_filter.*), the first a high-pass section and the
// other two notches with zeros on the unit circle just below 50 Hz (about
// 35 and 46 Hz at 16 kHz), so mains hum is taken down by ~67 dB while
// speech above ~100 Hz passes within 0.2 dB. One filter per channel.
//
// APM applies it in the full band (`apply_in_full_band`, the default), at
// the capture's full-band rate, first in the capture chain
// (AudioProcessingImpl::ProcessCaptureStreamLocked). Only 16, 32 and
// 48 kHz have tables; libwebrtc DCHECKs on another rate and uses the 16 kHz
// table in release builds, as this port does.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sm/audio/cascaded_biquad_filter.h"
#include "sm/core/metrics.h"

namespace sm::audio::hpf {

class HighPassFilter : public MetricsSource {
 public:
  explicit HighPassFilter(int sample_rate_hz = 16000, size_t num_channels = 1);
  // Filters one channel's samples in place.
  void Process(std::span<float> x, size_t channel = 0);
  // Filters every channel in place (HighPassFilter::Process(vector*)).
  void Process(std::vector<std::vector<float>>& audio);
  void Reset();
  // Keeps the state of the channels that remain, adds fresh ones.
  void Reset(size_t num_channels);

  int sample_rate_hz() const { return sample_rate_hz_; }
  size_t num_channels() const { return filters_.size(); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  const int sample_rate_hz_;
  std::vector<CascadedBiQuadFilter> filters_;
  uint64_t samples_ = 0;
};

}  // namespace sm::audio::hpf
