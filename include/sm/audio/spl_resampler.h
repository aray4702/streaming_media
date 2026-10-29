// 6.11 Sample-rate conversion: libwebrtc M157's `Resampler`
// (common_audio/resampler/resampler.cc), the fixed-point int16 resampler on
// the signal processing library's allpass half-band filters and fractional
// polyphase filters (common_audio/signal_processing/resample_by_2.c,
// resample_by_2_internal.c, resample.c, resample_48khz.c,
// resample_fractional.c), with their filter coefficients.
//
// Supports the rate pairs whose reduced ratio is one of libwebrtc's 21 modes
// (1:1, 1:2, 1:3, 1:4, 1:6, 1:12, 2:3, 2:11, 4:11, 8:11, 11:16, 11:32 and
// their inverses), mono or interleaved stereo. Some modes take only whole
// blocks (for example 160 input samples for 1:3), as libwebrtc. Every call
// returns 0 on success and -1 on failure, as libwebrtc.
//
// Differences from libwebrtc: scratch buffers are members, not malloc'ed per
// call, and the fixed-point sums wrap explicitly (libwebrtc relies on
// two's-complement overflow under RTC_NO_SANITIZE, bugs.webrtc.org/5486), so
// the output is the same, bit for bit, without undefined behaviour. The
// ARMv7 and MIPS assembly variants are not ported; the portable C ones are.
// Only the library functions this resampler calls are ported (not the
// 48 <-> 8 kHz ones, which only other modules use).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sm::audio {

class SplResampler {
 public:
  SplResampler() = default;
  SplResampler(int in_freq, int out_freq, size_t num_channels);
  ~SplResampler();
  SplResampler(const SplResampler&) = delete;
  SplResampler& operator=(const SplResampler&) = delete;

  // Resets all state. -1 for an unsupported rate pair or channel count
  // (only 1 and 2 are supported).
  int Reset(int in_freq, int out_freq, size_t num_channels);
  // Resets only if a parameter changed (rates compared in whole kHz, as
  // libwebrtc).
  int ResetIfNeeded(int in_freq, int out_freq, size_t num_channels);
  // Resamples `length_in` interleaved samples into `samples_out`, which has
  // room for `max_len`; `out_len` receives the number written.
  int Push(const int16_t* samples_in, size_t length_in, int16_t* samples_out, size_t max_len,
           size_t& out_len);

  // Signal processing library state structs (signal_processing_library.h).
  struct State48khzTo16khz {
    int32_t S_48_48[16];
    int32_t S_48_32[8];
    int32_t S_32_16[8];
  };
  struct State16khzTo48khz {
    int32_t S_16_32[8];
    int32_t S_32_24[8];
    int32_t S_24_48[8];
  };
  struct State22khzTo16khz {
    int32_t S_22_44[8];
    int32_t S_44_32[8];
    int32_t S_32_16[8];
  };
  struct State16khzTo22khz {
    int32_t S_16_32[8];
    int32_t S_32_22[8];
  };
  struct State22khzTo8khz {
    int32_t S_22_22[16];
    int32_t S_22_16[8];
    int32_t S_16_8[8];
  };
  struct State8khzTo22khz {
    int32_t S_8_16[8];
    int32_t S_16_11[8];
    int32_t S_11_22[8];
  };

 private:
  enum Mode {
    kMode1To1,
    kMode1To2,
    kMode1To3,
    kMode1To4,
    kMode1To6,
    kMode1To12,
    kMode2To3,
    kMode2To11,
    kMode4To11,
    kMode8To11,
    kMode11To16,
    kMode11To32,
    kMode2To1,
    kMode3To1,
    kMode4To1,
    kMode6To1,
    kMode12To1,
    kMode3To2,
    kMode11To2,
    kMode11To4,
    kMode11To8
  };
  static int ComputeMode(int in_freq_hz, int out_freq_hz, Mode* mode);
  void ClearStates();

  // The states libwebrtc mallocs per mode: up to three by-2 filter states
  // (8 words each) and one of the fractional resamplers' structs.
  int32_t by2_[3][8] = {};
  State48khzTo16khz s48_16_ = {};
  State16khzTo48khz s16_48_ = {};
  State22khzTo16khz s22_16_ = {};
  State16khzTo22khz s16_22_ = {};
  State22khzTo8khz s22_8_ = {};
  State8khzTo22khz s8_22_ = {};
  std::vector<int16_t> tmp_, tmp2_;
  std::vector<int32_t> tmp_mem_;

  int in_khz_ = 0;
  int out_khz_ = 0;
  Mode mode_ = kMode1To1;
  size_t num_channels_ = 0;
  // Stereo runs two mono instances, as libwebrtc.
  std::unique_ptr<SplResampler> left_, right_;
  std::vector<int16_t> left_in_, right_in_, left_out_, right_out_;
};

}  // namespace sm::audio
