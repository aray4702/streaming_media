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
