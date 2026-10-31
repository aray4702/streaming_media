// Ported from compare/webrtc/reference/common_audio/resampler/resampler.cc
// and common_audio/signal_processing/{resample_by_2,resample_by_2_internal,
// resample,resample_48khz,resample_fractional}.c (libwebrtc M157), same
// coefficients, block sizes and operation order; see the header.
#include "sm/audio/spl_resampler.h"

#include <cstring>

namespace sm::audio {

namespace {

// Two's-complement arithmetic without signed overflow: libwebrtc lets these
// sums wrap (RTC_NO_SANITIZE("signed-integer-overflow"), bugs.webrtc.org/5486).
int32_t Add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t Sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t MulAdd(int32_t acc, int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(acc) + static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
int32_t Shl(int32_t a, int n) { return static_cast<int32_t>(static_cast<uint32_t>(a) << n); }

int16_t SatW32ToW16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return static_cast<int16_t>(v);
}

// ---- resample_by_2.c (portable path) ----

constexpr uint16_t kResampleAllpass1[3] = {3284, 24441, 49528};
constexpr uint16_t kResampleAllpass2[3] = {12199, 37471, 60255};

// WEBRTC_SPL_SCALEDIFF32: C + the 32 most significant bits of A * B.
int32_t ScaleDiff32(int32_t a, int32_t b, int32_t c) {
  return Add(MulAdd(c, b >> 16, a),
             static_cast<int32_t>((static_cast<uint32_t>(b & 0x0000FFFF) * static_cast<uint32_t>(a)) >> 16));
}

void DownsampleBy2(const int16_t* in, size_t len, int16_t* out, int32_t* st) {
  int32_t s0 = st[0], s1 = st[1], s2 = st[2], s3 = st[3], s4 = st[4], s5 = st[5], s6 = st[6], s7 = st[7];
  for (size_t i = len >> 1; i > 0; --i) {
    // lower allpass filter
    int32_t in32 = static_cast<int32_t>(*in++) * (1 << 10);
    int32_t diff = Sub(in32, s1);
    int32_t tmp1 = ScaleDiff32(kResampleAllpass2[0], diff, s0);
    s0 = in32;
    diff = Sub(tmp1, s2);
    int32_t tmp2 = ScaleDiff32(kResampleAllpass2[1], diff, s1);
    s1 = tmp1;
    diff = Sub(tmp2, s3);
    s3 = ScaleDiff32(kResampleAllpass2[2], diff, s2);
    s2 = tmp2;
    // upper allpass filter
    in32 = static_cast<int32_t>(*in++) * (1 << 10);
    diff = Sub(in32, s5);
    tmp1 = ScaleDiff32(kResampleAllpass1[0], diff, s4);
    s4 = in32;
    diff = Sub(tmp1, s6);
    tmp2 = ScaleDiff32(kResampleAllpass1[1], diff, s5);
    s5 = tmp1;
    diff = Sub(tmp2, s7);
    s7 = ScaleDiff32(kResampleAllpass1[2], diff, s6);
    s6 = tmp2;
    // add two allpass outputs, divide by two and round
    *out++ = SatW32ToW16(Add(Add(s3, s7), 1024) >> 11);
  }
  st[0] = s0, st[1] = s1, st[2] = s2, st[3] = s3, st[4] = s4, st[5] = s5, st[6] = s6, st[7] = s7;
}

void UpsampleBy2(const int16_t* in, size_t len, int16_t* out, int32_t* st) {
  int32_t s0 = st[0], s1 = st[1], s2 = st[2], s3 = st[3], s4 = st[4], s5 = st[5], s6 = st[6], s7 = st[7];
  for (size_t i = len; i > 0; --i) {
    // lower allpass filter
    int32_t in32 = static_cast<int32_t>(*in++) * (1 << 10);
    int32_t diff = Sub(in32, s1);
    int32_t tmp1 = ScaleDiff32(kResampleAllpass1[0], diff, s0);
    s0 = in32;
    diff = Sub(tmp1, s2);
    int32_t tmp2 = ScaleDiff32(kResampleAllpass1[1], diff, s1);
    s1 = tmp1;
    diff = Sub(tmp2, s3);
    s3 = ScaleDiff32(kResampleAllpass1[2], diff, s2);
    s2 = tmp2;
    *out++ = SatW32ToW16(Add(s3, 512) >> 10);
    // upper allpass filter
    diff = Sub(in32, s5);
    tmp1 = ScaleDiff32(kResampleAllpass2[0], diff, s4);
    s4 = in32;
    diff = Sub(tmp1, s6);
    tmp2 = ScaleDiff32(kResampleAllpass2[1], diff, s5);
    s5 = tmp1;
    diff = Sub(tmp2, s7);
    s7 = ScaleDiff32(kResampleAllpass2[2], diff, s6);
    s6 = tmp2;
    *out++ = SatW32ToW16(Add(s7, 512) >> 10);
  }
  st[0] = s0, st[1] = s1, st[2] = s2, st[3] = s3, st[4] = s4, st[5] = s5, st[6] = s6, st[7] = s7;
}

// ---- resample_by_2_internal.c ----

constexpr int16_t kResampleAllpass[2][3] = {{821, 6110, 12382}, {3050, 9368, 15063}};

// One three-section allpass chain on state s[0..3], as every filter loop in
// resample_by_2_internal.c writes it out: the first section rounds the
// difference, the other two truncate it (`>> 14`, then +1 if negative).
int32_t Allpass(int32_t x, int32_t* s, const int16_t* k) {
  int32_t diff = Sub(x, s[1]);
  diff = Add(diff, 1 << 13) >> 14;
  int32_t tmp1 = MulAdd(s[0], diff, k[0]);
  s[0] = x;
  diff = Sub(tmp1, s[2]);
  diff = diff >> 14;
  if (diff < 0) diff += 1;
  int32_t tmp0 = MulAdd(s[1], diff, k[1]);
  s[1] = tmp1;
  diff = Sub(tmp0, s[3]);
  diff = diff >> 14;
  if (diff < 0) diff += 1;
  s[3] = MulAdd(s[2], diff, k[2]);
  s[2] = tmp0;
  return s[3];
}

int32_t ShortToQ15(int16_t v) { return Shl(v, 15) + (1 << 14); }

int16_t ClampW16(int32_t v) {
  if (v > 0x00007FFF) v = 0x00007FFF;
  if (v < static_cast<int32_t>(0xFFFF8000)) v = static_cast<int32_t>(0xFFFF8000);
  return static_cast<int16_t>(v);
}

// decimator; input int32 (Q15 + offset), OVERWRITTEN; output int16 (len/2).
void DownBy2IntToShort(int32_t* in, int32_t len, int16_t* out, int32_t* state) {
  len >>= 1;
  // lower allpass filter (operates on even input samples)
  for (int32_t i = 0; i < len; i++) in[i << 1] = Allpass(in[i << 1], &state[0], kResampleAllpass[1]) >> 1;
  ++in;
  // upper allpass filter (operates on odd input samples)
  for (int32_t i = 0; i < len; i++) in[i << 1] = Allpass(in[i << 1], &state[4], kResampleAllpass[0]) >> 1;
  --in;
  // combine allpass outputs
  for (int32_t i = 0; i < len; i += 2) {
    out[i] = ClampW16(Add(in[i << 1], in[(i << 1) + 1]) >> 15);
    out[i + 1] = ClampW16(Add(in[(i << 1) + 2], in[(i << 1) + 3]) >> 15);
  }
}

// interpolator; input int16; output int32 (normalized, len*2).
void UpBy2ShortToInt(const int16_t* in, int32_t len, int32_t* out, int32_t* state) {
  // upper allpass filter (generates odd output samples)
  for (int32_t i = 0; i < len; i++) out[i << 1] = Allpass(ShortToQ15(in[i]), &state[4], kResampleAllpass[0]) >> 15;
  ++out;
  // lower allpass filter (generates even output samples)
  for (int32_t i = 0; i < len; i++) out[i << 1] = Allpass(ShortToQ15(in[i]), &state[0], kResampleAllpass[1]) >> 15;
}

// interpolator; input int32 (Q15 + offset); output int16 (saturated).
void UpBy2IntToShort(const int32_t* in, int32_t len, int16_t* out, int32_t* state) {
  for (int32_t i = 0; i < len; i++) out[i << 1] = ClampW16(Allpass(in[i], &state[4], kResampleAllpass[0]) >> 15);
  ++out;
  for (int32_t i = 0; i < len; i++) out[i << 1] = ClampW16(Allpass(in[i], &state[0], kResampleAllpass[1]) >> 15);
}

// decimator; input int16; output int32 (Q15 + offset, len/2).
void DownBy2ShortToInt(const int16_t* in, int32_t len, int32_t* out, int32_t* state) {
  len >>= 1;
  // lower allpass filter (operates on even input samples)
  for (int32_t i = 0; i < len; i++) out[i] = Allpass(ShortToQ15(in[i << 1]), &state[0], kResampleAllpass[1]) >> 1;
  ++in;
  // upper allpass filter (operates on odd input samples)
  for (int32_t i = 0; i < len; i++)
    out[i] = Add(out[i], Allpass(ShortToQ15(in[i << 1]), &state[4], kResampleAllpass[0]) >> 1);
}

// lowpass filter; input int16; output int32 (normalized). State 16.
void LPBy2ShortToInt(const int16_t* in, int32_t len, int32_t* out, int32_t* state) {
  len >>= 1;
  // lower allpass filter: odd input -> even output samples
  ++in;
  int32_t tmp0 = state[12];  // initial state of polyphase delay element
  for (int32_t i = 0; i < len; i++) {
    out[i << 1] = Allpass(tmp0, &state[0], kResampleAllpass[1]) >> 1;
    tmp0 = ShortToQ15(in[i << 1]);
  }
  --in;
  // upper allpass filter: even input -> even output samples
  for (int32_t i = 0; i < len; i++)
    out[i << 1] = Add(out[i << 1], Allpass(ShortToQ15(in[i << 1]), &state[4], kResampleAllpass[0]) >> 1) >> 15;
  // switch to odd output samples
  ++out;
  // lower allpass filter: even input -> odd output samples
  for (int32_t i = 0; i < len; i++) out[i << 1] = Allpass(ShortToQ15(in[i << 1]), &state[8], kResampleAllpass[1]) >> 1;
  // upper allpass filter: odd input -> odd output samples
  ++in;
  for (int32_t i = 0; i < len; i++)
    out[i << 1] = Add(out[i << 1], Allpass(ShortToQ15(in[i << 1]), &state[12], kResampleAllpass[0]) >> 1) >> 15;
}

// lowpass filter; input int32 (Q15 + offset); output int32 (normalized). State 16.
void LPBy2IntToInt(const int32_t* in, int32_t len, int32_t* out, int32_t* state) {
  len >>= 1;
  // lower allpass filter: odd input -> even output samples
  ++in;
  int32_t tmp0 = state[12];  // initial state of polyphase delay element
  for (int32_t i = 0; i < len; i++) {
    out[i << 1] = Allpass(tmp0, &state[0], kResampleAllpass[1]) >> 1;
    tmp0 = in[i << 1];
  }
  --in;
  // upper allpass filter: even input -> even output samples
  for (int32_t i = 0; i < len; i++)
    out[i << 1] = Add(out[i << 1], Allpass(in[i << 1], &state[4], kResampleAllpass[0]) >> 1) >> 15;
  // switch to odd output samples
  ++out;
  // lower allpass filter: even input -> odd output samples
  for (int32_t i = 0; i < len; i++) out[i << 1] = Allpass(in[i << 1], &state[8], kResampleAllpass[1]) >> 1;
  // upper allpass filter: odd input -> odd output samples
  ++in;
  for (int32_t i = 0; i < len; i++)
    out[i << 1] = Add(out[i << 1], Allpass(in[i << 1], &state[12], kResampleAllpass[0]) >> 1) >> 15;
}

// ---- resample_fractional.c ----

constexpr int16_t kCoefficients48To32[2][8] = {{778, -2050, 1087, 23285, 12903, -3783, 441, 222},
                                               {222, 441, -3783, 12903, 23285, 1087, -2050, 778}};

constexpr int16_t kCoefficients32To24[3][8] = {{767, -2362, 2434, 24406, 10620, -3838, 721, 90},
                                               {386, -381, -2646, 19062, 19062, -2646, -381, 386},
                                               {90, 721, -3838, 10620, 24406, 2434, -2362, 767}};

constexpr int16_t kCoefficients44To32[4][9] = {{117, -669, 2245, -6183, 26267, 13529, -3245, 845, -138},
                                               {-101, 612, -2283, 8532, 29790, -5138, 1789, -524, 91},
                                               {50, -292, 1016, -3064, 32010, 3933, -1147, 315, -53},
                                               {-156, 974, -3863, 18603, 21691, -6246, 2353, -712, 126}};

// 1 << 14 plus the dot product of `n` coefficients with in[0..n-1].
int32_t Dot(const int16_t* coef, const int32_t* in, int n) {
  int32_t tmp = 1 << 14;
  for (int j = 0; j < n; ++j) tmp = MulAdd(tmp, coef[j], in[j]);
  return tmp;
}

// Two 9-tap inner products, in1 forward and in2 backward
// (WebRtcSpl_ResampDotProduct, WebRtcSpl_DotProdIntToInt).
void DotProduct(const int32_t* in1, const int32_t* in2, const int16_t* coef, int32_t* out1, int32_t* out2) {
  int32_t tmp1 = 16384, tmp2 = 16384;
  for (int j = 0; j < 9; ++j) {
    tmp1 = MulAdd(tmp1, coef[j], in1[j]);
    tmp2 = MulAdd(tmp2, coef[j], in2[-j]);
  }
  *out1 = tmp1;
  *out2 = tmp2;
}

// Ratio 2/3: int32 (normalized) 3*K -> int32 (Q15 + offset) 2*K.
void Resample48khzTo32khz(const int32_t* in, int32_t* out, size_t k) {
  for (size_t m = 0; m < k; ++m) {
    out[0] = Dot(kCoefficients48To32[0], &in[0], 8);
    out[1] = Dot(kCoefficients48To32[1], &in[1], 8);
    in += 3;
    out += 2;
  }
}

// Ratio 3/4: int32 (normalized) 4*K -> int32 (Q15 + offset) 3*K.
void Resample32khzTo24khz(const int32_t* in, int32_t* out, size_t k) {
  for (size_t m = 0; m < k; ++m) {
    out[0] = Dot(kCoefficients32To24[0], &in[0], 8);
    out[1] = Dot(kCoefficients32To24[1], &in[1], 8);
    out[2] = Dot(kCoefficients32To24[2], &in[2], 8);
    in += 4;
    out += 3;
  }
}

// Ratio 8/11: int32 (normalized) 11*K -> int32 (Q15 + offset) 8*K.
void Resample44khzTo32khz(const int32_t* in, int32_t* out, size_t k) {
  for (size_t m = 0; m < k; ++m) {
    // first output sample
    out[0] = Shl(in[3], 15) + (1 << 14);
    out[4] = Dot(kCoefficients44To32[3], &in[5], 9);
    DotProduct(&in[0], &in[17], kCoefficients44To32[0], &out[1], &out[7]);
    DotProduct(&in[2], &in[15], kCoefficients44To32[1], &out[2], &out[6]);
    DotProduct(&in[3], &in[14], kCoefficients44To32[2], &out[3], &out[5]);
    in += 11;
    out += 8;
  }
}

// ---- resample.c ----

constexpr int16_t kCoefficients32To22[5][9] = {{127, -712, 2359, -6333, 23456, 16775, -3695, 945, -154},
                                               {-39, 230, -830, 2785, 32366, -2324, 760, -218, 38},
                                               {117, -663, 2222, -6133, 26634, 13070, -3174, 831, -137},
                                               {-77, 457, -1677, 5958, 31175, -4136, 1405, -408, 71},
                                               {98, -560, 1900, -5406, 29240, 9423, -2480, 663, -110}};

// The five inner products of the 16 -> 11 filter, shared by the int and
// short outputs.
template <typename F>
void DotProducts32To22(const int32_t* in, F store) {
  int32_t a, b;
  DotProduct(&in[0], &in[22], kCoefficients32To22[0], &a, &b), store(1, a), store(10, b);
  DotProduct(&in[2], &in[20], kCoefficients32To22[1], &a, &b), store(2, a), store(9, b);
  DotProduct(&in[3], &in[19], kCoefficients32To22[2], &a, &b), store(3, a), store(8, b);
  DotProduct(&in[5], &in[17], kCoefficients32To22[3], &a, &b), store(4, a), store(7, b);
  DotProduct(&in[6], &in[16], kCoefficients32To22[4], &a, &b), store(5, a), store(6, b);
}

// Ratio 11/16: int32 (normalized) 16*K -> int32 (Q15 + offset) 11*K.
void Resample32khzTo22khzIntToInt(const int32_t* in, int32_t* out, int32_t k) {
  for (int32_t m = 0; m < k; ++m) {
    out[0] = Shl(in[3], 15) + (1 << 14);
    DotProducts32To22(in, [out](int i, int32_t v) { out[i] = v; });
    in += 16;
    out += 11;
  }
}

// Ratio 11/16: int32 (normalized) 16*K -> int16 (saturated) 11*K.
void Resample32khzTo22khzIntToShort(const int32_t* in, int16_t* out, int32_t k) {
  for (int32_t m = 0; m < k; ++m) {
    out[0] = ClampW16(in[3]);
    // scale down, round and saturate (WebRtcSpl_DotProdIntToShort)
    DotProducts32To22(in, [out](int i, int32_t v) { out[i] = ClampW16(v >> 15); });
    in += 16;
    out += 11;
  }
}

using State48To16 = SplResampler::State48khzTo16khz;
using State16To48 = SplResampler::State16khzTo48khz;
using State22To16 = SplResampler::State22khzTo16khz;
using State16To22 = SplResampler::State16khzTo22khz;
using State22To8 = SplResampler::State22khzTo8khz;
using State8To22 = SplResampler::State8khzTo22khz;

// 480 in -> 160 out; tmpmem 496 words.
void Resample48khzTo16khz(const int16_t* in, int16_t* out, State48To16* state, int32_t* tmpmem) {
  LPBy2ShortToInt(in, 480, tmpmem + 16, state->S_48_48);
  std::memcpy(tmpmem + 8, state->S_48_32, 8 * sizeof(int32_t));
  std::memcpy(state->S_48_32, tmpmem + 488, 8 * sizeof(int32_t));
  Resample48khzTo32khz(tmpmem + 8, tmpmem, 160);
  DownBy2IntToShort(tmpmem, 320, out, state->S_32_16);
}

// 160 in -> 480 out; tmpmem 336 words.
void Resample16khzTo48khz(const int16_t* in, int16_t* out, State16To48* state, int32_t* tmpmem) {
  UpBy2ShortToInt(in, 160, tmpmem + 16, state->S_16_32);
  std::memcpy(tmpmem + 8, state->S_32_24, 8 * sizeof(int32_t));
  std::memcpy(state->S_32_24, tmpmem + 328, 8 * sizeof(int32_t));
  Resample32khzTo24khz(tmpmem + 8, tmpmem, 80);
  UpBy2IntToShort(tmpmem, 240, out, state->S_24_48);
}

// 220 in -> 160 out, in 5 sub-blocks; tmpmem 104 words.
void Resample22khzTo16khz(const int16_t* in, int16_t* out, State22To16* state, int32_t* tmpmem) {
  constexpr int kSubBlocks = 5;
  for (int k = 0; k < kSubBlocks; k++) {
    UpBy2ShortToInt(in, 220 / kSubBlocks, tmpmem + 16, state->S_22_44);
    std::memcpy(tmpmem + 8, state->S_44_32, 8 * sizeof(int32_t));
    std::memcpy(state->S_44_32, tmpmem + 440 / kSubBlocks + 8, 8 * sizeof(int32_t));
    Resample44khzTo32khz(tmpmem + 8, tmpmem, 40 / kSubBlocks);
    DownBy2IntToShort(tmpmem, 320 / kSubBlocks, out, state->S_32_16);
    in += 220 / kSubBlocks;
    out += 160 / kSubBlocks;
  }
}

// 160 in -> 220 out, in 4 sub-blocks; tmpmem 88 words.
void Resample16khzTo22khz(const int16_t* in, int16_t* out, State16To22* state, int32_t* tmpmem) {
  constexpr int kSubBlocks = 4;
  for (int k = 0; k < kSubBlocks; k++) {
    UpBy2ShortToInt(in, 160 / kSubBlocks, tmpmem + 8, state->S_16_32);
    std::memcpy(tmpmem, state->S_32_22, 8 * sizeof(int32_t));
    std::memcpy(state->S_32_22, tmpmem + 320 / kSubBlocks, 8 * sizeof(int32_t));
    Resample32khzTo22khzIntToShort(tmpmem, out, 20 / kSubBlocks);
    in += 160 / kSubBlocks;
    out += 220 / kSubBlocks;
  }
}

// 220 in -> 80 out, in 2 sub-blocks; tmpmem 126 words.
void Resample22khzTo8khz(const int16_t* in, int16_t* out, State22To8* state, int32_t* tmpmem) {
  constexpr int kSubBlocks = 2;
  for (int k = 0; k < kSubBlocks; k++) {
    LPBy2ShortToInt(in, 220 / kSubBlocks, tmpmem + 16, state->S_22_22);
    std::memcpy(tmpmem + 8, state->S_22_16, 8 * sizeof(int32_t));
    std::memcpy(state->S_22_16, tmpmem + 220 / kSubBlocks + 8, 8 * sizeof(int32_t));
    Resample44khzTo32khz(tmpmem + 8, tmpmem, 20 / kSubBlocks);
    DownBy2IntToShort(tmpmem, 160 / kSubBlocks, out, state->S_16_8);
    in += 220 / kSubBlocks;
    out += 80 / kSubBlocks;
  }
}

// 80 in -> 220 out, in 2 sub-blocks; tmpmem 98 words.
void Resample8khzTo22khz(const int16_t* in, int16_t* out, State8To22* state, int32_t* tmpmem) {
  constexpr int kSubBlocks = 2;
  for (int k = 0; k < kSubBlocks; k++) {
    UpBy2ShortToInt(in, 80 / kSubBlocks, tmpmem + 18, state->S_8_16);
    std::memcpy(tmpmem + 10, state->S_16_11, 8 * sizeof(int32_t));
    std::memcpy(state->S_16_11, tmpmem + 160 / kSubBlocks + 10, 8 * sizeof(int32_t));
    Resample32khzTo22khzIntToInt(tmpmem + 10, tmpmem, 10 / kSubBlocks);
    UpBy2IntToShort(tmpmem, 110 / kSubBlocks, out, state->S_11_22);
    in += 80 / kSubBlocks;
    out += 220 / kSubBlocks;
  }
}

}  // namespace

// resample_48khz.c: 48 -> 24 -> 24 (LP) -> 16 -> 8 kHz, 480 in -> 80 out.
void SplResample48khzTo8khz(const int16_t* in, int16_t* out, SplState48khzTo8khz* state, int32_t* tmpmem) {
  DownBy2ShortToInt(in, 480, tmpmem + 256, state->S_48_24);
  LPBy2IntToInt(tmpmem + 256, 240, tmpmem + 16, state->S_24_24);
  // copy state to and from input array
  std::memcpy(tmpmem + 8, state->S_24_16, 8 * sizeof(int32_t));
  std::memcpy(state->S_24_16, tmpmem + 248, 8 * sizeof(int32_t));
  Resample48khzTo32khz(tmpmem + 8, tmpmem, 80);
  DownBy2IntToShort(tmpmem, 160, out, state->S_16_8);
}

void SplResetResample48khzTo8khz(SplState48khzTo8khz* state) { *state = SplState48khzTo8khz{}; }

SplResampler::SplResampler(int in_freq, int out_freq, size_t num_channels) {
  Reset(in_freq, out_freq, num_channels);
}

SplResampler::~SplResampler() = default;

int SplResampler::ResetIfNeeded(int in_freq, int out_freq, size_t num_channels) {
  if (in_freq / 1000 != in_khz_ || out_freq / 1000 != out_khz_ || num_channels != num_channels_)
    return Reset(in_freq, out_freq, num_channels);
  return 0;
}

void SplResampler::ClearStates() {
  std::memset(by2_, 0, sizeof(by2_));
  s48_16_ = {};
  s16_48_ = {};
  s22_16_ = {};
  s16_22_ = {};
  s22_8_ = {};
  s8_22_ = {};
}

int SplResampler::Reset(int in_freq, int out_freq, size_t num_channels) {
  if (num_channels != 1 && num_channels != 2) return -1;
  Mode mode;
  if (ComputeMode(in_freq, out_freq, &mode) != 0) return -1;
  num_channels_ = num_channels;
  mode_ = mode;
  ClearStates();
  left_.reset();
  right_.reset();
  in_khz_ = in_freq / 1000;
  out_khz_ = out_freq / 1000;
  if (num_channels_ == 2) {
    left_ = std::make_unique<SplResampler>(in_freq, out_freq, 1);
    right_ = std::make_unique<SplResampler>(in_freq, out_freq, 1);
  }
  return 0;
}

int SplResampler::ComputeMode(int in_freq_hz, int out_freq_hz, Mode* mode) {
  if (in_freq_hz <= 0 || out_freq_hz <= 0) return -1;  // libwebrtc divides by zero here.
  // Euclid's algorithm for the gcd.
  int a = in_freq_hz, b = out_freq_hz, c = a % b;
  while (c != 0) {
    a = b;
    b = c;
    c = a % b;
  }
  const int in = in_freq_hz / b;
  const int out = out_freq_hz / b;
  if (in == out) {
    *mode = kMode1To1;
  } else if (in == 1) {
    switch (out) {
      case 2: *mode = kMode1To2; break;
      case 3: *mode = kMode1To3; break;
      case 4: *mode = kMode1To4; break;
      case 6: *mode = kMode1To6; break;
      case 12: *mode = kMode1To12; break;
      default: return -1;
    }
  } else if (out == 1) {
    switch (in) {
      case 2: *mode = kMode2To1; break;
      case 3: *mode = kMode3To1; break;
      case 4: *mode = kMode4To1; break;
      case 6: *mode = kMode6To1; break;
      case 12: *mode = kMode12To1; break;
      default: return -1;
    }
  } else if (in == 2 && out == 3) {
    *mode = kMode2To3;
  } else if (in == 2 && out == 11) {
    *mode = kMode2To11;
  } else if (in == 4 && out == 11) {
    *mode = kMode4To11;
  } else if (in == 8 && out == 11) {
    *mode = kMode8To11;
  } else if (in == 3 && out == 2) {
    *mode = kMode3To2;
  } else if (in == 11 && out == 2) {
    *mode = kMode11To2;
  } else if (in == 11 && out == 4) {
    *mode = kMode11To4;
  } else if (in == 11 && out == 16) {
    *mode = kMode11To16;
  } else if (in == 11 && out == 32) {
    *mode = kMode11To32;
  } else if (in == 11 && out == 8) {
    *mode = kMode11To8;
  } else {
    return -1;
  }
  return 0;
}

int SplResampler::Push(const int16_t* samples_in, size_t length_in, int16_t* samples_out, size_t max_len,
                       size_t& out_len) {
  if (num_channels_ == 2) {
    // Split up the signal and run the helper instance for each channel.
    const size_t half = length_in / 2;
    left_in_.resize(half);
    right_in_.resize(half);
    left_out_.resize(max_len / 2);
    right_out_.resize(max_len / 2);
    for (size_t i = 0; i + 1 < length_in; i += 2) {
      left_in_[i >> 1] = samples_in[i];
      right_in_[i >> 1] = samples_in[i + 1];
    }
    size_t out_left = 0, out_right = 0;
    int res = left_->Push(left_in_.data(), half, left_out_.data(), max_len / 2, out_left);
    res |= right_->Push(right_in_.data(), half, right_out_.data(), max_len / 2, out_right);
    if (res || out_left != out_right) return -1;
    for (size_t i = 0; i < out_left; i++) {
      samples_out[i * 2] = left_out_[i];
      samples_out[i * 2 + 1] = right_out_[i];
    }
    out_len = 2 * out_left;
    return 0;
  }

  int32_t* tmp_mem = nullptr;
  auto mem = [&](size_t words) {
    tmp_mem_.resize(words);
    tmp_mem = tmp_mem_.data();
  };
  auto tmp = [&](size_t n) {
    tmp_.resize(n);
    return tmp_.data();
  };

  switch (mode_) {
    case kMode1To1:
      if (length_in > 0) std::memmove(samples_out, samples_in, length_in * sizeof(int16_t));
      out_len = length_in;
      return 0;
    case kMode1To2:
      if (max_len < length_in * 2) return -1;
      UpsampleBy2(samples_in, length_in, samples_out, by2_[0]);
      out_len = length_in * 2;
      return 0;
    case kMode1To3:
      // Only blocks of 160 samples.
      if (length_in % 160 != 0) return -1;
      if (max_len < length_in * 3) return -1;
      mem(336);
      for (size_t i = 0; i < length_in; i += 160)
        Resample16khzTo48khz(samples_in + i, samples_out + i * 3, &s16_48_, tmp_mem);
      out_len = length_in * 3;
      return 0;
    case kMode1To4: {
      if (max_len < length_in * 4) return -1;
      int16_t* t = tmp(2 * length_in);
      UpsampleBy2(samples_in, length_in, t, by2_[0]);
      UpsampleBy2(t, length_in * 2, samples_out, by2_[1]);
      out_len = length_in * 4;
      return 0;
    }
    case kMode1To6: {
      // Only blocks of 80 samples.
      if (length_in % 80 != 0) return -1;
      if (max_len < length_in * 6) return -1;
      mem(336);
      int16_t* t = tmp(2 * length_in);
      UpsampleBy2(samples_in, length_in, t, by2_[0]);
      out_len = length_in * 2;
      for (size_t i = 0; i < out_len; i += 160) Resample16khzTo48khz(t + i, samples_out + i * 3, &s16_48_, tmp_mem);
      out_len = out_len * 3;
      return 0;
    }
    case kMode1To12: {
      // Only blocks of 40 samples.
      if (length_in % 40 != 0) return -1;
      if (max_len < length_in * 12) return -1;
      mem(336);
      int16_t* t = tmp(4 * length_in);
      UpsampleBy2(samples_in, length_in, samples_out, by2_[0]);
      out_len = length_in * 2;
      UpsampleBy2(samples_out, out_len, t, by2_[1]);
      out_len = out_len * 2;
      // The data is at 32 kHz here despite the function's name; x3 is 96 kHz.
      for (size_t i = 0; i < out_len; i += 160) Resample16khzTo48khz(t + i, samples_out + i * 3, &s16_48_, tmp_mem);
      out_len = out_len * 3;
      return 0;
    }
    case kMode2To3: {
      if (max_len < length_in * 3 / 2) return -1;
      // Only blocks of 160 samples.
      if (length_in % 160 != 0) return -1;
      int16_t* t = tmp(length_in * 3);
      mem(336);
      for (size_t i = 0; i < length_in; i += 160) Resample16khzTo48khz(samples_in + i, t + i * 3, &s16_48_, tmp_mem);
      length_in = length_in * 3;
      DownsampleBy2(t, length_in, samples_out, by2_[0]);
      out_len = length_in / 2;
      return 0;
    }
    case kMode2To11: {
      // Only blocks of 80 samples.
      if (length_in % 80 != 0) return -1;
      if (max_len < (length_in * 11) / 2) return -1;
      int16_t* t = tmp(2 * length_in);
      UpsampleBy2(samples_in, length_in, t, by2_[0]);
      length_in *= 2;
      mem(98);
      for (size_t i = 0; i < length_in; i += 80)
        Resample8khzTo22khz(t + i, samples_out + (i * 11) / 4, &s8_22_, tmp_mem);
      out_len = (length_in * 11) / 4;
      return 0;
    }
    case kMode4To11:
      // Only blocks of 80 samples.
      if (length_in % 80 != 0) return -1;
      if (max_len < (length_in * 11) / 4) return -1;
      mem(98);
      for (size_t i = 0; i < length_in; i += 80)
        Resample8khzTo22khz(samples_in + i, samples_out + (i * 11) / 4, &s8_22_, tmp_mem);
      out_len = (length_in * 11) / 4;
      return 0;
    case kMode8To11:
      // Only blocks of 160 samples.
      if (length_in % 160 != 0) return -1;
      if (max_len < (length_in * 11) / 8) return -1;
      mem(88);
      for (size_t i = 0; i < length_in; i += 160)
        Resample16khzTo22khz(samples_in + i, samples_out + (i * 11) / 8, &s16_22_, tmp_mem);
      out_len = (length_in * 11) / 8;
      return 0;
    case kMode11To16: {
      // Only blocks of 110 samples.
      if (length_in % 110 != 0) return -1;
      if (max_len < (length_in * 16) / 11) return -1;
      mem(104);
      int16_t* t = tmp(length_in * 2);
      UpsampleBy2(samples_in, length_in, t, by2_[0]);
      for (size_t i = 0; i < length_in * 2; i += 220)
        Resample22khzTo16khz(t + i, samples_out + (i / 220) * 160, &s22_16_, tmp_mem);
      out_len = (length_in * 16) / 11;
      return 0;
    }
    case kMode11To32: {
      // Only blocks of 110 samples.
      if (length_in % 110 != 0) return -1;
      if (max_len < (length_in * 32) / 11) return -1;
      mem(104);
      int16_t* t = tmp(length_in * 2);
      // 11 -> 22 kHz in samples_out
      UpsampleBy2(samples_in, length_in, samples_out, by2_[0]);
      // 22 -> 16 in tmp
      for (size_t i = 0; i < length_in * 2; i += 220)
        Resample22khzTo16khz(samples_out + i, t + (i / 220) * 160, &s22_16_, tmp_mem);
      // 16 -> 32 in samples_out
      UpsampleBy2(t, (length_in * 16) / 11, samples_out, by2_[1]);
      out_len = (length_in * 32) / 11;
      return 0;
    }
    case kMode2To1:
      if (max_len < length_in / 2) return -1;
      DownsampleBy2(samples_in, length_in, samples_out, by2_[0]);
      out_len = length_in / 2;
      return 0;
    case kMode3To1:
      // Only blocks of 480 samples.
      if (length_in % 480 != 0) return -1;
      if (max_len < length_in / 3) return -1;
      mem(496);
      for (size_t i = 0; i < length_in; i += 480)
        Resample48khzTo16khz(samples_in + i, samples_out + i / 3, &s48_16_, tmp_mem);
      out_len = length_in / 3;
      return 0;
    case kMode4To1: {
      if (max_len < length_in / 4) return -1;
      int16_t* t = tmp(length_in / 2);
      DownsampleBy2(samples_in, length_in, t, by2_[0]);
      DownsampleBy2(t, length_in / 2, samples_out, by2_[1]);
      out_len = length_in / 4;
      return 0;
    }
    case kMode6To1: {
      // Only blocks of 480 samples.
      if (length_in % 480 != 0) return -1;
      if (max_len < length_in / 6) return -1;
      mem(496);
      int16_t* t = tmp(length_in / 3);
      for (size_t i = 0; i < length_in; i += 480) Resample48khzTo16khz(samples_in + i, t + i / 3, &s48_16_, tmp_mem);
      out_len = length_in / 3;
      DownsampleBy2(t, out_len, samples_out, by2_[0]);
      out_len = out_len / 2;
      return 0;
    }
    case kMode12To1: {
      // Only blocks of 480 samples.
      if (length_in % 480 != 0) return -1;
      if (max_len < length_in / 12) return -1;
      mem(496);
      int16_t* t = tmp(length_in / 3);
      tmp2_.resize(length_in / 6);
      // The data is at 96 kHz here despite the function's name; /3 is 32 kHz.
      for (size_t i = 0; i < length_in; i += 480) Resample48khzTo16khz(samples_in + i, t + i / 3, &s48_16_, tmp_mem);
      out_len = length_in / 3;
      DownsampleBy2(t, out_len, tmp2_.data(), by2_[0]);
      out_len = out_len / 2;
      DownsampleBy2(tmp2_.data(), out_len, samples_out, by2_[1]);
      out_len = out_len / 2;
      return 0;
    }
    case kMode3To2: {
      if (max_len < length_in * 2 / 3) return -1;
      int16_t* t = tmp(length_in * 2);
      UpsampleBy2(samples_in, length_in, t, by2_[0]);
      length_in *= 2;
      // Only blocks of 480 samples (after the 3:6 step, as libwebrtc).
      if (length_in % 480 != 0) return -1;
      mem(496);
      for (size_t i = 0; i < length_in; i += 480) Resample48khzTo16khz(t + i, samples_out + i / 3, &s48_16_, tmp_mem);
      out_len = length_in / 3;
      return 0;
    }
    case kMode11To2: {
      // Only blocks of 220 samples.
      if (length_in % 220 != 0) return -1;
      if (max_len < (length_in * 2) / 11) return -1;
      mem(126);
      int16_t* t = tmp((length_in * 4) / 11);
      for (size_t i = 0; i < length_in; i += 220)
        Resample22khzTo8khz(samples_in + i, t + (i * 4) / 11, &s22_8_, tmp_mem);
      length_in = (length_in * 4) / 11;
      DownsampleBy2(t, length_in, samples_out, by2_[0]);
      out_len = length_in / 2;
      return 0;
    }
    case kMode11To4:
      // Only blocks of 220 samples.
      if (length_in % 220 != 0) return -1;
      if (max_len < (length_in * 4) / 11) return -1;
      mem(126);
      for (size_t i = 0; i < length_in; i += 220)
        Resample22khzTo8khz(samples_in + i, samples_out + (i * 4) / 11, &s22_8_, tmp_mem);
      out_len = (length_in * 4) / 11;
      return 0;
    case kMode11To8:
      // Only blocks of 220 samples.
      if (length_in % 220 != 0) return -1;
      if (max_len < (length_in * 8) / 11) return -1;
      mem(104);
      for (size_t i = 0; i < length_in; i += 220)
        Resample22khzTo16khz(samples_in + i, samples_out + (i * 8) / 11, &s22_16_, tmp_mem);
      out_len = (length_in * 8) / 11;
      return 0;
  }
  return 0;
}

}  // namespace sm::audio
