#include "sm/timing/packet_buffer.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

namespace {
RtpPacket Pkt(uint16_t seq, uint32_t ts, bool marker) {
  RtpPacket p;
  p.seq = seq;
  p.timestamp = ts;
  p.marker = marker;
  p.payload = {static_cast<uint8_t>(seq)};
  return p;
}
Timestamp T0() { return Timestamp::Millis(0); }
}  // namespace

SM_TEST(PacketBuffer, AssemblesInOrderFrame) {
  PacketBuffer pb;
  pb.Insert(Pkt(10, 100, false), true, T0());
  pb.Insert(Pkt(11, 100, false), false, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(12, 100, true), false, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
  SM_EXPECT_EQ(f[0].packets[0].seq, 10);
  SM_EXPECT_EQ(f[0].packets[2].seq, 12);
  SM_EXPECT_EQ(pb.buffered_packets(), 0u);
}

SM_TEST(PacketBuffer, ReorderedPacketsCompleteFrame) {
  PacketBuffer pb;
  pb.Insert(Pkt(3, 1, true), false, T0());
  pb.Insert(Pkt(1, 1, false), true, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(2, 1, false), false, T0());  // Middle arrives last.
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].packets[1].seq, 2);
  SM_EXPECT_EQ(pb.stats().reordered, 2u);
}

SM_TEST(PacketBuffer, DuplicatesAndTooOldRejected) {
  PacketBufferConfig cfg;
  cfg.window = 100;
  PacketBuffer pb(cfg);
  SM_EXPECT_TRUE(pb.Insert(Pkt(5, 1, true), true, T0()) == InsertResult::kInserted);
  SM_EXPECT_TRUE(pb.Insert(Pkt(5, 1, true), true, T0()) == InsertResult::kDuplicate);  // After delivery.
  pb.Insert(Pkt(6, 2, false), true, T0());
  SM_EXPECT_TRUE(pb.Insert(Pkt(6, 2, false), true, T0()) == InsertResult::kDuplicate);  // While buffered.
  pb.Insert(Pkt(300, 3, true), true, T0());
  SM_EXPECT_TRUE(pb.Insert(Pkt(7, 2, true), false, T0()) == InsertResult::kTooOld);
  SM_EXPECT_EQ(pb.stats().duplicates, 2u);
  SM_EXPECT_EQ(pb.stats().too_old, 1u);
}

SM_TEST(PacketBuffer, FramesAcrossSequenceWrap) {
  PacketBuffer pb;
  pb.Insert(Pkt(65534, 7, false), true, T0());
  pb.Insert(Pkt(65535, 7, false), false, T0());
  pb.Insert(Pkt(0, 7, true), false, T0());
  pb.Insert(Pkt(1, 8, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 2u);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
  SM_EXPECT_EQ(f[1].packets.size(), 1u);
}

SM_TEST(PacketBuffer, MissingStartBlocksFrame) {
  PacketBuffer pb;
  pb.Insert(Pkt(2, 1, false), false, T0());
  pb.Insert(Pkt(3, 1, true), false, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  // A different timestamp at 1 means 2 cannot be a continuation of it.
  pb.Insert(Pkt(1, 0, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 0u);
  SM_EXPECT_EQ(CollectMetrics(pb)["buffered_packets"], 2.0);
}
