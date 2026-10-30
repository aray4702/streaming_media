#include "sm/loss/red.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {
std::vector<uint8_t> Frame(uint8_t v, size_t n) { return std::vector<uint8_t>(n, v); }
}

SM_TEST(Red, EncodeParseWithRfc2198Layout) {
  RedEncoder enc(2);
  enc.Encode(111, 0, Frame(1, 20));
  enc.Encode(111, 960, Frame(2, 30));
  auto pkt = enc.Encode(111, 1920, Frame(3, 40));
  // 2 redundant headers (4 bytes each) + primary header (1) + payloads.
  SM_EXPECT_EQ(pkt.size(), 4u + 4u + 1u + 20u + 30u + 40u);
  SM_EXPECT_EQ(pkt[0], 0x80 | 111);
  auto blocks = ParseRed(pkt, 1920);
  SM_ASSERT_TRUE(blocks.has_value());
  SM_ASSERT_EQ(blocks->size(), 3u);
  SM_EXPECT_EQ((*blocks)[0].timestamp, 0u);
  SM_EXPECT_EQ((*blocks)[1].timestamp, 960u);
  SM_EXPECT_EQ((*blocks)[2].timestamp, 1920u);
  SM_EXPECT_TRUE((*blocks)[2].primary);
  SM_EXPECT_EQ((*blocks)[1].payload, Frame(2, 30));
}

SM_TEST(Red, RejectsMalformed) {
  SM_EXPECT_FALSE(ParseRed(std::vector<uint8_t>{}, 0).has_value());
  SM_EXPECT_FALSE(ParseRed(std::vector<uint8_t>{0x80 | 111, 0, 0}, 0).has_value());
  // Header claims 100 bytes of redundancy but packet is short.
  SM_EXPECT_FALSE(ParseRed(std::vector<uint8_t>{0x80 | 111, 0, 0x04, 100, 111, 1, 2}, 0).has_value());
}

SM_TEST(Red, ReceiverRecoversLostFrameOnce) {
  RedEncoder enc(1);
  RedReceiver rx;
  auto p0 = enc.Encode(111, 0, Frame(1, 10));
  auto p1 = enc.Encode(111, 960, Frame(2, 10));
  auto p2 = enc.Encode(111, 1920, Frame(3, 10));
  SM_EXPECT_EQ(rx.OnPacket(p0, 0).size(), 1u);
  // p1 lost; p2 carries frame 2 as redundancy.
  auto out = rx.OnPacket(p2, 1920);
  SM_ASSERT_EQ(out.size(), 2u);
  SM_EXPECT_EQ(out[0].timestamp, 960u);
  SM_EXPECT_FALSE(out[0].primary);
  SM_EXPECT_EQ(rx.stats().recovered_frames, 1u);
  // Late duplicate of p1 adds nothing.
  SM_EXPECT_TRUE(rx.OnPacket(p1, 960).empty());
}

SM_TEST(Red, SkipsOversizedRedundancy) {
  RedEncoder enc(1, 15);
  enc.Encode(111, 0, Frame(1, 50));
  auto pkt = enc.Encode(111, 960, Frame(2, 10));
  SM_EXPECT_EQ(pkt.size(), 1u + 10u);  // Too-big previous frame not repeated.
}

// RedPayloadSplitter's unit tests (red_payload_splitter_unittest.cc
// OnePacketTwoPayloads, TwoPacketsThreePayloads, WrongPayloadLength,
// RejectsIncompleteHeaders, CheckRedPayloadsRecursiveRed): the primary
// first, then the redundant blocks newest to oldest, with their red levels.
SM_TEST(Red, SplitRedPutsThePrimaryFirstAsNetEq) {
  RedEncoder enc(2);
  enc.Encode(111, 0, Frame(1, 20));
  enc.Encode(111, 960, Frame(2, 30));
  const auto pkt = enc.Encode(111, 1920, Frame(3, 40));
  const auto blocks = SplitRed(pkt, 1920, 63);
  SM_ASSERT_TRUE(blocks.has_value());
  SM_ASSERT_EQ(blocks->size(), 3u);
  SM_EXPECT_EQ((*blocks)[0].timestamp, 1920u);
  SM_EXPECT_EQ((*blocks)[0].red_level, 0);
  SM_EXPECT_TRUE((*blocks)[0].primary);
  SM_EXPECT_EQ((*blocks)[0].payload, Frame(3, 40));
  SM_EXPECT_EQ((*blocks)[1].timestamp, 960u);
  SM_EXPECT_EQ((*blocks)[1].red_level, 1);
  SM_EXPECT_EQ((*blocks)[1].payload, Frame(2, 30));
  SM_EXPECT_EQ((*blocks)[2].timestamp, 0u);
  SM_EXPECT_EQ((*blocks)[2].red_level, 2);
  SM_EXPECT_FALSE((*blocks)[2].primary);
  // One byte short of the second block: NetEq fails the packet.
  SM_EXPECT_FALSE(SplitRed(std::span(pkt).first(pkt.size() - 41), 1920, 63).has_value());
  // Headers cut short.
  SM_EXPECT_FALSE(SplitRed(std::span(pkt).first(3), 1920, 63).has_value());
  SM_EXPECT_FALSE(SplitRed(std::vector<uint8_t>{}, 0, 63).has_value());
}

SM_TEST(Red, SplitRedChecksPayloadTypesAndSkipsEmptyBlocks) {
  // A redundant block of another payload type (110) is removed, as is an
  // empty one; only the primary (111) is left.
  const std::vector<uint8_t> pkt{0x80 | 110, 0, 0x0c, 2,  // 3 samples back, 2 bytes.
                                 0x80 | 111, 0, 0x18, 0,  // 6 samples back, empty.
                                 111,        7, 7,    9, 9, 9};
  const auto blocks = SplitRed(pkt, 1000, 63);
  SM_ASSERT_TRUE(blocks.has_value());
  SM_ASSERT_EQ(blocks->size(), 1u);
  SM_EXPECT_EQ((*blocks)[0].timestamp, 1000u);
  SM_EXPECT_EQ((*blocks)[0].payload, (std::vector<uint8_t>{9, 9, 9}));
  // Nested RED (the RED payload type inside) is removed, leaving nothing.
  SM_EXPECT_FALSE(SplitRed(std::vector<uint8_t>{63, 1, 2}, 0, 63).has_value());
}

SM_TEST(Red, SplitForNetEqPassesEveryCopyAndCountsFramesOnce) {
  RedEncoder enc(1);
  RedReceiver rx;
  auto p0 = enc.Encode(111, 0, Frame(1, 10));
  auto p1 = enc.Encode(111, 960, Frame(2, 10));
  SM_EXPECT_EQ(rx.SplitForNetEq(p0, 0, 63).size(), 1u);
  const auto out = rx.SplitForNetEq(p1, 960, 63);
  SM_ASSERT_EQ(out.size(), 2u);  // Frame 1's copy too: NetEq's packet buffer drops it.
  SM_EXPECT_EQ(out[0].timestamp, 960u);
  SM_EXPECT_EQ(out[1].timestamp, 0u);
  SM_EXPECT_EQ(rx.stats().frames_out, 2u);
  SM_EXPECT_EQ(rx.stats().recovered_frames, 0u);
}
