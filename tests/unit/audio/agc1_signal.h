// Deterministic test signal for the AGC1 golden tests (agc1_test.cc,
// agc1_vad_test.cc), shared with the throwaway harnesses that ran
// libwebrtc's own files to produce the golden values. Integer synthesis only,
// so it is the same on every platform.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// FloatS16 samples: 400 ms talk spurts (two triangle waves, ~180 and
// ~550 Hz, with a 4 Hz amplitude ripple) and pauses over noise at about
// -60 dBFS, at three levels (peaks about -40, -20 and -1 dBFS) for 10 s each.
inline std::vector<float> Agc1TestSignal(int fs, int seconds) {
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

// A more voice-like variant (FloatS16) that libwebrtc's pitch-based VAD takes
// for speech: a glottal pulse train at 125-180 Hz (a slow pitch glide)
// through two fixed-point formant resonators (~600 and ~1500 Hz), in 600 ms
// talk spurts with 300 ms pauses over noise at about -60 dBFS, at three
// levels (peaks about -30, -15 and -3 dBFS) for 10 s each.
inline std::vector<float> Agc1VoicedTestSignal(int fs, int seconds) {
  std::vector<float> x(static_cast<size_t>(fs) * static_cast<size_t>(seconds));
  uint32_t lcg = 54321;
  auto triangle = [](int64_t i, int64_t period) {  // In [-1000, 1000].
    const int64_t p = i % period;
    const int64_t half = period / 2;
    return p < half ? -1000 + 2000 * p / half : 1000 - 2000 * (p - half) / (period - half);
  };
  const int64_t n = static_cast<int64_t>(x.size());
  int64_t y1 = 0, y2 = 0, z1 = 0, z2 = 0;  // Resonator states.
  int64_t countdown = 0;
  for (int64_t i = 0; i < n; ++i) {
    lcg = lcg * 1664525u + 1013904223u;
    const int64_t noise = static_cast<int64_t>((lcg >> 16) & 0x3F) - 32;
    const bool talk = (i * 10 / fs) % 9 < 6;  // 600 ms on, 300 ms off.
    int64_t e = 0;
    if (--countdown <= 0) {
      countdown = fs / 152 + triangle(i, 2 * fs) * fs / 4400000;  // 125..180 Hz.
      e = talk ? 4096 : 0;
    }
    // Formant 1: r = 0.97, 600 Hz; formant 2: r = 0.9, 1500 Hz (Q14).
    const int64_t y = e + ((30907 * y1 - 15416 * y2) >> 14);
    y2 = y1;
    y1 = y;
    const int64_t z = y + ((24522 * z1 - 13271 * z2) >> 14);
    z2 = z1;
    z1 = z;
    const int64_t level = i < n / 3 ? 1 : (i < 2 * n / 3 ? 6 : 24);
    const int64_t v = z * level / 64;
    x[static_cast<size_t>(i)] = static_cast<float>(std::clamp<int64_t>(v + noise, -32768, 32767));
  }
  return x;
}
