// Section 4 end-to-end tests.
#include <map>
#include <optional>

#include "timing_sims.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::e2e;
using namespace sm::timing;

// F4.1 acceptance (3), libwebrtc's behaviour (issue #899): over a reordering,
// duplicating link every frame reaches the frame buffer intact and is decoded
// once. A copy that arrives while its packet is buffered is a duplicate; one
// that arrives after the frame was handed out is taken again (the slot is
// empty, packet_buffer.cc InsertPacket), so a one-packet frame can be
// assembled twice, and the frame buffer drops the repeat as
// FrameBuffer::InsertFrame does (already inserted, or at or below the last
// decoded id). Frames are decoded in order, 60 ms after they are complete (a
// jitter-buffer delay, so a late copy can still land before the decode), and
// each decode calls ClearTo with the frame's last packet, as
// RtpVideoStreamReceiver2::FrameDecoded does; a copy behind it is then old.
SM_TEST(E2E_PacketBuffer, ReorderedDuplicatedStreamDecodesEveryFrameOnce) {
  sim::LinkConfig lc;
  lc.jitter_stddev = TimeDelta::Millis(8);
  lc.duplicate_rate = 0.03;
  sim::NetworkLink<std::pair<RtpPacket, bool>> link(lc, 3);
  PacketBuffer pb;
  uint16_t seq = 60000;  // Crosses the 16-bit wrap during the run.
  const int frames = 1500;
  auto packets_of = [](int f) { return f % 3 == 0 ? 1 : 6; };  // One-packet frames too.
  for (int f = 0; f < frames; ++f) {
    const int per = packets_of(f);
    for (int i = 0; i < per; ++i) {
      RtpPacket p;
      p.seq = seq++;
      p.timestamp = static_cast<uint32_t>(f * 3000);
      p.marker = i == per - 1;
      p.payload = {static_cast<uint8_t>(f), static_cast<uint8_t>(i)};
      link.Send(Timestamp::Millis(f * 33 + i), {p, i == 0}, DataSize::Bytes(1000));
    }
  }
  std::vector<int> assembled(frames, 0), decoded(frames, 0);
  struct Buffered {
    int64_t id;  // The frame's last sequence number.
    Timestamp complete;
  };
  std::map<int, Buffered> frame_buffer;  // By frame index.
  std::optional<int64_t> last_decoded_id;
  int next = 0;  // The next frame to decode, in order.
  link.ReceiveEach(Timestamp::Seconds(1000), [&](std::pair<RtpPacket, bool> pk, Timestamp at) {
    pb.Insert(pk.first, pk.second, at);
    for (auto& fr : pb.PopCompleteFrames()) {
      const int f = static_cast<int>(fr.timestamp / 3000);
      ++assembled[static_cast<size_t>(f)];
      SM_ASSERT_EQ(static_cast<int>(fr.packets.size()), packets_of(f));
      for (int i = 0; i < packets_of(f); ++i) SM_EXPECT_EQ(fr.packets[static_cast<size_t>(i)].payload[1], i);
      // FrameBuffer::InsertFrame: at or below the last decoded id, or already inserted.
      if (last_decoded_id && fr.last_seq <= *last_decoded_id) continue;
      frame_buffer.emplace(f, Buffered{fr.last_seq, at});
    }
    for (auto it = frame_buffer.find(next); it != frame_buffer.end() && it->second.complete + TimeDelta::Millis(60) <= at;
         it = frame_buffer.find(next)) {
      ++decoded[static_cast<size_t>(next)];
      last_decoded_id = it->second.id;
      pb.ClearTo(it->second.id);
      frame_buffer.erase(it);
      ++next;
    }
  });
  for (auto it = frame_buffer.find(next); it != frame_buffer.end(); it = frame_buffer.find(next)) {
    ++decoded[static_cast<size_t>(next)];
    frame_buffer.erase(it);
    ++next;
  }
  int once = 0, twice = 0;
  for (int f = 0; f < frames; ++f) {
    once += decoded[static_cast<size_t>(f)] == 1;
    twice += assembled[static_cast<size_t>(f)] > 1;
  }
  SM_RECORD("frames_decoded_once_ratio", static_cast<double>(once) / frames);
  SM_RECORD("frames_assembled_twice", twice);
  SM_RECORD("reordered_packets", pb.stats().reordered);
  SM_RECORD("duplicates_dropped", pb.stats().duplicates);
  SM_RECORD("copies_reinserted", pb.stats().reinserted);
  SM_RECORD("copies_too_old", pb.stats().too_old);
  SM_EXPECT_EQ(once, frames);
  SM_EXPECT_GT(twice, 0);
  SM_EXPECT_EQ(pb.stats().cleared, 0u);
  SM_EXPECT_GT(pb.stats().reordered, 100u);
  // Every copy the link made is a duplicate of a buffered packet, taken again, or behind ClearTo.
  SM_EXPECT_EQ(pb.stats().duplicates + pb.stats().reinserted + pb.stats().too_old, link.stats().duplicated);
  SM_EXPECT_GT(pb.stats().reinserted, 0u);
}

SM_TEST(E2E_RtcpStats, MeasuresRttLossAndJitter) {
  sim::LinkConfig fwd_cfg;
  fwd_cfg.base_delay = TimeDelta::Millis(30);
  fwd_cfg.jitter_stddev = TimeDelta::Millis(5);
  fwd_cfg.allow_reordering = false;
  fwd_cfg.loss_rate = 0.05;
  sim::LinkConfig rev_cfg;
  rev_cfg.base_delay = TimeDelta::Millis(30);
  sim::NetworkLink<std::vector<uint8_t>> fwd(fwd_cfg, 1), rev(rev_cfg, 2);
  ReceiveStatistics rx(1, 48000);
  RttEstimator rtt;
  Histogram fraction;
  for (int64_t t = 0; t < 60000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t % 20 == 0) {
      RtpPacket p;
      p.seq = static_cast<uint16_t>(t / 20);
      p.timestamp = static_cast<uint32_t>(t * 48);
      fwd.Send(now, p.Serialize(), DataSize::Bytes(100));
    }
    if (t % 1000 == 500) {
      SenderReport sr;
      sr.ssrc = 1;
      sr.ntp = NtpTime::FromTimestamp(now);
      sr.rtp_timestamp = static_cast<uint32_t>(t * 48);
      fwd.Send(now, SerializeSenderReport(sr), DataSize::Bytes(28));
    }
    fwd.ReceiveEach(now, [&](std::vector<uint8_t> b, Timestamp at) {
      if (b.size() > 1 && b[1] == 200) {
        if (auto sr = ParseSenderReport(b)) rx.OnSenderReport(*sr, at);
      } else if (auto p = RtpPacket::Parse(b)) {
        rx.OnPacket(p->seq, p->timestamp, at);
      }
    });
    if (t % 1000 == 999) {
      ReceiverReport rr{2, {rx.MakeReportBlock(now)}};
      fraction.Add(rr.blocks[0].fraction_lost / 256.0);
      rev.Send(now, SerializeReceiverReport(rr), DataSize::Bytes(32));
    }
    rev.ReceiveEach(now, [&](std::vector<uint8_t> b, Timestamp at) {
      if (auto rr = ParseReceiverReport(b)) rtt.OnReportBlock(rr->blocks[0], at);
    });
  }
  SM_RECORD("rtt_min_ms", rtt.min().ms_f());
  SM_RECORD("rtt_smoothed_ms", rtt.smoothed().ms_f());
  SM_RECORD("fraction_lost_mean", fraction.mean());
  SM_RECORD("jitter_ms", rx.jitter_ms());
  SM_EXPECT_NEAR(rtt.min().ms_f(), 60, 2);
  SM_EXPECT_NEAR(rtt.smoothed().ms_f(), 64, 6);
  SM_EXPECT_NEAR(fraction.mean(), 0.05, 0.015);
  SM_EXPECT_GT(rx.jitter_ms(), 1.0);
  SM_EXPECT_LT(rx.jitter_ms(), 8.0);
}

SM_TEST(E2E_DelayManager, TargetFollowsJitterStep) {
  AudioJbSimParams p;
  p.jitter = TimeDelta::Millis(5);
  p.jitter_second_half = TimeDelta::Millis(40);
  p.loss = 0;
  auto r = RunAudioJbSim(p);
  SM_RECORD("target_low_jitter_ms", r.target_first_half_ms);
  SM_RECORD("target_high_jitter_ms", r.target_second_half_ms);
  SM_EXPECT_LE(r.target_first_half_ms, 40);
  SM_EXPECT_GE(r.target_second_half_ms, 60);
}

SM_TEST(E2E_TimeStretch, LatencyRecoversAfterStall) {
  AudioJbSimParams p;
  p.jitter = TimeDelta::Millis(3);
  p.loss = 0;
  p.seconds = 30;
  p.stall_at = TimeDelta::Seconds(10);
  p.stall_for = TimeDelta::Millis(600);
  auto with = RunAudioJbSim(p);
  p.time_stretch = false;
  auto without = RunAudioJbSim(p);
  SM_RECORD("stretch_recover_s", with.recover_s);
  SM_RECORD("stretch_final_level_ms", with.final_level_ms);
  SM_RECORD("no_stretch_final_level_ms", without.final_level_ms);
  SM_EXPECT_GT(with.recover_s, 0.0);
  SM_EXPECT_LT(with.recover_s, 8.0);
  SM_EXPECT_GT(without.final_level_ms, with.final_level_ms + 300);
}

SM_TEST(E2E_AudioJitterBuffer, AdaptiveBeatsFixedBuffers) {
  AudioJbSimParams p;
  p.jitter = TimeDelta::Millis(25);
  p.loss = 0.02;
  auto adaptive = RunAudioJbSim(p);
  p.fixed_delay_ms = 20;
  auto small = RunAudioJbSim(p);
  p.fixed_delay_ms = 200;
  auto large = RunAudioJbSim(p);
  SM_RECORD("adaptive_concealment", adaptive.concealment_ratio);
  SM_RECORD("adaptive_mean_delay_ms", adaptive.mean_buffer_delay_ms);
  SM_RECORD("fixed20_concealment", small.concealment_ratio);
  SM_RECORD("fixed20_final_level_ms", small.final_level_ms);
  SM_RECORD("adaptive_final_level_ms", adaptive.final_level_ms);
  SM_RECORD("fixed200_mean_delay_ms", large.mean_buffer_delay_ms);
  // Concealment is the 2% loss plus at most 1.5% late packets.
  SM_EXPECT_LT(adaptive.concealment_ratio, p.loss + 0.015);
  // A large fixed buffer pays 2-4x the latency for the same robustness.
  SM_EXPECT_LT(adaptive.mean_buffer_delay_ms, large.mean_buffer_delay_ms * 0.5);
  // A small buffer without acceleration waits for every late packet, so its
  // latency ratchets up after jitter spikes and cannot return to its target.
  SM_EXPECT_GE(small.final_level_ms, 3 * 20.0);
}

// Concealment through NetEq (Expand, Merge, Normal) at 10% packet loss: every
// lost packet is concealed, and the concealed audio keeps the speech's level
// instead of dropping to silence.
SM_TEST(E2E_Plc, SpeechOverLossyLink) {
  sim::LinkConfig cfg;
  cfg.base_delay = TimeDelta::Millis(40);
  cfg.loss_rate = 0.10;
  auto r = RunPlcSim(cfg, 20.0, 11);
  SM_RECORD("plc_loss", r.loss);
  SM_RECORD("plc_concealed", r.concealed);
  SM_RECORD("plc_events", static_cast<double>(r.events));
  SM_RECORD("plc_expand_level_ratio", r.expand_level_ratio);
  SM_RECORD("plc_normal_level_ratio", r.normal_level_ratio);
  SM_EXPECT_GT(r.loss, 0.07);
  SM_EXPECT_GT(r.concealed, r.loss * 0.5);
  SM_EXPECT_LT(r.concealed, r.loss * 2.0);
  SM_EXPECT_GT(r.events, 0u);
  SM_EXPECT_GT(r.expand_level_ratio, 0.6);
  SM_EXPECT_GT(r.normal_level_ratio, 0.8);
}

SM_TEST(E2E_VideoJitter, EstimatorPreventsLateFrames) {
  auto with = RunVideoJitterSim(true, TimeDelta::Millis(10), 4);
  auto without = RunVideoJitterSim(false, TimeDelta::Millis(10), 4);
  SM_RECORD("video_late_with_estimator", with.late_ratio);
  SM_RECORD("video_late_without", without.late_ratio);
  SM_RECORD("video_mean_delay_ms", with.mean_delay_ms);
  SM_RECORD("video_mean_jitter_ms", with.mean_jitter_ms);
  SM_EXPECT_LT(with.late_ratio, 0.03);
  SM_EXPECT_GT(without.late_ratio, 0.3);
  SM_EXPECT_LT(with.mean_delay_ms, 200);
}
