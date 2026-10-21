// Unit tests: libwebrtc M157's high-pass filter (`hpf::HighPassFilter`,
// `apm v2`, issue #239; modules/audio_processing/high_pass_filter.*) and the
// cascaded biquads under it (utility/cascaded_biquad_filter.*).
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "sm/audio/cascaded_biquad_filter.h"
#include "sm/audio/hpf.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

namespace {

// --- Ports of utility/cascaded_biquad_filter_unittest.cc ------------------

// A second order Butterworth high-pass filter with cutoff 100 Hz.
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 1> kHighPassFilterCoefficients = {{
    {{0.97261f, -1.94523f, 0.97261f}, {-1.94448f, 0.94598f}},
}};
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 1> kTransparentCoefficients = {{
    {{1.f, 0.f, 0.f}, {0.f, 0.f}},
}};
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 1> kBlockingCoefficients = {{
    {{0.f, 0.f, 0.f}, {0.f, 0.f}},
}};
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 2> kCascadedCoefficients = {{
    {{-1.f, 0.f, 0.f}, {0.f, 0.f}},
    {{0.f, 0.f, -1.f}, {0.f, 0.f}},
}};

std::vector<float> CreateInputWithIncreasingValues(size_t vector_length) {
  std::vector<float> v(vector_length);
  for (size_t k = 0; k < v.size(); ++k) v[k] = static_cast<float>(k);
  return v;
}

// Gain (dB) of a `freq` Hz sine through the filter at `fs`, after a 1 s
// transient, in 10 ms frames as APM runs it.
double GainDb(double freq, int fs) {
  hpf::HighPassFilter f(fs);
  auto x = sim::Sine(freq, fs, 3.0, 0.5f);
  const size_t frame = static_cast<size_t>(fs / 100);
  for (size_t i = 0; i + frame <= x.size(); i += frame) f.Process(std::span<float>(x.data() + i, frame));
  std::span<const float> tail(x.data() + fs, 2 * static_cast<size_t>(fs));
  return dsp::RmsDbfs(tail) - dsp::LinearToDb(0.5f / std::sqrt(2.0f));
}

}  // namespace

SM_TEST(CascadedBiquadFilter, BlockingConfiguration) {
  std::vector<float> values = CreateInputWithIncreasingValues(1000);
  CascadedBiQuadFilter filter(kBlockingCoefficients);
  filter.Process(values);
  for (float v : values) SM_EXPECT_EQ(v, 0.f);
}

SM_TEST(CascadedBiquadFilter, HighPassConfiguration) {
  std::vector<float> values(1000, 1.f);
  CascadedBiQuadFilter filter(kHighPassFilterCoefficients);
  filter.Process(values);
  for (size_t k = values.size() / 2; k < values.size(); ++k) SM_EXPECT_NEAR(0.f, values[k], 1e-2);
}

SM_TEST(CascadedBiquadFilter, HighPassConfigurationResetFunctionality) {
  CascadedBiQuadFilter filter(kHighPassFilterCoefficients);
  std::vector<float> values1(100, 1.f);
  filter.Process(values1);
  filter.Reset();
  std::vector<float> values2(100, 1.f);
  filter.Process(values2);
  for (size_t k = 0; k < values1.size(); ++k) SM_EXPECT_EQ(values1[k], values2[k]);
}

SM_TEST(CascadedBiquadFilter, TransparentConfiguration) {
  const std::vector<float> input = CreateInputWithIncreasingValues(1000);
  std::vector<float> output(input.size());
  CascadedBiQuadFilter filter(kTransparentCoefficients);
  filter.Process(input, output);
  for (size_t k = 0; k < input.size(); ++k) SM_EXPECT_EQ(input[k], output[k]);
}

SM_TEST(CascadedBiquadFilter, CascadedConfiguration) {
  const std::vector<float> input = CreateInputWithIncreasingValues(1000);
  std::vector<float> output(input.size());
  CascadedBiQuadFilter filter(kCascadedCoefficients);
  filter.Process(input, output);
  // The cascaded biquad is expected to result in a delay of two samples.
  for (size_t i = 2; i < output.size(); ++i) SM_EXPECT_EQ(output[i], input[i - 2]);
}

// --- high_pass_filter.cc --------------------------------------------------

// libwebrtc's 16 kHz table (high_pass_filter.cc,
// kHighPassFilterCoefficients16kHz), in double, for the analytic response.
constexpr double kTable16k[3][5] = {
    {0.8773539420715290582, -1.754683920749088077, 0.8773539420715289472, -1.881687317862849707,
     0.8880584644559580410},
    {1.0, -1.999810143464515022, 1.0, -1.976035417167170793, 0.9779708644868606582},
    {1.0, -1.999669231394235469, 1.0, -1.994265767864654482, 0.9954861594635392441},
};

// |H(e^jw)| (dB) of the 16 kHz table at `freq`.
double TableGainDb(double freq) {
  const double w = 2 * std::numbers::pi * freq / 16000;
  double db = 0;
  for (const auto& c : kTable16k) {
    auto mag = [&](double c0, double c1, double c2) {
      const double re = c0 + c1 * std::cos(w) + c2 * std::cos(2 * w);
      const double im = -c1 * std::sin(w) - c2 * std::sin(2 * w);
      return std::hypot(re, im);
    };
    db += 20 * std::log10(mag(c[0], c[1], c[2]) / mag(1.0, c[3], c[4]));
  }
  return db;
}

// The 16 kHz filter has libwebrtc's response: the notches just below 50 Hz
// take mains hum down by more than 60 dB, and the pass band sits about
// 0.4 dB down (the first section's gain is 0.93 at high frequencies).
SM_TEST(Hpf, FrequencyResponse16kHz) {
  SM_RECORD("hpf2_gain_20hz_db", GainDb(20, 16000));
  SM_RECORD("hpf2_gain_50hz_db", GainDb(50, 16000));
  SM_RECORD("hpf2_gain_60hz_db", GainDb(60, 16000));
  SM_RECORD("hpf2_gain_100hz_db", GainDb(100, 16000));
  SM_RECORD("hpf2_gain_1000hz_db", GainDb(1000, 16000));
  SM_EXPECT_LT(GainDb(20, 16000), -30.0);
  SM_EXPECT_LT(GainDb(50, 16000), -60.0);
  for (double f : {60.0, 100.0, 150.0, 300.0, 1000.0, 3000.0, 6000.0}) {
    SM_EXPECT_NEAR(GainDb(f, 16000), TableGainDb(f), 0.05);
    SM_EXPECT_GT(GainDb(f, 16000), f >= 150 ? -0.7 : -50.0);
    SM_EXPECT_LT(GainDb(f, 16000), 0.2);
  }
}

// The 32 and 48 kHz tables put the same response at the same frequencies.
SM_TEST(Hpf, FrequencyResponseAtEveryRate) {
  for (int fs : {32000, 48000}) {
    SM_EXPECT_LT(GainDb(50, fs), -60.0);
    for (double f : {100.0, 300.0, 1000.0}) SM_EXPECT_NEAR(GainDb(f, fs), GainDb(f, 16000), 0.3);
  }
}

SM_TEST(Hpf, RemovesDcOffset) {
  hpf::HighPassFilter f;
  std::vector<float> x(16000, 0.3f);
  for (size_t i = 0; i < x.size(); i += 160) f.Process(std::span<float>(x.data() + i, 160));
  SM_EXPECT_LT(dsp::RmsDbfs(std::span<const float>(x).last(1600)), -80.0f);
}

// Each channel has its own state; Reset(n) keeps the channel count it is
// given and clears every filter, as HighPassFilter::Reset(size_t).
SM_TEST(Hpf, ChannelsAndReset) {
  hpf::HighPassFilter f(16000, 2);
  SM_EXPECT_EQ(f.num_channels(), 2u);
  SM_EXPECT_EQ(f.sample_rate_hz(), 16000);
  std::vector<std::vector<float>> a(2, std::vector<float>(160, 0.f));
  a[0].assign(160, 1.f);
  f.Process(a);
  for (float v : a[1]) SM_EXPECT_EQ(v, 0.f);
  // A fresh run of the same input after Reset() gives the same output.
  std::vector<float> first = a[0];
  f.Reset();
  std::vector<std::vector<float>> b(2, std::vector<float>(160, 0.f));
  b[0].assign(160, 1.f);
  f.Process(b);
  for (size_t k = 0; k < first.size(); ++k) SM_EXPECT_EQ(first[k], b[0][k]);
  f.Reset(3);
  SM_EXPECT_EQ(f.num_channels(), 3u);
  std::vector<std::vector<float>> c(3, std::vector<float>(160, 1.f));
  f.Process(c);
  for (size_t ch = 0; ch < 3; ++ch) {
    for (size_t k = 0; k < first.size(); ++k) SM_EXPECT_EQ(first[k], c[ch][k]);
  }
  f.Reset(1);
  SM_EXPECT_EQ(f.num_channels(), 1u);
}
