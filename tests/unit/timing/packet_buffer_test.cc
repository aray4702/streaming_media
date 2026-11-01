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
// A codec without H.264's `missing_packets_` hold (on by default since #580).
PacketBufferConfig NoHoldCfg() {
  PacketBufferConfig cfg;
  cfg.missing_packets = false;
  return cfg;
}
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
  pb.Insert(Pkt(6, 2, false), true, T0());
  SM_EXPECT_TRUE(pb.Insert(Pkt(6, 2, false), true, T0()) == InsertResult::kDuplicate);  // While buffered.
  pb.Insert(Pkt(300, 3, true), true, T0());
  SM_EXPECT_TRUE(pb.Insert(Pkt(7, 2, true), false, T0()) == InsertResult::kTooOld);
  SM_EXPECT_EQ(pb.stats().duplicates, 1u);
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
  pb.Insert(Pkt(2, 7, false), true, T0());
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

SM_TEST(PacketBuffer, TwoFramesWithOneTimestampGoOutAsOne) {
  // libwebrtc's H.264 walk back in FindFrames checks only the RTP timestamp, not
  // the marker bit (packet_buffer.cc), so a packet with the marker bit and the
  // frame after it at the same timestamp are handed out together (issue #899).
  PacketBuffer pb(NoHoldCfg());
  pb.Insert(Pkt(12, 7, true), false, T0());  // Not a start: it waits.
  pb.Insert(Pkt(13, 7, false), true, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(14, 7, true), false, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 12);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
  SM_EXPECT_EQ(pb.buffered_packets(), 0u);
}

SM_TEST(PacketBuffer, PacketThatIsNotContinuousDoesNotCompleteAFrame) {
  // PotentialNewFrame: a packet that is not a start continues a frame only
  // after a continuous packet of the same timestamp. 21 follows 20, which no
  // walk reached (its start, 19, is missing), so the marker at 21 waits.
  PacketBuffer pb(NoHoldCfg());
  pb.Insert(Pkt(10, 5, true), true, T0());
  pb.Insert(Pkt(20, 7, false), false, T0());
  pb.Insert(Pkt(21, 7, true), false, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);  // Only frame 5.
  pb.Insert(Pkt(19, 7, false), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 19);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
}

SM_TEST(PacketBuffer, ForwardWalkStopsWherePaddingIsNoLongerRemembered) {
  // FindFrames skips only the padding still in received_padding_; an older
  // padding sequence number is an empty slot, and the walk stops there, as it
  // stops at a packet already handed out in a frame (issue #899). Frame 300
  // (seq 13) is held behind hole 11; the padding at 12 is forgotten once seq
  // 200 is inserted (more than a quarter of the buffer ahead), so filling 11
  // releases frame 150 and the walk ends at 12.
  PacketBuffer pb;
  pb.Insert(Pkt(10, 100, true), true, T0(), true);  // Keyframe.
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.NotePadding(12);
  pb.Insert(Pkt(13, 300, true), true, T0());
  pb.Insert(Pkt(200, 900, true), true, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(11, 150, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 150u);
  SM_EXPECT_EQ(pb.buffered_packets(), 2u);
}

SM_TEST(PacketBuffer, ForwardWalkSkipsRememberedPaddingToAHeldFrame) {
  // The same with the padding still remembered: the walk goes over it and
  // releases frame 300.
  PacketBuffer pb;
  pb.Insert(Pkt(10, 100, true), true, T0(), true);
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.NotePadding(12);
  pb.Insert(Pkt(13, 300, true), true, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(11, 150, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 2u);
  SM_EXPECT_EQ(f[1].timestamp, 300u);
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

// libwebrtc's PacketBuffer::FindFrames for H.264 without a generic
// descriptor (packet_buffer.cc, the default H.264 path: H26xPacketBuffer is
// behind the WebRTC-Video-H26xPacketBuffer trial, off in M157): the IDR start
// is `is_first_packet_in_frame`, so its packets are continuous, and the walk
// back stops at the missing parameter-set packet. The frame is handed out from
// the IDR start; the late parameter-set packet that follows makes no frame.
SM_TEST(PacketBuffer, IdrStartOpensTheFrameWhenItsParameterSetPacketIsMissing) {
  PacketBuffer pb;
  pb.Insert(Pkt(1, 6, true), true, T0());   // The previous frame, delivered.
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(3, 7, false), true, T0(), true);  // IDR start.
  pb.Insert(Pkt(4, 7, true), false, T0(), true);  // IDR end.
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 3);
  SM_EXPECT_EQ(f[0].packets.size(), 2u);
  pb.Insert(Pkt(2, 7, false), true, T0());  // SPS+PPS, late or recovered by NACK.
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  // The next frame is unaffected.
  pb.Insert(Pkt(5, 8, true), true, T0());
  f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 8u);
}

// The same frame when the parameter-set packet arrives before the IDR's last
// packet: the walk back from the marker reaches it, and the frame starts there.
SM_TEST(PacketBuffer, ParameterSetPacketArrivingBeforeTheMarkerJoinsTheFrame) {
  PacketBuffer pb;
  pb.Insert(Pkt(1, 6, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(3, 7, false), true, T0(), true);  // IDR start.
  pb.Insert(Pkt(2, 7, false), true, T0());        // SPS+PPS.
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(4, 7, true), false, T0(), true);
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 2);
  SM_EXPECT_EQ(f[0].packets.size(), 3u);
}

// A packet that is not a start opens nothing: a delta frame whose first FU-A
// fragment is missing stays in the buffer (PotentialNewFrame is false without a
// continuous predecessor) until the fragment arrives.
SM_TEST(PacketBuffer, FrameWithoutItsFirstFragmentWaitsForIt) {
  PacketBuffer pb(NoHoldCfg());
  pb.Insert(Pkt(1, 6, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(3, 7, false), false, T0());
  pb.Insert(Pkt(4, 7, true), false, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(2, 7, false), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 2);
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
  PacketBuffer pb(NoHoldCfg());
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

namespace {
PacketBufferConfig MissingPacketsCfg() {
  PacketBufferConfig cfg;
  cfg.missing_packets = true;
  return cfg;
}
}  // namespace

SM_TEST(PacketBuffer, MissingPacketsHoldsADeltaFrameBehindAHoleUntilItIsFilled) {
  // libwebrtc's FindFrames: a complete H.264 delta frame is not handed out while an
  // earlier sequence number is missing; the packet that fills the hole releases both.
  PacketBuffer pb(MissingPacketsCfg());
  pb.Insert(Pkt(10, 100, true), true, T0(), true);  // Keyframe.
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.Insert(Pkt(12, 200, true), true, T0());  // Frame 200 (seq 11) is missing.
  pb.Insert(Pkt(13, 300, true), true, T0());
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  SM_EXPECT_EQ(pb.stats().frames_held, 2u);
  pb.Insert(Pkt(11, 150, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 3u);
  SM_EXPECT_EQ(f[0].timestamp, 150u);
  SM_EXPECT_EQ(f[1].timestamp, 200u);
  SM_EXPECT_EQ(f[2].timestamp, 300u);
}

SM_TEST(PacketBuffer, MissingPacketsOffHandsOutTheDeltaFrameAtOnce) {
  PacketBuffer pb(NoHoldCfg());
  pb.Insert(Pkt(10, 100, true), true, T0(), true);
  pb.Insert(Pkt(12, 200, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 2u);
  SM_EXPECT_EQ(pb.stats().frames_held, 0u);
}

SM_TEST(PacketBuffer, MissingPacketsReleasesAKeyframeAndForgetsTheHolesBeforeIt) {
  // The first packet of an IDR slice was dropped (or lost): the deltas after it wait, and the
  // next keyframe leaves at once and clears the holes up to its last packet.
  PacketBuffer pb(MissingPacketsCfg());
  pb.Insert(Pkt(1, 100, false), true, T0(), false);  // SPS+PPS; seq 2 (IDR start) never arrives.
  pb.Insert(Pkt(3, 100, true), false, T0());
  pb.Insert(Pkt(4, 200, true), true, T0());  // Delta.
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(5, 300, true), true, T0(), true);  // Keyframe.
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 300u);
  pb.Insert(Pkt(6, 400, true), true, T0());  // The delta after the keyframe is not held.
  f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 400u);
}

SM_TEST(PacketBuffer, MissingPacketsPaddingFillsAHole) {
  PacketBuffer pb(MissingPacketsCfg());
  pb.Insert(Pkt(10, 100, true), true, T0(), true);
  pb.Insert(Pkt(12, 200, true), true, T0());
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.NotePadding(11);  // InsertPadding: UpdateMissingPackets, then FindFrames(seq + 1).
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 200u);
}

SM_TEST(PacketBuffer, MissingPacketsForgetsHolesOlderThan1000) {
  // UpdateMissingPackets' kMaxPaddingAge: a hole 1000 or more behind the newest packet no longer
  // holds frames. FindFrames only walks forward from the packet inserted, so the frames held
  // before that stay held (they leave with ClearTo after the next keyframe).
  PacketBuffer pb(MissingPacketsCfg());
  pb.Insert(Pkt(10, 100, true), true, T0(), true);
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  for (uint16_t seq = 12; seq < 1012; ++seq) pb.Insert(Pkt(seq, seq * 10u, true), true, T0());  // Seq 11 is lost.
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  pb.Insert(Pkt(1012, 10120, true), true, T0());  // 11 is now 1001 behind.
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 10120u);
}

SM_TEST(PacketBuffer, MissingPacketsClearToForgetsHolesBelowIt) {
  PacketBuffer pb(MissingPacketsCfg());
  pb.Insert(Pkt(10, 100, true), true, T0(), true);
  pb.Insert(Pkt(14, 300, true), true, T0());  // Holes 11-13: held.
  SM_EXPECT_EQ(pb.PopCompleteFrames().size(), 1u);
  pb.ClearTo(13);
  pb.Insert(Pkt(15, 400, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].timestamp, 400u);
}

// Issue #899, PacketBuffer::InsertPacket (packet_buffer.cc): a copy is a
// duplicate only while its packet is in the buffer (`buffer_[index]->seq_num()
// == seq_num`). FindFrames moves a found frame's packets out, so the slot is
// empty and a later copy (a spurious RTX, a duplicating link) is taken again:
// a one-packet frame is handed out a second time (the frame buffer drops it).
SM_TEST(PacketBuffer, CopyOfAOnePacketFrameAfterAssemblyIsAssembledAgain) {
  PacketBuffer pb(NoHoldCfg());
  SM_EXPECT_EQ(pb.Insert(Pkt(5, 100, true), true, T0()), InsertResult::kInserted);
  SM_ASSERT_EQ(pb.PopCompleteFrames().size(), 1u);
  SM_EXPECT_EQ(pb.Insert(Pkt(5, 100, true), true, T0()), InsertResult::kInserted);
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, f[0].last_seq);
  SM_EXPECT_EQ(f[0].timestamp, 100u);
  SM_EXPECT_EQ(pb.stats().duplicates, 0u);
  SM_EXPECT_EQ(pb.stats().reinserted, 1u);
  SM_EXPECT_EQ(pb.stats().frames_assembled, 2u);
}

// A copy of one packet of an assembled multi-packet frame has no continuous
// predecessor (PotentialNewFrame: the previous slot is empty), so it makes no
// frame and stays in the buffer until ClearTo drops it, as in libwebrtc. While
// it is buffered, a second copy is the duplicate.
SM_TEST(PacketBuffer, CopyOfAPacketOfAnAssembledFrameStaysUntilClearTo) {
  PacketBuffer pb(NoHoldCfg());
  pb.Insert(Pkt(10, 100, false), true, T0());
  pb.Insert(Pkt(11, 100, false), false, T0());
  pb.Insert(Pkt(12, 100, true), false, T0());
  SM_ASSERT_EQ(pb.PopCompleteFrames().size(), 1u);
  SM_EXPECT_EQ(pb.Insert(Pkt(11, 100, false), false, T0()), InsertResult::kInserted);
  SM_EXPECT_EQ(pb.Insert(Pkt(12, 100, true), false, T0()), InsertResult::kInserted);
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  SM_EXPECT_EQ(pb.buffered_packets(), 2u);
  SM_EXPECT_EQ(pb.Insert(Pkt(12, 100, true), false, T0()), InsertResult::kDuplicate);
  SM_EXPECT_EQ(pb.stats().reinserted, 2u);
  SM_EXPECT_EQ(pb.stats().duplicates, 1u);
  // The next frame is not affected by the copies behind it.
  pb.Insert(Pkt(13, 200, true), true, T0());
  auto f = pb.PopCompleteFrames();
  SM_ASSERT_EQ(f.size(), 1u);
  SM_EXPECT_EQ(f[0].first_seq, 13);
  pb.ClearTo(13);
  SM_EXPECT_EQ(pb.buffered_packets(), 0u);
  // Behind the cleared-to point a copy is old.
  SM_EXPECT_EQ(pb.Insert(Pkt(10, 100, false), true, T0()), InsertResult::kTooOld);
}

// The copy of a frame's first packet does continue a frame when the rest is
// copied too: every packet of the frame taken again in order makes it again.
SM_TEST(PacketBuffer, CopiesOfAWholeFrameAfterAssemblyAssembleItAgain) {
  PacketBuffer pb(NoHoldCfg());
  for (int pass = 0; pass < 2; ++pass) {
    pb.Insert(Pkt(20, 300, false), true, T0());
    pb.Insert(Pkt(21, 300, true), false, T0());
    auto f = pb.PopCompleteFrames();
    SM_ASSERT_EQ(f.size(), 1u);
    SM_EXPECT_EQ(f[0].first_seq, 20);
    SM_EXPECT_EQ(f[0].last_seq, 21);
  }
  SM_EXPECT_EQ(pb.stats().reinserted, 2u);
}

// `drop_copies_after_assembly` (timing v1 only, a recorded deviation): a copy
// of a packet already handed out is a duplicate, so a frame is never handed
// out twice to a receiver without a frame buffer.
SM_TEST(PacketBuffer, DropCopiesAfterAssemblyRejectsACopyOfAHandedOutPacket) {
  PacketBufferConfig cfg = NoHoldCfg();
  cfg.drop_copies_after_assembly = true;
  PacketBuffer pb(cfg);
  SM_EXPECT_EQ(pb.Insert(Pkt(5, 100, true), true, T0()), InsertResult::kInserted);
  SM_ASSERT_EQ(pb.PopCompleteFrames().size(), 1u);
  SM_EXPECT_EQ(pb.Insert(Pkt(5, 100, true), true, T0()), InsertResult::kDuplicate);
  SM_EXPECT_TRUE(pb.PopCompleteFrames().empty());
  SM_EXPECT_EQ(pb.stats().duplicates, 1u);
  SM_EXPECT_EQ(pb.stats().reinserted, 0u);
}
