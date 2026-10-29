// Unit tests: libwebrtc M157's fixed-point `Resampler` (SplResampler) and
// `PushResampler<T>` (sm/audio/spl_resampler.h, sm/audio/push_resampler.h,
// #565 F6.11).
//
// `SplResampler, *Reset*`, `Mono`, `Stereo` and `MultipleResets` port
// common_audio/resampler/resampler_unittest.cc, and
// `PushResampler, VerifiesInputParameters` push_resampler_unittest.cc.
//
// The golden hashes (`SplResampler, BitExactWithLibwebrtc`) come from
// libwebrtc itself: compare/webrtc/reference/common_audio/resampler/
// resampler.cc and the signal processing library's resample*.c, compiled
// unchanged, driven with Signal() below for 100 blocks of 10 ms per rate pair
// and channel count; FNV-1a-style hash (h ^= uint16(sample); h *= prime) of
// every output sample. 8 <-> 11 kHz is left out: 10 ms (80 or 110 samples)
// is not a whole block of those modes, so both refuse it. The resampler is integer only, so the hashes hold on
// every platform.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "sm/audio/push_resampler.h"
#include "sm/audio/sinc_resampler.h"
#include "sm/audio/spl_resampler.h"
#include "sm_test.h"

using namespace sm::audio;

namespace {

// Rates we must support (resampler_unittest.cc).
constexpr int kMaxRate = 96000;
constexpr int kRates[] = {8000, 16000, 32000, 44000, 48000, kMaxRate};
constexpr int kMaxChannels = 2;
constexpr size_t kDataSize = static_cast<size_t>(kMaxChannels * kMaxRate / 100);

bool ValidRates(int in_rate, int out_rate) {
  return !((in_rate == 44000 && (out_rate == 48000 || out_rate == 96000)) ||
           (out_rate == 44000 && (in_rate == 48000 || in_rate == 96000)));
}

// Integer only: a triangle wave, a square wave and LCG noise, with full-scale
// spikes so the saturation paths run.
std::vector<int16_t> Signal(int rate, size_t n, size_t ch) {
  std::vector<int16_t> x(n * ch);
  uint32_t s = 12345;
  for (size_t i = 0; i < n; ++i) {
    for (size_t c = 0; c < ch; ++c) {
      s = s * 1664525u + 1013904223u;
      int64_t p = static_cast<int64_t>((i * 440 + c * 97) % static_cast<size_t>(rate)) * 4 * 18000 / rate;
      int64_t tri = p < 36000 ? p - 18000 : 54000 - p;
      int64_t sq = ((i * 3100 / static_cast<size_t>(rate)) & 1) ? 6000 : -6000;
      int64_t v = tri + sq + (static_cast<int64_t>(s >> 16) - 32768) / 16;
      if (i % 997 < 3) v = (i & 1) ? 32767 : -32768;
      x[i * ch + c] = static_cast<int16_t>(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
  }
  return x;
}

struct Golden {
  int in, out;
  size_t channels, samples;
  uint64_t hash;
};
// From libwebrtc's resampler.cc (see the file comment).
constexpr Golden kGolden[] = {
    {8000, 8000, 1, 8000u, 0x237610987ab762a8ull},
    {8000, 8000, 2, 16000u, 0x460169c504bf9de1ull},
    {8000, 16000, 1, 16000u, 0x56b315e75c5ebbe0ull},
    {8000, 16000, 2, 32000u, 0xd322964ba9db7c82ull},
    {8000, 22000, 1, 22000u, 0x9fd9dd2842c5bec1ull},
    {8000, 22000, 2, 44000u, 0xcc622d54334e98deull},
    {8000, 32000, 1, 32000u, 0x7e92e670c4a39789ull},
    {8000, 32000, 2, 64000u, 0x89a40937b4a43bddull},
    {8000, 44000, 1, 44000u, 0x7a6f1f28f87538adull},
    {8000, 44000, 2, 88000u, 0x84366f81ce268e5full},
    {8000, 48000, 1, 48000u, 0x50c9e29b15dd673full},
    {8000, 48000, 2, 96000u, 0xc24cb4b18ca5e4c1ull},
    {8000, 96000, 1, 96000u, 0x455a8df536d25aa4ull},
    {8000, 96000, 2, 192000u, 0x1d40e25a4c476dffull},
    {11000, 11000, 1, 11000u, 0x0727a88a52c0ca09ull},
    {11000, 11000, 2, 22000u, 0x739c4f9e3c4cef5full},
    {11000, 16000, 1, 16000u, 0xb0d9d8b79f47a207ull},
    {11000, 16000, 2, 32000u, 0x9c603e908ec65fd2ull},
    {11000, 22000, 1, 22000u, 0x06de777ee024d07eull},
    {11000, 22000, 2, 44000u, 0x06b2edf54f3e7424ull},
    {11000, 32000, 1, 32000u, 0x8bf06bca5d52ce5bull},
    {11000, 32000, 2, 64000u, 0x34cb9f001583f12eull},
    {11000, 44000, 1, 44000u, 0x11a7b6964a1fe9f0ull},
    {11000, 44000, 2, 88000u, 0x84b742a076bfe3d0ull},
    {16000, 8000, 1, 8000u, 0x512d950429059095ull},
    {16000, 8000, 2, 16000u, 0xe0a7fbc89642f9ceull},
    {16000, 16000, 1, 16000u, 0xfe7fcf648f10753cull},
    {16000, 16000, 2, 32000u, 0xa7b3af4927065df7ull},
    {16000, 22000, 1, 22000u, 0x862c00d6876a47b1ull},
    {16000, 22000, 2, 44000u, 0xc5a830f21465b7e0ull},
    {16000, 32000, 1, 32000u, 0x2711a5b7c4bd6d88ull},
    {16000, 32000, 2, 64000u, 0xdd37f4c51c05a936ull},
    {16000, 44000, 1, 44000u, 0xbdf58605e8e4d605ull},
    {16000, 44000, 2, 88000u, 0x5bb53d521ceeaa18ull},
    {16000, 48000, 1, 48000u, 0x946b71cb48e6a677ull},
    {16000, 48000, 2, 96000u, 0x4fc5f9f475484860ull},
    {16000, 96000, 1, 96000u, 0x6f04c75461ab69efull},
    {16000, 96000, 2, 192000u, 0x2527b3248405cf00ull},
    {22000, 8000, 1, 8000u, 0x4f52689f97c11dd9ull},
    {22000, 8000, 2, 16000u, 0xfe8b55b1136aa292ull},
    {22000, 11000, 1, 11000u, 0x013fb9102c977fb5ull},
    {22000, 11000, 2, 22000u, 0x0c1fd55eeebe71dfull},
    {22000, 16000, 1, 16000u, 0x30e5120dac3d39baull},
    {22000, 16000, 2, 32000u, 0xe017db8fd45bd5b0ull},
    {22000, 22000, 1, 22000u, 0xfba4b64929e7e4a1ull},
    {22000, 22000, 2, 44000u, 0x92ecb7eb13ab955dull},
    {22000, 32000, 1, 32000u, 0x11f14a6529910a6eull},
    {22000, 32000, 2, 64000u, 0x2ab9f2731cb77fcdull},
    {22000, 44000, 1, 44000u, 0xd318fcc36a3f165full},
    {22000, 44000, 2, 88000u, 0xd82bfeb3fc12bf8full},
    {32000, 8000, 1, 8000u, 0xc7afe30447fb3dadull},
    {32000, 8000, 2, 16000u, 0x5eb5a48975bc73d9ull},
    {32000, 16000, 1, 16000u, 0x49293404caebb7c9ull},
    {32000, 16000, 2, 32000u, 0x103159dd953e2f12ull},
    {32000, 32000, 1, 32000u, 0xabc1da5be52dd723ull},
    {32000, 32000, 2, 64000u, 0x743b1a823c4d9f97ull},
    {32000, 44000, 1, 44000u, 0x48e12d297a1af7eaull},
    {32000, 44000, 2, 88000u, 0x7caf95cf28e10494ull},
    {32000, 48000, 1, 48000u, 0x9df4fea17c9654a7ull},
    {32000, 48000, 2, 96000u, 0xfd3d796b9c71d69eull},
    {32000, 96000, 1, 96000u, 0x20df97e8261205bbull},
    {32000, 96000, 2, 192000u, 0xda8a28ce2ae38e14ull},
    {44000, 8000, 1, 8000u, 0xca79f944f21cabf5ull},
    {44000, 8000, 2, 16000u, 0x1a23748beaaa8efdull},
    {44000, 11000, 1, 11000u, 0x2e221d6599a47748ull},
    {44000, 11000, 2, 22000u, 0x597eda72c8333b22ull},
    {44000, 16000, 1, 16000u, 0x13bc1628523a8c58ull},
    {44000, 16000, 2, 32000u, 0x6ad2144b02402d7cull},
    {44000, 22000, 1, 22000u, 0x254da06f60fe2ceeull},
    {44000, 22000, 2, 44000u, 0x062d87dbffe35f98ull},
    {44000, 32000, 1, 32000u, 0xce7b3d687b496445ull},
    {44000, 32000, 2, 64000u, 0x1ef334242c536849ull},
    {44000, 44000, 1, 44000u, 0x2beca9f1ffc06f0aull},
    {44000, 44000, 2, 88000u, 0x929304b10114a7c6ull},
    {48000, 8000, 1, 8000u, 0x0199abe328a0ac7full},
    {48000, 8000, 2, 16000u, 0x316cbe692ec647caull},
    {48000, 16000, 1, 16000u, 0x1c26437b3b0537d0ull},
    {48000, 16000, 2, 32000u, 0xcc1a0c714edafb19ull},
    {48000, 32000, 1, 32000u, 0xbe86d71045fd8892ull},
    {48000, 32000, 2, 64000u, 0x840f90527a6390b9ull},
    {48000, 48000, 1, 48000u, 0x7435155a9d0b03c6ull},
    {48000, 48000, 2, 96000u, 0xce86c6381263e508ull},
    {48000, 96000, 1, 96000u, 0xde6dfb9527bce521ull},
    {48000, 96000, 2, 192000u, 0x92b44fd8ede865beull},
    {96000, 8000, 1, 8000u, 0xbdca68944bc8be71ull},
    {96000, 8000, 2, 16000u, 0x55fa56f53773a6f7ull},
    {96000, 16000, 1, 16000u, 0x27a961e758084d79ull},
    {96000, 16000, 2, 32000u, 0x6c80ba09c3baa918ull},
    {96000, 32000, 1, 32000u, 0x36e6fefb87349921ull},
    {96000, 32000, 2, 64000u, 0x986834133825a5e3ull},
    {96000, 48000, 1, 48000u, 0x182acd49caaef22bull},
    {96000, 48000, 2, 96000u, 0x0e1ed6fc60e1ea18ull},
    {96000, 96000, 1, 96000u, 0x69ecd9d736f69b1aull},
    {96000, 96000, 2, 192000u, 0xef2a297538602604ull},
};

}  // namespace

SM_TEST(SplResampler, Reset) {
  SplResampler rs;
  for (int in : kRates) {
    for (int out : kRates) {
      for (size_t ch : {1u, 2u}) SM_EXPECT_EQ(rs.Reset(in, out, ch), ValidRates(in, out) ? 0 : -1);
    }
  }
  // Only mono and stereo, and positive rates.
  SM_EXPECT_EQ(rs.Reset(16000, 16000, 0), -1);
  SM_EXPECT_EQ(rs.Reset(16000, 16000, 3), -1);
  SM_EXPECT_EQ(rs.Reset(0, 16000, 1), -1);
}

SM_TEST(SplResampler, Mono) {
  std::vector<int16_t> in(kDataSize, 0x0101), out(kDataSize);
  SplResampler rs;
  for (int in_rate : kRates) {
    for (int out_rate : kRates) {
      if (!ValidRates(in_rate, out_rate)) {
        SM_EXPECT_EQ(rs.Reset(in_rate, out_rate, 1), -1);
        continue;
      }
      size_t out_length = 0;
      SM_EXPECT_EQ(rs.Reset(in_rate, out_rate, 1), 0);
      SM_EXPECT_EQ(rs.Push(in.data(), static_cast<size_t>(in_rate / 100), out.data(), kDataSize, out_length), 0);
      SM_EXPECT_EQ(out_length, static_cast<size_t>(out_rate / 100));
    }
  }
}

SM_TEST(SplResampler, Stereo) {
  std::vector<int16_t> in(kDataSize, 0x0101), out(kDataSize);
  SplResampler rs;
  for (int in_rate : kRates) {
    for (int out_rate : kRates) {
      if (!ValidRates(in_rate, out_rate)) {
        SM_EXPECT_EQ(rs.Reset(in_rate, out_rate, 2), -1);
        continue;
      }
      size_t out_length = 0;
      SM_EXPECT_EQ(rs.Reset(in_rate, out_rate, 2), 0);
      SM_EXPECT_EQ(rs.Push(in.data(), static_cast<size_t>(2 * in_rate / 100), out.data(), kDataSize, out_length), 0);
      SM_EXPECT_EQ(out_length, static_cast<size_t>(2 * out_rate / 100));
    }
  }
}

// Multiple resets between a few supported and unsupported rates.
SM_TEST(SplResampler, MultipleResets) {
  constexpr int kInRates[] = {8000, 44000, 44000, 32000, 32000};
  constexpr int kOutRates[] = {16000, 48000, 48000, 16000, 16000};
  constexpr size_t kNumChannels[] = {2, 2, 2, 2, 1};
  std::vector<int16_t> in(kDataSize, 0x0101), out(kDataSize);
  SplResampler rs;
  for (size_t i = 0; i < 5; ++i) {
    if (!ValidRates(kInRates[i], kOutRates[i])) {
      SM_EXPECT_EQ(rs.ResetIfNeeded(kInRates[i], kOutRates[i], kNumChannels[i]), -1);
      continue;
    }
    size_t out_length = 0;
    SM_EXPECT_EQ(rs.ResetIfNeeded(kInRates[i], kOutRates[i], kNumChannels[i]), 0);
    SM_EXPECT_EQ(rs.Push(in.data(), static_cast<size_t>(kInRates[i] / 100), out.data(), kDataSize, out_length), 0);
    SM_EXPECT_EQ(out_length, static_cast<size_t>(kOutRates[i] / 100));
  }
}

// Modes that take whole blocks refuse others, and a short output buffer is
// refused, as libwebrtc.
SM_TEST(SplResampler, RefusesPartialBlocksAndShortOutput) {
  std::vector<int16_t> in(960, 0), out(2000);
  size_t out_length = 0;
  SplResampler up(16000, 48000, 1);  // 1:3, blocks of 160.
  SM_EXPECT_EQ(up.Push(in.data(), 100, out.data(), out.size(), out_length), -1);
  SM_EXPECT_EQ(up.Push(in.data(), 160, out.data(), 479, out_length), -1);
  SM_EXPECT_EQ(up.Push(in.data(), 320, out.data(), out.size(), out_length), 0);
  SM_EXPECT_EQ(out_length, 960u);
  SplResampler down(48000, 16000, 1);  // 3:1, blocks of 480.
  SM_EXPECT_EQ(down.Push(in.data(), 160, out.data(), out.size(), out_length), -1);
  SplResampler odd(8000, 11000, 1);  // 8:11, blocks of 160: 10 ms is 80.
  SM_EXPECT_EQ(odd.Push(in.data(), 80, out.data(), out.size(), out_length), -1);
}

SM_TEST(SplResampler, BitExactWithLibwebrtc) {
  for (const Golden& g : kGolden) {
    SplResampler rs;
    SM_EXPECT_EQ(rs.Reset(g.in, g.out, g.channels), 0);
    const size_t blk = static_cast<size_t>(g.in / 100);
    const std::vector<int16_t> x = Signal(g.in, blk * 100, g.channels);
    std::vector<int16_t> out(static_cast<size_t>(g.out / 100) * g.channels + 64);
    uint64_t h = 1469598103934665603ull;
    size_t total = 0;
    for (size_t k = 0; k < 100; ++k) {
      size_t len = 0;
      SM_EXPECT_EQ(rs.Push(&x[k * blk * g.channels], blk * g.channels, out.data(), out.size(), len), 0);
      for (size_t i = 0; i < len; ++i) h = (h ^ static_cast<uint16_t>(out[i])) * 1099511628211ull;
      total += len;
    }
    SM_EXPECT_EQ(total, g.samples);
    SM_EXPECT_EQ(h, g.hash);
  }
}

SM_TEST(PushResampler, VerifiesInputParameters) {
  PushResampler<int16_t> resampler1(160, 160, 1);
  PushResampler<int16_t> resampler2(160, 160, 2);
  PushResampler<int16_t> resampler3(160, 160, 8);
  PushResampler<float> resampler4(480, 160, 8);
}

// Matching rates copy, as libwebrtc's.
SM_TEST(PushResampler, MatchingRatesCopy) {
  PushResampler<float> rs;
  std::vector<float> in(320), out(320);
  for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(i) - 100.5f;
  rs.Resample(std::span<const float>(in), std::span<float>(out), 2);
  SM_EXPECT_TRUE(in == out);
}

// Interleaved channels are resampled each by its own PushSincResampler.
SM_TEST(PushResampler, InterleavedChannelsMatchMonoResamplers) {
  constexpr size_t kChannels = 3, kSrc = 480, kDst = 160;
  PushResampler<float> rs(kSrc, kDst, kChannels);
  std::vector<std::unique_ptr<PushSincResampler>> mono;
  for (size_t c = 0; c < kChannels; ++c) mono.push_back(std::make_unique<PushSincResampler>(kSrc, kDst));
  std::vector<float> in(kSrc * kChannels), out(kDst * kChannels), ch_in(kSrc), ch_out(kDst);
  bool same = true;
  for (int block = 0; block < 20; ++block) {
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(((i * 7919 + static_cast<size_t>(block) * 31) % 20000)) - 10000.f;
    rs.Resample(std::span<const float>(in), std::span<float>(out), kChannels);
    for (size_t c = 0; c < kChannels; ++c) {
      for (size_t j = 0; j < kSrc; ++j) ch_in[j] = in[j * kChannels + c];
      mono[c]->Resample(std::span<const float>(ch_in), std::span<float>(ch_out));
      for (size_t j = 0; j < kDst; ++j) same = same && ch_out[j] == out[j * kChannels + c];
    }
  }
  SM_EXPECT_TRUE(same);
}

// The int16 path reads int16 as FloatS16 and rounds back with saturation
// (PushSincResampler's int16 overload).
SM_TEST(PushResampler, Int16MatchesFloatRounded) {
  PushResampler<int16_t> r16;
  PushResampler<float> rf;
  std::vector<int16_t> in16(160), out16(480);
  std::vector<float> inf(160), outf(480);
  bool same = true;
  for (int block = 0; block < 20; ++block) {
    for (size_t i = 0; i < in16.size(); ++i) {
      in16[i] = static_cast<int16_t>((i % 2 ? 32767 : -32768) * (block % 3 == 0) + (block % 3 != 0) * static_cast<int>(i * 200 % 30000));
      inf[i] = in16[i];
    }
    r16.Resample(std::span<const int16_t>(in16), std::span<int16_t>(out16));
    rf.Resample(std::span<const float>(inf), std::span<float>(outf));
    for (size_t i = 0; i < outf.size(); ++i) {
      float v = std::max(std::min(outf[i], 32767.f), -32768.f);
      same = same && out16[i] == static_cast<int16_t>(v + std::copysign(0.5f, v));
    }
  }
  SM_EXPECT_TRUE(same);
}
