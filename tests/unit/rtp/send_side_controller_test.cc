#include <algorithm>
#include <optional>
#include <vector>

#include "sm/loss/rtx.h"
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

// PacketRouter hands the pacer RtpPacketToSend, whose queue counts payload and padding plus the
// transport overhead (QueueSizeData with IncludeOverheadInPacedSender, which RtpVideoSender sets
// with transport feedback); the budget still takes the wire size.
SM_TEST(SendSideController, PacerQueueDataLeavesOutTheRtpHeaders) {
  sync::ExtensionMap map;
  map.Register(3, sync::uri::kTransportCc);
  map.Register(2, sync::uri::kAbsSendTime);
  SendSideController c(SendSideConfig{}, &map);
  c.Start(Timestamp::Millis(0));
  SenderPacket p = Media(10, PacketKind::kVideo, cc::PacketPriority::kVideo, 1000);
  p.packet.padding_size = 50;
  c.Enqueue(std::move(p), Timestamp::Millis(0));
  // 12 B fixed header, 4 B extension header, transport-cc (3 B) and abs-send-time (4 B), padded to 24 B.
  SM_EXPECT_EQ(c.pacer().QueueSize().bytes(), 24 + 1000 + 50 + 28);
  SM_EXPECT_EQ(c.pacer().QueueSizeData().bytes(), 1000 + 50 + 28);
}

// GoogCC takes the pacer queue only on the process interval (ProcessInterval::pacer_queue,
// goog_cc_network_control.cc:215-217); a sent packet updates the outstanding data, not the queue.
SM_TEST(SendSideController, PushbackTakesThePacerQueueOnTheProcessInterval) {
  SendSideController c;
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 40; ++i)
    c.Enqueue(Media(i, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100), Timestamp::Millis(0));
  c.Process(Timestamp::Millis(0));
  const DataSize at_interval = c.congestion_window().pacing_queue();
  SM_EXPECT_EQ(at_interval.bytes(), c.pacer().QueueSizeData().bytes());
  SM_EXPECT_GT(at_interval.bytes(), 0);
  for (int t = 1; t < 25; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_LT(c.pacer().QueueSizeData().bytes(), at_interval.bytes());  // Packets left meanwhile.
  SM_EXPECT_EQ(c.congestion_window().pacing_queue().bytes(), at_interval.bytes());
  c.Process(Timestamp::Millis(25));
  SM_EXPECT_EQ(c.congestion_window().pacing_queue().bytes(), c.pacer().QueueSizeData().bytes());
}

// GoogCcNetworkController::OnTransportLossReport returns an empty update (goog_cc_network_control.cc:380-387):
// the estimator takes the loss at once, and the transport's target, pacer and allocator follow at the
// next process interval.
SM_TEST(SendSideController, LossReportReachesTheTargetAtTheNextProcessInterval) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  for (int t = 0; t <= 1000; ++t) c.Process(Timestamp::Millis(t));
  const DataRate before = c.target();
  c.OnLossReport(50, 100, Timestamp::Millis(1010));
  SM_EXPECT_LT(c.gcc().target().bps(), before.bps());  // 50% loss: the estimator backs off.
  SM_EXPECT_EQ(c.target().bps(), before.bps());
  for (int t = 1011; t < 1025; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_EQ(c.target().bps(), before.bps());
  c.Process(Timestamp::Millis(1025));
  SM_EXPECT_EQ(c.target().bps(), c.gcc().target().bps());
  SM_EXPECT_LT(c.target().bps(), before.bps());
}

// GoogCC's cwnd_reduce_ratio comes from the pushback target after it is clamped to the estimator's
// minimum (goog_cc_network_control.cc:596-608). Once the pushback reaches that minimum the ratio stops
// changing, so no new update undoes an AddObserver reallocation's reset to 0 (issue #738).
SM_TEST(SendSideController, CwndReduceRatioStopsAtTheMinimumBitrate) {
  sync::ExtensionMap map;
  map.Register(3, sync::uri::kTransportCc);
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  cfg.video_min = DataRate::KilobitsPerSec(60);
  SendSideController c(cfg, &map);
  SendSideController::AudioAllocation audio;
  audio.min = audio.max = audio.priority_rate = DataRate::KilobitsPerSec(40);
  c.ConfigureAllocation(audio, true);
  c.Start(Timestamp::Millis(0));
  const DataRate min = c.gcc().min_rate();
  SM_EXPECT_EQ(min.bps(), 100000);  // The minimum allocatable rate: 40 + 60 kbps.
  // Feedback for the first second sizes the congestion window. Then none comes back (an outage), so the
  // outstanding data fills the window and the pushback ratio steps down with every sent packet until
  // the pushback target reaches the minimum.
  uint16_t seq = 0;
  double max_ratio = 0;
  std::vector<uint16_t> unacked;
  for (int t = 0; t <= 2500; ++t) {
    if (t % 30 == 0) c.OnVideoFrameEncoded(Timestamp::Millis(t));  // Keeps the stream active.
    if (t % 10 == 0)
      for (int i = 0; i < 2; ++i) c.Enqueue(Media(seq++, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100), Timestamp::Millis(t));
    for (const auto& r : c.Process(Timestamp::Millis(t))) unacked.push_back(static_cast<uint16_t>(r.transport_seq));
    if (t < 1000 && t % 50 == 49 && !unacked.empty()) {
      cc::TransportFeedback fb;
      fb.base_seq = unacked.front();
      fb.reference_time = t / 64;
      for (uint16_t s : unacked) fb.packets.push_back({s, true, 4});
      unacked.clear();
      c.OnTransportFeedback(fb, Timestamp::Millis(t));
    }
    max_ratio = std::max(max_ratio, c.allocator().cwnd_reduce_ratio("video"));
  }
  const double floor = static_cast<double>(c.target().bps() - min.bps()) / static_cast<double>(c.target().bps());
  SM_EXPECT_GT(c.allocator().cwnd_reduce_ratio("video"), floor - 1e-9);
  SM_EXPECT_LT(max_ratio, floor + 1e-9);  // Not the pushback controller's own 30 kbps floor.
  // An encoder reconfiguration re-adds the stream (AddObserver), which resets the ratio to 0; with the
  // pushback at the minimum, later sent packets hand on no new ratio.
  c.OnVideoEncoderReconfigured(Timestamp::Millis(2501));
  SM_EXPECT_EQ(c.allocator().cwnd_reduce_ratio("video"), 0.0);
  for (int t = 2501; t <= 2700; ++t) {
    if (t % 30 == 0) c.OnVideoFrameEncoded(Timestamp::Millis(t));
    if (t % 10 == 0)
      for (int i = 0; i < 2; ++i) c.Enqueue(Media(seq++, PacketKind::kVideo, cc::PacketPriority::kVideo, 1100), Timestamp::Millis(t));
    c.Process(Timestamp::Millis(t));
  }
  SM_EXPECT_EQ(c.allocator().cwnd_reduce_ratio("video"), 0.0);
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

// PacketSequencer::PopulatePaddingFields (packet_sequencer.cc:120-137): empty RTX padding carries the
// last released media packet's timestamp advanced at 90 ticks per ms since, and 0 before any media
// (the probe's first packet goes before the frame); payload padding keeps the resent packet's.
SM_TEST(SendSideController, PaddingTimestampFollowsTheSequencerAndFitsItsAccountedSize) {
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
  size_t padding = 0, before_media = 0;
  std::optional<int> media_ms;
  for (int t = 0; t < 1000; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      auto p = RtpPacket::Parse(r.bytes);
      SM_ASSERT_TRUE(p.has_value());
      if (r.kind == PacketKind::kVideo) media_ms = t;
      if (r.kind != PacketKind::kPadding) continue;
      if (!p->payload.empty()) {
        SM_EXPECT_EQ(p->timestamp, 123456u);
      } else if (!media_ms) {
        SM_EXPECT_EQ(p->timestamp, 0u);
        ++before_media;
      } else {
        SM_EXPECT_EQ(p->timestamp, 123456u + 90u * static_cast<uint32_t>(t - *media_ms));
      }
      SM_EXPECT_LE(static_cast<int64_t>(r.bytes.size()) + 28, r.wire_size.bytes());
      ++padding;
    }
  }
  SM_EXPECT_GT(padding, before_media);
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

// `send_rate_stats` (issue #224): the share comes from per-type RateStatistics, as
// RtpSenderEgress::GetSendRates. RTX that starts after more than 1.5 s without
// any counts at its rate since it started; with the switch off it is averaged
// over the whole second.
namespace {
double ShareAfterRtxStarts(bool send_rate_stats, int rtx_gap_ms) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(5000);
  cfg.send_rate_stats = send_rate_stats;
  SendSideController c(cfg);
  c.Start(Timestamp::Millis(0));
  uint16_t seq = 0;
  const int rtx_start = 1000 + rtx_gap_ms;
  for (int t = 0; t <= rtx_start + 100; ++t) {
    // Video every 10 ms the whole time; a burst of RTX at 1000 ms, and RTX every
    // 10 ms from `rtx_start`, the same size as the video.
    if (t % 10 == 0) c.Enqueue(Media(seq++, PacketKind::kVideo, cc::PacketPriority::kVideo, 1000), Timestamp::Millis(t));
    if ((t == 1000 || (t >= rtx_start && t % 10 == 0)) && t < rtx_start + 100) {
      c.Enqueue(Media(seq++, PacketKind::kRtx, cc::PacketPriority::kRetransmission, 1000), Timestamp::Millis(t));
    }
    c.Process(Timestamp::Millis(t));
  }
  return c.repair_share();
}
}  // namespace

SM_TEST(SendSideController, RateStatisticsCountRepairThatJustStartedAtItsRateSinceItStarted) {
  // 2 s without RTX: the RTX tracker restarts, so 100 ms of RTX at the video's
  // rate is half of the rate (FecControllerDefault then caps it at 0.5).
  SM_EXPECT_NEAR(ShareAfterRtxStarts(true, 2000), 0.5, 0.03);
  // Averaged over the second, the same RTX is a tenth of the bytes.
  SM_EXPECT_NEAR(ShareAfterRtxStarts(false, 2000), 1.0 / 11.0, 0.02);
}

SM_TEST(SendSideController, RateStatisticsKeepTheWindowWhenTheLastRepairWasRecent) {
  // The last RTX was 1.2 s before (under 1.5 windows): RateStatistics keeps the
  // window start, so the new RTX is averaged over the whole second.
  SM_EXPECT_NEAR(ShareAfterRtxStarts(true, 1200), 1.0 / 11.0, 0.02);
}

// Issues #224 and #204: libwebrtc's send-side rates (RtpSenderEgress::GetSendRates) and its
// CongestionControlHandler are what a session uses by default, as RtpTransportControllerSend does.
SM_TEST(SendSideController, RateStatisticsAndControlHandlerAreTheDefault) {
  const SendSideConfig cfg;
  SM_EXPECT_TRUE(cfg.send_rate_stats);
  SM_EXPECT_TRUE(cfg.control_handler);
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

// Issue #217: RtpVideoSender::OnPacketFeedbackVector (call/rtp_video_sender.cc:1010-1035). A video
// packet that transport feedback reports lost, with no later packet of the stream received, is
// resent at once ("early loss detection"); one followed by a received packet is left to the NACK.
// The NACK that follows finds the resend pending and is ignored.
SM_TEST(SendSideController, TrailingLostVideoIsResentFromTransportFeedback) {
  sync::ExtensionMap map;
  map.Register(3, sync::uri::kTransportCc);
  SendSideController c(SendSideConfig{}, &map);
  c.Start(Timestamp::Millis(0));
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  RtpSender sender(sc);
  Timestamp now = Timestamp::Millis(0);
  const std::vector<uint8_t> chunk(300, 1);
  for (int f = 0; f < 3; ++f) {
    sender.SendVideoFrame(std::span<const std::vector<uint8_t>>(&chunk, 1), 9000 + 3000 * f, now);
    for (auto& p : sender.TakePackets()) c.Enqueue(std::move(p), now);
  }
  // RTP sequence numbers are assigned as the pacer releases the packets.
  std::vector<uint16_t> seqs, transport;
  for (int t = 0; t < 100; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      auto p = RtpPacket::Parse(r.bytes);
      SM_ASSERT_TRUE(p.has_value());
      seqs.push_back(p->seq);
      transport.push_back(static_cast<uint16_t>(r.transport_seq));
    }
  }
  SM_ASSERT_EQ(transport.size(), 3u);
  now = Timestamp::Millis(150);
  // The first packet is lost but the second arrived: the receiver can see the gap.
  cc::TransportFeedback fb;
  fb.base_seq = transport[0];
  fb.packets = {{transport[0], false, 0}, {transport[1], true, 4}, {transport[2], false, 0}};
  c.OnTransportFeedback(fb, now);
  // Only the trailing loss (the third packet) is resent.
  SM_EXPECT_EQ(sender.TakePackets().size(), 0u);  // Handed to the pacer, not left with the sender.
  std::vector<ReleasedPacket> rtx;
  for (int t = 150; t < 200; ++t) {
    for (auto& r : c.Process(Timestamp::Millis(t))) {
      if (r.kind == PacketKind::kRtx) rtx.push_back(r);
    }
  }
  SM_ASSERT_EQ(rtx.size(), 1u);
  auto parsed = RtpPacket::Parse(rtx[0].bytes);
  SM_ASSERT_TRUE(parsed.has_value());
  auto back = loss::RestoreFromRtx(*parsed, sc.ssrc, sc.payload_type);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->seq, seqs[2]);
  // The NACK for both: the first is resent, the third went out less than an RTT ago.
  const TimeDelta rtt = TimeDelta::Millis(100);
  sender.SetRtt(rtt, Timestamp::Millis(200));
  sender.OnNack(std::vector<uint16_t>{seqs[0], seqs[2]}, Timestamp::Millis(200), rtt);
  auto resend = sender.TakePackets();
  SM_ASSERT_EQ(resend.size(), 1u);
  auto first = loss::RestoreFromRtx(resend[0].packet, sc.ssrc, sc.payload_type);
  SM_ASSERT_TRUE(first.has_value());
  SM_EXPECT_EQ(first->seq, seqs[0]);
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
  // traffic yet: 50 kbps / (1488 bytes x 8) = 5 packets/s (rounded up) of 24 + 28 bytes (1460-byte
  // packets by default since #580).
  SM_EXPECT_NEAR(r.video_target.kbps(), 50.0 - 5 * (24 + 28) * 8 / 1000.0, 0.01);
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
  // 1 Mbps / (1488 bytes x 8) = 85 packets/s (rounded up), each with 24 + 28 bytes of overhead.
  const int64_t overhead = 85 * (24 + 28) * 8;
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

// Issue #326: VideoSendStreamImpl::OnBitrateUpdated (video_send_stream_impl.cc:902-914) takes the link
// allocation, the encoder target less the protection, before it caps the target at the encoder maximum,
// and RtpVideoSender::OnBitrateUpdated (rtp_video_sender.cc:885-939) computes the encoder overhead and
// the protection from the uncapped target. A 3 Mbps video allocation with a 2.5 Mbps maximum: 253
// packets/s (3000000 / (1488 x 8), rounded up) of 24 + 28 bytes leave a 2894752 bps payload; its own
// overhead is 248 packets/s (2894752 / (1464 x 8)), so the protection is 3000000 - 2894752 - 103168 =
// 2080 bps and the link allocation 2892672 bps, which is the encoder bitrate adjuster's headroom.
SM_TEST(SendSideController, LinkAllocationIsTakenBeforeTheEncoderMaximumAsLibwebrtc) {
  for (bool headroom : {true, false}) {
    SendSideConfig cfg;
    cfg.gcc.start_rate = DataRate::KilobitsPerSec(3000);
    cfg.max_packet_size = 1460;
    cfg.video_max = DataRate::KilobitsPerSec(2500);
    cfg.link_headroom = headroom;
    SendSideController c(cfg);
    c.ConfigureAllocation(DataRate::Zero(), true);
    c.Start(Timestamp::Millis(0));
    SM_EXPECT_EQ(c.allocator().allocation("video").bps(), 3000000);  // Up to twice the maximum.
    const SendRates r = c.ComputeMeasuredRates(DataRate::Zero());
    SM_EXPECT_EQ(r.video_target.bps(), 2500000);
    SM_EXPECT_EQ(r.video_link_allocation.bps(), headroom ? 2892672 : 2500000);
  }
}

// Below the maximum the link allocation is the encoder target (max(target, target - protection)).
SM_TEST(SendSideController, LinkAllocationIsTheEncoderTargetBelowTheMaximum) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::BitsPerSec(1240936);
  cfg.max_packet_size = 1460;
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::Zero(), true);
  c.Start(Timestamp::Millis(0));
  const SendRates r = c.ComputeMeasuredRates(DataRate::Zero());
  SM_EXPECT_EQ(r.video_target.bps(), 1197256);
  SM_EXPECT_EQ(r.video_link_allocation.bps(), 1197256);
}

// Issue #619: RtpVideoSender::OnBitrateUpdated (rtp_video_sender.cc:909-938) takes the post-encode
// overhead (RTPSenderVideo::post_encode_overhead_bitrate_, a 1 s BitrateTracker fed by
// LogAndSendToNetwork, rtp_sender_video.cc:241-258) off the encoder target and counts it in the media
// rate, so the protection is unchanged. Frames every 100 ms whose packets carry 125 bytes more than
// the encoder produced: at t = 1 s the window holds 10 of them, 1250 bytes = 10000 bps, so the
// 1197256 bps target of LinkAllocationIsTheEncoderTargetBelowTheMaximum drops by 10000 bps.
SM_TEST(SendSideController, PostEncodeOverheadComesOffTheEncoderTargetAsLibwebrtc) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::BitsPerSec(1240936);
  cfg.max_packet_size = 1460;
  cfg.control_handler = false;  // Read live; with the handler it applies at the next update.
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::Zero(), true);
  c.Start(Timestamp::Millis(0));
  for (int t = 0; t <= 1000; t += 100) c.OnVideoPacketized(5000, 5125, Timestamp::Millis(t));
  c.Process(Timestamp::Millis(1000));
  SM_EXPECT_EQ(c.allocator().allocation("video").bps(), 1240936);
  const SendRates r = c.ComputeMeasuredRates(DataRate::Zero());
  SM_EXPECT_EQ(r.video_target.bps(), 1197256 - 10000);
}

// rtp_sender_video.cc:252-257: a frame whose packets carry fewer bytes than the encoder produced
// (H.264 start codes are not sent) adds nothing; it is not a negative overhead.
SM_TEST(SendSideController, PacketizationThatSavesBytesAddsNoPostEncodeOverhead) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::BitsPerSec(1240936);
  cfg.max_packet_size = 1460;
  cfg.control_handler = false;
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::Zero(), true);
  c.Start(Timestamp::Millis(0));
  for (int t = 0; t <= 1000; t += 100) c.OnVideoPacketized(5000, 4996, Timestamp::Millis(t));
  c.Process(Timestamp::Millis(1000));
  SM_EXPECT_EQ(c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps(), 1197256);
}

// rtp_video_sender.cc:909-915: the post-encode overhead takes at most half the encoder target.
SM_TEST(SendSideController, PostEncodeOverheadTakesAtMostHalfTheEncoderTarget) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::BitsPerSec(1240936);
  cfg.max_packet_size = 1460;
  cfg.control_handler = false;
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::Zero(), true);
  c.Start(Timestamp::Millis(0));
  // 10 x 100000 bytes in the window: 8 Mbps of overhead.
  for (int t = 0; t <= 1000; t += 100) c.OnVideoPacketized(0, 100000, Timestamp::Millis(t));
  c.Process(Timestamp::Millis(1000));
  SM_EXPECT_EQ(c.ComputeMeasuredRates(DataRate::Zero()).video_target.bps(), 1197256 / 2);
}

// Issue #375: VideoSendStreamImpl's encoder activity check (video_send_stream_impl.cc:84, 677-760).
// Every kEncoderTimeOut (2 s) from the start, a video stream that encoded nothing since the last
// check leaves the BitrateAllocator, so max_total_allocated_bitrate falls to the audio's; an
// encoder reconfiguration (OnEncoderConfigurationChanged) or the next encoded frame
// (OnEncodedImage after the time-out) registers it again.
SM_TEST(SendSideController, VideoLeavesTheAllocatorAfterTwoSecondsWithoutAnEncodedFrame) {
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(300);
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::KilobitsPerSec(50), true);
  const int64_t both = c.allocator().limits().max_allocatable.bps();
  SM_EXPECT_EQ(both, DataRate::KilobitsPerSec(50).bps() + cfg.video_max.bps());
  c.Start(Timestamp::Millis(0));
  for (int t = 0; t < 2000; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_TRUE(c.video_registered());  // The first check is due at 2 s.
  c.Process(Timestamp::Millis(2000));
  SM_EXPECT_FALSE(c.video_registered());
  SM_EXPECT_EQ(c.allocator().limits().max_allocatable.bps(), DataRate::KilobitsPerSec(50).bps());
  SM_EXPECT_EQ(c.allocator().allocation("video").bps(), 0);
  // The encoder keeps the target it last got (no OnBitrateUpdated after RemoveObserver).
  SM_EXPECT_GT(c.ComputeMeasuredRates(DataRate::KilobitsPerSec(50)).video_target.bps(), 0);
  SM_EXPECT_GT(c.VideoPayloadRate().bps(), 0);
  // A new frame size reconfigures the encoder: the running stream registers again.
  c.OnVideoEncoderReconfigured(Timestamp::Millis(3000));
  SM_EXPECT_TRUE(c.video_registered());
  SM_EXPECT_EQ(c.allocator().limits().max_allocatable.bps(), both);
  SM_EXPECT_GT(c.allocator().allocation("video").bps(), 0);
  // Still no frame at the 4 s check: timed_out_ is already set, so it stays registered.
  for (int t = 3001; t <= 4000; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_TRUE(c.video_registered());
}

SM_TEST(SendSideController, ActivityCheckDueAtAReconfigurationRunsFirst) {
  // The encoder queue posts OnEncoderConfigurationChanged to the worker queue, so
  // the activity task due at the same instant runs before it: the stream times
  // out and the reconfiguration registers it again at once (issue #375).
  SendSideConfig cfg;
  cfg.gcc.start_rate = DataRate::KilobitsPerSec(300);
  SendSideController c(cfg);
  c.ConfigureAllocation(DataRate::KilobitsPerSec(50), true);
  const int64_t both = c.allocator().limits().max_allocatable.bps();
  c.Start(Timestamp::Millis(0));
  for (int t = 0; t < 2000; ++t) c.Process(Timestamp::Millis(t));
  c.OnVideoEncoderReconfigured(Timestamp::Millis(2000));
  SM_EXPECT_TRUE(c.video_registered());
  SM_EXPECT_EQ(c.allocator().limits().max_allocatable.bps(), both);
  // The 2 s check already ran: the Process call at 2 s does not remove it again.
  c.Process(Timestamp::Millis(2000));
  SM_EXPECT_TRUE(c.video_registered());
  // The next check is at 4 s; timed_out_ is set, so a frameless stream stays.
  for (int t = 2001; t <= 4000; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_TRUE(c.video_registered());
}

SM_TEST(SendSideController, EncodedFramesKeepVideoInTheAllocator) {
  SendSideController c;
  c.ConfigureAllocation(DataRate::KilobitsPerSec(50), true);
  c.Start(Timestamp::Millis(0));
  for (int t = 0; t <= 10000; ++t) {
    if (t % 100 == 50) c.OnVideoFrameEncoded(Timestamp::Millis(t));
    c.Process(Timestamp::Millis(t));
  }
  SM_EXPECT_TRUE(c.video_registered());
  // Frames stop after 9.95 s: the 10 s check still saw one, the 12 s check sees none and removes it.
  for (int t = 10001; t <= 12000; ++t) c.Process(Timestamp::Millis(t));
  SM_EXPECT_FALSE(c.video_registered());
  // The first frame after the time-out registers it at once (disable_padding_).
  c.OnVideoFrameEncoded(Timestamp::Millis(12500));
  SM_EXPECT_TRUE(c.video_registered());
  SM_EXPECT_EQ(c.allocator().limits().max_allocatable.bps(), DataRate::KilobitsPerSec(50).bps() + SendSideConfig{}.video_max.bps());
}
