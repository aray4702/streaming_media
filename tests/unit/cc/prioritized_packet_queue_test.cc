// Ports of libwebrtc's modules/pacing/prioritized_packet_queue_unittest.cc (M157,
// compare/webrtc/reference/) to PrioritizedPacketQueue, the pacer v2 queue (#166).
// Each case is named after the one it follows. libwebrtc's sequence numbers are
// the packets' ids here; its SSRCs map to our stream kinds (one stream of each
// kind in a one-to-one call).
//
// Not ported, because the defaults the pacer runs with leave the code path off:
//   - PrioritizeAudioRetransmissionBeforeVideoRetransmissionIfConfigured and
//     ReportsLeadingPacketEnqueueTimeForRetransmission:
//     prioritize_audio_retransmission is false by default
//     (PacingController::Configuration), and LeadingPacketEnqueueTimeForRetransmission
//     is read only with fast_retransmissions, also off;
//   - PacketsDroppedIfNotPulledWithinTttl, DontSendPacketsAfterTttl,
//     SendsNewVideoPacketAfterPurgingLastOldRtxPacket and
//     SendsPacketsAfterTttlIfPrioHigherThanPushedPackets: PacketQueueTTL is
//     infinite on every level by default;
//   - ReturnsEqualPrioPacketsInRoundRobinOrder: round robin between the SSRCs of
//     one level (simulcast); a one-to-one call has one stream per level.
#include <optional>
#include <vector>

#include "sm/cc/pacing_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {

constexpr int64_t kDefaultPayloadSize = 789;

PacedPacket CreatePacket(PacketPriority type, uint64_t seq, bool is_key_frame = false) {
  PacedPacket p;
  p.id = seq;
  p.priority = type;
  p.size = DataSize::Bytes(kDefaultPayloadSize);
  p.is_key_frame = is_key_frame;
  p.is_padding = type == PacketPriority::kPadding;
  return p;
}

uint64_t PopId(PrioritizedPacketQueue& queue) {
  std::optional<PacedPacket> p = queue.Pop();
  return p ? p->id : ~uint64_t{0};
}

}  // namespace

SM_TEST(PrioritizedPacketQueue, ReturnsPacketsInPrioritizedOrder) {
  const Timestamp now = Timestamp::Millis(0);
  PrioritizedPacketQueue queue;

  // Add packets in low to high packet order.
  queue.Push(now, CreatePacket(PacketPriority::kPadding, 1));
  queue.Push(now, CreatePacket(PacketPriority::kVideo, 2));
  queue.Push(now, CreatePacket(PacketPriority::kFec, 3));
  queue.Push(now, CreatePacket(PacketPriority::kRetransmission, 4));  // Of video.
  queue.Push(now, CreatePacket(PacketPriority::kRetransmission, 5));  // Of audio.
  queue.Push(now, CreatePacket(PacketPriority::kAudio, 6));

  // Packets are returned in high to low order.
  SM_EXPECT_EQ(PopId(queue), 6u);
  // Audio and video retransmission have the same priority; video was enqueued first.
  SM_EXPECT_EQ(PopId(queue), 4u);
  SM_EXPECT_EQ(PopId(queue), 5u);
  // Video and FEC are prioritized equally; video was enqueued first.
  SM_EXPECT_EQ(PopId(queue), 2u);
  SM_EXPECT_EQ(PopId(queue), 3u);
  SM_EXPECT_EQ(PopId(queue), 1u);
  SM_EXPECT_TRUE(!queue.Pop().has_value());
}

SM_TEST(PrioritizedPacketQueue, ReportsSizeInPackets) {
  PrioritizedPacketQueue queue;
  SM_EXPECT_EQ(queue.SizeInPackets(), 0u);

  queue.Push(Timestamp::Millis(0), CreatePacket(PacketPriority::kVideo, 1));
  SM_EXPECT_EQ(queue.SizeInPackets(), 1u);

  queue.Pop();
  SM_EXPECT_EQ(queue.SizeInPackets(), 0u);
}

// ReportsPayloadSize: our sizes are wire sizes (see the class comment).
SM_TEST(PrioritizedPacketQueue, ReportsPayloadSize) {
  PrioritizedPacketQueue queue;
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), 0);

  queue.Push(Timestamp::Millis(0), CreatePacket(PacketPriority::kVideo, 1));
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), kDefaultPayloadSize);

  queue.Pop();
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), 0);
}

SM_TEST(PrioritizedPacketQueue, ReportsPaddingSize) {
  PrioritizedPacketQueue queue;
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), 0);
  constexpr int64_t kPaddingSize = 190;

  PacedPacket packet = CreatePacket(PacketPriority::kPadding, 1);
  packet.size = DataSize::Bytes(kPaddingSize);
  queue.Push(Timestamp::Millis(0), packet);
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), kPaddingSize);

  queue.Pop();
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), 0);
}

SM_TEST(PrioritizedPacketQueue, ReportsOldestEnqueueTime) {
  PrioritizedPacketQueue queue;
  queue.UpdateAverageQueueTime(Timestamp::Millis(0));  // creation_time
  SM_EXPECT_TRUE(queue.OldestEnqueueTime() == Timestamp::MinusInfinity());

  // Add three packets, with the middle packet having higher prio.
  queue.Push(Timestamp::Millis(10), CreatePacket(PacketPriority::kPadding, 1));
  queue.Push(Timestamp::Millis(20), CreatePacket(PacketPriority::kVideo, 2));
  queue.Push(Timestamp::Millis(30), CreatePacket(PacketPriority::kPadding, 3));
  SM_EXPECT_EQ(queue.OldestEnqueueTime().ms(), 10);

  queue.Pop();  // Pop packet with enqueue time 20.
  SM_EXPECT_EQ(queue.OldestEnqueueTime().ms(), 10);

  queue.Pop();  // Pop packet with enqueue time 10.
  SM_EXPECT_EQ(queue.OldestEnqueueTime().ms(), 30);

  queue.Pop();  // Pop packet with enqueue time 30, queue empty again.
  SM_EXPECT_TRUE(queue.OldestEnqueueTime() == Timestamp::MinusInfinity());
}

SM_TEST(PrioritizedPacketQueue, ReportsAverageQueueTime) {
  PrioritizedPacketQueue queue;
  queue.UpdateAverageQueueTime(Timestamp::Millis(0));  // creation_time
  SM_EXPECT_EQ(queue.AverageQueueTime().us(), 0);

  // Add three packets, with the middle packet having higher prio.
  queue.Push(Timestamp::Millis(10), CreatePacket(PacketPriority::kPadding, 1));
  queue.Push(Timestamp::Millis(20), CreatePacket(PacketPriority::kVideo, 2));
  queue.Push(Timestamp::Millis(30), CreatePacket(PacketPriority::kPadding, 3));

  queue.UpdateAverageQueueTime(Timestamp::Millis(40));
  // Packets have waited 30, 20, 10 ms -> average = 20 ms.
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 20);

  queue.Pop();  // Pop packet with enqueue time 20.
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 20);

  queue.Pop();  // Pop packet with enqueue time 10.
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 10);

  queue.Pop();  // Pop packet with enqueue time 30, queue empty again.
  SM_EXPECT_EQ(queue.AverageQueueTime().us(), 0);
}

SM_TEST(PrioritizedPacketQueue, SubtractsPausedTimeFromAverageQueueTime) {
  PrioritizedPacketQueue queue;
  queue.UpdateAverageQueueTime(Timestamp::Millis(0));  // creation_time
  SM_EXPECT_EQ(queue.AverageQueueTime().us(), 0);

  // Add a packet and then enable paused state.
  queue.Push(Timestamp::Millis(100), CreatePacket(PacketPriority::kPadding, 1));
  queue.SetPauseState(true, Timestamp::Millis(600));
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 500);

  // Enqueue a packet 500 ms into the paused state. The queue time of the
  // original packet is still 500 ms and the new one has 0 ms: 250 ms average.
  queue.Push(Timestamp::Millis(1100), CreatePacket(PacketPriority::kVideo, 2));
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 250);

  // Unpause some time later, queue time still unchanged.
  queue.SetPauseState(false, Timestamp::Millis(1600));
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 250);

  // Update queue time 500 ms after the pause ended.
  queue.UpdateAverageQueueTime(Timestamp::Millis(2100));
  SM_EXPECT_EQ(queue.AverageQueueTime().ms(), 750);
}

SM_TEST(PrioritizedPacketQueue, ReportsLeadingPacketEnqueueTime) {
  PrioritizedPacketQueue queue;
  queue.UpdateAverageQueueTime(Timestamp::Millis(0));  // creation_time
  SM_EXPECT_TRUE(queue.LeadingPacketEnqueueTime(PacketPriority::kAudio) == Timestamp::MinusInfinity());
  SM_EXPECT_TRUE(queue.LeadingPacketEnqueueTime(PacketPriority::kVideo) == Timestamp::MinusInfinity());

  queue.Push(Timestamp::Millis(10), CreatePacket(PacketPriority::kVideo, 1));
  SM_EXPECT_TRUE(queue.LeadingPacketEnqueueTime(PacketPriority::kAudio) == Timestamp::MinusInfinity());
  SM_EXPECT_EQ(queue.LeadingPacketEnqueueTime(PacketPriority::kVideo).ms(), 10);

  queue.Push(Timestamp::Millis(20), CreatePacket(PacketPriority::kAudio, 2));
  SM_EXPECT_EQ(queue.LeadingPacketEnqueueTime(PacketPriority::kAudio).ms(), 20);
  SM_EXPECT_EQ(queue.LeadingPacketEnqueueTime(PacketPriority::kVideo).ms(), 10);

  queue.Pop();  // Pop audio packet.
  SM_EXPECT_TRUE(queue.LeadingPacketEnqueueTime(PacketPriority::kAudio) == Timestamp::MinusInfinity());
  SM_EXPECT_EQ(queue.LeadingPacketEnqueueTime(PacketPriority::kVideo).ms(), 10);

  queue.Pop();  // Pop video packet.
  SM_EXPECT_TRUE(queue.LeadingPacketEnqueueTime(PacketPriority::kAudio) == Timestamp::MinusInfinity());
  SM_EXPECT_TRUE(queue.LeadingPacketEnqueueTime(PacketPriority::kVideo) == Timestamp::MinusInfinity());
}

SM_TEST(PrioritizedPacketQueue, PushAndPopUpdatesSizeInPacketsPerRtpPacketMediaType) {
  const Timestamp now = Timestamp::Millis(0);
  PrioritizedPacketQueue queue;
  const auto count = [&](PacketPriority p) { return queue.SizeInPacketsPerPriority()[static_cast<size_t>(p)]; };

  // Initially all sizes are zero.
  for (size_t n : queue.SizeInPacketsPerPriority()) SM_EXPECT_EQ(n, 0u);

  queue.Push(now, CreatePacket(PacketPriority::kAudio, 1));
  SM_EXPECT_EQ(count(PacketPriority::kAudio), 1u);
  queue.Push(now, CreatePacket(PacketPriority::kVideo, 2));
  SM_EXPECT_EQ(count(PacketPriority::kVideo), 1u);
  queue.Push(now, CreatePacket(PacketPriority::kRetransmission, 3));
  SM_EXPECT_EQ(count(PacketPriority::kRetransmission), 1u);
  queue.Push(now, CreatePacket(PacketPriority::kFec, 4));
  SM_EXPECT_EQ(count(PacketPriority::kFec), 1u);
  queue.Push(now, CreatePacket(PacketPriority::kPadding, 5));
  SM_EXPECT_EQ(count(PacketPriority::kPadding), 1u);

  // Now all sizes are 1.
  for (size_t n : queue.SizeInPacketsPerPriority()) SM_EXPECT_EQ(n, 1u);

  // Each pop decrements the counter of the popped packet's type.
  for (size_t i = 0; i < queue.SizeInPacketsPerPriority().size(); ++i) {
    std::optional<PacedPacket> popped = queue.Pop();
    SM_ASSERT_TRUE(popped.has_value());
    SM_EXPECT_EQ(count(popped->priority), 0u);
  }

  // All packets popped: all sizes are zero.
  for (size_t n : queue.SizeInPacketsPerPriority()) SM_EXPECT_EQ(n, 0u);
}

// ClearsPackets: RemovePacketsForSsrc on the video stream (and its RTX stream,
// as PacingController::EnqueuePacket does on a keyframe).
SM_TEST(PrioritizedPacketQueue, ClearsPackets) {
  const Timestamp now = Timestamp::Millis(0);
  PrioritizedPacketQueue queue;

  // Two packets of each video-stream type.
  uint64_t seq = 0;
  for (PacketPriority p : {PacketPriority::kRetransmission, PacketPriority::kVideo, PacketPriority::kFec}) {
    queue.Push(now, CreatePacket(p, seq++));
    queue.Push(now, CreatePacket(p, seq++));
  }
  SM_EXPECT_EQ(queue.SizeInPackets(), 6u);

  // Remove all of them.
  SM_EXPECT_EQ(queue.RemoveVideoStreamPackets().size(), 6u);
  SM_EXPECT_TRUE(queue.Empty());
  SM_EXPECT_EQ(queue.SizeInBytes().bytes(), 0);
  SM_EXPECT_EQ(queue.AverageQueueTime().us(), 0);

  // No more packets are removed when called again.
  SM_EXPECT_TRUE(queue.RemoveVideoStreamPackets().empty());
  SM_EXPECT_TRUE(queue.Empty());
}

// ClearPacketsAffectsOnlySpecifiedSsrc: the audio stream stays.
SM_TEST(PrioritizedPacketQueue, ClearPacketsAffectsOnlySpecifiedSsrc) {
  const Timestamp now = Timestamp::Millis(0);
  PrioritizedPacketQueue queue;

  // A video packet and a retransmission of the stream being removed, first in line.
  queue.Push(now, CreatePacket(PacketPriority::kVideo, 1));
  queue.Push(now, CreatePacket(PacketPriority::kRetransmission, 2));
  // An audio packet and a padding packet, which are other streams.
  queue.Push(now, CreatePacket(PacketPriority::kAudio, 3));
  queue.Push(now, CreatePacket(PacketPriority::kPadding, 4));
  SM_EXPECT_EQ(queue.SizeInPackets(), 4u);

  // libwebrtc returns the removed packets level by level: retransmission, then video.
  std::vector<PacedPacket> removed = queue.RemoveVideoStreamPackets();
  SM_ASSERT_EQ(removed.size(), 2u);
  SM_EXPECT_EQ(removed[0].id, 2u);
  SM_EXPECT_EQ(removed[1].id, 1u);
  SM_EXPECT_EQ(queue.SizeInPackets(), 2u);

  // The audio packet first, then the padding.
  SM_EXPECT_EQ(PopId(queue), 3u);
  SM_EXPECT_EQ(PopId(queue), 4u);
  SM_EXPECT_TRUE(queue.Empty());
}

// With include_padding the flush also takes the padding level: libwebrtc's padding goes out on the
// video stream's RTX SSRC, which EnqueuePacket clears with RemovePacketsForSsrc(rtx_ssrc) (#437).
SM_TEST(PrioritizedPacketQueue, ClearPacketsIncludesPaddingOnRequest) {
  const Timestamp now = Timestamp::Millis(0);
  PrioritizedPacketQueue queue;
  queue.Push(now, CreatePacket(PacketPriority::kPadding, 1));
  queue.Push(now, CreatePacket(PacketPriority::kVideo, 2));
  queue.Push(now, CreatePacket(PacketPriority::kAudio, 3));
  queue.Push(now, CreatePacket(PacketPriority::kRetransmission, 4));

  std::vector<PacedPacket> removed = queue.RemoveVideoStreamPackets(/*include_padding=*/true);
  SM_ASSERT_EQ(removed.size(), 3u);
  SM_EXPECT_EQ(removed[0].id, 4u);  // Level by level: retransmission, video, padding.
  SM_EXPECT_EQ(removed[1].id, 2u);
  SM_EXPECT_EQ(removed[2].id, 1u);
  SM_EXPECT_EQ(PopId(queue), 3u);  // Audio stays.
  SM_EXPECT_TRUE(queue.Empty());
}

// ReportsKeyframePackets, for the one video stream.
SM_TEST(PrioritizedPacketQueue, ReportsKeyframePackets) {
  const Timestamp now = Timestamp::Millis(0);
  PrioritizedPacketQueue queue;
  SM_EXPECT_TRUE(!queue.HasKeyframePackets());

  queue.Push(now, CreatePacket(PacketPriority::kVideo, 1, /*is_key_frame=*/true));
  queue.Push(now, CreatePacket(PacketPriority::kVideo, 2, /*is_key_frame=*/true));
  queue.Push(now, CreatePacket(PacketPriority::kVideo, 3, /*is_key_frame=*/false));
  SM_EXPECT_TRUE(queue.HasKeyframePackets());

  SM_EXPECT_EQ(PopId(queue), 1u);
  SM_EXPECT_TRUE(queue.HasKeyframePackets());
  SM_EXPECT_EQ(PopId(queue), 2u);
  SM_EXPECT_TRUE(!queue.HasKeyframePackets());  // Only the delta packet is left.

  queue.Push(now, CreatePacket(PacketPriority::kVideo, 4, /*is_key_frame=*/true));
  SM_EXPECT_TRUE(queue.HasKeyframePackets());
  queue.RemoveVideoStreamPackets();
  SM_EXPECT_TRUE(!queue.HasKeyframePackets());
}
