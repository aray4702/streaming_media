// Portable radix-2 FFT (no SIMD, no external dependency). Used by the echo
// canceller, delay estimator and noise suppressor.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace sm {

using Complex = std::complex<float>;

// In-place complex FFT. n must be a power of two.
class Fft {
 public:
  explicit Fft(size_t n);
  size_t size() const { return n_; }
  void Forward(Complex* data) const { Transform(data, false); }
  // Scaled by 1/n so Inverse(Forward(x)) == x.
  void Inverse(Complex* data) const;

 private:
  void Transform(Complex* data, bool inverse) const;
  size_t n_;
  std::vector<size_t> bitrev_;
  std::vector<Complex> twiddle_;  // e^{-2 pi i k / n}, k < n/2.
};

// Real FFT of size n via a half-size complex FFT.
class RealFft {
 public:
  explicit RealFft(size_t n);
  size_t size() const { return n_; }
  size_t bins() const { return n_ / 2 + 1; }
  // in: n samples. out: n/2+1 bins (unscaled).
  void Forward(const float* in, Complex* out);
  // in: n/2+1 bins. out: n samples, scaled so Inverse(Forward(x)) == x.
  void Inverse(const Complex* in, float* out);

 private:
  size_t n_;
  Fft half_;
  std::vector<Complex> w_;  // e^{-2 pi i k / n}, k <= n/2.
  std::vector<Complex> buf_;
};

}  // namespace sm
