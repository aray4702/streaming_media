// VideoToolbox H.264 encoder and decoder on macOS (#1104): encode a synthetic
// pattern and decode it (PSNR), bitrate tracking at three targets, a keyframe
// on request, the H.264 RTP packetizer round trip with the encoder's output,
// and a decode of what the receive path rebuilds (libwebrtc-style Annex B,
// with the SPS VUI rewritten), including a resolution change.
#include "sm/video/videotoolbox_h264.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/rtp_packet.h"
#include "sm/rtp/h264.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::video;
using Bytes = std::vector<uint8_t>;

namespace {

constexpr int kFps = 30;

// The test pattern plus deterministic sensor-like noise, so that the encoder
// has camera-like detail to spend its rate on.
I420Frame MakeFrame(int w, int h, int t) {
  I420Frame f = MakeTestFrame(w, h, t);
  uint32_t s = 0x9e3779b9u * static_cast<uint32_t>(t + 1);
  for (uint8_t& p : f.y) {
    s = s * 1664525u + 1013904223u;
    const int v = p + static_cast<int>((s >> 24) % 9) - 4;
    p = static_cast<uint8_t>(std::clamp(v, 0, 255));
  }
  return f;
}

Timestamp FrameTime(int i) { return Timestamp::Millis(10'000 + i * 1000 / kFps); }
uint32_t RtpTime(int i) { return static_cast<uint32_t>(i * 90'000 / kFps); }

std::vector<uint8_t> NaluTypes(const Bytes& annexb) {
  std::vector<uint8_t> types;
  for (auto u : rtp::SplitAnnexB(annexb)) {
    if (!u.empty()) types.push_back(u[0] & 0x1f);
  }
  return types;
}

std::vector<RtpPacket> AsPackets(const std::vector<Bytes>& payloads) {
  std::vector<RtpPacket> out;
  uint16_t seq = 100;
  for (const Bytes& p : payloads) {
    RtpPacket r;
    r.seq = seq++;
    r.payload = p;
    out.push_back(std::move(r));
  }
  return out;
}

bool Start(VideoToolboxH264Encoder& enc, int w, int h, int64_t kbps) {
  if (enc.InitEncode({w, h}) != EncoderStatus::kOk) return false;
  enc.SetRates({DataRate::KilobitsPerSec(kbps), kFps});
  return true;
}

}  // namespace

SM_TEST(VideoToolboxH264, EncodeDecodePsnr) {
  VideoToolboxH264Encoder enc;
  SM_ASSERT_TRUE(Start(enc, 640, 480, 1000));
  VideoToolboxH264Decoder dec;
  SM_ASSERT_TRUE(dec.Configure({DecoderCodecType::kH264}));
  double sum = 0, worst = 1e9;
  int decoded = 0;
  for (int i = 0; i < 60; ++i) {
    const I420Frame in = MakeFrame(640, 480, i);
    std::optional<H264EncodedFrame> out;
    SM_ASSERT_TRUE(enc.Encode(in, RtpTime(i), false, FrameTime(i), out) == EncoderStatus::kOk);
    SM_ASSERT_TRUE(out.has_value());
    if (i == 0) SM_EXPECT_TRUE(out->keyframe);
    I420Frame pic;
    SM_ASSERT_TRUE(dec.Decode(out->annexb, FrameTime(i), pic) == DecoderStatus::kOk);
    SM_ASSERT_EQ(pic.width, 640);
    SM_ASSERT_EQ(pic.height, 480);
    const double psnr = PsnrY(in, pic);
    sum += psnr;
    worst = std::min(worst, psnr);
    ++decoded;
  }
  SM_EXPECT_EQ(decoded, 60);
  SM_RECORD("vt_h264.psnr_mean_db", sum / decoded);
  SM_RECORD("vt_h264.psnr_min_db", worst);
  SM_EXPECT_GE(sum / decoded, 32.0);
  SM_EXPECT_GE(worst, 28.0);
  SM_EXPECT_EQ(dec.stats().format_changes, 1u);
}

SM_TEST(VideoToolboxH264, BitrateTracksTarget) {
  for (int64_t kbps : {300, 800, 1500}) {
    VideoToolboxH264Encoder enc;
    SM_ASSERT_TRUE(Start(enc, 640, 480, kbps));
    uint64_t bytes = 0;
    const int frames = 10 * kFps;
    for (int i = 0; i < frames; ++i) {
      std::optional<H264EncodedFrame> out;
      SM_ASSERT_TRUE(enc.Encode(MakeFrame(640, 480, i), RtpTime(i), false, FrameTime(i), out) == EncoderStatus::kOk);
      if (out) bytes += out->annexb.size();
    }
    const double achieved_kbps = static_cast<double>(bytes) * 8 / 10 / 1000;
    std::printf("[metric] vt_h264.bitrate_target_%lld_kbps=%.1f (adjusted %.0f kbps, dropped %llu)\n",
                static_cast<long long>(kbps), achieved_kbps, enc.encoder_bitrate().kbps(),
                static_cast<unsigned long long>(enc.stats().dropped));
    SM_EXPECT_GE(achieved_kbps, 0.85 * static_cast<double>(kbps));
    SM_EXPECT_LE(achieved_kbps, 1.15 * static_cast<double>(kbps));
  }
}

SM_TEST(VideoToolboxH264, KeyframeOnRequest) {
  VideoToolboxH264Encoder enc;
  SM_ASSERT_TRUE(Start(enc, 640, 480, 800));
  for (int i = 0; i < 40; ++i) {
    const bool request = i == 20;
    std::optional<H264EncodedFrame> out;
    SM_ASSERT_TRUE(enc.Encode(MakeFrame(640, 480, i), RtpTime(i), request, FrameTime(i), out) ==
                   EncoderStatus::kOk);
    SM_ASSERT_TRUE(out.has_value());
    const std::vector<uint8_t> types = NaluTypes(out->annexb);
    SM_ASSERT_TRUE(!types.empty());
    if (i == 0 || i == 20) {
      SM_EXPECT_TRUE(out->keyframe);
      // SPS and PPS first, then the IDR slice(s); VideoToolbox puts an SEI before the first slice.
      SM_ASSERT_GE(types.size(), 3u);
      SM_EXPECT_EQ(types[0], 7);
      SM_EXPECT_EQ(types[1], 8);
      size_t k = 2;
      while (k < types.size() && types[k] == 6) ++k;
      SM_ASSERT_TRUE(k < types.size());
      SM_EXPECT_EQ(types[k], 5);
      // kVTProfileLevel_H264_Baseline_3_1, as RTCVideoEncoderH264 maps
      // profile-level-id 42e01f: profile_idc 66, level_idc 31.
      const auto sps = rtp::SplitAnnexB(out->annexb)[0];
      SM_ASSERT_GE(sps.size(), 4u);
      SM_EXPECT_EQ(sps[1], 66);
      SM_EXPECT_EQ(sps[3], 31);
    } else {
      SM_EXPECT_FALSE(out->keyframe);
      for (uint8_t t : types) SM_EXPECT_TRUE(t != 5 && t != 7 && t != 8);
    }
    // Annex B with 4-byte start codes.
    SM_EXPECT_TRUE(out->annexb.size() > 4 && out->annexb[0] == 0 && out->annexb[1] == 0 && out->annexb[2] == 0 &&
                   out->annexb[3] == 1);
  }
  SM_EXPECT_EQ(enc.stats().keyframes, 2u);
}

SM_TEST(VideoToolboxH264, RtpRoundTripAndDecode) {
  VideoToolboxH264Encoder enc;
  SM_ASSERT_TRUE(Start(enc, 640, 480, 1000));
  VideoToolboxH264Decoder dec;
  SM_ASSERT_TRUE(dec.Configure({DecoderCodecType::kH264}));
  rtp::PayloadSizeLimits limits;
  int packets = 0;
  // Ends with a switch to 320x240: the encoder restarts and the decoder takes
  // the new SPS/PPS.
  for (int i = 0; i < 45; ++i) {
    const int w = i < 30 ? 640 : 320, h = i < 30 ? 480 : 240;
    const I420Frame in = MakeFrame(w, h, i);
    std::optional<H264EncodedFrame> out;
    SM_ASSERT_TRUE(enc.Encode(in, RtpTime(i), false, FrameTime(i), out) == EncoderStatus::kOk);
    SM_ASSERT_TRUE(out.has_value());
    const auto payloads = rtp::H264Packetizer::Packetize(out->annexb, limits);
    SM_ASSERT_TRUE(!payloads.empty());
    for (const Bytes& p : payloads) SM_EXPECT_LE(p.size(), static_cast<size_t>(limits.max_payload_len));
    packets += static_cast<int>(payloads.size());
    Bytes rebuilt;
    SM_ASSERT_TRUE(rtp::H264Depacketizer::Reassemble(AsPackets(payloads), rebuilt));
    // Every unit comes back; only the SPS may be rewritten (VUI, as libwebrtc's receiver does).
    const auto sent = rtp::SplitAnnexB(out->annexb);
    const auto got = rtp::SplitAnnexB(rebuilt);
    SM_ASSERT_EQ(sent.size(), got.size());
    for (size_t k = 0; k < sent.size(); ++k) {
      if ((sent[k][0] & 0x1f) == 7) continue;
      SM_EXPECT_TRUE(Bytes(sent[k].begin(), sent[k].end()) == Bytes(got[k].begin(), got[k].end()));
    }
    I420Frame pic;
    SM_ASSERT_TRUE(dec.Decode(rebuilt, FrameTime(i), pic) == DecoderStatus::kOk);
    SM_ASSERT_EQ(pic.width, w);
    SM_ASSERT_EQ(pic.height, h);
    SM_EXPECT_GE(PsnrY(in, pic), 28.0);
  }
  SM_EXPECT_GT(packets, 45);
  SM_EXPECT_EQ(enc.stats().session_restarts, 1u);
  SM_EXPECT_EQ(dec.stats().format_changes, 2u);
}

SM_TEST(VideoToolboxH264, DecoderNeedsParameterSets) {
  VideoToolboxH264Encoder enc;
  SM_ASSERT_TRUE(Start(enc, 320, 240, 500));
  std::vector<H264EncodedFrame> frames;
  for (int i = 0; i < 3; ++i) {
    std::optional<H264EncodedFrame> out;
    SM_ASSERT_TRUE(enc.Encode(MakeFrame(320, 240, i), RtpTime(i), false, FrameTime(i), out) == EncoderStatus::kOk);
    SM_ASSERT_TRUE(out.has_value());
    frames.push_back(*out);
  }
  VideoToolboxH264Decoder dec;
  I420Frame pic;
  SM_EXPECT_TRUE(dec.Decode(frames[0].annexb, FrameTime(0), pic) == DecoderStatus::kUninitialized);
  SM_ASSERT_TRUE(dec.Configure({DecoderCodecType::kH264}));
  // A delta frame before any SPS/PPS can't be decoded: the caller asks for a keyframe.
  SM_EXPECT_TRUE(dec.Decode(frames[1].annexb, FrameTime(1), pic) == DecoderStatus::kError);
  SM_EXPECT_TRUE(dec.Decode(frames[0].annexb, FrameTime(0), pic) == DecoderStatus::kOk);
  SM_EXPECT_TRUE(dec.Decode(frames[1].annexb, FrameTime(1), pic) == DecoderStatus::kOk);
  SM_EXPECT_TRUE(dec.Decode(frames[2].annexb, FrameTime(2), pic) == DecoderStatus::kOk);
  SM_EXPECT_EQ(dec.GetDecoderInfo().implementation_name, std::string("VideoToolbox"));
}
