// 6.4 AGC1's loudness analysis as libwebrtc M157's `Agc` (the class
// `MonoAgc` asks for the RMS error each 10 ms), with the voice activity
// detector it runs. `apm v2` (#242).
//
// Ported from compare/webrtc/reference/ with libwebrtc's constants, types
// (float vs double) and operation order:
//  - modules/audio_processing/agc/: agc.{cc,h} (`Agc`),
//    loudness_histogram.{cc,h} (`LoudnessHistogram`), utility.{cc,h}.
//  - modules/audio_processing/vad/: voice_activity_detector.{cc,h},
//    standalone_vad.{cc,h}, vad_audio_proc.{cc,h} and
//    vad_audio_proc_internal.h, pitch_based_vad.{cc,h}, pitch_internal.cc,
//    gmm.{cc,h} with noise_gmm_tables.h and voice_gmm_tables.h,
//    pole_zero_filter.{cc,h}, vad_circular_buffer.{cc,h}, common.h.
//  - common_audio/vad/: webrtc_vad.c, vad_core.{c,h}, vad_filterbank.{c,h},
//    vad_gmm.{c,h}, vad_sp.{c,h} (the fixed-point GMM VAD), and the
//    common_audio/signal_processing helpers they call (`WebRtcSpl_Energy`,
//    `WebRtcSpl_GetScalingSquare`, `WebRtcSpl_NormW32`, `WebRtcSpl_NormU32`,
//    `WebRtcSpl_DivW32W16`).
//  - modules/audio_coding/codecs/isac/main/source/: what `VadAudioProc`
//    reaches of filter_functions.c (`WebRtcIsac_AutoCorr`,
//    `WebRtcIsac_WeightingFilter`), isac_vad.c (the inits,
//    `WebRtcIsac_LevDurb`, `WebRtcIsac_SplitAndFilterFloat`),
//    pitch_estimator.c (`WebRtcIsac_PitchAnalysis`) and pitch_filter.c (the
//    pre, lookahead and gain filters), with settings.h / structs.h.
//  - common_audio/third_party/ooura/fft_size_256/fft4g.cc: `WebRtc_rdft`,
//    forward transform (`sm::audio::ooura::Rdft`, shared with the NS port).
//
// The results are bit-exact with the prebuilt libwebrtc M157 library on
// x86-64 (golden values in `Agc1VoiceActivity.*MatchesLibwebrtc*`; the files are
// built with -ffp-contract=off, as Chromium builds libwebrtc).
//
// Differences from libwebrtc (scope, not behaviour):
//  - `VoiceActivityDetector` and `Agc` take 10 ms at 16 kHz only (160
//    samples): `AgcManagerDirect` feeds them split band 0, which is 16 kHz.
//    libwebrtc resamples other rates with `Resampler`; here other chunk sizes
//    are ignored.
//  - `WebRtcVad` runs at 8, 16 and 32 kHz; 48 kHz (which needs SPL's
//    48-to-8 kHz resampler) returns -1.
//  - Factory functions (`Create`) are constructors. Only `Agc` is virtual,
//    as in libwebrtc, so `MonoAgc`'s tests can script its RMS errors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sm::audio::agc1 {

namespace detail {

// ---- common_audio/vad (WebRtcVad) ----------------------------------------

inline constexpr int kVadNumChannels = 6;   // Frequency bands.
inline constexpr int kVadNumGaussians = 2;  // Gaussians per band in the GMM.
inline constexpr int kVadTableSize = kVadNumChannels * kVadNumGaussians;
inline constexpr int kVadMinEnergy = 10;  // Minimum energy to trigger.

// vad_core.h: VadInstT (without the 48 kHz resampler state).
struct VadInst {
  int vad = 0;
  int32_t downsampling_filter_states[4]{};
  int16_t noise_means[kVadTableSize]{};
  int16_t speech_means[kVadTableSize]{};
  int16_t noise_stds[kVadTableSize]{};
  int16_t speech_stds[kVadTableSize]{};
  int32_t frame_counter = 0;
  int16_t over_hang = 0;
  int16_t num_of_speech = 0;
  int16_t index_vector[16 * kVadNumChannels]{};
  int16_t low_value_vector[16 * kVadNumChannels]{};
  int16_t mean_value[kVadNumChannels]{};
  int16_t upper_state[5]{};
  int16_t lower_state[5]{};
  int16_t hp_filter_state[4]{};
  int16_t over_hang_max_1[3]{};
  int16_t over_hang_max_2[3]{};
  int16_t individual[3]{};
  int16_t total[3]{};
  int init_flag = 0;
};

// `WebRtcVad_InitCore` (mode 0) and `WebRtcVad_set_mode_core` (0..3).
int VadInitCore(VadInst& self);
int VadSetModeCore(VadInst& self, int mode);
// `WebRtcVad_CalcVad{8,16,32}khz`: 0 (noise) or 1..6 (speech).
int VadCalcVad8khz(VadInst& self, const int16_t* speech_frame, size_t frame_length);
int VadCalcVad16khz(VadInst& self, const int16_t* speech_frame, size_t frame_length);
int VadCalcVad32khz(VadInst& self, const int16_t* speech_frame, size_t frame_length);
// vad_filterbank.c: `WebRtcVad_CalculateFeatures` (8 kHz input, 80, 160 or
// 240 samples); returns the total energy, `features` gets 6 log energies.
int16_t VadCalculateFeatures(VadInst& self, const int16_t* data_in, size_t data_length, int16_t* features);
// vad_gmm.c: `WebRtcVad_GaussianProbability` (Q20; `delta` in Q11).
int32_t VadGaussianProbability(int16_t input, int16_t mean, int16_t std, int16_t* delta);
// vad_sp.c: `WebRtcVad_Downsampling` and `WebRtcVad_FindMinimum`.
void VadDownsampling(const int16_t* signal_in, int16_t* signal_out, int32_t* filter_state, size_t in_length);
int16_t VadFindMinimum(VadInst& self, int16_t feature_value, int channel);
// webrtc_vad.c: `WebRtcVad_ValidRateAndFrameLength` (0 if valid, else -1).
int VadValidRateAndFrameLength(int rate, size_t frame_length);

// webrtc_vad.c: `WebRtcVad_Create` + `WebRtcVad_Init`, `WebRtcVad_set_mode`,
// `WebRtcVad_Process` (10, 20 or 30 ms; returns 1, 0 or -1 on error).
class WebRtcVad {
 public:
  WebRtcVad() { VadInitCore(inst_); }
  int set_mode(int mode) { return VadSetModeCore(inst_, mode); }
  int Process(int fs, const int16_t* audio_frame, size_t frame_length);
  const VadInst& inst() const { return inst_; }

 private:
  VadInst inst_;
};

// ---- modules/audio_processing/vad ------------------------------------------

// common.h.
inline constexpr int kVadSampleRateHz = 16000;
inline constexpr size_t kVadLength10Ms = kVadSampleRateHz / 100;
inline constexpr size_t kVadMaxNumFrames = 4;

struct AudioFeatures {
  double log_pitch_gain[kVadMaxNumFrames]{};
  double pitch_lag_hz[kVadMaxNumFrames]{};
  double spectral_peak[kVadMaxNumFrames]{};
  double rms[kVadMaxNumFrames]{};
  size_t num_frames = 0;
  bool silence = false;
};

// standalone_vad.h: WebRtcVad (mode 3 by default) over 30 ms buffers, as a
// one-sided indicator (0.5 if active, 0.01 if passive).
class StandaloneVad {
 public:
  StandaloneVad();
  int GetActivity(double* p, size_t length_p);
  int AddAudio(const int16_t* data, size_t length);  // 10 ms at 16 kHz.
  int set_mode(int mode);
  int mode() const { return mode_; }

 private:
  static constexpr size_t kMaxNum10msFrames = 3;
  WebRtcVad vad_;
  int16_t buffer_[kMaxNum10msFrames * kVadLength10Ms]{};
  size_t index_ = 0;
  int mode_;
};

// pole_zero_filter.h. Orders up to 24 and a non-zero first denominator
// coefficient (`Create` returns null otherwise).
class PoleZeroFilter {
 public:
  PoleZeroFilter(const float* numerator_coefficients, size_t order_numerator, const float* denominator_coefficients,
                 size_t order_denominator);
  int Filter(const int16_t* in, size_t num_input_samples, float* output);

 private:
  static constexpr size_t kMaxFilterOrder = 24;
  int16_t past_input_[kMaxFilterOrder * 2]{};
  float past_output_[kMaxFilterOrder * 2]{};
  float numerator_coefficients_[kMaxFilterOrder + 1]{};
  float denominator_coefficients_[kMaxFilterOrder + 1]{};
  size_t order_numerator_;
  size_t order_denominator_;
  size_t highest_order_;
};

// vad_circular_buffer.h (`buffer_size` > 0).
class VadCircularBuffer {
 public:
  explicit VadCircularBuffer(int buffer_size);
  bool is_full() const { return is_full_; }
  double Oldest() const;
  void Insert(double value);
  void Reset();
  double Mean();
  int RemoveTransient(int width_threshold, double val_threshold);

 private:
  int Get(int index, double* value) const;
  int Set(int index, double value);
  int BufferLevel();
  int ConvertToLinearIndex(int* index) const;

  std::vector<double> buffer_;
  bool is_full_ = false;
  int index_ = 0;
  int buffer_size_;
  double sum_ = 0;
};

// gmm.h, with the tables of noise_gmm_tables.h and voice_gmm_tables.h.
struct GmmParameters {
  const double* weight;
  const double* mean;
  const double* covar_inverse;
  int dimension;
  int num_mixtures;
};
double EvaluateGmm(const double* x, const GmmParameters& gmm_parameters);
const GmmParameters& NoiseGmm();
const GmmParameters& VoiceGmm();

// pitch_internal.h.
void GetSubframesPitchParameters(int sampling_rate_hz, double* gains, double* lags, int num_in_frames,
                                 int num_out_frames, double* log_old_gain, double* old_lag, double* log_pitch_gain,
                                 double* pitch_lag_hz);

// pitch_based_vad.h.
class PitchBasedVad {
 public:
  PitchBasedVad();
  int VoicingProbability(const AudioFeatures& features, double* p_combined);

 private:
  int UpdatePrior(double p);
  GmmParameters noise_gmm_;
  GmmParameters voice_gmm_;
  double p_prior_;
  VadCircularBuffer circular_buffer_;
};

// ---- iSAC pieces VadAudioProc uses (settings.h, structs.h) ----------------
namespace isac {

inline constexpr int kQLookahead = 24;                      // QLOOKAHEAD
inline constexpr int kFrameSamples = 480;                   // FRAMESAMPLES
inline constexpr int kFrameSamplesHalf = 240;               // FRAMESAMPLES_HALF
inline constexpr int kAllpassSections = 2;                  // ALLPASSSECTIONS
inline constexpr int kQOrder = 3;                           // QORDER
inline constexpr int kHpOrder = 2;                          // HPORDER
inline constexpr int kPitchFrameLen = kFrameSamplesHalf;    // PITCH_FRAME_LEN
inline constexpr int kPitchMaxLag = 140;                    // PITCH_MAX_LAG
inline constexpr int kPitchCorrLen2 = 60;                   // PITCH_CORR_LEN2
inline constexpr int kPitchCorrStep2 = kPitchFrameLen / 4;  // PITCH_CORR_STEP2
inline constexpr int kPitchWlpcOrder = 6;                   // PITCH_WLPCORDER
inline constexpr int kPitchWlpcWinLen = kPitchFrameLen;     // PITCH_WLPCWINLEN
inline constexpr int kPitchWlpcBufLen = kPitchWlpcWinLen;   // PITCH_WLPCBUFLEN
inline constexpr int kPitchBuffSize = kPitchMaxLag + 50;    // PITCH_BUFFSIZE
inline constexpr int kPitchDampOrder = 5;                   // PITCH_DAMPORDER

// structs.h: PitchFiltstr.
struct PitchFiltState {
  double ubuf[kPitchBuffSize];
  double ystate[kPitchDampOrder];
  double oldlagp[1];
  double oldgainp[1];
};
// structs.h: WeightFiltstr.
struct WeightFiltState {
  double buffer[kPitchWlpcBufLen];
  double istate[kPitchWlpcOrder];
  double weostate[kPitchWlpcOrder];
  double whostate[kPitchWlpcOrder];
  double window[kPitchWlpcWinLen];
};
// structs.h: PitchAnalysisStruct.
struct PitchAnalysisState {
  double dec_buffer[kPitchCorrLen2 + kPitchCorrStep2 + kPitchMaxLag / 2 - kPitchFrameLen / 2 + 2];
  double decimator_state[2 * kAllpassSections + 1];
  double hp_state[2];
  double whitened_buf[kQLookahead];
  double inbuf[kQLookahead];
  PitchFiltState PFstr_wght;
  PitchFiltState PFstr;
  WeightFiltState Wghtstr;
};
// structs.h: PreFiltBankstr, the float members (the only ones the float
// filter bank uses).
struct PreFiltBankState {
  float INSTAT1_float[2 * (kQOrder - 1)];
  float INSTAT2_float[2 * (kQOrder - 1)];
  float INSTATLA1_float[2 * (kQOrder - 1)];
  float INSTATLA2_float[2 * (kQOrder - 1)];
  float INLABUF1_float[kQLookahead];
  float INLABUF2_float[kQLookahead];
  float HPstates_float[kHpOrder];
};

void InitPitchAnalysis(PitchAnalysisState& state);           // WebRtcIsac_InitPitchAnalysis
void InitPreFilterbank(PreFiltBankState& prefiltdata);       // WebRtcIsac_InitPreFilterbank
double LevDurb(double* a, double* k, double* r, size_t order);  // WebRtcIsac_LevDurb
void AutoCorr(double* r, const double* x, size_t n, size_t order);  // WebRtcIsac_AutoCorr
// WebRtcIsac_SplitAndFilterFloat: 480 samples in, 240 per band out.
void SplitAndFilterFloat(const float* in, float* lp, float* hp, double* lp_la, double* hp_la,
                         PreFiltBankState& prefiltdata);
// WebRtcIsac_PitchAnalysis: 240 samples in, 264 out, 4 lags and gains.
void PitchAnalysis(const double* in, double* out, PitchAnalysisState& state, double* lags, double* gains);

}  // namespace isac

// vad_audio_proc.h: features (pitch gain and lag, spectral peak, RMS) of
// three 10 ms subframes every 30 ms.
class VadAudioProc {
 public:
  VadAudioProc();
  int ExtractFeatures(const int16_t* audio_frame, size_t length, AudioFeatures* audio_features);

  static constexpr size_t kDftSize = 512;

 private:
  void PitchAnalysis(double* pitch_gains, double* pitch_lags_hz, size_t length);
  void SubframeCorrelation(double* corr, size_t length_corr, size_t subframe_index);
  void GetLpcPolynomials(double* lpc, size_t length_lpc);
  void FindFirstSpectralPeaks(double* f_peak, size_t length_f_peak);
  void Rms(double* rms, size_t length_rms);
  void ResetBuffer();

  static constexpr size_t kNumPastSignalSamples = size_t{kVadSampleRateHz / 200};
  static constexpr int kNoError = 0;
  static constexpr size_t kNum10msSubframes = 3;
  static constexpr size_t kNumSubframeSamples = size_t{kVadSampleRateHz / 100};
  static constexpr size_t kNumSamplesToProcess = kNum10msSubframes * kNumSubframeSamples;
  static constexpr size_t kBufferLength = kNumPastSignalSamples + kNumSamplesToProcess;
  static constexpr size_t kIpLength = kDftSize >> 1;
  static constexpr size_t kWLength = kDftSize >> 1;
  static constexpr size_t kLpcOrder = 16;

  size_t ip_[kIpLength]{};
  float w_fft_[kWLength]{};
  float audio_buffer_[kBufferLength]{};
  size_t num_buffer_samples_;
  double log_old_gain_;
  double old_lag_;
  isac::PitchAnalysisState pitch_analysis_;
  isac::PreFiltBankState pre_filter_;
  PoleZeroFilter high_pass_filter_;
};

}  // namespace detail

// voice_activity_detector.h: per 10 ms chunk, the voice probability and RMS
// of each 10 ms subframe once 30 ms are buffered (three values every third
// chunk, none otherwise).
class VoiceActivityDetector {
 public:
  VoiceActivityDetector();
  // 160 samples (10 ms at 16 kHz); other sizes are ignored.
  void ProcessChunk(std::span<const int16_t> audio);
  std::span<const double> chunkwise_voice_probabilities() const { return {probabilities_.data(), num_chunkwise_}; }
  std::span<const double> chunkwise_rms() const { return {rms_.data(), num_chunkwise_}; }
  float last_voice_probability() const { return last_voice_probability_; }

 private:
  std::array<double, detail::kVadMaxNumFrames> probabilities_{};
  std::array<double, detail::kVadMaxNumFrames> rms_{};
  size_t num_chunkwise_ = 0;
  float last_voice_probability_;
  detail::VadAudioProc audio_processing_;
  detail::StandaloneVad standalone_vad_;
  detail::PitchBasedVad pitch_based_vad_;
  detail::AudioFeatures features_;
};

// loudness_histogram.h: a histogram of loudness weighted by activity
// probability, over the whole past (default constructor) or the last
// `window_size` updates.
class LoudnessHistogram {
 public:
  LoudnessHistogram();
  explicit LoudnessHistogram(int window_size);  // `window_size` >= 0.
  void Update(double rms, double activity_probability);
  void Reset();
  double CurrentRms() const;    // Mean of the histogram, linear.
  double AudioContent() const;  // Sum of the histogram.
  int num_updates() const { return num_updates_; }

 private:
  int GetBinIndex(double rms);
  void RemoveOldestEntryAndUpdate();
  void InsertNewestEntryAndUpdate(int activity_prob_q10, int hist_index);
  void UpdateHist(int activity_prob_q10, int hist_index);
  void RemoveTransient();

  static constexpr int kHistSize = 77;
  int num_updates_ = 0;
  int64_t audio_content_q10_ = 0;
  int64_t bin_count_q10_[kHistSize]{};
  std::vector<int> activity_probability_;
  std::vector<int> hist_bin_index_;
  int buffer_index_ = 0;
  int buffer_is_full_ = false;
  int len_circular_buffer_ = 0;
  int len_high_activity_ = 0;
};

// utility.h.
double Loudness2Db(double loudness);
double Linear2Loudness(double rms);
double Db2Loudness(double db);
double Dbfs2Loudness(double dbfs);

// agc.h: `Agc`, the loudness estimate behind `MonoAgc`'s gain decisions.
class Agc {
 public:
  Agc();
  virtual ~Agc() = default;
  // One mono 10 ms chunk at 16 kHz (160 samples).
  virtual void Process(std::span<const int16_t> audio);
  // The target minus the current RMS level in dB, once 100 frames with
  // enough voice activity are in (then the histogram restarts).
  virtual bool GetRmsErrorDb(int* error);
  virtual void Reset();
  virtual int set_target_level_dbfs(int level);  // (-100, 0); else -1.
  virtual int target_level_dbfs() const { return target_level_dbfs_; }
  virtual float voice_probability() const { return vad_.last_voice_probability(); }

 private:
  double target_level_loudness_;
  int target_level_dbfs_;
  LoudnessHistogram histogram_;
  LoudnessHistogram inactive_histogram_;
  VoiceActivityDetector vad_;
};

}  // namespace sm::audio::agc1
