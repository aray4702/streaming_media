#include <algorithm>
#include <optional>
#include <vector>

#include "sm/rtp/send_side_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
SenderPacket Media(uint16_t seq, PacketKind kind, cc::PacketPriority prio, size_t payload = 100) {
  SenderPacket p;
  p.packet.ssrc = 1;
  p.packet.payload_type = 96;
  p.packet.seq = seq;
  p.packet.payload.assign(payload, 0);
  p.kind = kind;
  p.priority = prio;
  return p;
}
}  // namespace

SM_TEST(SendSideController, ReleasesWithTransportSequenceNumbersInPriorityOrder) {
  sync::ExtensionMap map;
  map.Register(3, sync::uri::kTransportCc);
  SendSideController c(SendSideConfig{}, &map);
  c.Start(Timestamp::Millis(0));
  Timestamp now = Timestamp::Millis(0);
  c.Enqueue(Media(10, PacketKind::kVideo, cc::PacketPriority::kVideo), now);
  c.Enqueue(Media(11, PacketKind::kVideo, cc::PacketPriority::kVideo), now);
  c.Enqueue(Media(500, PacketKind::kAudio, cc::PacketPriority::kAudio, 40), now);
  std::vector<ReleasedPacket> all;
  for (int t = 0; t < 50; ++t) {
    auto r = c.Process(Timestamp::Millis(t));
    all.insert(all.end(), r.begin(), r.end());
  }
  SM_ASSERT_EQ(all.size(), 3u);
  sync::ExtensionAccessor access(map);
  for (size_t i = 0; i < all.size(); ++i) {
    SM_EXPECT_EQ(all[i].transport_seq, i + 1);  // From 1, as libwebrtc's PacketRouter.
    auto parsed = RtpPacket::Parse(all[i].bytes);
    SM_ASSERT_TRUE(parsed.has_value());
    SM_EXPECT_TRUE(access.GetTransportSeq(*parsed) == std::optional<uint16_t>(static_cast<uint16_t>(i + 1)));
    SM_EXPECT_EQ(all[i].wire_size.bytes(), static_cast<int64_t>(all[i].bytes.size()) + 28);
  }
  SM_EXPECT_TRUE(all[0].kind == PacketKind::kAudio);  // Audio goes first.
  SM_EXPECT_EQ(c.stats().released, 3u);
  SM_EXPECT_GT(c.send_rate(Timestamp::Millis(49)).bps(), 0);
  SM_EXPECT_EQ(c.send_rate(Timestamp::Millis(10000)).bps(), 0);
}

SM_TEST(SendSideController, PacingSpreadsAFrameOverTime) {
  SendSideController c;
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 40; ++i)
    c.Enqueue(Media(i, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100), Timestamp::Millis(0));
  size_t at_zero = c.Process(Timestamp::Millis(0)).size();
  SM_EXPECT_LT(at_zero, 40u);
  size_t total = at_zero;
  for (int t = 1; t < 500; ++t) total += c.Process(Timestamp::Millis(t)).size();
  SM_EXPECT_EQ(total, 40u);
}

SM_TEST(SendSideController, ComputeRatesDividesOutProtectionAndClamps) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  cfg.video_min = DataRate::KilobitsPerSec(50);
  cfg.video_max = DataRate::KilobitsPerSec(600);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  SendRates r = c.ComputeRates(DataRate::KilobitsPerSec(100), 0.0);
  SM_EXPECT_EQ(r.video_target.bps(), 600000);  // 900 kbps clamped to the maximum.
  r = c.ComputeRates(DataRate::KilobitsPerSec(100), 0.5);
  SM_EXPECT_EQ(r.video_target.bps(), 600000);  // 900 / 1.5 = 600.
  r = c.ComputeRates(DataRate::KilobitsPerSec(100), 1.0);
  SM_EXPECT_EQ(r.video_target.bps(), 450000);
  r = c.ComputeRates(DataRate::KilobitsPerSec(990), 0.0);
  SM_EXPECT_EQ(r.video_target.bps(), 50000);  // Clamped to the minimum.
  SM_EXPECT_EQ(r.audio_rate.bps(), 990000);
}

SM_TEST(SendSideController, SilentFeedbackDoesNothingUnderGccV2) {
  // libwebrtc has no outage monitor (issue #151): on a feedback time-out GoogCC backs the target off and the
  // congestion window drops frames, but the encoder keeps running, the pacer keeps sending and no keyframe is
  // asked for; the receiver's decode time-out asks for one after the link is back.
  SendSideConfig cfg;
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  c.gcc().OnRttUpdate(TimeDelta::Millis(50));
  for (int t = 0; t < 8000; t += 10) {
    c.Process(Timestamp::Millis(t));
    if (t < 7000) c.outage().OnFeedback(Timestamp::Millis(t < 1000 ? t : 999));
    ControllerActions a = c.TakeActions();
    SM_EXPECT_FALSE(a.pause_encoder);
    SM_EXPECT_FALSE(a.resume_encoder);
    SM_EXPECT_FALSE(a.request_keyframe);
  }
}

SM_TEST(SendSideController, PaddingCarriesTheLastMediaTimestampAndFitsItsAccountedSize) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(2, sync::uri::kAbsSendTime);
  SendSideController c(SendSideConfig{}, &map);
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  sc.extensions = &map;
  RtpSender sender(sc);
  c.SetPaddingSender(&sender);
  c.Start(Timestamp::Millis(0));
  // A packet large enough to start the start-up probes, which then go out as padding.
  sender.SendVideoFrame(std::vector<std::vector<uint8_t>>{std::vector<uint8_t>(300, 1)}, 123456, Timestamp::Millis(0));
  for (auto& p : sender.TakePackets()) c.Enqueue(std::move(p), Timestamp::Millis(0));
  size_t padding = 0;
  for (int t = 0; t < 1000; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      if (r.kind != PacketKind::kPadding) continue;
      auto p = RtpPacket::Parse(r.bytes);
      SM_ASSERT_TRUE(p.has_value());
      SM_EXPECT_EQ(p->timestamp, 123456u);
      SM_EXPECT_LE(static_cast<int64_t>(r.bytes.size()) + 28, r.wire_size.bytes());
      ++padding;
    }
  }
  SM_EXPECT_GT(padding, 0u);
}

SM_TEST(SendSideController, VideoBudgetShrinksByTheMeasuredRepairShare) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  cfg.video_min = DataRate::KilobitsPerSec(50);
  cfg.video_max = DataRate::KilobitsPerSec(5000);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  SM_EXPECT_EQ(c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps(), 2000000);  // Nothing sent yet.
  // Three media packets for every resend: a quarter of the bytes is repair.
  for (uint16_t i = 0; i < 12; ++i) {
    c.Enqueue(Media(i, i % 4 == 3 ? PacketKind::kRtx : PacketKind::kVideo,
                    i % 4 == 3 ? cc::PacketPriority::kRetransmission : cc::PacketPriority::kVideo, 500),
              Timestamp::Millis(0));
  }
  for (int t = 0; t < 200; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_NEAR(c.repair_share(), 0.25, 0.01);
  SM_EXPECT_NEAR(static_cast<double>(c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps()), 1500000.0, 20000.0);
  // The share is measured over one second, so it is forgotten afterwards.
  c.Process(Timestamp::Millis(1500));
  SM_EXPECT_EQ(c.repair_share(), 0.0);
}

SM_TEST(SendSideController, RepairNeverTakesMoreThanHalfOfTheBudget) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  c.Enqueue(Media(0, PacketKind::kVideo, cc::PacketPriority::kVideo, 100), Timestamp::Millis(0));
  for (uint16_t i = 1; i < 11; ++i)
    c.Enqueue(Media(i, PacketKind::kRtx, cc::PacketPriority::kRetransmission, 500), Timestamp::Millis(0));
  for (int t = 0; t < 200; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_GT(c.repair_share(), 0.9);
  SM_EXPECT_NEAR(static_cast<double>(c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps()), 1000000.0, 1.0);
}

SM_TEST(SendSideController, AWindowWithRepairButNoVideoIsCappedLikeLibwebrtc) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 10; ++i)
    c.Enqueue(Media(i, PacketKind::kRtx, cc::PacketPriority::kRetransmission, 500), Timestamp::Millis(0));
  for (int t = 0; t < 200; ++t) c.Process(Timestamp::Millis(t));
  // FecControllerDefault divides by the total sent rate, so repair alone is a 100% share, capped at 50%.
  SM_EXPECT_EQ(c.repair_share(), 1.0);
  SM_EXPECT_EQ(c.ComputeMeasuredRates(DataRate::KilobitsPerSec(100)).video_target.bps(), 950000);
  // The encoder keeps at least video_min whatever the share.
  SM_EXPECT_GE(c.ComputeMeasuredRates(DataRate::KilobitsPerSec(1990)).video_target.bps(), cfg.video_min.bps());
}

// The keyframe's first packet flushes the queued video packets under pacer v2
// (PacingController::EnqueuePacket); the flushed packets are never released
// and leave nothing behind in the controller.
SM_TEST(SendSideController, KeyframeFlushesQueuedVideoUnderPacerV2) {
  SendSideConfig cfg;
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  const Timestamp t0 = Timestamp::Millis(0);
  for (uint16_t i = 0; i < 20; ++i) c.Enqueue(Media(i, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100), t0);
  SenderPacket key = Media(100, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100);
  key.is_key_frame = true;
  key.first_packet_of_frame = true;
  c.Enqueue(key, t0);
  SM_EXPECT_GT(c.stats().flushed, 0u);
  uint64_t released_ids = 0;
  std::vector<uint16_t> seqs;
  for (int t = 0; t < 2000; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      ++released_ids;
      auto p = RtpPacket::Parse(r.bytes);
      if (p && r.kind == PacketKind::kVideo) seqs.push_back(p->seq);
    }
  }
  SM_EXPECT_EQ(c.stats().unknown_ids, 0u);
  // The keyframe packet goes out; the flushed ones never do.
  SM_EXPECT_TRUE(std::find(seqs.begin(), seqs.end(), 100) != seqs.end());
  SM_EXPECT_EQ(released_ids + c.stats().flushed, 21u);
}

// Issue #127/#154: the RTP sequence number is assigned when the pacer releases the
// packet (libwebrtc's PacketRouter), so packets a keyframe flushes leave no gap for the receiver to NACK.
SM_TEST(SendSideController, FlushedPacketsLeaveNoSequenceGapUnderRtxV2) {
  SendSideConfig cfg;
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  RtpSender sender(sc);
  const Timestamp t0 = Timestamp::Millis(0);
  const std::vector<std::vector<uint8_t>> frame(10, std::vector<uint8_t>(1100, 1));
  sender.SendVideoFrame(frame, 9000, t0);
  for (auto& p : sender.TakePackets()) c.Enqueue(std::move(p), t0);
  sender.SendVideoFrame(frame, 12000, t0, true);  // A keyframe: its first packet flushes the queue.
  for (auto& p : sender.TakePackets()) c.Enqueue(std::move(p), t0);
  SM_ASSERT_GT(c.stats().flushed, 0u);
  std::vector<uint16_t> seqs;
  for (int t = 0; t < 3000; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      auto p = RtpPacket::Parse(r.bytes);
      if (p && r.kind == PacketKind::kVideo) seqs.push_back(p->seq);
    }
  }
  SM_ASSERT_GE(seqs.size(), 10u);
  for (size_t i = 1; i < seqs.size(); ++i) SM_EXPECT_EQ(static_cast<uint16_t>(seqs[i]), static_cast<uint16_t>(seqs[i - 1] + 1));
}

// A resend that a keyframe flushes from the pacer never reaches the history's MarkPacketAsSent. As in
// libwebrtc (PacingController::EnqueuePacket drops the RTX stream's queue, pacing_controller.cc:227-231,
// and RtpPacketHistory keeps the packet pending) a later NACK for it gets nothing (issue #151).
SM_TEST(SendSideController, FlushedResendIsNotSentAgainUnderPacerV2AndRtxV2) {
  SendSideConfig cfg;
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  RtpSender sender(sc);
  Timestamp now = Timestamp::Millis(0);
  const std::vector<uint8_t> chunk(300, 1);
  sender.SendVideoFrame(std::span<const std::vector<uint8_t>>(&chunk, 1), 9000, now);
  auto media = sender.TakePackets();
  SM_ASSERT_EQ(media.size(), 1u);
  const uint16_t seq = media[0].packet.seq;
  for (auto& p : media) c.Enqueue(std::move(p), now);
  for (int t = 0; t < 100; ++t) c.Process(Timestamp::Millis(t));  // The pacer sends it: the history stores it.
  now = Timestamp::Millis(300);
  const TimeDelta rtt = TimeDelta::Millis(50);
  sender.SetRtt(rtt, now);
  sender.OnNack(std::vector<uint16_t>{seq}, now, rtt);
  auto resend = sender.TakePackets();
  SM_ASSERT_EQ(resend.size(), 1u);
  SM_EXPECT_TRUE(resend[0].kind == PacketKind::kRtx);
  for (auto& p : resend) c.Enqueue(std::move(p), now);  // Queued, not yet sent.
  // A keyframe starts: the pacer discards the queued resend.
  SenderPacket key = Media(100, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100);
  key.is_key_frame = true;
  key.first_packet_of_frame = true;
  c.Enqueue(key, now);
  SM_ASSERT_GT(c.stats().flushed, 0u);
  // Asked again later, the sender ignores it: the flushed resend is still pending.
  now = Timestamp::Millis(900);
  sender.OnNack(std::vector<uint16_t>{seq}, now, rtt);
  SM_EXPECT_EQ(sender.TakePackets().size(), 0u);
}

// Issue #104: the sum of the enforced minimums (audio + video) is the lower bound
// of the target, as GoogCC's min_total_allocated_bitrate is, so the sender does
// not run below what it must send.
SM_TEST(SendSideController, AllocatorLiftsTheTargetToTheSumOfTheMinimums) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(60);
  cfg.video_min = DataRate::KilobitsPerSec(50);
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::KilobitsPerSec(50), true);
  c.Start(Timestamp::Millis(0));
  SM_EXPECT_GE(c.target().kbps(), 100.0);
  const SendRates r = c.ComputeMeasuredRates(DataRate::KilobitsPerSec(50));
  SM_EXPECT_NEAR(r.audio_rate.kbps(), 50.0, 0.01);
  // Its minimum less the packet-overhead rate (RtpVideoSender::OnBitrateUpdated), with no repair
  // traffic yet: 50 kbps / (1228 bytes x 8) = 6 packets/s (rounded up) of 24 + 28 bytes.
  SM_EXPECT_NEAR(r.video_target.kbps(), 50.0 - 6 * (24 + 28) * 8 / 1000.0, 0.01);
  SM_EXPECT_EQ(c.allocator().limits().min_allocatable.kbps(), 100.0);
}

// RtpVideoSender::OnBitrateUpdated (#61 glue): with `cc v2` the packet-overhead
// rate comes off the video allocation before the protection share does.
SM_TEST(SendSideController, V2SubtractsThePacketOverheadFromTheVideoAllocation) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::Zero(), true);
  c.Start(Timestamp::Millis(0));
  const SendRates r = c.ComputeMeasuredRates(DataRate::Zero());
  // 1 Mbps / (1228 bytes x 8) = 102 packets/s (rounded up), each with 24 + 28 bytes of overhead.
  const int64_t overhead = 102 * (24 + 28) * 8;
  SM_EXPECT_EQ(r.video_target.bps(), c.allocator().allocation("video").bps() - overhead);
}

// RtpVideoSender::OnBitrateUpdated computes the overhead rate for its
// rtp_config_.max_packet_size (1460 by default, rtp_rtcp_impl2.cc:149), the
// packet size `packetization v2` sends (issue #151). libwebrtc's per-second
// video trace on `issue_151_outage_3s_v2` (parity-data, seed 1, t = 5 s) has a
// 1197256 bps encoder target for a 1240936 bps video allocation: 105 packets/s
// (1240936 / (1488 x 8), rounded up) of 24 + 28 bytes.
SM_TEST(SendSideController, V2OverheadFollowsTheConfiguredPacketSizeAsLibwebrtc) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::BitsPerSec(1240936);
  cfg.max_packet_size = 1460;
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::Zero(), true);
  c.Start(Timestamp::Millis(0));
  SM_EXPECT_EQ(c.allocator().allocation("video").bps(), 1240936);
  SM_EXPECT_EQ(c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps(), 1197256);
}

// `control_handler` (issue #204): RtpVideoSender::OnBitrateUpdated fixes the encoder target, with the
// protection share sent in the last second, when an allocation arrives; the encoder keeps it until the
// transport's CongestionControlHandler passes on the next update. Without the switch the share is read
// live, as in VideoBudgetShrinksByTheMeasuredRepairShare.
SM_TEST(SendSideController, ControlHandlerKeepsTheEncoderTargetUntilTheNextUpdate) {
  for (bool handler : {false, true}) {
    SendSideConfig cfg;
    cfg.gcc.start_rate = DataRate::KilobitsPerSec(2000);
    cfg.control_handler = handler;
    SendSideController c(cfg);
    c.ConfigureAllocation(DataRate::Zero(), true);
    c.Start(Timestamp::Millis(0));
    const int64_t before = c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps();
    for (uint16_t i = 0; i < 12; ++i) {
      c.Enqueue(Media(i, i % 4 == 3 ? PacketKind::kRtx : PacketKind::kVideo,
                      i % 4 == 3 ? cc::PacketPriority::kRetransmission : cc::PacketPriority::kVideo, 500),
                Timestamp::Millis(0));
    }
    for (int t = 0; t < 200; ++t) c.Process(Timestamp::Millis(t));
    SM_EXPECT_NEAR(c.repair_share(), 0.25, 0.01);
    SM_EXPECT_EQ(c.target().bps(), 2000000);  // No feedback: GoogCC's target has not moved.
    const int64_t after = c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps();
    if (handler) {
      SM_EXPECT_EQ(after, before);
    } else {
      SM_EXPECT_NEAR(static_cast<double>(after), before * 0.75, 20000.0);
    }
  }
}
