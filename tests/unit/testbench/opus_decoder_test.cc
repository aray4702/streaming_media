// The Opus receive path of the test bench (issue #476): libopus 55513e81
// called as libwebrtc's AudioDecoderOpusImpl calls it, and the bench decoder
// that keeps libopus's call sequence to NetEq's.
#include <algorithm>
#include <cmath>
#include <vector>

#include "opus_decoder.h"
#include "opus_encoder.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

constexpr uint32_t kFrame = OpusSendEncoder::kFrameSamples;  // 960: 20 ms at 48 kHz.

// `n` packets of 48 kHz speech, encoded as the bench encodes it, with the
// projected loss that makes libopus code LBRR.
std::vector<std::vector<uint8_t>> Packets(size_t n, int loss_percent, uint64_t seed = 7) {
  std::vector<int16_t> pcm;
  for (float v : sim::SpeechLike(48000, static_cast<double>(n) / 50 + 1, seed, true)) pcm.push_back(OpusSendEncoder::ToPcm(v));
  OpusSendEncoder enc(32000, true);
  enc.SetPacketLossPercent(loss_percent);
  std::vector<std::vector<uint8_t>> out;
  for (size_t k = 0; k < n; ++k) out.push_back(enc.Encode(std::span<const int16_t>(pcm).subspan(k * kFrame)));
  return out;
}

// libwebrtc's calls, written out from opus_interface.cc: WebRtcOpus_DecoderCreate
// (48 kHz, 1 channel), WebRtcOpus_Decode (room for 120 ms) and
// WebRtcOpus_DecodeFec (the packet's samples per frame, decode_fec = 1).
struct LibwebrtcOpusCalls {
  OpusDecoder* dec;
  LibwebrtcOpusCalls() {
    int err = 0;
    dec = opus_decoder_create(48000, 1, &err);
  }
  ~LibwebrtcOpusCalls() { opus_decoder_destroy(dec); }
  std::vector<float> Decode(const std::vector<uint8_t>& p) {
    std::vector<int16_t> out(120 * 48);
    const int n = opus_decode(dec, p.data(), static_cast<opus_int32>(p.size()), out.data(), 120 * 48, 0);
    return ToFloat(out, n);
  }
  std::vector<float> DecodeFec(const std::vector<uint8_t>& p) {
    const int fs = opus_packet_get_samples_per_frame(p.data(), 48000);
    std::vector<int16_t> out(static_cast<size_t>(fs));
    const int n = opus_decode(dec, p.data(), static_cast<opus_int32>(p.size()), out.data(), fs, 1);
    return ToFloat(out, n);
  }
  static std::vector<float> ToFloat(const std::vector<int16_t>& v, int n) {
    std::vector<float> f;
    for (int i = 0; i < n; ++i) f.push_back(static_cast<float>(v[static_cast<size_t>(i)]) / 32767.f);
    return f;
  }
};

}  // namespace

SM_TEST(OpusDecoder, ConfiguredAsAudioDecoderOpusImpl) {
  // opus/48000/2 without stereo=1: one channel at 48 kHz (AudioDecoderOpus::SdpToConfig).
  SM_EXPECT_EQ(OpusReceiveDecoder::kSampleRateHz, 48000);
  SM_EXPECT_EQ(OpusReceiveDecoder::kChannels, 1);
  SM_EXPECT_EQ(OpusReceiveDecoder::kMaxDecodeFrameSizeMs, 120);  // kWebRtcOpusMaxDecodeFrameSizeMs.
  OpusReceiveDecoder d;
  SM_EXPECT_TRUE(d.ok());
  const auto p = Packets(1, 0);
  SM_EXPECT_EQ(d.Decode(p[0]).size(), static_cast<size_t>(kFrame));
}

// In order, every frame: the samples libwebrtc's calls give, sample for sample.
SM_TEST(OpusDecoder, InOrderSamplesAreLibwebrtcs) {
  const auto packets = Packets(100, 10);
  OpusAudioDecoder bench(48000);
  LibwebrtcOpusCalls ref;
  size_t mismatched = 0, with_fec = 0;
  for (size_t k = 0; k < packets.size(); ++k) {
    const uint32_t ts = static_cast<uint32_t>(k) * kFrame;
    // The receiver asks for the LBRR copy of the previous frame first; that
    // frame arrived, so libopus must not see it.
    if (const uint32_t d = bench.FecDuration(packets[k]); d > 0) {
      ++with_fec;
      SM_EXPECT_EQ(d, kFrame);
      if (k > 0) {
        auto fec = bench.DecodeFec(packets[k], ts - d);
        SM_EXPECT_TRUE(fec.has_value());
      }
    }
    auto got = bench.Decode(packets[k], ts);
    const auto want = ref.Decode(packets[k]);
    if (!got || *got != want) ++mismatched;
  }
  SM_RECORD("frames_with_lbrr", static_cast<double>(with_fec));
  SM_EXPECT_GT(with_fec, 10u);
  SM_EXPECT_EQ(mismatched, 0u);
}

// A lost frame with an LBRR copy in the next packet: NetEq decodes the copy
// (WebRtcOpus_DecodeFec) in its place, then the packet itself.
SM_TEST(OpusDecoder, LostFrameDecodesTheLbrrCopyAsLibwebrtc) {
  const auto packets = Packets(60, 10);
  OpusAudioDecoder bench(48000);
  LibwebrtcOpusCalls ref;
  size_t fec_decodes = 0, mismatched = 0;
  for (size_t k = 0; k < packets.size(); ++k) {
    const uint32_t ts = static_cast<uint32_t>(k) * kFrame;
    const bool lost = k % 7 == 3;
    if (lost) continue;
    const bool prev_lost = k > 0 && (k - 1) % 7 == 3;
    if (const uint32_t d = bench.FecDuration(packets[k]); d > 0 && k > 0) {
      auto fec = bench.DecodeFec(packets[k], ts - d);
      if (prev_lost) {
        ++fec_decodes;
        if (!fec || *fec != ref.DecodeFec(packets[k])) ++mismatched;
      }
    }
    auto got = bench.Decode(packets[k], ts);
    if (!got || *got != ref.Decode(packets[k])) ++mismatched;
  }
  SM_EXPECT_GT(fec_decodes, 3u);
  SM_EXPECT_EQ(mismatched, 0u);
}

// Duplicates and RED copies of frames already decoded give the same samples
// and leave libopus alone (NetEq drops them unplayed).
SM_TEST(OpusDecoder, DuplicatesDoNotTouchTheDecoder) {
  const auto packets = Packets(40, 0);
  OpusAudioDecoder bench(48000);
  LibwebrtcOpusCalls ref;
  size_t mismatched = 0;
  for (size_t k = 0; k < packets.size(); ++k) {
    const uint32_t ts = static_cast<uint32_t>(k) * kFrame;
    auto first = bench.Decode(packets[k], ts);
    if (!first || *first != ref.Decode(packets[k])) ++mismatched;
    if (k > 0) {
      // The RED copy of the frame before, then a duplicate of this one.
      auto red = bench.Decode(packets[k - 1], ts - kFrame);
      auto dup = bench.Decode(packets[k], ts);
      if (!red || !dup || *dup != *first) ++mismatched;
    }
  }
  SM_EXPECT_EQ(mismatched, 0u);
}

// A frame that arrives after a later one is decoded in timestamp order, and
// the frames after the reordering get the samples of an in-order decode.
SM_TEST(OpusDecoder, ReorderedFrameIsDecodedInTimestampOrder) {
  const auto packets = Packets(30, 0);
  std::vector<std::vector<float>> in_order;
  {
    LibwebrtcOpusCalls ref;
    for (const auto& p : packets) in_order.push_back(ref.Decode(p));
  }
  OpusAudioDecoder bench(48000);
  // Arrival order: 0..9, 11, 10, 12..29.
  std::vector<size_t> order;
  for (size_t k = 0; k < packets.size(); ++k) order.push_back(k);
  std::swap(order[10], order[11]);
  std::vector<std::vector<float>> got(packets.size());
  for (size_t k : order) {
    auto s = bench.Decode(packets[k], static_cast<uint32_t>(k) * kFrame);
    SM_EXPECT_TRUE(s.has_value());
    if (s) got[k] = *s;
  }
  size_t mismatched = 0;
  for (size_t k = 0; k < packets.size(); ++k) {
    if (k == 11) continue;  // Decoded before frame 10 arrived: NetEq would decode it after.
    if (got[k] != in_order[k]) ++mismatched;
  }
  SM_EXPECT_EQ(mismatched, 0u);
}

// A frame older than every frame remembered, once the window is full (issue
// #533): its samples are returned, it is not kept, and the frames after it
// decode as in order (no use of the frame the window trim erases).
SM_TEST(OpusDecoder, FrameOlderThanTheWindowIsDecodedNotKept) {
  const size_t window = OpusAudioDecoder::kRemembered;
  const auto packets = Packets(window + 3, 0);
  std::vector<std::vector<float>> in_order;  // Frames 1.. in order, as libwebrtc decodes them.
  {
    LibwebrtcOpusCalls ref;
    for (size_t k = 1; k < packets.size(); ++k) in_order.push_back(ref.Decode(packets[k]));
  }
  OpusAudioDecoder bench(48000);
  // Frames 1..window+1: the window holds 2..window+1.
  for (size_t k = 1; k <= window + 1; ++k) bench.Decode(packets[k], static_cast<uint32_t>(k) * kFrame);
  // Frame 0 arrives more than 5 s late, then its LBRR path for the same frame.
  auto old = bench.Decode(packets[0], 0);
  SM_EXPECT_TRUE(old.has_value());
  if (old) SM_EXPECT_EQ(old->size(), static_cast<size_t>(kFrame));
  SM_EXPECT_TRUE(bench.DecodeFec(packets[1], 0).has_value());
  // The next frame starts from the state after frame window+1, as in order.
  const size_t k = window + 2;
  auto next = bench.Decode(packets[k], static_cast<uint32_t>(k) * kFrame);
  SM_EXPECT_TRUE(next.has_value());
  if (next) SM_EXPECT_TRUE(*next == in_order[k - 1]);
}

// A primary that arrives after its frame was decoded from the LBRR copy
// replaces the copy, and the decoder goes on as if the copy had not been used.
SM_TEST(OpusDecoder, LatePrimaryReplacesTheLbrrCopy) {
  const auto packets = Packets(30, 10);
  std::vector<std::vector<float>> in_order;
  {
    LibwebrtcOpusCalls ref;
    for (const auto& p : packets) in_order.push_back(ref.Decode(p));
  }
  // Find a packet with LBRR to carry frame k - 1.
  size_t k = 0;
  for (size_t i = 5; i < packets.size() - 2 && k == 0; ++i) {
    if (OpusReceiveDecoder::FecDuration(packets[i]) > 0) k = i;
  }
  SM_EXPECT_GT(k, 0u);
  OpusAudioDecoder bench(48000);
  for (size_t i = 0; i + 1 < k; ++i) bench.Decode(packets[i], static_cast<uint32_t>(i) * kFrame);
  // Frame k arrives first; frame k - 1 comes from its LBRR copy, then arrives itself.
  SM_EXPECT_TRUE(bench.DecodeFec(packets[k], static_cast<uint32_t>(k - 1) * kFrame).has_value());
  bench.Decode(packets[k], static_cast<uint32_t>(k) * kFrame);
  auto late = bench.Decode(packets[k - 1], static_cast<uint32_t>(k - 1) * kFrame);
  SM_EXPECT_TRUE(late.has_value());
  if (late) SM_EXPECT_TRUE(*late == in_order[k - 1]);
  auto next = bench.Decode(packets[k + 1], static_cast<uint32_t>(k + 1) * kFrame);
  SM_EXPECT_TRUE(next.has_value());
  if (next) SM_EXPECT_TRUE(*next == in_order[k + 1]);
}

// At a bench rate other than 48 kHz the 48 kHz output is resampled to 20 ms
// at that rate, and the RTP units of the LBRR copy follow the rate.
SM_TEST(OpusDecoder, OtherBenchRatesAreResampled) {
  const auto packets = Packets(20, 10);
  for (int rate : {8000, 16000, 32000}) {
    OpusAudioDecoder bench(rate);
    for (size_t k = 0; k < packets.size(); ++k) {
      auto s = bench.Decode(packets[k], static_cast<uint32_t>(k * static_cast<size_t>(rate / 50)));
      SM_EXPECT_TRUE(s.has_value());
      if (s) SM_EXPECT_EQ(s->size(), static_cast<size_t>(rate / 50));
      const uint32_t d = bench.FecDuration(packets[k]);
      SM_EXPECT_TRUE(d == 0 || d == static_cast<uint32_t>(rate / 50));
    }
  }
}
