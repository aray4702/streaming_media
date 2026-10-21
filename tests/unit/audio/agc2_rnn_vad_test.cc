// Unit tests: AGC2's RNN VAD and the sinc resampler it runs on (`apm v2`,
// #242), ported from libwebrtc M157's modules/audio_processing/agc2/
// rnn_vad/*_unittest.cc, vad_wrapper_unittest.cc and common_audio/resampler/
// push_sinc_resampler_unittest.cc with their test data and tolerances. The
// tests that read libwebrtc's resource files (recorded pitch buffers and
// speech probabilities) are not ported: those files are not in the
// reference sources.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "sm/audio/agc2.h"
#include "sm/audio/agc2_rnn_vad.h"
#include "sm/audio/sinc_resampler.h"
#include "sm/core/random.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;
using namespace sm::audio::agc2;
using namespace sm::audio::agc2::rnn_vad;

namespace {

bool AllNear(std::span<const float> expected, std::span<const float> computed, float tolerance) {
  if (expected.size() != computed.size()) return false;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (std::fabs(expected[i] - computed[i]) > tolerance) return false;
  }
  return true;
}

}  // namespace

// rnn_fc_unittest.cc: CheckFullyConnectedLayerOutput.
SM_TEST(Agc2RnnVad, FullyConnectedLayerOutputWithinTolerance) {
  constexpr std::array<float, 42> kFullyConnectedInputVector = {
      -1.00131f,   -0.627069f, -7.81097f,  7.86285f,    -2.87145f,  3.32365f,  -0.653161f, 0.529839f,  -0.425307f,
      0.25583f,    0.235094f,  0.230527f,  -0.144687f,  0.182785f,  0.57102f,  0.125039f,  0.479482f,  -0.0255439f,
      -0.0073141f, -0.147346f, -0.217106f, -0.0846906f, -8.34943f,  3.09065f,  1.42628f,   -0.85235f,  -0.220207f,
      -0.811163f,  2.09032f,   -2.01425f,  -0.690268f,  -0.925327f, -0.541354f, 0.58455f,  -0.606726f, -0.0372358f,
      0.565991f,   0.435854f,  0.420812f,  0.162198f,   -2.13f,     10.0089f};
  constexpr std::array<float, 24> kFullyConnectedExpectedOutput = {
      -0.623293f, -0.988299f, 0.999378f, 0.967168f, 0.103087f, -0.978545f, -0.856347f, 0.346675f,
      1.f,        -0.717442f, -0.544176f, 0.960363f, 0.983443f, 0.999991f, -0.824335f, 0.984742f,
      0.990208f,  0.938179f,  0.875092f, 0.999846f, 0.997707f, -0.999382f, 0.973153f,  -0.966605f};
  FullyConnectedLayer fc(static_cast<int>(rnnoise::kInputLayerInputSize),
                         static_cast<int>(rnnoise::kInputLayerOutputSize), rnnoise::kInputDenseBias,
                         rnnoise::kInputDenseWeights, ActivationFunction::kTansigApproximated);
  fc.ComputeOutput(kFullyConnectedInputVector);
  SM_EXPECT_TRUE(AllNear(kFullyConnectedExpectedOutput, fc.output(), 1e-5f));
}

// rnn_gru_unittest.cc: CheckGatedRecurrentLayer.
SM_TEST(Agc2RnnVad, GatedRecurrentLayerOutputWithinTolerance) {
  constexpr int kGruInputSize = 5;
  constexpr int kGruOutputSize = 4;
  constexpr std::array<int8_t, 12> kGruBias = {96, -99, -81, -114, 49, 119, -118, 68, -76, 91, 121, 125};
  constexpr std::array<int8_t, 60> kGruWeights = {
      124, 9,   1,    116,  -66, -21,  -118, -110, 104,  75,  -23,  -51,   // Input 0.
      -72, -111, 47,  93,   77,  -98,  41,   -8,   40,   -23, -43,  -107,  // Input 1.
      9,   -73, 30,   -32,  -2,  64,   -26,  91,   -48,  -24, -28,  -104,  // Input 2.
      74,  -46, 116,  15,   32,  52,   -126, -38,  -121, 12,  -16,  110,   // Input 3.
      -95, 66,  -103, -35,  -38, 3,    -126, -61,  28,   98,  -117, -43};  // Input 4.
  constexpr std::array<int8_t, 48> kGruRecurrentWeights = {
      -3,  87,  50,  51,  -22,  27, -39, 62,   31,  -83, -52,  -48,   // Output 0.
      -6,  83,  -19, 104, 105,  48, 23,  68,   23,  40,  7,    -120,  // Output 1.
      64,  -62, 117, 85,  51,   -43, 54, -105, 120, 56,  -128, -107,  // Output 2.
      39,  50,  -17, -47, -117, 14, 108, 12,   -7,  -72, 103,  -87};  // Output 3.
  constexpr std::array<float, 20> kGruInputSequence = {
      0.89395463f, 0.93224651f, 0.55788344f, 0.32341808f, 0.93355054f, 0.13475326f, 0.97370994f,
      0.14253306f, 0.93710381f, 0.76093364f, 0.65780413f, 0.41657975f, 0.49403164f, 0.46843281f,
      0.75138855f, 0.24517593f, 0.47657707f, 0.57064998f, 0.435184f,   0.19319285f};
  constexpr std::array<float, 16> kGruExpectedOutputSequence = {
      0.0239123f,  0.5773077f,  0.f, 0.f,         0.01282811f, 0.64330572f, 0.f,         0.04863098f,
      0.00781069f, 0.75267816f, 0.f, 0.02579715f, 0.00471378f, 0.59162533f, 0.11087593f, 0.01334511f};
  GatedRecurrentLayer gru(kGruInputSize, kGruOutputSize, kGruBias, kGruWeights, kGruRecurrentWeights);
  gru.Reset();
  for (int i = 0; i < 4; ++i) {
    gru.ComputeOutput(std::span<const float>(kGruInputSequence).subspan(static_cast<size_t>(i * kGruInputSize),
                                                                        kGruInputSize));
    SM_EXPECT_TRUE(AllNear(std::span<const float>(kGruExpectedOutputSequence)
                               .subspan(static_cast<size_t>(i * kGruOutputSize), kGruOutputSize),
                           gru.output(), 3e-6f));
  }
}

// spectral_features_internal_unittest.cc: TestOpusScaleBoundaries.
SM_TEST(Agc2RnnVad, OpusScaleBoundaries) {
  constexpr int kBandFrequencyBoundariesHz[kNumBands - 1] = {200,  400,  600,  800,  1000, 1200,  1400,
                                                             1600, 2000, 2400, 2800, 3200, 4000,  4800,
                                                             5600, 6800, 8000, 9600, 12000, 15600, 20000};
  constexpr auto kOpusScaleNumBins24kHz20ms = GetOpusScaleNumBins24kHz20ms();
  int prev = 0;
  for (size_t i = 0; i < kOpusScaleNumBins24kHz20ms.size(); ++i) {
    const int boundary = kBandFrequencyBoundariesHz[i] * kFrameSize20ms24kHz / kSampleRate24kHz;
    SM_EXPECT_EQ(kOpusScaleNumBins24kHz20ms[i], boundary - prev);
    prev = boundary;
  }
}

// spectral_features_internal_unittest.cc: SpectralCorrelatorValidOutput.
SM_TEST(Agc2RnnVad, SpectralCorrelatorValidOutput) {
  std::vector<float> in(kFrameSize20ms24kHz, 1.f);
  in[1] = 0.f;  // Nyquist frequency.
  std::array<float, kOpusBands24kHz> out;
  SpectralCorrelator e;
  e.ComputeAutoCorrelation(in, out);
  for (float v : out) SM_EXPECT_GT(v, 0.f);
}

// spectral_features_internal_unittest.cc:
// ComputeSmoothedLogMagnitudeSpectrumWithinTolerance.
SM_TEST(Agc2RnnVad, SmoothedLogMagnitudeSpectrumWithinTolerance) {
  constexpr std::array<float, kNumBands> input = {
      {86.060539245605f, 275.668334960938f, 43.406528472900f, 6.541896820068f, 17.964015960693f, 8.090919494629f,
       1.261920094490f,  1.212702631950f,   1.619154453278f,  0.508935272694f, 0.346316039562f,  0.237035423517f,
       0.172424271703f,  0.271657168865f,   0.126088857651f,  0.139967113733f, 0.207200810313f,  0.155893072486f,
       0.091090843081f,  0.033391401172f,   0.013879744336f,  0.011973354965f}};
  constexpr std::array<float, kNumBands> expected_output = {
      {1.934854507446f,  2.440402746201f,  1.637655138969f,  0.816367030144f,  1.254645109177f,  0.908534288406f,
       0.104459829628f,  0.087320849299f,  0.211962252855f,  -0.284886807203f, -0.448164641857f, -0.607240796089f,
       -0.738917350769f, -0.550279200077f, -0.866177439690f, -0.824003994465f, -0.663138568401f, -0.780171751976f,
       -0.995288193226f, -1.362596273422f, -1.621970295906f, -1.658103585243f}};
  std::array<float, kNumBands> computed_output;
  ComputeSmoothedLogMagnitudeSpectrum(input, computed_output);
  SM_EXPECT_TRUE(AllNear(expected_output, computed_output, 1e-5f));
}

// spectral_features_internal_unittest.cc: ComputeDctWithinTolerance.
SM_TEST(Agc2RnnVad, DctWithinTolerance) {
  constexpr std::array<float, kNumBands> input = {
      {0.232155621052f,  0.678957760334f,  0.220818966627f,  -0.077363930643f, -0.559227049351f, 0.432545185089f,
       0.353900641203f,  0.398993015289f,  0.409774333239f,  0.454977899790f,  0.300520688295f,  -0.010286616161f,
       0.272525429726f,  0.098067551851f,  0.083649002016f,  0.046226885170f,  -0.033228103071f, 0.144773483276f,
       -0.117661058903f, -0.005628800020f, -0.009547689930f, -0.045382082462f}};
  constexpr std::array<float, kNumBands> expected_output = {
      {0.697072803974f,  0.442710995674f,  -0.293156713247f, -0.060711503029f, 0.292050391436f,  0.489301353693f,
       0.402255415916f,  0.134404733777f,  -0.086305990815f, -0.199605688453f, -0.234511867166f, -0.413774639368f,
       -0.388507157564f, -0.032798115164f, 0.044605545700f,  0.112466648221f,  -0.050096966326f, 0.045971218497f,
       -0.029815061018f, -0.410366982222f, -0.209233760834f, -0.128037497401f}};
  const auto dct_table = ComputeDctTable();
  std::array<float, kNumBands> computed_output;
  ComputeDct(input, dct_table, computed_output);
  SM_EXPECT_TRUE(AllNear(expected_output, computed_output, 1e-5f));
}

// The 480-point DFT that stands in for PFFFT: against a direct DFT, in
// PFFFT's ordered layout (DC, Nyquist, then re/im of bins 1..239).
SM_TEST(Agc2RnnVad, RealDftMatchesDirectDft) {
  Random rng(7);
  std::array<float, kFrameSize20ms24kHz> in;
  for (float& v : in) v = static_cast<float>(rng.Uniform(-1000.0, 1000.0));
  std::array<float, kFrameSize20ms24kHz> out;
  RealDft480 dft;
  dft.Forward(in, out);
  constexpr int n = kFrameSize20ms24kHz;
  double max_err = 0, max_mag = 0;
  for (int k = 0; k <= n / 2; ++k) {
    std::complex<double> x = 0;
    for (int t = 0; t < n; ++t) x += static_cast<double>(in[t]) * std::polar(1.0, -2.0 * std::numbers::pi * k * t / n);
    std::complex<double> got;
    if (k == 0) {
      got = {out[0], 0.0};
    } else if (k == n / 2) {
      got = {out[1], 0.0};
    } else {
      got = {out[2 * k], out[2 * k + 1]};
    }
    max_err = std::max(max_err, std::abs(got - x));
    max_mag = std::max(max_mag, std::abs(x));
  }
  SM_EXPECT_LT(max_err, 1e-5 * max_mag);
}

namespace {

constexpr int kTestFeatureVectorSize = kNumBands + 3 * kNumLowerBands + 1;
constexpr float kInitialFeatureVal = -9999.f;

// Non-zero sample values.
void WriteTestData(std::span<float> samples) {
  for (size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<float>(i % 100);
}

bool ComputeSpectralFeatures(SpectralFeaturesExtractor& sfe, std::span<const float, kFrameSize20ms24kHz> samples,
                             std::span<float, kTestFeatureVectorSize> fv) {
  return sfe.CheckSilenceComputeFeatures(samples, samples, fv.subspan<kNumLowerBands, kNumBands - kNumLowerBands>(),
                                         fv.first<kNumLowerBands>(), fv.subspan<kNumBands, kNumLowerBands>(),
                                         fv.subspan<kNumBands + kNumLowerBands, kNumLowerBands>(),
                                         fv.subspan<kNumBands + 2 * kNumLowerBands, kNumLowerBands>(),
                                         &fv[kNumBands + 3 * kNumLowerBands]);
}

}  // namespace

// spectral_features_unittest.cc: SpectralFeaturesWithAndWithoutSilence.
SM_TEST(Agc2RnnVad, SpectralFeaturesWithAndWithoutSilence) {
  SpectralFeaturesExtractor sfe;
  std::array<float, kFrameSize20ms24kHz> samples{};
  std::array<float, kTestFeatureVectorSize> feature_vector;
  std::fill(feature_vector.begin(), feature_vector.end(), kInitialFeatureVal);
  // With silence the output is not overwritten.
  SM_EXPECT_TRUE(ComputeSpectralFeatures(sfe, samples, feature_vector));
  SM_EXPECT_TRUE(std::all_of(feature_vector.begin(), feature_vector.end(),
                             [](float x) { return x == kInitialFeatureVal; }));
  // Without silence it is.
  WriteTestData(samples);
  SM_EXPECT_FALSE(ComputeSpectralFeatures(sfe, samples, feature_vector));
  SM_EXPECT_FALSE(std::all_of(feature_vector.begin(), feature_vector.end(),
                              [](float x) { return x == kInitialFeatureVal; }));
}

// spectral_features_unittest.cc: CepstralFeaturesConstantAverageZeroDerivative.
SM_TEST(Agc2RnnVad, CepstralFeaturesConstantAverageZeroDerivative) {
  SpectralFeaturesExtractor sfe;
  std::array<float, kFrameSize20ms24kHz> samples;
  WriteTestData(samples);
  std::array<float, kTestFeatureVectorSize> feature_vector;
  for (int i = 0; i < kCepstralCoeffsHistorySize; ++i) ComputeSpectralFeatures(sfe, samples, feature_vector);
  std::array<float, kTestFeatureVectorSize> feature_vector_last;
  ComputeSpectralFeatures(sfe, samples, feature_vector_last);
  // Average is unchanged; first and second derivatives are zero; variability
  // is unchanged.
  for (int i = 0; i < kNumLowerBands; ++i) {
    SM_EXPECT_EQ(feature_vector[i], feature_vector_last[i]);
    SM_EXPECT_EQ(feature_vector_last[kNumBands + i], 0.f);
    SM_EXPECT_EQ(feature_vector_last[kNumBands + kNumLowerBands + i], 0.f);
  }
  SM_EXPECT_NEAR(feature_vector[kNumBands + 3 * kNumLowerBands], feature_vector_last[kNumBands + 3 * kNumLowerBands],
                 1e-6f);
}

// features_extraction_unittest.cc: FeatureExtractionLowHighPitch.
SM_TEST(Agc2RnnVad, FeatureExtractionLowHighPitch) {
  constexpr int kNumTestDataFrames = (kBufSize24kHz + kFrameSize10ms24kHz - 1) / kFrameSize10ms24kHz;
  constexpr int kNumTestDataSize = kNumTestDataFrames * kFrameSize10ms24kHz;
  const auto pitch_is_valid = [](float pitch_hz) {
    const int pitch_period = static_cast<int>(static_cast<float>(kSampleRate24kHz) / pitch_hz);
    return kInitialMinPitch24kHz <= pitch_period && pitch_period <= kMaxPitch24kHz;
  };
  const auto create_pure_tone = [](float amplitude, float freq_hz, std::span<float> dst) {
    for (size_t i = 0; i < dst.size(); ++i) {
      dst[i] = amplitude * static_cast<float>(std::sin(2.f * std::numbers::pi * freq_hz * static_cast<double>(i) /
                                                       kSampleRate24kHz));
    }
  };
  const auto feed = [](FeaturesExtractor& fe, std::span<const float> samples,
                       std::span<float, kFeatureVectorSize> feature_vector) {
    bool is_silence = true;
    const size_t num_frames = samples.size() / kFrameSize10ms24kHz;
    for (size_t i = 0; i < num_frames; ++i) {
      is_silence = fe.CheckSilenceComputeFeatures(
          samples.subspan(i * kFrameSize10ms24kHz).first<kFrameSize10ms24kHz>(), feature_vector);
    }
    return is_silence;
  };
  constexpr float amplitude = 1000.f;
  constexpr float low_pitch_hz = 150.f;
  constexpr float high_pitch_hz = 250.f;
  SM_ASSERT_TRUE(pitch_is_valid(low_pitch_hz));
  SM_ASSERT_TRUE(pitch_is_valid(high_pitch_hz));
  FeaturesExtractor features_extractor;
  std::vector<float> samples(kNumTestDataSize);
  std::array<float, kFeatureVectorSize> feature_vector;
  constexpr int pitch_feature_index = kFeatureVectorSize - 2;
  // Low frequency tone, i.e. high period.
  create_pure_tone(amplitude, low_pitch_hz, samples);
  SM_ASSERT_FALSE(feed(features_extractor, samples, feature_vector));
  const float high_pitch_period = feature_vector[pitch_feature_index];
  // High frequency tone, i.e. low period.
  features_extractor.Reset();
  create_pure_tone(amplitude, high_pitch_hz, samples);
  SM_ASSERT_FALSE(feed(features_extractor, samples, feature_vector));
  const float low_pitch_period = feature_vector[pitch_feature_index];
  SM_EXPECT_LT(low_pitch_period, high_pitch_period);
}

// The pitch period of a pure tone: 48 kHz period near 48000 / f.
SM_TEST(Agc2RnnVad, PitchEstimatorFindsTonePeriod) {
  for (float f : {100.f, 150.f, 200.f, 250.f}) {
    std::array<float, kBufSize24kHz> buf;
    for (int i = 0; i < kBufSize24kHz; ++i) {
      buf[static_cast<size_t>(i)] =
          1000.f * static_cast<float>(std::sin(2.0 * std::numbers::pi * f * i / kSampleRate24kHz));
    }
    PitchEstimator pe;
    const int period = pe.Estimate(buf);
    SM_EXPECT_NEAR(period, 48000.0 / f, 2.0);
  }
}

namespace {

// vad_wrapper_unittest.cc's MockVad: returns the probabilities in turn and
// counts the calls.
class FakeVad : public VoiceActivityDetectorWrapper::MonoVad {
 public:
  FakeVad(int sample_rate_hz, std::vector<float> probabilities, int* resets, std::vector<size_t>* frame_sizes)
      : sample_rate_hz_(sample_rate_hz), probabilities_(std::move(probabilities)), resets_(resets),
        frame_sizes_(frame_sizes) {}
  int SampleRateHz() const override { return sample_rate_hz_; }
  void Reset() override { ++*resets_; }
  float Analyze(std::span<const float> frame) override {
    if (frame_sizes_) frame_sizes_->push_back(frame.size());
    const float p = probabilities_[next_];
    next_ = (next_ + 1) % probabilities_.size();
    return p;
  }

 private:
  int sample_rate_hz_;
  std::vector<float> probabilities_;
  size_t next_ = 0;
  int* resets_;
  std::vector<size_t>* frame_sizes_;
};

constexpr int kNoVadPeriodicReset = 10 * (std::numeric_limits<int>::max() / 10);

}  // namespace

// vad_wrapper_unittest.cc: CheckSpeechProbabilities, VadNoPeriodicReset.
SM_TEST(Agc2RnnVad, WrapperReturnsSpeechProbabilities) {
  const std::vector<float> speech_probabilities{0.709f, 0.484f, 0.882f, 0.167f, 0.44f,  0.525f,
                                                0.858f, 0.314f, 0.653f, 0.965f, 0.413f, 0.0f};
  int resets = 0;
  VoiceActivityDetectorWrapper w(kNoVadPeriodicReset,
                                 std::make_unique<FakeVad>(8000, speech_probabilities, &resets, nullptr), 8000);
  std::vector<float> frame(80, 0.f);
  for (float p : speech_probabilities) SM_EXPECT_EQ(w.Analyze(frame), p);
  SM_EXPECT_EQ(resets, 1);
}

// vad_wrapper_unittest.cc: VadPeriodicReset.
SM_TEST(Agc2RnnVad, WrapperResetsPeriodically) {
  for (int num_frames : {1, 19, 123}) {
    for (int reset_period_frames : {2, 5, 20, 53}) {
      int resets = 0;
      VoiceActivityDetectorWrapper w(reset_period_frames * 10,
                                     std::make_unique<FakeVad>(8000, std::vector<float>{1.f}, &resets, nullptr), 8000);
      std::vector<float> frame(80, 0.f);
      for (int i = 0; i < num_frames; ++i) w.Analyze(frame);
      SM_EXPECT_EQ(resets, 1 + num_frames / reset_period_frames);
    }
  }
}

// vad_wrapper_unittest.cc: CheckResampledFrameSize.
SM_TEST(Agc2RnnVad, WrapperResamplesToTheVadRate) {
  for (int input_rate : {8000, 16000, 48000}) {
    for (int vad_rate : {6000, 8000, 12000, 16000, 24000}) {
      int resets = 0;
      std::vector<size_t> sizes;
      VoiceActivityDetectorWrapper w(kNoVadPeriodicReset,
                                     std::make_unique<FakeVad>(vad_rate, std::vector<float>{1.f}, &resets, &sizes),
                                     input_rate);
      std::vector<float> frame(static_cast<size_t>(input_rate / 100), 0.f);
      w.Analyze(frame);
      SM_ASSERT_EQ(sizes.size(), size_t{1});
      SM_EXPECT_EQ(sizes[0], static_cast<size_t>(vad_rate / 100));
      SM_EXPECT_EQ(resets, 1);
    }
  }
}

// Silence gives exactly 0: the GRU is reset (rnn.cc).
SM_TEST(Agc2RnnVad, SilenceGivesZeroProbability) {
  VoiceActivityDetectorWrapper vad(16000);
  std::vector<float> frame(160, 0.f);
  for (int i = 0; i < 50; ++i) SM_EXPECT_EQ(vad.Analyze(frame), 0.f);
}

// push_sinc_resampler_unittest.cc: ResampleFloat, for the rates around the
// 16 -> 24 kHz conversion of AGC2's VAD, with libwebrtc's thresholds.
SM_TEST(Agc2RnnVad, PushSincResamplerChirpErrorWithinLibwebrtcThresholds) {
  struct Case {
    int in, out;
    double rms_error, low_freq_error;
  };
  constexpr double kResamplingRMSError = -14.42;
  const Case cases[] = {{16000, 22000, kResamplingRMSError, -62.54},
                        {8000, 22000, kResamplingRMSError, -62.73},
                        {16000, 44100, kResamplingRMSError, -62.54},
                        {16000, 48000, kResamplingRMSError, -63.96},
                        {48000, 22000, -15.01, -25.56}};
  const auto dbfs = [](double x) { return 20 * std::log10(x); };
  for (const Case& c : cases) {
    const size_t input_block_size = static_cast<size_t>(c.in / 100);
    const size_t output_block_size = static_cast<size_t>(c.out / 100);
    const size_t input_samples = static_cast<size_t>(c.in);
    const size_t output_samples = static_cast<size_t>(c.out);
    const double input_nyquist_freq = 0.5 * c.in;
    // sinusoidal_linear_chirp_source.cc, from 5 Hz.
    constexpr double kMinFrequency = 5;
    const auto chirp = [&](int rate, size_t samples, double delay, std::vector<float>& dst) {
      const double k = (input_nyquist_freq - kMinFrequency) / (static_cast<double>(samples) / rate);
      for (size_t i = 0; i < samples; ++i) {
        const double f = kMinFrequency + (static_cast<double>(i) - delay) * (input_nyquist_freq - kMinFrequency) /
                                             static_cast<double>(samples);
        if (f > 0.5 * rate || static_cast<double>(i) < delay) {
          dst[i] = 0;
        } else {
          const double t = (static_cast<double>(i) - delay) / rate;
          dst[i] = static_cast<float>(std::sin(2 * std::numbers::pi * (kMinFrequency * t + (k / 2) * t * t)));
        }
      }
    };
    PushSincResampler resampler(input_block_size, output_block_size);
    const size_t output_delay_samples = output_block_size - resampler.chunk_size_for_testing();
    std::vector<float> source(input_samples), resampled(output_samples), pure(output_samples);
    chirp(c.in, input_samples, 0, source);
    for (size_t i = 0; i < 100; ++i) {
      resampler.Resample(std::span<const float>(source).subspan(i * input_block_size, input_block_size),
                         std::span<float>(resampled).subspan(i * output_block_size, output_block_size));
    }
    chirp(c.out, output_samples, static_cast<double>(output_delay_samples), pure);
    const int minimum_rate = std::min(c.in, c.out);
    const double low_frequency_range = 0.7 * 0.5 * minimum_rate;
    const double high_frequency_range = 0.9 * 0.5 * minimum_rate;
    double sum_of_squares = 0, low_freq_max_error = 0, high_freq_max_error = 0;
    for (size_t i = 0; i < output_samples; ++i) {
      const double error = std::fabs(resampled[i] - pure[i]);
      const double f = kMinFrequency + (static_cast<double>(i) - static_cast<double>(output_delay_samples)) *
                                           (input_nyquist_freq - kMinFrequency) / static_cast<double>(output_samples);
      if (f < low_frequency_range) {
        low_freq_max_error = std::max(low_freq_max_error, error);
      } else if (f < high_frequency_range) {
        high_freq_max_error = std::max(high_freq_max_error, error);
      }
      sum_of_squares += error * error;
    }
    SM_EXPECT_LE(dbfs(std::sqrt(sum_of_squares / static_cast<double>(output_samples))), c.rms_error);
    SM_EXPECT_LE(dbfs(low_freq_max_error - 2.0 / 32767), c.low_freq_error);
    SM_EXPECT_LE(dbfs(high_freq_max_error - 2.0 / 32767), -6.01);
  }
}

// The RNN VAD on the test bench's speech-like signal and on white noise:
// speech gets a high probability in most frames, noise in few.
SM_TEST(Agc2RnnVad, SpeechLikeVersusNoise) {
  const auto speech_share = [](const std::vector<float>& x) {
    VoiceActivityDetectorWrapper vad(16000);
    int speech = 0, frames = 0;
    std::vector<float> s16(160);
    for (size_t off = 0; off + 160 <= x.size(); off += 160, ++frames) {
      for (size_t i = 0; i < 160; ++i) s16[i] = x[off + i] * 32768.f;
      if (vad.Analyze(s16) >= kVadConfidenceThreshold) ++speech;
    }
    return static_cast<double>(speech) / frames;
  };
  const double speech = speech_share(sim::SpeechLike(16000, 10.0, 3));
  const double noise = speech_share(sim::WhiteNoise(16000, 10.0, 0.05f, 3));
  SM_RECORD("speech_like_share", speech);
  SM_RECORD("white_noise_share", noise);
  SM_EXPECT_GT(speech, 0.7);
  SM_EXPECT_LT(noise, 0.05);
}

// GainController2 with its own RNN VAD on silence: no speech frames.
SM_TEST(Agc2RnnVad, GainController2RunsTheRnnVad) {
  GainController2 gc;
  std::vector<float> frame(160);
  for (int n = 0; n < 200; ++n) {
    std::fill(frame.begin(), frame.end(), 0.f);
    gc.Process(frame);
  }
  SM_EXPECT_EQ(gc.stats().speech_frames, uint64_t{0});
  SM_EXPECT_EQ(gc.stats().speech_probability, 0.f);
}
