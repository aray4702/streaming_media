// Unit tests: AGC1's digital compression stage (`apm v2`, #242), ported from
// libwebrtc M157's agc/legacy/{digital,analog}_agc.cc and
// gain_control_impl.cc. libwebrtc has no unit tests for these files; the
// golden values come from those files compiled from
// compare/webrtc/reference/ (with the SPL files they call) and driven as
// GainControlImpl drives them in kFixedDigital mode: WebRtcAgc_Init,
// WebRtcAgc_set_config, then per 10 ms frame WebRtcAgc_Analyze and the float
// ApplyDigitalGain, on the signal below.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "sm/audio/agc1.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio::agc1;

namespace {

// Deterministic test signal (FloatS16), shared with the harness that ran
// libwebrtc's files. Integer synthesis only, so it is the same on every
// platform: 400 ms talk spurts (two triangle waves, ~180 and ~550 Hz, with
// a 4 Hz amplitude ripple) and pauses over noise at about -60 dBFS, at
// three levels (peaks about -40, -20 and -1 dBFS) for 10 s each.
std::vector<float> Agc1TestSignal(int fs, int seconds) {
  std::vector<float> x(static_cast<size_t>(fs) * static_cast<size_t>(seconds));
  uint32_t lcg = 12345;
  auto triangle = [](int64_t i, int64_t period) {  // In [-1000, 1000].
    const int64_t p = i % period;
    const int64_t half = period / 2;
    return p < half ? -1000 + 2000 * p / half : 1000 - 2000 * (p - half) / (period - half);
  };
  const int64_t n = static_cast<int64_t>(x.size());
  for (int64_t i = 0; i < n; ++i) {
    lcg = lcg * 1664525u + 1013904223u;
    const int64_t noise = static_cast<int64_t>((lcg >> 16) & 0x3F) - 32;
    const int64_t spurt = i * 5 / (2 * fs);  // 400 ms segments.
    const bool talk = spurt % 3 != 2;
    const int64_t level = i < n / 3 ? 330 : (i < 2 * n / 3 ? 3300 : 29000);
    const int64_t ripple = 800 + triangle(i, fs / 4) / 5;  // 600..1000.
    const int64_t tone = (7 * triangle(i, 89) + 3 * triangle(i, 29)) / 10;
    const int64_t v = talk ? level * ripple / 1000 * tone / 1000 : 0;
    x[static_cast<size_t>(i)] = static_cast<float>(std::clamp<int64_t>(v + noise, -32768, 32767));
  }
  return x;
}

int16_t ToS16(float v) {
  v = std::min(32767.f, std::max(-32768.f, v));
  return static_cast<int16_t>(v + std::copysign(0.5f, v));
}

struct Run {
  std::array<int32_t, 11> gains50{}, gains1000{}, gains2500{};
  uint64_t hash_out = 1469598103934665603ull, hash_gains = 1469598103934665603ull;
};

Run RunGolden(int target, int compression, bool limiter) {
  GainControlConfig cfg;
  cfg.target_level_dbfs = target;
  cfg.compression_gain_db = compression;
  cfg.enable_limiter = limiter;
  GainControl gc(cfg);
  auto x = Agc1TestSignal(16000, 30);
  Run r;
  for (size_t f = 0; f + 160 <= x.size(); f += 160) {
    const std::span<float> bands[1] = {std::span<float>(x.data() + f, 160)};
    gc.ProcessBands(bands);
    for (int32_t g : gc.last_gains()) {
      r.hash_gains ^= static_cast<uint32_t>(g);
      r.hash_gains *= 1099511628211ull;
    }
    for (size_t i = 0; i < 160; ++i) {
      r.hash_out ^= static_cast<uint16_t>(ToS16(x[f + i]));
      r.hash_out *= 1099511628211ull;
    }
    const size_t frame = f / 160;
    if (frame == 50) r.gains50 = gc.last_gains();
    if (frame == 1000) r.gains1000 = gc.last_gains();
    if (frame == 2500) r.gains2500 = gc.last_gains();
  }
  return r;
}

std::array<int32_t, 32> Table(int target, int compression, bool limiter) {
  std::array<int32_t, 32> t{};
  const auto c = static_cast<int16_t>(compression + target);
  CalculateGainTable(t, c, static_cast<int16_t>(target), limiter, c);
  return t;
}

}  // namespace

// WebRtcAgc_CalculateGainTable as WebRtcAgc_set_config calls it in
// fixed-digital mode, for AgcManagerDirect's setup (2 dB, 7 dB, limiter),
// APM's GainController1 defaults (3 dB, 9 dB), AgcManagerDirect's maximum
// compression (12 dB) and no gain.
SM_TEST(Agc1, GainTableMatchesLibwebrtc) {
  const std::array<int32_t, 32> kCall = {
      37092,  52326,  74176,  104632, 136096, 145656, 147512, 147760, 147800, 147800, 147800,
      147776, 147800, 147800, 147776, 147800, 147800, 147776, 147800, 147800, 147776, 147800,
      147800, 147776, 147800, 147800, 147776, 147800, 147800, 147800, 147800, 147800};
  const std::array<int32_t, 32> kDefaults = {
      32814,  45708,  65620,  91416,  131256, 170336, 179856, 181696, 181968, 181984, 182008,
      182008, 181984, 182008, 182008, 181984, 182008, 181984, 181984, 182008, 182008, 181984,
      182008, 181984, 181968, 182008, 181984, 181984, 182008, 181984, 182008, 182008};
  const std::array<int32_t, 32> kMaxCompression = {
      37092,  52326,  74176,  104632, 148344, 209264, 252936, 259784, 260888, 261008, 261040,
      261040, 261040, 261040, 261008, 261040, 261040, 261008, 261040, 261040, 261008, 261040,
      261040, 261008, 261040, 261040, 261008, 261040, 261040, 261008, 261040, 261040};
  SM_EXPECT_TRUE(Table(2, 7, true) == kCall);
  SM_EXPECT_TRUE(Table(3, 9, true) == kDefaults);
  SM_EXPECT_TRUE(Table(2, 12, true) == kMaxCompression);
  for (int32_t g : Table(0, 0, false)) SM_EXPECT_EQ(g, 65536);
}

// The gains and the output, bit-exact, over 30 s of talk spurts (peaks at
// about -40, -20 and -1 dBFS; the last third runs into the limiter).
SM_TEST(Agc1, GainsAndOutputMatchLibwebrtc) {
  {
    const Run r = RunGolden(2, 7, true);
    const std::array<int32_t, 11> g1000 = {147800, 147779, 147800, 147800, 147800, 147800,
                                           147800, 147800, 147800, 147800, 147800};
    for (int32_t g : r.gains50) SM_EXPECT_EQ(g, 147800);
    SM_EXPECT_TRUE(r.gains1000 == g1000);
    for (int32_t g : r.gains2500) SM_EXPECT_EQ(g, 98817);  // The limiter.
    SM_EXPECT_EQ(r.hash_out, 10912781145674789583ull);
    SM_EXPECT_EQ(r.hash_gains, 4286951150141464545ull);
  }
  {
    const Run r = RunGolden(2, 12, true);
    const std::array<int32_t, 11> g1000 = {261040, 261010, 261010, 261011, 261012, 261012,
                                           261013, 261014, 261015, 261016, 261016};
    for (int32_t g : r.gains50) SM_EXPECT_EQ(g, 261040);
    SM_EXPECT_TRUE(r.gains1000 == g1000);
    for (int32_t g : r.gains2500) SM_EXPECT_EQ(g, 98817);
    SM_EXPECT_EQ(r.hash_out, 11813847512680454717ull);
    SM_EXPECT_EQ(r.hash_gains, 5364637501829955990ull);
  }
  {
    const Run r = RunGolden(0, 0, false);
    for (int32_t g : r.gains1000) SM_EXPECT_EQ(g, 65536);
    SM_EXPECT_EQ(r.hash_out, 8522843450546656392ull);
    SM_EXPECT_EQ(r.hash_gains, 3132477922162371683ull);
  }
}

// GainControlImpl's setters keep its ranges ([0, 31] and [0, 90]).
SM_TEST(Agc1, SettersRejectOutOfRange) {
  GainControl gc;
  SM_EXPECT_FALSE(gc.set_target_level_dbfs(32));
  SM_EXPECT_FALSE(gc.set_target_level_dbfs(-1));
  SM_EXPECT_FALSE(gc.set_compression_gain_db(91));
  SM_EXPECT_FALSE(gc.set_compression_gain_db(-1));
  SM_EXPECT_EQ(gc.compression_gain_db(), 7);
  SM_EXPECT_TRUE(gc.set_compression_gain_db(12));
  SM_EXPECT_EQ(gc.compression_gain_db(), 12);
}

// The compression gain is applied below the knee and the limiter holds loud
// input under full scale.
SM_TEST(Agc1, CompressesQuietAndLimitsLoud) {
  GainControl gc;
  std::vector<float> quiet(160), loud(160);
  float peak_out = 0;
  for (int f = 0; f < 300; ++f) {
    for (size_t i = 0; i < 160; ++i) {
      const double t = (f * 160.0 + static_cast<double>(i)) / 16000.0;
      quiet[i] = static_cast<float>(0.01 * std::sin(2 * 3.14159265358979 * 300 * t));
    }
    gc.Process(quiet);
  }
  // At most the table's +7.06 dB below the knee (147800 in Q16); the gate
  // takes a little off a steady tone (low short-term deviation).
  SM_EXPECT_LE(gc.stats().gain_db, 7.07);
  SM_EXPECT_GE(gc.stats().gain_db, 5.0);
  for (int f = 0; f < 300; ++f) {
    for (size_t i = 0; i < 160; ++i) {
      const double t = (f * 160.0 + static_cast<double>(i)) / 16000.0;
      loud[i] = static_cast<float>(0.9 * std::sin(2 * 3.14159265358979 * 300 * t));
    }
    gc.Process(loud);
    for (float v : loud) peak_out = std::max(peak_out, std::fabs(v));
  }
  SM_EXPECT_LE(peak_out, 32767.0f / 32768.0f);
  SM_EXPECT_LT(gc.stats().gain_db, 1.0);
}

// WebRtcAgc_ProcessVad: the log likelihood ratio rises on a level jump and
// falls back in silence.
SM_TEST(Agc1, VadFollowsActivity) {
  AgcVad vad;
  InitVad(vad);
  std::vector<int16_t> frame(160, 0);
  for (int f = 0; f < 20; ++f) ProcessVad(vad, frame.data(), frame.size());
  const int16_t silent = vad.log_ratio;
  int16_t active = silent;
  for (int f = 20; f < 40; ++f) {
    for (size_t i = 0; i < frame.size(); ++i) {
      frame[i] = static_cast<int16_t>(((f * 160 + static_cast<int>(i)) / 20) % 2 ? 8000 : -8000);  // 400 Hz.
    }
    active = ProcessVad(vad, frame.data(), frame.size());
  }
  // The values of libwebrtc's WebRtcAgc_ProcessVad on the same frames.
  SM_EXPECT_EQ(silent, -483);
  SM_EXPECT_EQ(active, 876);
}
