// Unit tests: band splitting (libwebrtc's SplittingFilter, issue #241).
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include "sm/audio/splitting_filter.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

namespace {

constexpr size_t kBand = SplittingFilter::kSamplesPerBand;

struct SplitResult {
  std::vector<double> band_energy;  // Mean square per band, per chunk: [chunk * bands + band].
  std::vector<double> xcorr;        // Best input/output cross-correlation per chunk.
  std::vector<int> present;         // Bit mask of the tones in each chunk.
};

// Each 10 ms chunk carries the tones whose bit is set in the chunk index,
// one per band. Records the band energies after analysis and the best
// cross-correlation of the reconstruction with the input (over lags within
// the chunk).
SplitResult SplitTones(int fs, const std::vector<double>& freqs, float amplitude, size_t chunks) {
  SplittingFilter f(fs);
  const size_t n = f.full_band_samples();
  const size_t bands = static_cast<size_t>(f.num_bands());
  SplitResult r;
  std::vector<float> in(n), split(n), out(n);
  for (size_t c = 0; c < chunks; ++c) {
    int mask = static_cast<int>(c % (size_t{1} << bands));
    r.present.push_back(mask);
    std::fill(in.begin(), in.end(), 0.f);
    for (size_t b = 0; b < bands; ++b) {
      if (!(mask & (1 << b))) continue;
      for (size_t k = 0; k < n; ++k) {
        double t = static_cast<double>(c * n + k) / fs;
        in[k] += amplitude * static_cast<float>(std::sin(2 * std::numbers::pi * freqs[b] * t));
      }
    }
    f.Analysis(in, split);
    for (size_t b = 0; b < bands; ++b) {
      double e = 0;
      for (size_t k = 0; k < kBand; ++k) e += split[b * kBand + k] * split[b * kBand + k];
      r.band_energy.push_back(e / kBand);
    }
    f.Synthesis(split, out);
    double best = 0;
    for (size_t lag = 0; lag < n; ++lag) {
      double x = 0;
      for (size_t j = lag; j < n; ++j) x += in[j - lag] * out[j];
      best = std::max(best, x / static_cast<double>(n));
    }
    r.xcorr.push_back(best);
  }
  return r;
}

void ExpectSplits(const SplitResult& r, size_t bands, float amplitude) {
  const double quarter = amplitude * amplitude / 4.0;
  for (size_t c = 0; c < r.present.size(); ++c) {
    for (size_t b = 0; b < bands; ++b) {
      double e = r.band_energy[c * bands + b];
      if (r.present[c] & (1 << b)) {
        SM_EXPECT_GT(e, quarter);
      } else {
        SM_EXPECT_LT(e, quarter);
      }
    }
    if (r.present[c] != 0) SM_EXPECT_GT(r.xcorr[c], quarter);
  }
}

}  // namespace

SM_TEST(SplittingFilter, SplitsIntoTwoBandsAndReconstructs) {
  // 32 kHz: band 0 is 0-8 kHz, band 1 is 8-16 kHz.
  const float kAmplitude = 0.25f;
  auto r = SplitTones(32000, {1000, 12000}, kAmplitude, 8);
  ExpectSplits(r, 2, kAmplitude);
}

SM_TEST(SplittingFilter, SplitsIntoThreeBandsAndReconstructs) {
  // 48 kHz: bands of 0-8, 8-16 and 16-24 kHz.
  const float kAmplitude = 0.25f;
  auto r = SplitTones(48000, {1000, 12000, 18000}, kAmplitude, 8);
  ExpectSplits(r, 3, kAmplitude);
}

SM_TEST(SplittingFilter, TwoBandQmfIsAllPassCascadeOnEvenAndOddSamples) {
  // An impulse through analysis, against a direct-form evaluation of
  // WebRtcSpl_AllPassQMF's three sections: y[n] = a x[n] + x[n-1] - a y[n-1].
  auto cascade = [](std::vector<double> x, const double (&a)[3]) {
    for (double c : a) {
      std::vector<double> y(x.size());
      double x1 = 0, y1 = 0;
      for (size_t n = 0; n < x.size(); ++n) {
        y[n] = c * x[n] + x1 - c * y1;
        x1 = x[n];
        y1 = y[n];
      }
      x = y;
    }
    return x;
  };
  const double a1[3] = {0.0979309082, 0.5643005371, 0.8737335205};
  const double a2[3] = {0.32551574707, 0.74862670898, 0.96145629882};

  SplittingFilter f(32000);
  std::vector<float> in(320, 0.f), bands(320);
  in[1] = 0.5f;  // Odd sample: goes through the first all-pass branch.
  in[2] = 0.25f;  // Even sample: the second branch.
  std::vector<double> odd(kBand, 0.0), even(kBand, 0.0);
  for (size_t i = 0; i < kBand; ++i) {
    even[i] = in[2 * i];
    odd[i] = in[2 * i + 1];
  }
  auto f1 = cascade(odd, a1), f2 = cascade(even, a2);
  // Two frames, so the second continues from the stored state.
  f.Analysis(in, bands);
  for (size_t i = 0; i < kBand; ++i) {
    SM_EXPECT_NEAR(bands[i], 0.5 * (f1[i] + f2[i]), 1e-6);
    SM_EXPECT_NEAR(bands[kBand + i], 0.5 * (f1[i] - f2[i]), 1e-6);
  }
  std::vector<float> zeros(320, 0.f);
  std::vector<double> z(kBand * 2, 0.0), odd2(kBand * 2, 0.0), even2(kBand * 2, 0.0);
  for (size_t i = 0; i < kBand; ++i) {
    odd2[i] = odd[i];
    even2[i] = even[i];
  }
  auto g1 = cascade(odd2, a1), g2 = cascade(even2, a2);
  f.Analysis(zeros, bands);
  for (size_t i = 0; i < kBand; ++i) {
    SM_EXPECT_NEAR(bands[i], 0.5 * (g1[kBand + i] + g2[kBand + i]), 1e-6);
    SM_EXPECT_NEAR(bands[kBand + i], 0.5 * (g1[kBand + i] - g2[kBand + i]), 1e-6);
  }
}

SM_TEST(SplittingFilter, ReconstructionKeepsTheLevel) {
  // A white-ish chirp through analysis and synthesis: the two-band QMF is
  // all-pass (magnitude kept); the three-band bank keeps the level within
  // its pass-band ripple at a fixed delay.
  for (int fs : {32000, 48000}) {
    SplittingFilter f(fs);
    const size_t n = f.full_band_samples();
    const size_t chunks = 50;
    std::vector<float> in(n * chunks), out(n * chunks), split(n);
    for (size_t i = 0; i < in.size(); ++i) {
      double t = static_cast<double>(i) / fs;
      in[i] = 0.1f * static_cast<float>(std::sin(2 * std::numbers::pi * (200 + 2000 * t) * t));  // 200 Hz -> 4.2 kHz.
    }
    for (size_t c = 0; c < chunks; ++c) {
      f.Analysis(std::span<const float>(in.data() + c * n, n), split);
      f.Synthesis(split, std::span<float>(out.data() + c * n, n));
    }
    double ein = 0, eout = 0;
    for (size_t i = n * 5; i < in.size(); ++i) {
      ein += in[i] * in[i];
      eout += out[i] * out[i];
    }
    double gain_db = 10 * std::log10(eout / ein);
    SM_RECORD(fs == 32000 ? "split2_reconstruction_gain_db" : "split3_reconstruction_gain_db", gain_db);
    SM_EXPECT_NEAR(gain_db, 0.0, fs == 32000 ? 0.05 : 0.5);
  }
}

SM_TEST(SplittingFilter, SynthesisSaturatesAsS16) {
  // WebRtcSpl_SynthesisQMF clamps to [-32768, 32767]; on [-1, 1) floats.
  SplittingFilter f(32000);
  std::vector<float> bands(320, 0.f), out(320);
  for (size_t i = 0; i < kBand; ++i) bands[i] = 4.0f;
  f.Synthesis(bands, out);
  float mx = 0, mn = 0;
  for (float v : out) {
    mx = std::max(mx, v);
    mn = std::min(mn, v);
  }
  SM_EXPECT_NEAR(mx, 32767.0f / 32768.0f, 1e-7);
  bands.assign(320, 0.f);
  for (size_t i = 0; i < kBand; ++i) bands[i] = -4.0f;
  SplittingFilter g(32000);
  g.Synthesis(bands, out);
  for (float v : out) mn = std::min(mn, v);
  SM_EXPECT_NEAR(mn, -1.0f, 1e-7);
}

namespace {
// Three tones and a deterministic pseudo-random sequence.
float GoldenInput(int fs, size_t i) {
  double t = static_cast<double>(i) / fs;
  uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
  double r = static_cast<double>(h >> 8) / static_cast<double>(1u << 24) - 0.5;
  return static_cast<float>(0.2 * std::sin(2 * std::numbers::pi * 700 * t) +
                            0.1 * std::sin(2 * std::numbers::pi * 9500 * t) +
                            0.05 * std::sin(2 * std::numbers::pi * 19000 * t) + 0.1 * r);
}

struct Golden {
  size_t index;
  double value;
};
}  // namespace

SM_TEST(SplittingFilter, MatchesLibwebrtcOutput) {
  // libwebrtc's own splitting_filter.c and three_band_filter_bank.cc (M157,
  // compiled with stub check macros) on GoldenInput's fifth 10 ms frame,
  // scaled from S16 back to [-1, 1). The port was bit-exact on that build.
  const Golden bands32[] = {{0, -0.0603633709},  {37, -0.0765799135}, {159, -0.116980962},
                            {160, 0.00779802725}, {201, 0.0895136148},  {319, 0.103714623}};
  const Golden out32[] = {{0, -0.0463352352}, {1, -0.0116983652},  {100, 0.0474858657},
                          {255, -0.0511841625}, {319, -0.156902105}};
  const Golden bands48[] = {{0, -0.175306737},   {37, 0.191529408},    {159, -0.131639615},
                            {160, -0.0850182772}, {201, -0.0389476903}, {319, 0.0285732672},
                            {320, 0.0285155103},  {400, 0.0191811696},  {479, 0.0496741273}};
  const Golden out48[] = {{0, 0.0218553357},   {1, 0.289489746},    {100, -0.195304602}, {255, 0.0676051378},
                          {319, -0.0818151683}, {400, 0.1654796}, {479, 0.11490982}};
  for (int fs : {32000, 48000}) {
    SplittingFilter f(fs);
    const size_t n = f.full_band_samples();
    std::vector<float> in(n), bands(n), out(n);
    for (size_t frame = 0; frame < 5; ++frame) {
      for (size_t i = 0; i < n; ++i) in[i] = GoldenInput(fs, frame * n + i);
      f.Analysis(in, bands);
      f.Synthesis(bands, out);
    }
    // Float rounding may differ by platform (FMA contraction), not more.
    for (const Golden& g : fs == 32000 ? std::span<const Golden>(bands32) : std::span<const Golden>(bands48)) {
      SM_EXPECT_NEAR(bands[g.index], g.value, 1e-5);
    }
    for (const Golden& g : fs == 32000 ? std::span<const Golden>(out32) : std::span<const Golden>(out48)) {
      SM_EXPECT_NEAR(out[g.index], g.value, 1e-5);
    }
  }
}
