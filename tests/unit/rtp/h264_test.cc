#include <vector>

#include "sm/core/rtp_packet.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/sps_vui_rewriter.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {
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

Bytes RoundTrip(const Bytes& frame, size_t max_payload, std::vector<Bytes>* payloads = nullptr) {
  auto chunks = H264Packetizer::Packetize(frame, max_payload);
  for (const Bytes& c : chunks) SM_EXPECT_LE(c.size(), max_payload);
  Bytes out;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets(chunks), out));
  if (payloads) *payloads = chunks;
  return out;
}

// A frame as Session::SendVideo hands it to the packetizer: the SPS's VUI
// already rewritten, so the receive-side rewrite leaves it as it is.
Bytes SentFrame(bool key, size_t bytes) { return ParseOutgoingBitstreamAndRewrite(MakeFakeH264Frame(key, bytes)); }

Bytes Nal(std::initializer_list<uint8_t> b) {
  Bytes out = {0, 0, 0, 1};
  out.insert(out.end(), b.begin(), b.end());
  return out;
}
}  // namespace

SM_TEST(H264, SplitAnnexBHandlesThreeAndFourByteStartCodes) {
  Bytes s = {0, 0, 1, 0x67, 1, 2, 0, 0, 0, 1, 0x68, 3, 0, 0, 1, 0x65, 4};
  auto nals = SplitAnnexB(s);
  SM_ASSERT_EQ(nals.size(), 3u);
  SM_EXPECT_EQ(nals[0].size(), 3u);  // The zero before the 4-byte code belongs to the code.
  SM_EXPECT_EQ(nals[1].size(), 2u);
  SM_EXPECT_EQ(nals[2].size(), 2u);
  Bytes bare = {0x41, 9, 9};
  SM_EXPECT_EQ(SplitAnnexB(bare).size(), 1u);
  SM_EXPECT_TRUE(SplitAnnexB({}).empty());
}

SM_TEST(H264, SmallNalsAreAggregatedIntoStapA) {
  Bytes frame = MakeFakeH264Frame(true, 200);  // SPS + PPS + a small IDR: all fit one packet.
  auto chunks = H264Packetizer::Packetize(frame, 1188);
  SM_ASSERT_EQ(chunks.size(), 1u);
  SM_EXPECT_EQ(chunks[0][0] & 0x1F, kH264NalStapA);
  H264PayloadInfo i = H264Depacketizer::Parse(chunks[0]);
  SM_EXPECT_TRUE(i.valid && i.sps && i.pps && i.idr && i.first_packet_in_frame);
}

SM_TEST(H264, SingleNalUnitPacketWhenAloneAndFitting) {
  Bytes frame = MakeFakeH264Frame(false, 300);
  auto chunks = H264Packetizer::Packetize(frame, 1188);
  SM_ASSERT_EQ(chunks.size(), 1u);
  SM_EXPECT_EQ(chunks[0][0], 0x41);
  SM_EXPECT_EQ(chunks[0].size(), 300u - 4);
  H264PayloadInfo i = H264Depacketizer::Parse(chunks[0]);
  SM_EXPECT_TRUE(i.valid && !i.idr && i.first_packet_in_frame);
}

SM_TEST(H264, LargeNalIsFragmentedWithFuA) {
  Bytes frame = SentFrame(true, 5000);
  std::vector<Bytes> payloads;
  SM_EXPECT_TRUE(RoundTrip(frame, 1188, &payloads) == frame);
  // STAP-A (SPS+PPS) then the IDR in FU-A fragments about equal in size.
  SM_ASSERT_GE(payloads.size(), 5u);
  SM_EXPECT_EQ(payloads[0][0] & 0x1F, kH264NalStapA);
  size_t lo = 99999, hi = 0;
  for (size_t k = 1; k < payloads.size(); ++k) {
    SM_EXPECT_EQ(payloads[k][0] & 0x1F, kH264NalFuA);
    SM_EXPECT_EQ(payloads[k][0] & 0xE0, 0x60);  // NRI of the IDR NAL header (0x65).
    SM_EXPECT_EQ(payloads[k][1] & 0x1F, kH264NalIdr);
    SM_EXPECT_EQ((payloads[k][1] & 0x80) != 0, k == 1);                 // Start bit.
    SM_EXPECT_EQ((payloads[k][1] & 0x40) != 0, k + 1 == payloads.size());  // End bit.
    lo = std::min(lo, payloads[k].size());
    hi = std::max(hi, payloads[k].size());
  }
  SM_EXPECT_LE(hi - lo, 1u);
  // The remainder goes to the last fragments, as in libwebrtc.
  SM_EXPECT_GE(payloads.back().size(), payloads[1].size());
  H264PayloadInfo first = H264Depacketizer::Parse(payloads[1]);
  SM_EXPECT_TRUE(first.valid && first.idr && first.fu_start && first.first_packet_in_frame);
  H264PayloadInfo mid = H264Depacketizer::Parse(payloads[2]);
  SM_EXPECT_TRUE(mid.valid && !mid.idr && mid.fu_continuation && !mid.first_packet_in_frame);
}

SM_TEST(H264, RoundTripAtEveryPayloadLimitAndSize) {
  for (bool key : {false, true}) {
    for (size_t bytes : {40u, 300u, 1200u, 1201u, 2399u, 7000u}) {
      for (size_t limit : {3u, 20u, 100u, 1187u, 1188u}) {
        Bytes frame = SentFrame(key, bytes);
        SM_EXPECT_TRUE(RoundTrip(frame, limit) == frame);
      }
    }
  }
}

SM_TEST(H264, MultipleSlicesAndAggregationOfSmallNals) {
  Bytes frame;
  for (uint8_t k = 0; k < 6; ++k) {
    Bytes n = Nal({0x41, static_cast<uint8_t>(k == 0 ? 0xAA : 0x22), 5, 6, 7});
    frame.insert(frame.end(), n.begin(), n.end());
  }
  std::vector<Bytes> payloads;
  SM_EXPECT_TRUE(RoundTrip(frame, 20, &payloads) == frame);
  SM_EXPECT_GE(payloads.size(), 2u);  // 6 units of 5 bytes do not fit one 20-byte STAP-A.
  SM_EXPECT_EQ(payloads[0][0] & 0x1F, kH264NalStapA);
}

SM_TEST(H264, StapAHeaderTakesTheHighestNri) {
  Bytes frame = Nal({0x27, 1, 2});
  Bytes b = Nal({0x68, 3, 4});
  frame.insert(frame.end(), b.begin(), b.end());
  auto chunks = H264Packetizer::Packetize(frame, 100);
  SM_ASSERT_EQ(chunks.size(), 1u);
  SM_EXPECT_EQ(chunks[0][0], 0x60 | kH264NalStapA);  // NRI 1 from 0x27, 3 from 0x68: the larger.
}

SM_TEST(H264, FrameStartNeedsFirstMbZero) {
  Bytes first = {0x41, 0x88, 1, 2};
  Bytes later = {0x41, 0x08, 1, 2};  // first_mb_in_slice != 0: a later slice of the same picture.
  SM_EXPECT_TRUE(H264Depacketizer::Parse(first).first_packet_in_frame);
  SM_EXPECT_FALSE(H264Depacketizer::Parse(later).first_packet_in_frame);
  SM_EXPECT_TRUE(H264Depacketizer::Parse(later).valid);
  Bytes sei = {0x06, 1, 2};
  SM_EXPECT_TRUE(H264Depacketizer::Parse(sei).first_packet_in_frame);
}

SM_TEST(H264, MalformedPayloadsAreRejected) {
  // Where VideoRtpDepacketizerH264::Parse returns nullopt.
  SM_EXPECT_FALSE(H264Depacketizer::Parse({}).valid);
  Bytes stap_short = {kH264NalStapA, 0, 5, 0x41, 1};  // Declared 5 bytes, 2 present.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_short).valid);
  Bytes stap_empty = {kH264NalStapA};  // ParseStapA finds no unit.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_empty).valid);
  Bytes stap_zero = {kH264NalStapA, 0, 0};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_zero).valid);
  Bytes stap_cut = {kH264NalStapA, 0, 1, 0x06, 0};  // A size field cut off.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_cut).valid);
  Bytes fu_short = {kH264NalFuA};  // Shorter than kFuAHeaderSize.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(fu_short).valid);
  // A STAP-A or FU-A inside a STAP-A ("Unexpected STAP-A or FU-A received").
  Bytes nested_stap = {kH264NalStapA, 0, 2, kH264NalStapA, 0};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(nested_stap).valid);
  Bytes nested_fu = {kH264NalStapA, 0, 3, kH264NalFuA, 0x85, 0xAA};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(nested_fu).valid);
}

SM_TEST(H264, ParameterSetsAndSlicesThatDontParseAreDropped) {
  // An SPS SpsVuiRewriter can't parse (profile and level, then no id).
  Bytes sps = {0x67, 0x42, 0x00, 0x1F};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(sps).valid);
  // A PPS (PpsParser::ParsePpsIds) or a slice (ParseSliceHeader) whose ue(v)
  // ids run out of data.
  Bytes pps = {0x68, 0x00};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(pps).valid);
  Bytes slice = {0x41, 0x80};  // first_mb_in_slice 0, then slice_type runs out.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(slice).valid);
  Bytes idr = {0x65, 0x00, 0x00};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(idr).valid);
  // In a STAP-A one such unit drops the whole packet.
  Bytes stap = {kH264NalStapA, 0, 2, 0x06, 1, 0, 2, 0x68, 0x00};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap).valid);
  // The fake encoder's PPS and slices parse: pps 0, sps 0; first_mb 0, slice
  // type 1, pps 0.
  SM_EXPECT_TRUE(ParsePpsIds(std::span<const uint8_t>(kFakeH264Pps + 1, sizeof(kFakeH264Pps) - 1)).has_value());
  const auto h = ParseSliceHeader(Bytes{0xAA, 0x11});
  SM_ASSERT_TRUE(h.has_value());
  SM_EXPECT_EQ(h->first_mb_in_slice, 0u);
  SM_EXPECT_EQ(h->pic_parameter_set_id, 0u);
}

SM_TEST(H264, PayloadsLibwebrtcAcceptsAreAccepted) {
  // ParseFuaNalu checks only the size: two bytes, start and end together, or
  // the reserved bit set, all pass.
  Bytes fu_two = {kH264NalFuA, 0x85};
  SM_EXPECT_TRUE(H264Depacketizer::Parse(fu_two).valid);
  Bytes fu_both = {kH264NalFuA, 0xC5, 0xAA};
  SM_EXPECT_TRUE(H264Depacketizer::Parse(fu_both).valid);
  Bytes fu_reserved = {kH264NalFuA, 0xA5, 0xAA};
  SM_EXPECT_TRUE(H264Depacketizer::Parse(fu_reserved).valid);
  // A first FU-A fragment whose slice header doesn't parse is only logged.
  Bytes fu_bad_slice = {kH264NalFuA, 0x81, 0x80};
  const H264PayloadInfo bad = H264Depacketizer::Parse(fu_bad_slice);
  SM_EXPECT_TRUE(bad.valid && !bad.first_packet_in_frame);
  // STAP-B, MTAP16, MTAP24, FU-B and the unspecified types go down the single
  // NAL unit path, which reads nothing from them.
  for (uint8_t t : {25, 26, 27, 29, 30, 31, 0}) {
    Bytes b = {t, 1, 2, 3};
    const H264PayloadInfo i = H264Depacketizer::Parse(b);
    SM_EXPECT_TRUE(i.valid && !i.first_packet_in_frame && !i.keyframe);
  }
  // Empty units are skipped; end of sequence, end of stream and filler start
  // nothing.
  for (uint8_t t : {0x41, 0x0A, 0x0B, 0x0C}) {
    Bytes b = {t};
    SM_EXPECT_TRUE(H264Depacketizer::Parse(b).valid);
  }
  Bytes eos = {0x0A, 1};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(eos).first_packet_in_frame);
}

SM_TEST(H264, FuAFrameStartIsAFirstSliceOnly) {
  // ParseFuaNalu sets is_first_packet_in_frame from the slice header of a
  // first fragment of a slice only; a fragmented SPS, PPS, SEI or AUD doesn't
  // start a frame there.
  Bytes slice_start = {kH264NalFuA | 0x40, 0x81, 0xAA};
  SM_EXPECT_TRUE(H264Depacketizer::Parse(slice_start).first_packet_in_frame);
  Bytes later_slice = {kH264NalFuA | 0x40, 0x81, 0x08, 0x11};  // first_mb_in_slice 7.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(later_slice).first_packet_in_frame);
  for (uint8_t t : {kH264NalSps, kH264NalPps, kH264NalSei, kH264NalAud}) {
    Bytes b = {kH264NalFuA | 0x60, static_cast<uint8_t>(0x80 | t), 0xAA, 0x11};
    const H264PayloadInfo i = H264Depacketizer::Parse(b);
    SM_EXPECT_TRUE(i.valid && !i.first_packet_in_frame && !i.sps && !i.pps);
  }
}

SM_TEST(H264, SpsIsRewrittenOnReceive) {
  // The fake encoder's SPS has no bitstream_restriction; the depacketizer
  // rewrites it as SpsVuiRewriter (kIncoming) and the frame comes out as the
  // sender's outgoing rewrite would have made it, in a single NAL unit packet,
  // a STAP-A (its length field rewritten) or behind FU-A slices.
  for (size_t limit : {100u, 1188u}) {
    for (size_t bytes : {40u, 300u, 5000u}) {
      const Bytes raw = MakeFakeH264Frame(true, bytes);
      SM_EXPECT_TRUE(RoundTrip(raw, limit) == ParseOutgoingBitstreamAndRewrite(raw));
    }
  }
  const Bytes raw = MakeFakeH264Frame(true, 40);
  const auto nals = SplitAnnexB(raw);
  Bytes single(nals[0].begin(), nals[0].end());
  SM_EXPECT_EQ(single[0] & 0x1F, kH264NalSps);
  Bytes out;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets({single}), out));
  SM_EXPECT_EQ(out.size(), 4 + single.size() + 5);  // bitstream_restriction added.
  // The SPS's resolution is reported as video_header.width and height.
  const H264PayloadInfo i = H264Depacketizer::Parse(single);
  SM_EXPECT_TRUE(i.valid && i.sps && i.keyframe && i.first_packet_in_frame);
  SpsState state;
  Bytes ignored;
  SM_ASSERT_TRUE(ParseAndRewriteSps(std::span<const uint8_t>(single).subspan(1), ignored, &state) != SpsRewriteResult::kFailure);
  SM_EXPECT_TRUE(state.width > 0 && state.height > 0);
  SM_EXPECT_EQ(i.width, state.width);
  SM_EXPECT_EQ(i.height, state.height);
}

SM_TEST(H264, ReassemblyCopiesFragmentsAsTheTracker) {
  // H264SpsPpsTracker::CopyAndFixBitstream doesn't check the FU-A sequence:
  // a start code goes before each first fragment and the rest is appended.
  // The packet buffer hands over only frames from a first packet to a marker.
  Bytes frame = SentFrame(false, 3000);
  auto chunks = H264Packetizer::Packetize(frame, 1200);
  SM_ASSERT_EQ(chunks.size(), 3u);
  Bytes out;
  std::vector<Bytes> no_start(chunks.begin() + 1, chunks.end());
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets(no_start), out));
  SM_EXPECT_EQ(out.size(), chunks[1].size() - 2 + chunks[2].size() - 2);  // No start code, no NAL header.
  out.clear();
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets(chunks), out));
  SM_EXPECT_TRUE(out == frame);
  // A payload the depacketizer drops can't be reassembled.
  out.clear();
  SM_EXPECT_FALSE(H264Depacketizer::Reassemble(AsPackets({Bytes{0x68, 0x00}}), out));
}

SM_TEST(H264, ParserReportsKeyframeParts) {
  H264PayloadParser parser;
  Bytes frame = MakeFakeH264Frame(true, 4000);
  auto chunks = H264Packetizer::Packetize(frame, 1188);
  auto pkts = AsPackets(chunks);
  VideoPayloadInfo a = parser.Parse(pkts[0]);
  SM_EXPECT_TRUE(a.valid && a.frame_start && a.sps && a.pps && !a.idr);
  VideoPayloadInfo b = parser.Parse(pkts[1]);
  SM_EXPECT_TRUE(b.valid && b.frame_start && b.idr);  // The IDR start also looks like a start.
  VideoPayloadInfo c = parser.Parse(pkts[2]);
  SM_EXPECT_TRUE(c.valid && !c.frame_start && !c.idr);
}

SM_TEST(H264, EveryPacketOfAnIdrIsAKeyframePacket) {
  // VideoRtpDepacketizerH264 (ParseFuaNalu) reads the original NAL type from
  // every FU-A fragment, so each fragment of an IDR slice is kVideoFrameKey;
  // the STAP-A with the SPS is key too (ProcessStapAOrSingleNalu). Issue #335.
  H264PayloadParser parser;
  auto key = AsPackets(H264Packetizer::Packetize(MakeFakeH264Frame(true, 6000), 1188));
  SM_ASSERT_TRUE(key.size() > 3);
  for (const auto& p : key) SM_EXPECT_TRUE(parser.Parse(p).keyframe);
  auto delta = AsPackets(H264Packetizer::Packetize(MakeFakeH264Frame(false, 6000), 1188));
  SM_ASSERT_TRUE(delta.size() > 2);
  for (const auto& p : delta) SM_EXPECT_FALSE(parser.Parse(p).keyframe);
}

SM_TEST(H264, FakeEncoderFramesHaveTheStatedStructure) {
  Bytes key = MakeFakeH264Frame(true, 1000);
  SM_EXPECT_EQ(key.size(), 1000u);
  auto nals = SplitAnnexB(key);
  SM_ASSERT_EQ(nals.size(), 3u);
  SM_EXPECT_EQ(nals[0][0], 0x67);
  SM_EXPECT_EQ(nals[0].size(), 8u);
  SM_EXPECT_EQ(nals[1][0], 0x68);
  SM_EXPECT_EQ(nals[1].size(), 11u);
  SM_EXPECT_EQ(nals[2][0], 0x65);
  Bytes delta = MakeFakeH264Frame(false, 500);
  SM_EXPECT_EQ(delta.size(), 500u);
  SM_ASSERT_EQ(SplitAnnexB(delta).size(), 1u);
  SM_EXPECT_EQ(SplitAnnexB(delta)[0][0], 0x41);
  for (uint8_t b : delta) (void)b;
  int zeros = 0;
  for (size_t i = 4; i < delta.size(); ++i) zeros += delta[i] == 0;
  SM_EXPECT_EQ(zeros, 0);
}

// Ports the intent of libwebrtc's rtp_format_h264_unittest.cc (the reduction
// cases): the first, last and single packet have less room.
SM_TEST(H264, ReductionsShrinkTheFirstAndLastPacketOfAFrame) {
  Bytes frame = Nal({0x41});
  frame.resize(frame.size() + 1000, 0x55);  // One 1001-byte NAL unit.
  PayloadSizeLimits limits;
  limits.max_payload_len = 400;
  limits.first_packet_reduction_len = 50;
  limits.last_packet_reduction_len = 30;
  limits.single_packet_reduction_len = 80;
  auto payloads = H264Packetizer::Packetize(frame, limits);
  SM_ASSERT_EQ(payloads.size(), 3u);
  SM_EXPECT_LE(static_cast<int>(payloads.front().size()), 400 - 50);
  SM_EXPECT_LE(static_cast<int>(payloads.back().size()), 400 - 30);
  SM_EXPECT_LE(static_cast<int>(payloads[1].size()), 400);
  Bytes out;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets(payloads), out));
  SM_EXPECT_TRUE(out == frame);
}

SM_TEST(H264, SingleNalUsesTheSinglePacketReduction) {
  Bytes frame = Nal({0x41});
  frame.resize(frame.size() + 99, 0x55);  // A 100-byte NAL unit.
  PayloadSizeLimits limits;
  limits.max_payload_len = 120;
  limits.single_packet_reduction_len = 0;
  SM_EXPECT_EQ(H264Packetizer::Packetize(frame, limits).size(), 1u);
  limits.single_packet_reduction_len = 30;  // 100 > 120 - 30: fragmented.
  auto payloads = H264Packetizer::Packetize(frame, limits);
  SM_ASSERT_EQ(payloads.size(), 2u);
  SM_EXPECT_LE(static_cast<int>(payloads[0].size()), 120 - 30);
  Bytes out;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets(payloads), out));
  SM_EXPECT_TRUE(out == frame);
}

SM_TEST(H264, StapAHonoursTheLastPacketReduction) {
  // Two 40-byte units: STAP-A is 1 + 2 + 40 + 2 + 40 = 85 bytes.
  Bytes frame = Nal({0x61});
  frame.resize(frame.size() + 39, 1);
  Bytes second = Nal({0x41});
  second.resize(second.size() + 39, 2);
  frame.insert(frame.end(), second.begin(), second.end());
  PayloadSizeLimits limits;
  limits.max_payload_len = 90;
  SM_EXPECT_EQ(H264Packetizer::Packetize(frame, limits).size(), 1u);
  limits.single_packet_reduction_len = 10;  // Not the single packet: the unit is first and last in turn.
  limits.last_packet_reduction_len = 10;    // 85 + 10 > 90: the last unit goes alone.
  auto payloads = H264Packetizer::Packetize(frame, limits);
  SM_ASSERT_EQ(payloads.size(), 2u);
  SM_EXPECT_EQ(payloads[0].size(), 40u);
  SM_EXPECT_EQ(payloads[1].size(), 40u);
}

SM_TEST(H264, NoRoomForAFuAHeaderGivesNoPackets) {
  Bytes frame = Nal({0x41});
  frame.resize(frame.size() + 50, 7);
  PayloadSizeLimits limits;
  limits.max_payload_len = 2;
  SM_EXPECT_TRUE(H264Packetizer::Packetize(frame, limits).empty());
}

SM_TEST(H264, NegativeOrZeroPayloadLimitGivesNoPacketsWithoutUndefinedBehaviour) {
  Bytes frame = Nal({0x41});
  frame.resize(frame.size() + 50, 7);
  PayloadSizeLimits limits;
  limits.max_payload_len = -1;
  limits.single_packet_reduction_len = 5;
  limits.first_packet_reduction_len = 5;
  limits.last_packet_reduction_len = 5;
  SM_EXPECT_TRUE(H264Packetizer::Packetize(frame, limits).empty());
  limits.max_payload_len = 0;
  SM_EXPECT_TRUE(H264Packetizer::Packetize(frame, limits).empty());
}

namespace {
// Parameter sets with ids other than 0, hand-encoded (H.264 7.3.2.1.1, 7.3.2.2, 7.3.3).
// SPS id 1: Baseline, level 3.1, 320x192, POC type 2, one reference frame, no VUI.
const Bytes kSpsId1 = {0x67, 0x42, 0x00, 0x1F, 0x56, 0x81, 0x41, 0x99};
const Bytes kPps1Sps1 = {0x68, 0x4A};  // pic_parameter_set_id 1, seq_parameter_set_id 1.
const Bytes kPps0Sps1 = {0x68, 0xA8};  // pic_parameter_set_id 0, seq_parameter_set_id 1.
const Bytes kIdrPps1 = {0x65, 0x88, 0x50};  // first_mb_in_slice 0, slice_type 7, PPS 1.
const Bytes kIdrPps0 = {0x65, 0x88, 0xC0};  // first_mb_in_slice 0, slice_type 7, PPS 0.

Bytes StapA(std::initializer_list<Bytes> units) {
  Bytes p = {kH264NalStapA};
  for (const Bytes& u : units) {
    p.push_back(static_cast<uint8_t>(u.size() >> 8));
    p.push_back(static_cast<uint8_t>(u.size()));
    p.insert(p.end(), u.begin(), u.end());
  }
  return p;
}

Bytes FuAStart(const Bytes& nal) {  // The first FU-A fragment of `nal` (all of it).
  Bytes p = {static_cast<uint8_t>((nal[0] & 0xE0) | kH264NalFuA), static_cast<uint8_t>(0x80 | (nal[0] & 0x1F))};
  p.insert(p.end(), nal.begin() + 1, nal.end());
  return p;
}
}  // namespace

SM_TEST(H264, ParseReportsEachUnitsIds) {
  // RTPVideoHeaderH264::nalus as ProcessStapAOrSingleNalu fills it
  // (video_rtp_depacketizer_h264.cc:97-219): SPS id, PPS and SPS ids, slice PPS id.
  const H264PayloadInfo s = H264Depacketizer::Parse(StapA({kSpsId1, kPps0Sps1, kIdrPps0}));
  SM_ASSERT_TRUE(s.valid);
  SM_ASSERT_EQ(s.nalus.size(), 3u);
  SM_EXPECT_EQ(s.nalus[0].type, kH264NalSps);
  SM_EXPECT_EQ(s.nalus[0].sps_id, 1);
  SM_EXPECT_EQ(s.nalus[0].pps_id, -1);
  SM_EXPECT_EQ(s.nalus[1].type, kH264NalPps);
  SM_EXPECT_EQ(s.nalus[1].pps_id, 0);
  SM_EXPECT_EQ(s.nalus[1].sps_id, 1);
  SM_EXPECT_EQ(s.nalus[2].type, kH264NalIdr);
  SM_EXPECT_EQ(s.nalus[2].pps_id, 0);
  SM_EXPECT_EQ(s.nalus[2].sps_id, -1);
  SM_EXPECT_EQ(s.width, 320u);
  SM_EXPECT_EQ(s.height, 192u);
  // Units without ids are listed with -1 (SEI); empty units are skipped.
  const H264PayloadInfo sei = H264Depacketizer::Parse(StapA({{0x06, 0x05}, {0x06}}));
  SM_ASSERT_TRUE(sei.valid);
  SM_ASSERT_EQ(sei.nalus.size(), 1u);
  SM_EXPECT_EQ(sei.nalus[0].type, kH264NalSei);
  // ParseFuaNalu (video_rtp_depacketizer_h264.cc:226-290): the first fragment
  // lists its unit with the slice's PPS id; an SPS or PPS fragment has none;
  // later fragments list nothing.
  const H264PayloadInfo fu_idr = H264Depacketizer::Parse(FuAStart(kIdrPps1));
  SM_ASSERT_EQ(fu_idr.nalus.size(), 1u);
  SM_EXPECT_EQ(fu_idr.nalus[0].type, kH264NalIdr);
  SM_EXPECT_EQ(fu_idr.nalus[0].pps_id, 1);
  const H264PayloadInfo fu_sps = H264Depacketizer::Parse(FuAStart(kSpsId1));
  SM_ASSERT_TRUE(fu_sps.valid);
  SM_ASSERT_EQ(fu_sps.nalus.size(), 1u);
  SM_EXPECT_EQ(fu_sps.nalus[0].type, kH264NalSps);
  SM_EXPECT_EQ(fu_sps.nalus[0].sps_id, -1);
  SM_EXPECT_FALSE(fu_sps.sps);
  const Bytes middle = {kH264NalFuA, 0x05, 0xAA};
  SM_EXPECT_TRUE(H264Depacketizer::Parse(middle).nalus.empty());
}

SM_TEST(H264, IdsOutOfBoundsAreDroppedAsPpsParser) {
  // A PPS with pic_parameter_set_id 256 (ue: 8 zeros, 100000001) or a slice
  // naming PPS 256 fails PpsParser, so Parse drops the packet.
  const Bytes pps256 = {0x68, 0x00, 0x80, 0xE0};  // pps 256, sps 0, trailing bits.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(pps256).valid);
  const Bytes slice256 = {0x41, 0x88, 0x00, 0x80, 0xC0};  // first_mb 0, type 7, pps 256, trailing.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(slice256).valid);
}

SM_TEST(H264, SpsPpsTrackerKeepsParameterSetsById) {
  // H264SpsPpsTracker::CopyAndFixBitstream (h264_sps_pps_tracker.cc:37-110).
  using A = H264SpsPpsTracker::Action;
  auto check = [](H264SpsPpsTracker& t, const Bytes& payload) {
    const H264PayloadInfo i = H264Depacketizer::Parse(payload);
    SM_EXPECT_TRUE(i.valid);
    return t.CopyAndFixBitstream(i.nalus, i.first_packet_in_frame);
  };
  {
    // SPS 0 and PPS 0 (the fake encoder's) are known, but the IDR names PPS 1:
    // a keyframe request, where any-SPS-and-any-PPS would let it through.
    H264SpsPpsTracker t;
    Bytes sps0 = {0x67}, pps0 = {0x68};
    sps0.insert(sps0.end(), kFakeH264Sps, kFakeH264Sps + sizeof(kFakeH264Sps));
    pps0.insert(pps0.end(), kFakeH264Pps, kFakeH264Pps + sizeof(kFakeH264Pps));
    SM_EXPECT_TRUE(check(t, StapA({sps0, pps0})) == A::kInsert);
    SM_EXPECT_TRUE(check(t, kIdrPps1) == A::kRequestKeyframe);
    SM_EXPECT_TRUE(check(t, kIdrPps0) == A::kInsert);
    // PPS 1 refers to SPS 1, which hasn't come: still a request.
    SM_EXPECT_TRUE(check(t, kPps1Sps1) == A::kInsert);
    SM_EXPECT_TRUE(check(t, kIdrPps1) == A::kRequestKeyframe);
    SM_EXPECT_TRUE(check(t, kSpsId1) == A::kInsert);
    SM_EXPECT_TRUE(check(t, kIdrPps1) == A::kInsert);
    // An FU-A IDR start is checked the same way; a later fragment isn't.
    SM_EXPECT_TRUE(check(t, FuAStart(kIdrPps1)) == A::kInsert);
    SM_EXPECT_TRUE(check(t, Bytes{kH264NalFuA, 0x45, 0xAA}) == A::kInsert);
  }
  {
    // Units are handled in order: [SPS, PPS, IDR] passes, [IDR, SPS, PPS] doesn't.
    H264SpsPpsTracker a;
    SM_EXPECT_TRUE(check(a, StapA({kSpsId1, kPps1Sps1, kIdrPps1})) == A::kInsert);
    H264SpsPpsTracker b;
    SM_EXPECT_TRUE(check(b, StapA({kIdrPps1, kSpsId1, kPps1Sps1})) == A::kRequestKeyframe);
    // A PPS id can be redefined to another SPS.
    SM_EXPECT_TRUE(check(a, kPps1Sps1) == A::kInsert);
    SM_EXPECT_TRUE(check(a, Bytes{0x68, 0x4E}) == A::kInsert);  // pps 1 -> sps 2 (ue 1, ue 2).
    SM_EXPECT_TRUE(check(a, kIdrPps1) == A::kRequestKeyframe);  // No SPS 2.
  }
  {
    // A parameter set in an FU-A has no ids: kDrop. An IDR that doesn't start
    // a frame (first_mb_in_slice > 0) isn't checked.
    H264SpsPpsTracker t;
    SM_EXPECT_TRUE(check(t, FuAStart(kSpsId1)) == A::kDrop);
    SM_EXPECT_TRUE(check(t, FuAStart(kPps1Sps1)) == A::kDrop);
    const Bytes idr_not_first = {0x65, 0x42, 0x30};  // first_mb_in_slice 1, slice_type 7, PPS 0.
    const H264PayloadInfo i = H264Depacketizer::Parse(idr_not_first);
    SM_ASSERT_TRUE(i.valid);
    SM_EXPECT_FALSE(i.first_packet_in_frame);
    SM_EXPECT_TRUE(t.CopyAndFixBitstream(i.nalus, i.first_packet_in_frame) == A::kInsert);
  }
}
