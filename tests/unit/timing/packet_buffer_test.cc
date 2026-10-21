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

SM_TEST(PacketBuffer, FrameWithSeveralStartPacketsStartsAtTheFirstOfThem) {
  // H.264: the parameter sets and the IDR slice both look like starts.
  PacketBuffer pb;
  pb.Insert(Pkt(1, 7, false), true, T0());   // SPS+PPS
  pb.Insert(Pkt(2, 7, false), true, T0());   // IDR start
  pb.Insert(Pkt(3, 7, true), false, T0());   // IDR end
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 1);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
}

SM_TEST(PacketBuffer, FirstPacketEverInsertedNeedsNoPredecessor) {
  PacketBuffer pb;
  pb.Insert(Pkt(2, 7, false), true, T0(), true);
  pb.Insert(Pkt(3, 7, true), false, T0());
  // Packet 1 never arrived, but 2 is the first packet this buffer has seen:
  // there is nothing before it to wait for.
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
}

SM_TEST(PacketBuffer, MarkerEndsTheWalkBackAtThePreviousFrame) {
  PacketBuffer pb;
  pb.Insert(Pkt(1, 7, true), true, T0());    // A one-packet frame.
  pb.Insert(Pkt(2, 8, false), true, T0());
  pb.Insert(Pkt(3, 8, true), false, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 2u);
  SM_EXPECT_EQ(f[1].first_seq, 2);
}

SM_TEST(PacketBuffer, PrecedingMediaSeqSkipsPadding) {
  PacketBuffer pb;
  pb.Insert(Pkt(1, 7, true), true, T0());
  pb.NotePadding(2);
  pb.NotePadding(3);
  SM_EXPECT_EQ(pb.PrecedingMediaSeq(4), 1);
  SM_EXPECT_EQ(pb.PrecedingMediaSeq(2), 1);
  SM_EXPECT_EQ(pb.PrecedingMediaSeq(1), 0);
}

SM_TEST(PacketBuffer, StartPacketWaitsForAMissingPredecessor) {
  // The IDR packets of a frame arrive before its parameter-set packet.
  PacketBuffer pb;
  pb.Insert(Pkt(1, 6, true), true, T0());   // The previous frame, delivered.
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(3, 7, false), true, T0(), true);  // IDR start (also looks like a start).
  pb.Insert(Pkt(4, 7, true), false, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());  // Packet 2 may still come.
  pb.Insert(Pkt(2, 7, false), true, T0());  // SPS+PPS, late or recovered by NACK.
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 2);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
}

SM_TEST(PacketBuffer, FrameWithoutItsLostFirstPacketIsNeverDelivered) {
  PacketBuffer pb;
  pb.Insert(Pkt(1, 6, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(3, 7, false), true, T0(), true);
  pb.Insert(Pkt(4, 7, true), false, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  // The next frame is unaffected: packet 4 ended frame 7, so 5 is a real start.
  pb.Insert(Pkt(5, 8, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 8u);
}

SM_TEST(PacketBuffer, StartAfterPaddingIsARealStart) {
  PacketBuffer pb;
  pb.Insert(Pkt(1, 6, true), true, T0());
  pb.NotePadding(2);
  pb.Insert(Pkt(3, 7, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 2u);
}

SM_TEST(PacketBuffer, AnUnambiguousStartDoesNotWaitForAMissingPredecessor) {
  // A frame whose predecessor was lost whole still assembles: it opens with a
  // packet that can only be a frame's first, and the loss is NACK's to repair.
  PacketBuffer pb;
  pb.Insert(Pkt(1, 6, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(5, 8, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
}

// libwebrtc PacketBuffer (#76): starts at 512 slots, doubles up to 2048 when a
// slot is taken by another sequence number, and clears at the maximum.
SM_TEST(PacketBuffer, GrowsThenClearsWhenFull) {
  PacketBuffer pb;
  SM_EXPECT_EQ(pb.capacity(), 512u);
  // Frame start missing, so nothing assembles and the packets stay held.
  pb.Insert(Pkt(0, 1, false), false, T0());
  SM_EXPECT_EQ(pb.Insert(Pkt(512, 2, false), false, T0()), InsertResult::kInserted);
  SM_EXPECT_EQ(pb.capacity(), 1024u);
  SM_EXPECT_EQ(pb.Insert(Pkt(1024, 3, false), false, T0()), InsertResult::kInserted);
  SM_EXPECT_EQ(pb.capacity(), 2048u);
  SM_EXPECT_EQ(pb.Insert(Pkt(1536, 4, false), false, T0()), InsertResult::kInserted);
  SM_EXPECT_EQ(pb.Insert(Pkt(2048, 5, false), false, T0()), InsertResult::kBufferCleared);
  SM_EXPECT_EQ(pb.buffered_packets(), 0u);
  SM_EXPECT_EQ(pb.capacity(), 512u);
  SM_EXPECT_EQ(pb.stats().cleared, 1u);
}

SM_TEST(PacketBuffer, RestartsOnLargeBackwardJump) {
  PacketBuffer pb;
  pb.Insert(Pkt(5000, 1, false), false, T0());
  pb.Insert(Pkt(5001, 1, false), false, T0());
  SM_EXPECT_EQ(pb.buffered_packets(), 2u);
  // 1024 or more behind the lowest sequence number: the buffer restarts.
  pb.Insert(Pkt(3900, 9, false), false, T0());
  SM_EXPECT_EQ(pb.buffered_packets(), 1u);
}

// PacketBuffer::ClearTo (#101): after the decoder consumed a frame the packets
// up to its last one are dropped and a late copy of an older packet is ignored,
// not mistaken for a backward jump.
SM_TEST(PacketBuffer, ClearToDropsOldPacketsAndIgnoresLateOnes) {
  PacketBuffer pb;
  pb.Insert(Pkt(10, 100, true), true, T0());  // A complete one-packet frame.
  SM_ASSERT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(12, 200, false), false, T0());  // Waits for 11 / a start; stays held.
  SM_EXPECT_EQ(pb.buffered_packets(), 1u);
  pb.ClearTo(12);
  SM_EXPECT_EQ(pb.buffered_packets(), 0u);
  // Behind the cleared-to point: silently ignored even though it is 'a large jump back' (it is not).
  SM_EXPECT_EQ(pb.Insert(Pkt(11, 200, false), false, T0()), InsertResult::kTooOld);
  SM_EXPECT_EQ(pb.buffered_packets(), 0u);
  // Ahead of it still works.
  SM_EXPECT_EQ(pb.Insert(Pkt(13, 300, true), true, T0()), InsertResult::kInserted);
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
}

// received_padding_ is dropped once it falls more than a quarter of the buffer behind
// the packets being inserted, and padding inside a found frame is dropped with it.
SM_TEST(PacketBuffer, PaddingOnlyMattersNearTheInsertedPackets) {
  PacketBuffer pb;  // 512 slots: padding more than 128 behind goes.
  pb.Insert(Pkt(1000, 1, true), true, T0());
  pb.PopCompleteFrames();
  pb.NotePadding(1001);
  SM_EXPECT_EQ(pb.PrecedingMediaSeq(1002), 1000);
  pb.Insert(Pkt(1002, 2, true), true, T0());
  pb.PopCompleteFrames();
  SM_EXPECT_EQ(pb.PrecedingMediaSeq(1002), 1000);  // Still known near the front.
  pb.Insert(Pkt(1200, 9, false), false, T0());     // 198 ahead: 1001 is outside size / 4.
  SM_EXPECT_EQ(pb.PrecedingMediaSeq(1002), 1001);
}
