#include <vector>

#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/sps_vui_rewriter.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {
// The fake encoder's SPS (Constrained Baseline 320x192, no VUI) after
// SpsVuiRewriter's outgoing rewrite, worked out from sps_vui_rewriter.cc:
// vui_parameters_present_flag 1, eight zero flags, bitstream_restriction_flag
// 1, then AddBitstreamRestriction (1, ue 2, ue 1, ue 16, ue 16, ue 0,
// ue max_num_ref_frames = 1) and the original rbsp_trailing_bits. The
// compare-webrtc job checks the same bytes against libwebrtc's rewriter.
const Bytes kRewrittenFakeSps = {0x67, 0x42, 0xE0, 0x1F, 0xDA, 0x05, 0x06, 0x68, 0x06, 0xD0, 0x44, 0x23, 0x50};

Bytes Sps() {
  Bytes s = {0x67};
  s.insert(s.end(), kFakeH264Sps, kFakeH264Sps + sizeof(kFakeH264Sps));
  return s;
}

Bytes AnnexB(std::initializer_list<Bytes> nals) {
  Bytes out;
  for (const Bytes& n : nals) {
    out.insert(out.end(), {0, 0, 0, 1});
    out.insert(out.end(), n.begin(), n.end());
  }
  return out;
}
}  // namespace

SM_TEST(SpsVuiRewriter, ParsesTheFakeEncodersSps) {
  const Bytes sps = Sps();
  const auto s = ParseSpsUpToVui(ParseRbsp(std::span<const uint8_t>(sps).subspan(1)));
  SM_ASSERT_TRUE(s.has_value());
  SM_EXPECT_EQ(s->profile_idc, 66u);
  SM_EXPECT_EQ(s->level_idc, 31u);
  SM_EXPECT_EQ(s->width, 320u);
  SM_EXPECT_EQ(s->height, 192u);
  SM_EXPECT_EQ(s->pic_order_cnt_type, 2u);
  SM_EXPECT_EQ(s->max_num_ref_frames, 1u);
  SM_EXPECT_FALSE(s->vui_params_present);
}

SM_TEST(SpsVuiRewriter, AddsTheVuiLibwebrtcAdds) {
  const Bytes sps = Sps();
  Bytes out = {sps[0]};
  SM_EXPECT_TRUE(ParseAndRewriteSps(std::span<const uint8_t>(sps).subspan(1), out) == SpsRewriteResult::kVuiRewritten);
  SM_EXPECT_TRUE(out == kRewrittenFakeSps);
  SM_EXPECT_EQ(out.size(), sps.size() + 5);  // 8 -> 13 bytes, as on libwebrtc's wire (#342).

  // The rewritten SPS keeps its fields, now with a VUI that needs no change.
  const auto s = ParseSpsUpToVui(ParseRbsp(std::span<const uint8_t>(out).subspan(1)));
  SM_ASSERT_TRUE(s.has_value());
  SM_EXPECT_TRUE(s->vui_params_present);
  SM_EXPECT_EQ(s->width, 320u);
  Bytes again = {out[0]};
  SM_EXPECT_TRUE(ParseAndRewriteSps(std::span<const uint8_t>(out).subspan(1), again) == SpsRewriteResult::kVuiOk);
  SM_EXPECT_EQ(again.size(), 1u);  // Unchanged destination.
}

SM_TEST(SpsVuiRewriter, RewritesKeyframesAndDropsAccessUnitDelimiters) {
  const Bytes pps = {0x68, 0xce, 0x3c, 0x61};
  const Bytes idr = {0x65, 0xAA, 0x11};
  const Bytes in = AnnexB({{0x09, 0xF0}, Sps(), pps, idr});
  SpsRewriteStats stats;
  const Bytes out = ParseOutgoingBitstreamAndRewrite(in, &stats);
  SM_EXPECT_TRUE(out == AnnexB({kRewrittenFakeSps, pps, idr}));
  SM_EXPECT_EQ(stats.rewritten, 1u);
  SM_EXPECT_EQ(stats.vui_ok, 0u);

  // Three-byte start codes are kept as they are.
  Bytes three = {0, 0, 1};
  three.insert(three.end(), idr.begin(), idr.end());
  SM_EXPECT_TRUE(ParseOutgoingBitstreamAndRewrite(three) == three);
  // Nothing before the first start code is a NAL unit (libwebrtc's FindNaluIndices).
  SM_EXPECT_TRUE(ParseOutgoingBitstreamAndRewrite(Bytes{0x65, 1, 2}).empty());
}

SM_TEST(SpsVuiRewriter, LeavesAnUnparsableSpsAsItIs) {
  // An SPS cut short: the parser fails, the unit is copied, the failure counted.
  const Bytes cut = {0x67, 0x42, 0xE0};
  const Bytes in = AnnexB({cut, {0x65, 0xAA}});
  SpsRewriteStats stats;
  SM_EXPECT_TRUE(ParseOutgoingBitstreamAndRewrite(in, &stats) == in);
  SM_EXPECT_EQ(stats.parse_failures, 1u);
  SM_EXPECT_EQ(stats.rewritten, 0u);
}

SM_TEST(SpsVuiRewriter, RbspEscapingRoundTrips) {
  const Bytes rbsp = {0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0, 4, 0};
  Bytes escaped;
  WriteRbsp(rbsp, escaped);
  SM_EXPECT_TRUE(escaped == Bytes({0, 0, 3, 0, 0, 3, 0, 1, 0, 0, 3, 2, 0, 0, 3, 3, 0, 0, 4, 0}));
  SM_EXPECT_TRUE(ParseRbsp(escaped) == rbsp);
}

SM_TEST(SpsVuiRewriter, FakeEncoderKeyframeGrowsByFiveBytes) {
  for (size_t bytes : {40u, 200u, 3000u}) {
    const Bytes key = MakeFakeH264Frame(true, bytes);
    const Bytes out = ParseOutgoingBitstreamAndRewrite(key);
    SM_EXPECT_EQ(out.size(), key.size() + 5);
    const Bytes delta = MakeFakeH264Frame(false, bytes);
    SM_EXPECT_TRUE(ParseOutgoingBitstreamAndRewrite(delta) == delta);
  }
}

namespace {
// H.264 bits, MSB first, for building parameter sets in tests.
struct Bits {
  Bytes out;
  int used = 8;
  void Bit(uint32_t b) {
    if (used == 8) {
      out.push_back(0);
      used = 0;
    }
    if (b) out.back() |= static_cast<uint8_t>(0x80 >> used);
    ++used;
  }
  void U(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) Bit((v >> i) & 1);
  }
  void Ue(uint32_t v) {  // ue(v), H.264 9.1.
    const uint64_t code = uint64_t{v} + 1;
    int len = 0;
    while ((code >> len) > 1) ++len;
    U(0, len);
    for (int i = len; i >= 0; --i) Bit(static_cast<uint32_t>((code >> i) & 1));
  }
  Bytes Trailing() {  // rbsp_trailing_bits.
    Bit(1);
    while (used != 8) Bit(0);
    return out;
  }
};

// A Baseline SPS RBSP (no NAL header) of the given size in macroblocks, frame_mbs_only.
Bytes BaselineSps(uint32_t sps_id, uint32_t width_mbs, uint32_t height_mbs) {
  Bits b;
  b.U(66, 8);  // profile_idc
  b.U(0, 8);   // constraint flags, reserved_zero_2bits
  b.U(52, 8);  // level_idc
  b.Ue(sps_id);
  b.Ue(0);  // log2_max_frame_num_minus4
  b.Ue(2);  // pic_order_cnt_type
  b.Ue(1);  // max_num_ref_frames
  b.Bit(0);  // gaps_in_frame_num_value_allowed_flag
  b.Ue(width_mbs - 1);
  b.Ue(height_mbs - 1);
  b.Bit(1);  // frame_mbs_only_flag
  b.Bit(1);  // direct_8x8_inference_flag
  b.Bit(0);  // frame_cropping_flag
  b.Bit(0);  // vui_parameters_present_flag
  return b.Trailing();
}
}  // namespace

SM_TEST(SpsVuiRewriter, SpsResolutionIsBoundAsH264IsValidResolution) {
  // sps_parser.cc:213-222 rejects an SPS beyond level 5.2's limits
  // (h264_common.h:50-64): 543 macroblocks a side, 36864 in a frame.
  SM_EXPECT_TRUE(IsValidH264Resolution(4096, 2304));    // 256 x 144 = 36864 macroblocks.
  SM_EXPECT_FALSE(IsValidH264Resolution(4112, 2304));   // 257 x 144 = 37008.
  SM_EXPECT_TRUE(IsValidH264Resolution(8688, 16));      // 543 x 1.
  SM_EXPECT_FALSE(IsValidH264Resolution(8704, 16));     // 544 x 1.
  SM_EXPECT_FALSE(IsValidH264Resolution(0, 16));
  SM_EXPECT_FALSE(IsValidH264Resolution(16, -16));
  const auto ok = ParseSpsUpToVui(BaselineSps(0, 256, 144));
  SM_ASSERT_TRUE(ok.has_value());
  SM_EXPECT_EQ(ok->width, 4096u);
  SM_EXPECT_EQ(ok->height, 2304u);
  SM_EXPECT_TRUE(ParseSpsUpToVui(BaselineSps(0, 543, 67)).has_value());   // 36381 macroblocks.
  SM_EXPECT_FALSE(ParseSpsUpToVui(BaselineSps(0, 257, 144)).has_value());  // Too many macroblocks.
  SM_EXPECT_FALSE(ParseSpsUpToVui(BaselineSps(0, 544, 1)).has_value());    // Too wide.
  SM_EXPECT_FALSE(ParseSpsUpToVui(BaselineSps(0, 1, 544)).has_value());    // Too tall.
  // The id bound (kMaxSpsId 31, sps_parser.cc:71-74).
  SM_EXPECT_TRUE(ParseSpsUpToVui(BaselineSps(31, 20, 12)).has_value());
  SM_EXPECT_FALSE(ParseSpsUpToVui(BaselineSps(32, 20, 12)).has_value());
  // ParseAndRewriteSps fails on the same SPS, so the depacketizer drops it.
  Bytes out;
  SM_EXPECT_TRUE(ParseAndRewriteSps(BaselineSps(0, 257, 144), out) == SpsRewriteResult::kFailure);
}

SM_TEST(SpsVuiRewriter, PpsAndSliceIdsAreBoundAsPpsParser) {
  // PpsParser::ParsePpsIds (pps_parser.cc:44-62): pps id <= 255, sps id <= 31.
  auto pps = [](uint32_t pps_id, uint32_t sps_id) {
    Bits b;
    b.Ue(pps_id);
    b.Ue(sps_id);
    return b.Trailing();
  };
  const auto ids = ParsePpsIds(pps(255, 31));
  SM_ASSERT_TRUE(ids.has_value());
  SM_EXPECT_EQ(ids->pps_id, 255u);
  SM_EXPECT_EQ(ids->sps_id, 31u);
  SM_EXPECT_FALSE(ParsePpsIds(pps(256, 0)).has_value());
  SM_EXPECT_FALSE(ParsePpsIds(pps(0, 32)).has_value());
  // PpsParser::ParseSliceHeader (pps_parser.cc:64-88): pic_parameter_set_id <= 255.
  auto slice = [](uint32_t first_mb, uint32_t pps_id) {
    Bits b;
    b.Ue(first_mb);
    b.Ue(7);  // slice_type
    b.Ue(pps_id);
    return b.Trailing();
  };
  const auto h = ParseSliceHeader(slice(5, 255));
  SM_ASSERT_TRUE(h.has_value());
  SM_EXPECT_EQ(h->first_mb_in_slice, 5u);
  SM_EXPECT_EQ(h->pic_parameter_set_id, 255u);
  SM_EXPECT_FALSE(ParseSliceHeader(slice(0, 256)).has_value());
}
