#include <vector>

#include "sm/core/rtp_packet.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/h264.h"
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
  Bytes frame = MakeFakeH264Frame(true, 5000);
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
        Bytes frame = MakeFakeH264Frame(key, bytes);
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
  SM_EXPECT_FALSE(H264Depacketizer::Parse({}).valid);
  Bytes stap_short = {kH264NalStapA, 0, 5, 0x41, 1};  // Declared 5 bytes, 2 present.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_short).valid);
  Bytes stap_empty = {kH264NalStapA};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_empty).valid);
  Bytes stap_zero = {kH264NalStapA, 0, 0};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(stap_zero).valid);
  Bytes fu_short = {kH264NalFuA, 0x85};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(fu_short).valid);
  Bytes fu_both = {kH264NalFuA, 0xC5, 1};  // Start and end together.
  SM_EXPECT_FALSE(H264Depacketizer::Parse(fu_both).valid);
  Bytes fu_reserved = {kH264NalFuA, 0xA5, 1};
  SM_EXPECT_FALSE(H264Depacketizer::Parse(fu_reserved).valid);
  for (uint8_t t : {25, 26, 27, 29, 30, 31, 0}) {  // STAP-B, MTAP16, MTAP24, FU-B and reserved.
    Bytes b = {t, 1, 2, 3};
    SM_EXPECT_FALSE(H264Depacketizer::Parse(b).valid);
  }
}

SM_TEST(H264, ReassembleRejectsInconsistentFragments) {
  Bytes frame = MakeFakeH264Frame(false, 3000);
  auto chunks = H264Packetizer::Packetize(frame, 1200);
  SM_ASSERT_EQ(chunks.size(), 3u);
  Bytes out;
  // A middle fragment arrives without its start.
  std::vector<Bytes> no_start(chunks.begin() + 1, chunks.end());
  SM_EXPECT_FALSE(H264Depacketizer::Reassemble(AsPackets(no_start), out));
  // The end fragment is missing.
  out.clear();
  std::vector<Bytes> no_end(chunks.begin(), chunks.end() - 1);
  SM_EXPECT_FALSE(H264Depacketizer::Reassemble(AsPackets(no_end), out));
  // A new unit starts inside an unfinished one.
  out.clear();
  std::vector<Bytes> twice = {chunks[0], chunks[0]};
  SM_EXPECT_FALSE(H264Depacketizer::Reassemble(AsPackets(twice), out));
  // The complete sequence is fine.
  out.clear();
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(AsPackets(chunks), out));
  SM_EXPECT_TRUE(out == frame);
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
