#include "sm/core/fft.h"

#include <cassert>
#include <cmath>
#include <numbers>

namespace sm {

Fft::Fft(size_t n) : n_(n), bitrev_(n), twiddle_(n / 2) {
  assert(n >= 2 && (n & (n - 1)) == 0);
  size_t bits = 0;
  while ((size_t{1} << bits) < n) ++bits;
  for (size_t i = 0; i < n; ++i) {
    size_t r = 0;
    for (size_t b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
    bitrev_[i] = r;
  }
  for (size_t k = 0; k < n / 2; ++k) {
    double a = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
    twiddle_[k] = Complex(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
  }
}

void Fft::Transform(Complex* d, bool inverse) const {
  for (size_t i = 0; i < n_; ++i) {
    if (i < bitrev_[i]) std::swap(d[i], d[bitrev_[i]]);
  }
  for (size_t len = 2; len <= n_; len <<= 1) {
    size_t half = len / 2;
    size_t step = n_ / len;
    for (size_t i = 0; i < n_; i += len) {
      for (size_t j = 0; j < half; ++j) {
        Complex w = twiddle_[j * step];
        if (inverse) w = std::conj(w);
        Complex u = d[i + j];
        Complex v = d[i + j + half] * w;
        d[i + j] = u + v;
        d[i + j + half] = u - v;
      }
    }
  }
}

void Fft::Inverse(Complex* data) const {
  Transform(data, true);
  const float s = 1.0f / static_cast<float>(n_);
  for (size_t i = 0; i < n_; ++i) data[i] *= s;
}

RealFft::RealFft(size_t n) : n_(n), half_(n / 2), w_(n / 2 + 1), buf_(n / 2) {
  assert(n >= 4);
  for (size_t k = 0; k <= n / 2; ++k) {
    double a = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
    w_[k] = Complex(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
  }
}

void RealFft::Forward(const float* in, Complex* out) {
  const size_t h = n_ / 2;
  for (size_t i = 0; i < h; ++i) buf_[i] = Complex(in[2 * i], in[2 * i + 1]);
  half_.Forward(buf_.data());
  const Complex minus_half_i(0.0f, -0.5f);
  for (size_t k = 0; k <= h; ++k) {
    Complex zk = buf_[k % h];
    Complex zc = std::conj(buf_[(h - k) % h]);
    Complex even = 0.5f * (zk + zc);
    Complex odd = minus_half_i * (zk - zc);
    out[k] = even + w_[k] * odd;
  }
}

void RealFft::Inverse(const Complex* in, float* out) {
  const size_t h = n_ / 2;
  const Complex i_unit(0.0f, 1.0f);
  for (size_t k = 0; k < h; ++k) {
    Complex xk = in[k];
    Complex xc = std::conj(in[h - k]);
    Complex even = 0.5f * (xk + xc);
    Complex odd = 0.5f * (xk - xc) * std::conj(w_[k]);  // Divide by W^k.
    buf_[k] = even + i_unit * odd;
  }
  half_.Inverse(buf_.data());
  for (size_t i = 0; i < h; ++i) {
    out[2 * i] = buf_[i].real();
    out[2 * i + 1] = buf_[i].imag();
  }
}

}  // namespace sm
