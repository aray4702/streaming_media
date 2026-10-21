// Ported from libwebrtc M157's
// modules/audio_processing/utility/cascaded_biquad_filter.cc.
#include "sm/audio/cascaded_biquad_filter.h"

#include <algorithm>

namespace sm::audio {

CascadedBiQuadFilter::CascadedBiQuadFilter(std::span<const BiQuadCoefficients> coefficients) {
  for (const auto& c : coefficients) biquads_.push_back(BiQuad{c});
}

void CascadedBiQuadFilter::Process(std::span<const float> x, std::span<float> y) {
  if (biquads_.empty()) {
    std::copy(x.begin(), x.end(), y.begin());
    return;
  }
  ApplyBiQuad(x, y, biquads_[0]);
  for (size_t k = 1; k < biquads_.size(); ++k) ApplyBiQuad(y, y, biquads_[k]);
}

void CascadedBiQuadFilter::Process(std::span<float> y) {
  for (auto& biquad : biquads_) ApplyBiQuad(y, y, biquad);
}

void CascadedBiQuadFilter::Reset() {
  for (auto& biquad : biquads_) {
    biquad.x[0] = biquad.x[1] = biquad.y[0] = biquad.y[1] = 0.f;
  }
}

void CascadedBiQuadFilter::ApplyBiQuad(std::span<const float> x, std::span<float> y, BiQuad& biquad) {
  const float* c_b = biquad.coefficients.b;
  const float* c_a = biquad.coefficients.a;
  float* m_x = biquad.x;
  float* m_y = biquad.y;
  for (size_t k = 0; k < x.size(); ++k) {
    const float tmp = x[k];
    y[k] = c_b[0] * tmp + c_b[1] * m_x[0] + c_b[2] * m_x[1] - c_a[0] * m_y[0] - c_a[1] * m_y[1];
    m_x[1] = m_x[0];
    m_x[0] = tmp;
    m_y[1] = m_y[0];
    m_y[0] = y[k];
  }
}

}  // namespace sm::audio
