// Cascaded biquads, ported from libwebrtc M157's
// compare/webrtc/reference/modules/audio_processing/utility/cascaded_biquad_filter.*
// (direct form 1, float state). Shared by AEC3 (#240) and the `apm v2`
// high-pass filter (#239).
#pragma once

#include <span>
#include <vector>

namespace sm::audio {

class CascadedBiQuadFilter {
 public:
  struct BiQuadCoefficients {
    float b[3];
    float a[2];
  };
  explicit CascadedBiQuadFilter(std::span<const BiQuadCoefficients> coefficients);
  // Applies the biquads on the values in x to form the output in y.
  void Process(std::span<const float> x, std::span<float> y);
  // Applies the biquads on the values in y in place.
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

}  // namespace sm::audio
