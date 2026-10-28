// Deterministic, platform-independent PRNG (xoshiro256**). std::
// distributions differ between standard libraries, so tests and simulations
// use this to get identical results on every platform.
#pragma once

#include <cmath>
#include <cstdint>

namespace sm {

class Random {
 public:
  explicit Random(uint64_t seed = 0x5eed) {
    uint64_t z = seed;
    for (auto& s : s_) s = SplitMix(z);
  }
  uint64_t Next() {
    const uint64_t result = Rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = Rotl(s_[3], 45);
    return result;
  }
  // Uniform in [0, 1).
  double Uniform() { return static_cast<double>(Next() >> 11) * 0x1.0p-53; }
  double Uniform(double lo, double hi) { return lo + (hi - lo) * Uniform(); }
  // Uniform integer in [lo, hi].
  int64_t UniformInt(int64_t lo, int64_t hi) {
    return lo + static_cast<int64_t>(Next() % static_cast<uint64_t>(hi - lo + 1));
  }
  bool Bernoulli(double p) { return Uniform() < p; }
  double Gaussian(double mean = 0, double stddev = 1) {
    if (has_spare_) {
      has_spare_ = false;
      return mean + stddev * spare_;
    }
    double u, v, s;
    do {
      u = Uniform() * 2 - 1;
      v = Uniform() * 2 - 1;
      s = u * u + v * v;
    } while (s >= 1 || s == 0);
    double m = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * m;
    has_spare_ = true;
    return mean + stddev * u * m;
  }

 private:
  static uint64_t Rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  static uint64_t SplitMix(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }
  uint64_t s_[4];
  double spare_ = 0;
  bool has_spare_ = false;
};

// libwebrtc's webrtc::Random (rtc_base/random.{h,cc}): xorshift64* with the
// same output multiplier and the same integer draws, for the places where a
// port must draw the very numbers libwebrtc draws from the same seed (the
// RTCP report intervals, rtcp_sender.cc:130 and :693; issue #517).
class XorshiftRandom {
 public:
  // libwebrtc DCHECKs a nonzero seed: zero would stay zero forever.
  explicit XorshiftRandom(uint64_t seed) : state_(seed != 0 ? seed : 1) {}
  // Uniform in [0, t]: the low 32 bits of the output scaled by t + 1.
  uint32_t Rand(uint32_t t) {
    const uint32_t x = static_cast<uint32_t>(NextOutput());
    return static_cast<uint32_t>((x * (static_cast<uint64_t>(t) + 1)) >> 32);
  }
  // Uniform in [low, high].
  uint32_t Rand(uint32_t low, uint32_t high) { return Rand(high - low) + low; }
  int32_t Rand(int32_t low, int32_t high) {
    const int64_t low_i64 = low;
    return static_cast<int32_t>(Rand(static_cast<uint32_t>(high - low_i64)) + low_i64);
  }

 private:
  uint64_t NextOutput() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 2685821657736338717ull;
  }
  uint64_t state_;
};

}  // namespace sm
