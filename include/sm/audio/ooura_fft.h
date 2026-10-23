// libwebrtc M157's 128-point Ooura real FFT, OouraFft
// (compare/webrtc/reference/common_audio/third_party/ooura/fft_size_128/),
// which AEC3's Aec3Fft runs on. The C path with its stored tables; see
// src/audio/ooura_fft.cc.
#pragma once

namespace sm::audio::ooura {

// OouraFft (fft_size_128): the in-place rdft of a[0..127].
// Fft: a[2k] = sum_j a[j] cos(2 pi j k / 128), a[2k+1] = sum_j a[j]
// sin(2 pi j k / 128), with a[1] the bin 64.
// InverseFft: the inverse of that, scaled by 64.
class OouraFft128 {
 public:
  void Fft(float* a) const;
  void InverseFft(float* a) const;
};

}  // namespace sm::audio::ooura
