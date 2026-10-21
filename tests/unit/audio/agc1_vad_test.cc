// Unit tests: AGC1's `Agc` with its VoiceActivityDetector and WebRtcVad
// (`apm v2`, #242), ported from libwebrtc M157 (see sm/audio/agc1_vad.h).
//
// The golden values (`*MatchesLibwebrtc*`) come from libwebrtc's own files
// compiled from compare/webrtc/reference/ (webrtc_vad.c, vad_core.c,
// vad_filterbank.c, vad_gmm.c, vad_sp.c, the SPL energy, scaling and division
// files, the iSAC filter_functions.c, isac_vad.c, pitch_estimator.c and
// pitch_filter.c, fft4g.cc, and modules/audio_processing/vad/*.cc and
// agc/{agc,loudness_histogram,utility}.cc, with stubs for rtc_base's check
// macros and the never-called Resampler), with GCC 13 -O2 -ffp-contract=off
// -DWEBRTC_POSIX on x86-64 glibc, driven on the signals of agc1_signal.h at
// 16 kHz (S16):
//  - `WebRtcVad_Create`/`_Init`/`_set_mode`, then `WebRtcVad_Process` per
//    frame; FNV-1a-style hash (h ^= value; h *= prime) of the decisions;
//  - `VoiceActivityDetector::ProcessChunk` per 10 ms; hash of the bit
//    patterns of every chunkwise probability then RMS, and of
//    `last_voice_probability()` per chunk;
//  - `Agc::Process` then `Agc::GetRmsErrorDb` per 10 ms, as `MonoAgc` calls
//    them; the frames that returned true and their errors.
// The float results go through libm (exp, log, pow, sin, cos), so the
// bit-exact hashes are checked where they were produced (x86-64 glibc);
// elsewhere the values are checked to a tolerance.
//
// The other tests port libwebrtc's own unit tests where they need no
// resource files: vad_core, vad_filterbank, vad_gmm, vad_sp and vad
// unittests (common_audio/vad), gmm, pitch_internal, pole_zero_filter,
// vad_circular_buffer and standalone_vad (Api) unittests.
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "agc1_signal.h"
#include "sm/audio/agc1_vad.h"
#include "sm_test.h"

using namespace sm::audio::agc1;
using namespace sm::audio::agc1::detail;

namespace {

#if defined(__GLIBC__) && defined(__x86_64__)
constexpr bool kExactLibm = true;
#else
constexpr bool kExactLibm = false;
#endif

constexpr uint64_t kFnvBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
void Mix(uint64_t& h, uint64_t v) {
  h ^= v;
  h *= kFnvPrime;
}

std::vector<int16_t> S16(const std::vector<float>& x) {
  std::vector<int16_t> s(x.size());
  for (size_t i = 0; i < x.size(); ++i) s[i] = static_cast<int16_t>(x[i]);
  return s;
}

// The signal with digital silence in seconds 3, 10, 17 and 24.
std::vector<int16_t> WithSilence(std::vector<int16_t> s) {
  for (size_t i = 0; i < s.size(); ++i)
    if ((i / 16000) % 7 == 3) s[i] = 0;
  return s;
}

// vad_unittest.cc: ValidRatesAndFrameLengths (10, 20, 30 ms at 8, 16, 32,
// 48 kHz).
bool ValidRatesAndFrameLengths(int rate, size_t frame_length) {
  if (rate == 8000 || rate == 16000 || rate == 32000 || rate == 48000) {
    const size_t ms10 = static_cast<size_t>(rate / 100);
    return frame_length == ms10 || frame_length == 2 * ms10 || frame_length == 3 * ms10;
  }
  return false;
}
constexpr size_t kMaxFrameLength = 1440;
constexpr size_t kFrameLengths[] = {80, 120, 160, 240, 320, 480, 640, 960, kMaxFrameLength};

struct VadRun {
  int frames = 0, active = 0;
  uint64_t hash = kFnvBasis;
};

VadRun RunVad(int fs, int mode, int ms) {
  const auto s = S16(Agc1TestSignal(fs, 30));
  WebRtcVad vad;
  vad.set_mode(mode);
  const auto len = static_cast<size_t>(fs / 1000 * ms);
  VadRun r;
  for (size_t f = 0; f + len <= s.size(); f += len) {
    const int d = vad.Process(fs, s.data() + f, len);
    Mix(r.hash, static_cast<uint32_t>(d));
    r.active += d;
    ++r.frames;
  }
  return r;
}

struct VadSample {
  size_t chunk;
  uint64_t p[3];
  uint64_t rms[3];
};

struct VadDetectorRun {
  size_t count = 0;
  uint64_t hash = kFnvBasis, last_hash = kFnvBasis;
  std::vector<std::vector<double>> p, rms;  // Per chunk.
  std::vector<float> last;
};

VadDetectorRun RunVoiceActivityDetector(const std::vector<int16_t>& s) {
  VoiceActivityDetector vad;
  VadDetectorRun r;
  for (size_t f = 0; f + 160 <= s.size(); f += 160) {
    vad.ProcessChunk(std::span<const int16_t>(s.data() + f, 160));
    const auto p = vad.chunkwise_voice_probabilities();
    const auto rms = vad.chunkwise_rms();
    for (size_t j = 0; j < p.size(); ++j) {
      Mix(r.hash, std::bit_cast<uint64_t>(p[j]));
      Mix(r.hash, std::bit_cast<uint64_t>(rms[j]));
    }
    r.count += p.size();
    Mix(r.last_hash, std::bit_cast<uint32_t>(vad.last_voice_probability()));
    r.p.emplace_back(p.begin(), p.end());
    r.rms.emplace_back(rms.begin(), rms.end());
    r.last.push_back(vad.last_voice_probability());
  }
  return r;
}

void ExpectSamples(const VadDetectorRun& r, std::span<const VadSample> samples) {
  for (const VadSample& s : samples) {
    SM_ASSERT_EQ(r.p[s.chunk].size(), size_t{3});
    for (size_t j = 0; j < 3; ++j) {
      const double p = std::bit_cast<double>(s.p[j]);
      const double rms = std::bit_cast<double>(s.rms[j]);
      if (kExactLibm) {
        SM_EXPECT_EQ(std::bit_cast<uint64_t>(r.p[s.chunk][j]), s.p[j]);
        SM_EXPECT_EQ(std::bit_cast<uint64_t>(r.rms[s.chunk][j]), s.rms[j]);
      } else {
        SM_EXPECT_NEAR(r.p[s.chunk][j], p, 1e-6);
        SM_EXPECT_NEAR(r.rms[s.chunk][j], rms, 1e-9 * rms);
      }
    }
  }
}

struct AgcUpdate {
  size_t frame;
  int error;
};

struct AgcRun {
  std::vector<AgcUpdate> updates;
  uint64_t hash = kFnvBasis;
  std::vector<float> voice_probability;
};

AgcRun RunAgc(const std::vector<int16_t>& s) {
  Agc agc;
  AgcRun r;
  for (size_t f = 0, frame = 0; f + 160 <= s.size(); f += 160, ++frame) {
    agc.Process(std::span<const int16_t>(s.data() + f, 160));
    int error = 0;
    if (agc.GetRmsErrorDb(&error)) {
      Mix(r.hash, frame);
      Mix(r.hash, static_cast<uint32_t>(error));
      r.updates.push_back({frame, error});
    }
    r.voice_probability.push_back(agc.voice_probability());
  }
  return r;
}

void ExpectUpdates(const AgcRun& r, std::span<const AgcUpdate> golden, uint64_t hash) {
  if (kExactLibm) {
    SM_ASSERT_EQ(r.updates.size(), golden.size());
    for (size_t i = 0; i < golden.size(); ++i) {
      SM_EXPECT_EQ(r.updates[i].frame, golden[i].frame);
      SM_EXPECT_EQ(r.updates[i].error, golden[i].error);
    }
    SM_EXPECT_EQ(r.hash, hash);
  } else {
    // Another libm may round a probability differently by an ulp; the
    // updates then stay within a frame count and a dB.
    SM_EXPECT_NEAR(static_cast<double>(r.updates.size()), static_cast<double>(golden.size()), 1.0);
    SM_ASSERT_GE(r.updates.size(), size_t{2});
    SM_EXPECT_NEAR(r.updates.front().error, golden.front().error, 1);
    SM_EXPECT_NEAR(r.updates.back().error, golden.back().error, 1);
  }
}

}  // namespace

// ---- Golden values: libwebrtc's files on the same signals ----------------

// WebRtcVad as StandaloneVad uses it (mode 3, 16 kHz; 10 ms and the 30 ms
// buffers), the quality mode, and the 8 and 32 kHz paths. Integer only.
SM_TEST(Agc1VoiceActivity, WebRtcVadMatchesLibwebrtc) {
  struct Case {
    int fs, mode, ms, frames, active;
    uint64_t hash;
  };
  const Case kCases[] = {
      {16000, 3, 10, 3000, 1494, 3100468529113804529ull},  {16000, 0, 10, 3000, 2312, 14567716683287713683ull},
      {16000, 3, 30, 1000, 503, 1669540966902688216ull},   {8000, 3, 10, 3000, 1497, 5213465524202215364ull},
      {32000, 3, 20, 1500, 762, 15202838126158406213ull},
  };
  for (const Case& c : kCases) {
    const VadRun r = RunVad(c.fs, c.mode, c.ms);
    SM_EXPECT_EQ(r.frames, c.frames);
    SM_EXPECT_EQ(r.active, c.active);
    SM_EXPECT_EQ(r.hash, c.hash);
  }
}

// VoiceActivityDetector: 3000 probabilities and RMS values (three every
// 30 ms), on the voiced signal, the voiced signal with digital silence (the
// `silence` branch) and the triangle-wave signal (which the pitch-based VAD
// takes for noise).
SM_TEST(Agc1VoiceActivity, VoiceActivityDetectorMatchesLibwebrtc) {
  const auto voiced = S16(Agc1VoicedTestSignal(16000, 30));
  {
    const VadDetectorRun r = RunVoiceActivityDetector(voiced);
    SM_EXPECT_EQ(r.count, size_t{3000});
    const VadSample kSamples[] = {
        {2,
         {0x3fe85b3eaa629ef9ull, 0x3fefae147ae147aeull, 0x3fefae147ae147aeull},
         {0x4074f34215d28970ull, 0x40728e864671f717ull, 0x40729a299ea97dddull}},
        {500,
         {0x3fdffffffffffffbull, 0x3fdffffffffffffbull, 0x3fdffffffffffffbull},
         {0x40710f81b0cbed8eull, 0x407391bdfce9243eull, 0x407217c66d5c8cefull}},
        {1502,
         {0x3fee5564f5214382ull, 0x3f847ae147ae147bull, 0x3f847ae147ae147bull},
         {0x407dc703f38a98c6ull, 0x4034fc9f1db0c5aaull, 0x4032f071890f1029ull}},
        {2999,
         {0x3fefae147ae147aeull, 0x3fefae147ae147aeull, 0x3fefae147ae147aeull},
         {0x40bfb4fc2e031e88ull, 0x40bbb82ac4760356ull, 0x40bbd8a453cb7ebcull}},
    };
    ExpectSamples(r, kSamples);
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.last[299]), 0x3f000000u);
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.last[2500]), 0x3c23d70au);  // 0.01f
    if (kExactLibm) {
      SM_EXPECT_EQ(r.hash, 9892251402787727296ull);
      SM_EXPECT_EQ(r.last_hash, 7301032047745508533ull);
    }
  }
  {
    const VadDetectorRun r = RunVoiceActivityDetector(WithSilence(voiced));
    SM_EXPECT_EQ(r.count, size_t{3000});
    const VadSample kSamples[] = {
        {1502,
         {0x3fedb84fdf1b5165ull, 0x3f847ae147ae147bull, 0x3f847ae147ae147bull},
         {0x407dc703f38a98c6ull, 0x4034fc9f1db0c5aaull, 0x4032f071890f1029ull}},
    };
    ExpectSamples(r, kSamples);
    // In the silence: 0.01 and an RMS under 5.
    SM_EXPECT_EQ(r.p[1751][0], 0.01);
    SM_EXPECT_LT(r.rms[1751][0], 5.0);
    if (kExactLibm) {
      SM_EXPECT_EQ(r.hash, 5029479510946349548ull);
      SM_EXPECT_EQ(r.last_hash, 2454978586823692643ull);
    }
  }
  {
    const VadDetectorRun r = RunVoiceActivityDetector(S16(Agc1TestSignal(16000, 30)));
    SM_EXPECT_EQ(r.count, size_t{3000});
    const VadSample kSamples[] = {
        {2,
         {0x3f1abe722f1d832aull, 0x3f1abe722f1d832aull, 0x3f1abe722f1d832aull},
         {0x405719136a7e9486ull, 0x40595992f9b5d2efull, 0x40577bafc6a862b6ull}},
        {1502,
         {0x3f847ae147ae147bull, 0x3f847ae147ae147bull, 0x3f847ae147ae147bull},
         {0x408b8a0f8ce4fe95ull, 0x408e417e2b171250ull, 0x408fb2fe8679b980ull}},
    };
    ExpectSamples(r, kSamples);
    if (kExactLibm) {
      SM_EXPECT_EQ(r.hash, 11487687944210301305ull);
      SM_EXPECT_EQ(r.last_hash, 17232279542994620741ull);
    }
  }
}

// Agc: GetRmsErrorDb every 10 ms (as MonoAgc); an update about once a second
// (100 frames, then the histogram restarts) while there is voice. The target
// is -18 dBFS: the voiced signal's levels need about +22, +7 and -5 dB.
SM_TEST(Agc1VoiceActivity, AgcMatchesLibwebrtc) {
  const auto voiced = S16(Agc1VoicedTestSignal(16000, 30));
  {
    const AgcUpdate kUpdates[] = {
        {101, 22},   {203, 22},   {305, 23},   {407, 23},   {509, 23},   {638, 22},   {740, 22},  {842, 23},
        {944, 23},   {1046, 8},   {1148, 7},   {1250, 7},   {1352, 7},   {1454, 7},   {1556, 7},  {1658, 7},
        {1760, 7},   {1862, 7},   {1964, 7},   {2066, -2},  {2168, -5},  {2270, -5},  {2372, -5}, {2474, -5},
        {2576, -5},  {2678, -5},  {2780, -5},  {2882, -5},  {2984, -5}};
    const AgcRun r = RunAgc(voiced);
    ExpectUpdates(r, kUpdates, 13192863867034623569ull);
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.voice_probability[99]), 0x3f000000u);    // 0.5f
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.voice_probability[2000]), 0x3f7d70a4u);  // 0.99f
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.voice_probability[2999]), 0x3f7d70a4u);
  }
  {
    const AgcUpdate kUpdates[] = {
        {101, 22},  {203, 22},  {305, 23},  {494, 23},  {596, 22},  {725, 22},  {827, 22},  {929, 23},  {1133, 7},
        {1235, 7},  {1337, 7},  {1439, 7},  {1541, 7},  {1643, 7},  {1745, 7},  {1847, 7},  {1949, 7},  {2051, -3},
        {2153, -5}, {2255, -5}, {2357, -5}, {2459, -5}, {2561, -5}, {2663, -5}, {2765, -5}, {2867, -5}, {2969, -5}};
    const AgcRun r = RunAgc(WithSilence(voiced));
    ExpectUpdates(r, kUpdates, 7375193137349113214ull);
  }
  {
    // The triangle-wave signal never has enough voice for an update.
    const AgcRun r = RunAgc(S16(Agc1TestSignal(16000, 30)));
    SM_EXPECT_EQ(r.updates.size(), size_t{0});
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.voice_probability[99]), 0x38d5f391u);
    SM_EXPECT_EQ(std::bit_cast<uint32_t>(r.voice_probability[2000]), 0x3c23d70au);
  }
}

SM_TEST(Agc1VoiceActivity, AgcTargetLevel) {
  Agc agc;
  SM_EXPECT_EQ(agc.target_level_dbfs(), -18);
  SM_EXPECT_EQ(agc.set_target_level_dbfs(0), -1);
  SM_EXPECT_EQ(agc.set_target_level_dbfs(-100), -1);
  SM_EXPECT_EQ(agc.set_target_level_dbfs(-25), 0);
  SM_EXPECT_EQ(agc.target_level_dbfs(), -25);
  int error = 0;
  SM_EXPECT_FALSE(agc.GetRmsErrorDb(&error));  // No frames yet.
  SM_EXPECT_FALSE(agc.GetRmsErrorDb(nullptr));
}

// ---- common_audio/vad unit tests ------------------------------------------

// vad_core_unittest.cc: InitCore, set_mode_core, CalcVad.
SM_TEST(Agc1VoiceActivity, VadCore) {
  VadInst self;
  SM_EXPECT_EQ(VadInitCore(self), 0);
  SM_EXPECT_EQ(self.init_flag, 42);
  SM_EXPECT_EQ(VadSetModeCore(self, -1), -1);
  SM_EXPECT_EQ(VadSetModeCore(self, 1000), -1);
  for (int mode = 0; mode < 4; ++mode) SM_EXPECT_EQ(VadSetModeCore(self, mode), 0);

  int16_t speech[kMaxFrameLength] = {};
  SM_ASSERT_EQ(VadInitCore(self), 0);
  for (size_t len : kFrameLengths) {
    if (ValidRatesAndFrameLengths(8000, len)) SM_EXPECT_EQ(VadCalcVad8khz(self, speech, len), 0);
    if (ValidRatesAndFrameLengths(16000, len)) SM_EXPECT_EQ(VadCalcVad16khz(self, speech, len), 0);
    if (ValidRatesAndFrameLengths(32000, len)) SM_EXPECT_EQ(VadCalcVad32khz(self, speech, len), 0);
  }
  // A signal that triggers the VAD in all modes ((i * i) wraps around).
  for (size_t i = 0; i < kMaxFrameLength; ++i) speech[i] = static_cast<int16_t>(i * i);
  for (size_t len : kFrameLengths) {
    if (ValidRatesAndFrameLengths(8000, len)) SM_EXPECT_EQ(VadCalcVad8khz(self, speech, len), 1);
    if (ValidRatesAndFrameLengths(16000, len)) SM_EXPECT_EQ(VadCalcVad16khz(self, speech, len), 1);
    if (ValidRatesAndFrameLengths(32000, len)) SM_EXPECT_EQ(VadCalcVad32khz(self, speech, len), 1);
  }
}

// vad_filterbank_unittest.cc.
SM_TEST(Agc1VoiceActivity, VadFilterbank) {
  constexpr int kNumValidFrameLengths = 3;
  const int16_t kReference[kNumValidFrameLengths] = {48, 11, 11};
  const int16_t kFeatures[kNumValidFrameLengths * kVadNumChannels] = {
      1213, 759, 587, 462, 434, 272, 1479, 1385, 1291, 1200, 1103, 1099, 1732, 1692, 1681, 1629, 1436, 1436};
  const int16_t kOffsetVector[kVadNumChannels] = {368, 368, 272, 176, 176, 176};
  int16_t features[kVadNumChannels];
  int16_t speech[kMaxFrameLength];
  for (size_t i = 0; i < kMaxFrameLength; ++i) speech[i] = static_cast<int16_t>(i * i);

  VadInst self;
  int frame_length_index = 0;
  SM_ASSERT_EQ(VadInitCore(self), 0);
  for (size_t len : kFrameLengths) {
    if (ValidRatesAndFrameLengths(8000, len)) {
      SM_EXPECT_EQ(VadCalculateFeatures(self, speech, len, features), kReference[frame_length_index]);
      for (int k = 0; k < kVadNumChannels; ++k)
        SM_EXPECT_EQ(features[k], kFeatures[k + frame_length_index * kVadNumChannels]);
      frame_length_index++;
    }
  }
  SM_EXPECT_EQ(frame_length_index, kNumValidFrameLengths);

  // All zeros in gives kOffsetVector out.
  std::memset(speech, 0, sizeof(speech));
  SM_ASSERT_EQ(VadInitCore(self), 0);
  for (size_t len : kFrameLengths) {
    if (ValidRatesAndFrameLengths(8000, len)) {
      SM_EXPECT_EQ(VadCalculateFeatures(self, speech, len, features), 0);
      for (int k = 0; k < kVadNumChannels; ++k) SM_EXPECT_EQ(features[k], kOffsetVector[k]);
    }
  }
  // All ones in gives kOffsetVector out.
  for (size_t i = 0; i < kMaxFrameLength; ++i) speech[i] = 1;
  for (size_t len : kFrameLengths) {
    if (ValidRatesAndFrameLengths(8000, len)) {
      SM_ASSERT_EQ(VadInitCore(self), 0);
      SM_EXPECT_EQ(VadCalculateFeatures(self, speech, len, features), 0);
      for (int k = 0; k < kVadNumChannels; ++k) SM_EXPECT_EQ(features[k], kOffsetVector[k]);
    }
  }
}

// vad_gmm_unittest.cc.
SM_TEST(Agc1VoiceActivity, VadGmm) {
  int16_t delta = 0;
  // Input value at mean.
  SM_EXPECT_EQ(VadGaussianProbability(0, 0, 128, &delta), 1048576);
  SM_EXPECT_EQ(delta, 0);
  SM_EXPECT_EQ(VadGaussianProbability(16, 128, 128, &delta), 1048576);
  SM_EXPECT_EQ(delta, 0);
  SM_EXPECT_EQ(VadGaussianProbability(-16, -128, 128, &delta), 1048576);
  SM_EXPECT_EQ(delta, 0);
  // Largest possible input to give non-zero probability.
  SM_EXPECT_EQ(VadGaussianProbability(59, 0, 128, &delta), 1024);
  SM_EXPECT_EQ(delta, 7552);
  SM_EXPECT_EQ(VadGaussianProbability(75, 128, 128, &delta), 1024);
  SM_EXPECT_EQ(delta, 7552);
  SM_EXPECT_EQ(VadGaussianProbability(-75, -128, 128, &delta), 1024);
  SM_EXPECT_EQ(delta, -7552);
  // Too large input, should give zero probability.
  SM_EXPECT_EQ(VadGaussianProbability(105, 0, 128, &delta), 0);
  SM_EXPECT_EQ(delta, 13440);
}

// vad_sp_unittest.cc.
SM_TEST(Agc1VoiceActivity, VadSp) {
  constexpr size_t kMaxFrameLenSp = 960;
  int16_t zeros[kMaxFrameLenSp] = {0};
  int32_t state[2] = {0};
  int16_t data_in[kMaxFrameLenSp];
  int16_t data_out[kMaxFrameLenSp];
  const int16_t kReferenceMin[32] = {1600, 720, 509, 512, 532, 552, 570, 588, 606, 624, 642,
                                     659,  675, 691, 707, 723, 1600, 544, 502, 522, 542, 561,
                                     579,  597, 615, 633, 651, 667, 683, 699, 715, 731};
  for (size_t i = 0; i < kMaxFrameLenSp; ++i) data_in[i] = static_cast<int16_t>(i * i);
  VadDownsampling(zeros, data_out, state, kMaxFrameLenSp);
  SM_EXPECT_EQ(state[0], 0);
  SM_EXPECT_EQ(state[1], 0);
  for (size_t i = 0; i < kMaxFrameLenSp / 2; ++i) SM_EXPECT_EQ(data_out[i], 0);
  VadDownsampling(data_in, data_out, state, kMaxFrameLenSp);
  SM_EXPECT_EQ(state[0], 207);
  SM_EXPECT_EQ(state[1], 2270);

  VadInst self;
  SM_ASSERT_EQ(VadInitCore(self), 0);
  for (int16_t i = 0; i < 16; ++i) {
    const auto value = static_cast<int16_t>(500 * (i + 1));
    for (int j = 0; j < kVadNumChannels; ++j) {
      SM_EXPECT_EQ(VadFindMinimum(self, value, j), kReferenceMin[i]);
      SM_EXPECT_EQ(VadFindMinimum(self, 12000, j), kReferenceMin[i + 16]);
    }
    self.frame_counter++;
  }
}

// vad_unittest.cc: ApiTest (without 48 kHz, not ported) and
// ValidRatesFrameLengths.
SM_TEST(Agc1VoiceActivity, WebRtcVadApi) {
  int16_t zeros[kMaxFrameLength] = {0};
  int16_t speech[kMaxFrameLength];
  for (size_t i = 0; i < kMaxFrameLength; i++) speech[i] = static_cast<int16_t>(i * i);
  const int kRates[] = {8000, 12000, 16000, 24000, 32000};
  WebRtcVad vad;
  SM_EXPECT_EQ(vad.set_mode(-1), -1);
  SM_EXPECT_EQ(vad.set_mode(4), -1);
  SM_EXPECT_EQ(vad.Process(kRates[0], nullptr, kFrameLengths[0]), -1);
  SM_EXPECT_EQ(vad.Process(9999, speech, kFrameLengths[0]), -1);
  SM_EXPECT_EQ(vad.Process(kRates[0], zeros, kFrameLengths[0]), 0);
  for (int mode = 0; mode < 4; ++mode) {
    SM_EXPECT_EQ(vad.set_mode(mode), 0);
    for (int rate : kRates) {
      for (size_t len : kFrameLengths) {
        SM_EXPECT_EQ(vad.Process(rate, speech, len), ValidRatesAndFrameLengths(rate, len) ? 1 : -1);
      }
    }
  }
  SM_EXPECT_EQ(vad.Process(48000, speech, 480), -1);  // Not ported.

  const int kInvalidRates[] = {-8000, -4000, 0, 4000, 8000, 8001, 15999, 16000, 32000, 48000, 48001, 96000};
  const size_t kInvalidFrameLengths[] = {0, 80, 81, 159, 160, 240, 320, 480, 640, 960, 1440, 2000};
  for (int rate : kInvalidRates) {
    for (size_t len : kInvalidFrameLengths) {
      SM_EXPECT_EQ(VadValidRateAndFrameLength(rate, len), ValidRatesAndFrameLengths(rate, len) ? 0 : -1);
    }
  }
}

// ---- modules/audio_processing/vad unit tests ------------------------------

// standalone_vad_unittest.cc: Api.
SM_TEST(Agc1VoiceActivity, StandaloneVadApi) {
  StandaloneVad vad;
  int16_t data[kVadLength10Ms] = {0};
  // Valid frame length (for 32 kHz rate), but not what the VAD is expecting.
  SM_EXPECT_EQ(vad.AddAudio(data, 320), -1);
  constexpr size_t kMaxNumFrames = 3;
  double p[kMaxNumFrames];
  for (size_t n = 0; n < kMaxNumFrames; n++) SM_EXPECT_EQ(vad.AddAudio(data, kVadLength10Ms), 0);
  // Pretend `p` is shorter that it should be.
  SM_EXPECT_EQ(vad.GetActivity(p, kMaxNumFrames - 1), -1);
  SM_EXPECT_EQ(vad.GetActivity(p, kMaxNumFrames), 0);
  // Ask for activity when buffer is empty.
  SM_EXPECT_EQ(vad.GetActivity(p, kMaxNumFrames), -1);
  // Should reset and result in one buffer.
  for (size_t n = 0; n < kMaxNumFrames + 1; n++) SM_EXPECT_EQ(vad.AddAudio(data, kVadLength10Ms), 0);
  SM_EXPECT_EQ(vad.GetActivity(p, 1), 0);
  SM_EXPECT_EQ(vad.set_mode(-1), -1);
  SM_EXPECT_EQ(vad.set_mode(4), -1);
  SM_EXPECT_EQ(vad.set_mode(2), 0);
  SM_EXPECT_EQ(vad.mode(), 2);
}

// gmm_unittest.cc.
SM_TEST(Agc1VoiceActivity, EvaluateGmm) {
  const double kXVoice[3] = {-1.35893162459863, 602.862491970368, 178.022069191324};
  const double kXNoise[3] = {-2.33443722724409, 2827.97828765184, 141.114178166812};
  const double kPdfNoise = 1.88904409403101e-07;
  const double kPdfVoice = 1.30453996982266e-06;
  const double kAcceptedRelativeErr = 1e-10;
  double pdf = EvaluateGmm(kXVoice, VoiceGmm());
  SM_EXPECT_GT(pdf, 0);
  SM_EXPECT_LE(std::fabs(pdf - kPdfVoice) / kPdfVoice, kAcceptedRelativeErr);
  pdf = EvaluateGmm(kXNoise, NoiseGmm());
  SM_EXPECT_GT(pdf, 0);
  SM_EXPECT_LE(std::fabs(pdf - kPdfNoise) / kPdfNoise, kAcceptedRelativeErr);
}

// pitch_internal_unittest.cc.
SM_TEST(Agc1VoiceActivity, PitchInternal) {
  double log_old_gain = std::log(0.5);
  double gains[] = {0.6, 0.2, 0.5, 0.4};
  double old_lag = 70;
  double lags[] = {90, 111, 122, 50};
  const double expected_log_pitch_gain[] = {-0.541212549898316, -1.45672279045507, -0.80471895621705};
  const double expected_log_old_gain = std::log(gains[3]);
  const double expected_pitch_lag_hz[] = {92.3076923076923, 70.9010339734121, 93.0232558139535};
  const double expected_old_lag = lags[3];
  double log_pitch_gain[3];
  double pitch_lag_hz[4];
  GetSubframesPitchParameters(8000, gains, lags, 4, 3, &log_old_gain, &old_lag, log_pitch_gain, pitch_lag_hz);
  for (int n = 0; n < 3; n++) {
    SM_EXPECT_NEAR(pitch_lag_hz[n], expected_pitch_lag_hz[n], 1e-6);
    SM_EXPECT_NEAR(log_pitch_gain[n], expected_log_pitch_gain[n], 1e-8);
  }
  SM_EXPECT_NEAR(old_lag, expected_old_lag, 1e-6);
  SM_EXPECT_NEAR(log_old_gain, expected_log_old_gain, 1e-8);
}

// pole_zero_filter_unittest.cc, with VadAudioProc's high-pass filter.
SM_TEST(Agc1VoiceActivity, PoleZeroFilter) {
  constexpr int kInputSamples = 50;
  const int16_t kInput[kInputSamples] = {
      -2136,  -7116, 10715, 2464,  3164,   8139,   11393, 24013,  -32117, -5544,  -27740, 10181, 14190,
      -24055, -15912, 17393, 6359, -9950,  -13894, 32432, -23944, 3437,   -8381,  19768,  3087,  -19795,
      -5920,  13310, 1407,  3876,  4059,   3524,   -23130, 19121, -27900, -24840, 4089,   21422, -3625,
      3015,   -11236, 28856, 13424, 6571,  -19761, -6361,  15821, -9469,  29727,  32229};
  const float kReferenceOutput[kInputSamples] = {
      -2082.230472f,  -6878.572941f,  10697.090871f,  2358.373952f,   2973.936512f,   7738.580650f,
      10690.803213f,  22687.091576f,  -32676.684717f, -5879.621684f,  -27359.297432f, 10368.735888f,
      13994.584604f,  -23676.126249f, -15078.250390f, 17818.253338f,  6577.743123f,   -9498.369315f,
      -13073.651079f, 32460.026588f,  -23391.849347f, 3953.805667f,   -7667.761363f,  19995.153447f,
      3185.575477f,   -19207.365160f, -5143.103201f,  13756.317237f,  1779.654794f,   4142.269755f,
      4209.475034f,   3572.991789f,   -22509.089546f, 19307.878964f,  -27060.439759f, -23319.042810f,
      5547.685267f,   22312.718676f,  -2707.309027f,  3852.358490f,   -10135.510093f, 29241.509970f,
      13394.397233f,  6340.721417f,   -19510.207905f, -5908.442086f,  15882.301634f,  -9211.335255f,
      29253.056735f,  30874.443046f};
  const float kNumerator[3] = {0.974827f, -1.949650f, 0.974827f};
  const float kDenominator[3] = {1.0f, -1.971999f, 0.972457f};
  for (int num_subframes : {1, 2, 5, 10, 25, 50}) {
    PoleZeroFilter filter(kNumerator, 2, kDenominator, 2);
    float output[kInputSamples];
    const int num_subframe_samples = kInputSamples / num_subframes;
    for (int n = 0; n < num_subframes; n++) {
      filter.Filter(&kInput[n * num_subframe_samples], static_cast<size_t>(num_subframe_samples),
                    &output[n * num_subframe_samples]);
    }
    for (int n = 0; n < kInputSamples; n++) SM_EXPECT_NEAR(output[n], kReferenceOutput[n], 1);
  }
}

// vad_circular_buffer_unittest.cc.
SM_TEST(Agc1VoiceActivity, VadCircularBuffer) {
  constexpr int kWidthThreshold = 7;
  constexpr double kValThreshold = 1.0;
  constexpr int kLongBuffSize = 100;
  constexpr int kShortBuffSize = 10;
  auto insert_sequentially = [](int k, VadCircularBuffer& b) {
    for (int n = 1; n <= k; n++) {
      SM_EXPECT_TRUE(!b.is_full());
      b.Insert(n);
      SM_EXPECT_EQ(b.Mean(), (n + 1.0) / 2.);
    }
  };
  auto insert = [](double value, int num_insertion, VadCircularBuffer& b) {
    for (int n = 0; n < num_insertion; n++) b.Insert(value);
  };
  {  // GeneralTest.
    VadCircularBuffer b(kShortBuffSize);
    SM_EXPECT_EQ(b.Mean(), 0.0);
    insert_sequentially(kShortBuffSize, b);
    SM_EXPECT_TRUE(b.is_full());
    for (int n = 1; n < kShortBuffSize; n++) {
      b.Insert(n);
      SM_EXPECT_NEAR(b.Mean(), (kShortBuffSize + 1.) / 2., 1e-12);
      SM_EXPECT_TRUE(b.is_full());
    }
    b.Reset();
    SM_EXPECT_EQ(b.Mean(), 0.0);
    insert_sequentially(kShortBuffSize, b);
    SM_EXPECT_TRUE(b.is_full());
  }
  {  // TransientsRemoval.
    VadCircularBuffer b(kLongBuffSize);
    insert(0.0, kLongBuffSize - kWidthThreshold / 2, b);
    const double push_val = kValThreshold;
    for (int k = kWidthThreshold; k >= 1; k--) {
      insert(push_val, k, b);
      b.Insert(0);
      SM_EXPECT_NEAR(b.Mean(), k * push_val / kLongBuffSize, 1e-12);
      b.RemoveTransient(kWidthThreshold, kValThreshold);
      SM_EXPECT_NEAR(b.Mean(), 0.0, 1e-12);
    }
  }
  {  // TransientDetection.
    VadCircularBuffer b(kLongBuffSize);
    insert(0.0, kLongBuffSize - kWidthThreshold / 2, b);
    const double push_val = 2;
    int num_non_zero_elements = kWidthThreshold + 1;
    insert(push_val, num_non_zero_elements, b);
    SM_EXPECT_NEAR(b.Mean(), num_non_zero_elements * push_val / kLongBuffSize, 1e-12);
    b.Insert(0);
    SM_EXPECT_EQ(b.RemoveTransient(kWidthThreshold, kValThreshold), 0);
    SM_EXPECT_NEAR(b.Mean(), num_non_zero_elements * push_val / kLongBuffSize, 1e-12);
    // A transient right after a non-transient is removed.
    insert(push_val, 3, b);
    b.Insert(0);
    SM_EXPECT_EQ(b.RemoveTransient(kWidthThreshold, kValThreshold), 0);
    SM_EXPECT_NEAR(b.Mean(), num_non_zero_elements * push_val / kLongBuffSize, 1e-12);
    // The last input is above the threshold: not a transient.
    insert(push_val, 3, b);
    num_non_zero_elements += 3;
    SM_EXPECT_EQ(b.RemoveTransient(kWidthThreshold, kValThreshold), 0);
    SM_EXPECT_NEAR(b.Mean(), num_non_zero_elements * push_val / kLongBuffSize, 1e-12);
  }
}

// ---- modules/audio_processing/agc ------------------------------------------

// loudness_histogram_unittest.cc's clean-state checks (its data runs need
// resource files), and the transient removal of the sliding histogram.
SM_TEST(Agc1VoiceActivity, LoudnessHistogram) {
  for (int window : {0, 100}) {
    LoudnessHistogram hist = window ? LoudnessHistogram(window) : LoudnessHistogram();
    SM_EXPECT_EQ(hist.CurrentRms(), 7.59621091765857e-02);
    SM_EXPECT_EQ(hist.AudioContent(), 0.0);
    SM_EXPECT_EQ(hist.num_updates(), 0);
    hist.Update(1.0, 1.0);  // Bin 15 (center 1.0), probability 1024 in Q10.
    SM_EXPECT_EQ(hist.num_updates(), 1);
    SM_EXPECT_EQ(hist.AudioContent(), 1.0);
    SM_EXPECT_EQ(hist.CurrentRms(), 1.0);
    hist.Reset();
    SM_EXPECT_EQ(hist.CurrentRms(), 7.59621091765857e-02);
    SM_EXPECT_EQ(hist.AudioContent(), 0.0);
    SM_EXPECT_EQ(hist.num_updates(), 0);
  }
  // Sliding: a short burst of activity followed by a low probability is a
  // transient and is removed.
  LoudnessHistogram hist(100);
  for (int i = 0; i < 3; ++i) hist.Update(100.0, 0.9);
  SM_EXPECT_GT(hist.AudioContent(), 2.0);
  hist.Update(100.0, 0.1);
  SM_EXPECT_EQ(hist.AudioContent(), 0.0);
  // The window forgets updates older than 100.
  for (int i = 0; i < 100; ++i) hist.Update(100.0, 1.0);
  SM_EXPECT_EQ(hist.AudioContent(), 100.0);
  for (int i = 0; i < 100; ++i) hist.Update(10.0, 0.5);
  SM_EXPECT_EQ(hist.AudioContent(), 50.0);
  SM_EXPECT_EQ(hist.CurrentRms(), 9.33575086877358e+00);  // The bin of 10.0.
}

// utility.cc.
SM_TEST(Agc1VoiceActivity, Utility) {
  SM_EXPECT_EQ(Linear2Loudness(0), -15.0);
  SM_EXPECT_NEAR(Loudness2Db(Db2Loudness(12.5)), 12.5, 1e-12);
  SM_EXPECT_NEAR(Dbfs2Loudness(-18), Db2Loudness(72), 0);
  SM_EXPECT_NEAR(Loudness2Db(Linear2Loudness(10.0)), 20.0, 1e-12);
}
