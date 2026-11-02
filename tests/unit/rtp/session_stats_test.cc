// Issue #988 (C8): Session::GetStats, the getStats-style snapshot.
#include <algorithm>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/rtp/session.h"
#include "sm/video/freeze_detector.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
const test::TestVideoParser kParser;

class NullDecoder : public AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t) override {
    return std::vector<float>(960, 0.0f);
  }
};

SessionConfig Cfg(uint32_t ssrc) {
  SessionConfig c;
  c.local_ssrc = ssrc;
  c.cname = "test";
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.extensions.Register(3, sync::uri::kMid);
  c.mid_audio = "a";
  c.mid_video = "v";
  return c;
}

VideoStreamConfig Video() {
  VideoStreamConfig v;
  v.ssrc = 0x200;
  v.rtx_ssrc = 0x300;
  v.parser = &kParser;
  return v;
}

// `a` sends audio and video to `b`; `b` reports back.
struct Call {
  Call() : a(Cfg(1)), b(Cfg(2)) {
    AudioStreamConfig au;
    au.ssrc = 0x100;
    a_audio = a.AddAudioSend(au);
    a_video = a.AddVideoSend(Video());
    b_audio = b.AddAudioReceive(au, &dec);
    b_video = b.AddVideoReceive(Video());
  }
  // Runs both sessions for `ms` from `from_ms`: one audio frame per 20 ms and one video frame per
  // 100 ms from `a`, delivered to `b` 20 ms later, and `b`'s RTCP back to `a` at once. Video
  // packets whose index in the frame is in `drop` are lost.
  void Run(int64_t from_ms, int64_t ms, std::vector<size_t> drop = {}) {
    std::vector<std::pair<int64_t, std::vector<uint8_t>>> in_flight;
    for (int64_t t = from_ms; t < from_ms + ms; ++t) {
      const Timestamp now = Timestamp::Millis(t);
      if (t % 20 == 0) a.SendAudio(a_audio, {static_cast<uint32_t>(t * 48), std::vector<uint8_t>(60, 7), false}, now);
      if (t % 100 == 0) {
        auto chunks = test::MakeTestFrameChunks(t == 0, 3000, 1188);
        a.SendVideoChunks(a_video, chunks, static_cast<uint32_t>(t * 90), now, t == 0);
      }
      a.Process(now);
      size_t video_index = 0;
      for (auto& o : a.TakeOutgoing()) {
        if (o.kind == OutgoingKind::kVideo) {
          const bool lost = std::find(drop.begin(), drop.end(), video_index++) != drop.end();
          if (lost) continue;
        }
        in_flight.emplace_back(t + 20, std::move(o.bytes));
      }
      for (auto it = in_flight.begin(); it != in_flight.end();) {
        if (it->first <= t) {
          b.OnPacket(it->second, now);
          it = in_flight.erase(it);
        } else {
          ++it;
        }
      }
      b.Process(now);
      for (auto& o : b.TakeOutgoing()) a.OnPacket(o.bytes, now);
      float pcm[480];
      if (t % 10 == 0) b.GetAudio(b_audio, pcm, now);
      b.TakeVideoFrames(b_video);
    }
  }
  NullDecoder dec;
  Session a, b;
  AudioSendId a_audio;
  VideoSendId a_video;
  AudioRecvId b_audio;
  VideoRecvId b_video;
};
}  // namespace

#if SM_ENABLE_STATS

SM_TEST(SessionStats, EmptySessionReportsNoStreams) {
  Session s(Cfg(1));
  StatsReport r;
  SM_EXPECT_TRUE(s.GetStats(Timestamp::Millis(5), &r) == ObsStatus::kOk);
  SM_EXPECT_EQ(r.timestamp.ms(), 5);
  SM_EXPECT_EQ(r.outbound.size(), 0u);
  SM_EXPECT_EQ(r.inbound.size(), 0u);
  SM_EXPECT_EQ(r.transport.datagrams_received, 0u);
}

SM_TEST(SessionStats, SendAndReceiveCountersMatchTheStreams) {
  Call c;
  c.Run(0, 3000);
  StatsReport ra, rb;
  SM_ASSERT_TRUE(c.a.GetStats(Timestamp::Millis(3000), &ra) == ObsStatus::kOk);
  SM_ASSERT_TRUE(c.b.GetStats(Timestamp::Millis(3000), &rb) == ObsStatus::kOk);

  SM_ASSERT_EQ(ra.outbound.size(), 2u);
  const OutboundRtpStats& oa = ra.outbound[0];
  const OutboundRtpStats& ov = ra.outbound[1];
  SM_EXPECT_EQ(oa.ssrc, 0x100u);
  SM_EXPECT_FALSE(oa.video);
  SM_EXPECT_EQ(ov.ssrc, 0x200u);
  SM_EXPECT_TRUE(ov.video);
  SM_EXPECT_TRUE(ov.rtx_ssrc.has_value() && *ov.rtx_ssrc == 0x300u);
  SM_EXPECT_EQ(oa.packets_sent, c.a.audio_sender(c.a_audio).stats().media_packets);
  SM_EXPECT_EQ(ov.packets_sent, c.a.video_sender(c.a_video).stats().media_packets);
  SM_EXPECT_EQ(ov.frames_sent, c.a.video_sender(c.a_video).stats().frames);
  SM_EXPECT_GT(ov.payload_bytes_sent, 0u);
  // The peer's receiver reports came back: remote loss and an RTT.
  SM_EXPECT_TRUE(ov.remote_packets_lost.has_value());
  SM_EXPECT_EQ(ov.remote_packets_lost.value_or(-1), 0);
  SM_EXPECT_GT(ra.transport.round_trip_time.ms(), 0);
  SM_EXPECT_GT(ra.transport.target_bitrate.bps(), 0);

  SM_ASSERT_EQ(rb.inbound.size(), 2u);
  const InboundRtpStats& ia = rb.inbound[0];
  const InboundRtpStats& iv = rb.inbound[1];
  SM_EXPECT_EQ(ia.ssrc, 0x100u);
  SM_EXPECT_EQ(iv.ssrc, 0x200u);
  SM_EXPECT_EQ(iv.packets_received, ov.packets_sent);
  SM_EXPECT_EQ(iv.packets_lost, 0);
  SM_EXPECT_GT(iv.frames_received, 0u);
  SM_EXPECT_GT(ia.total_samples_received, 0u);
  SM_EXPECT_EQ(rb.transport.datagrams_received, c.b.stats().datagrams_in);
  // No freeze detector attached: no freeze stats.
  SM_EXPECT_FALSE(iv.freeze_count.has_value());
}

SM_TEST(SessionStats, LossIsCountedAndNackedOnTheReceiveSide) {
  Call c;
  c.Run(0, 2000, {1});  // The second packet of every frame is lost (until RTX resends it).
  StatsReport rb;
  SM_ASSERT_TRUE(c.b.GetStats(Timestamp::Millis(2000), &rb) == ObsStatus::kOk);
  SM_ASSERT_EQ(rb.inbound.size(), 2u);
  const InboundRtpStats& iv = rb.inbound[1];
  SM_EXPECT_GT(iv.nack_count, 0u);
  SM_EXPECT_GT(iv.rtx_packets_received, 0u);
  SM_EXPECT_GT(rb.transport.nack_sent, 0u);
}

SM_TEST(SessionStats, FreezeStatsComeFromTheAppsDetector) {
  Call c;
  video::FreezeDetector fd;
  c.b.SetFreezeDetector(c.b_video, &fd);
  for (int i = 0; i < 10; ++i) fd.OnFrameRendered(Timestamp::Millis(i * 33));
  fd.OnFrameRendered(Timestamp::Millis(9 * 33 + 1500));  // A 1.5 s gap: a freeze.
  StatsReport rb;
  SM_ASSERT_TRUE(c.b.GetStats(Timestamp::Millis(2000), &rb) == ObsStatus::kOk);
  SM_ASSERT_EQ(rb.inbound.size(), 2u);
  SM_EXPECT_TRUE(rb.inbound[1].freeze_count.has_value());
  SM_EXPECT_EQ(rb.inbound[1].freeze_count.value_or(0), 1u);
  SM_EXPECT_GT(rb.inbound[1].total_freezes_duration_ms.value_or(0), 1000.0);
  SM_EXPECT_FALSE(rb.inbound[0].freeze_count.has_value());  // Audio.
  c.b.SetFreezeDetector(c.b_video, nullptr);
  SM_ASSERT_TRUE(c.b.GetStats(Timestamp::Millis(2000), &rb) == ObsStatus::kOk);
  SM_EXPECT_FALSE(rb.inbound[1].freeze_count.has_value());
}

SM_TEST(SessionStats, GetStatsChangesNothing) {
  // Two identical calls; one asks for stats every 10 ms. Their packets stay identical.
  Call c1, c2;
  for (int64_t t = 0; t < 2000; t += 100) {
    c1.Run(t, 100, {2});
    c2.Run(t, 100, {2});
    StatsReport r;
    c2.a.GetStats(Timestamp::Millis(t + 100), &r);
    c2.b.GetStats(Timestamp::Millis(t + 100), &r);
  }
  SM_EXPECT_EQ(c1.a.stats().datagrams_out, c2.a.stats().datagrams_out);
  SM_EXPECT_EQ(c1.b.stats().datagrams_out, c2.b.stats().datagrams_out);
  SM_EXPECT_EQ(c1.a.send_side().target().bps(), c2.a.send_side().target().bps());
}

#else

SM_TEST(SessionStats, CompiledOutReturnsNotSupported) {
  Session s(Cfg(1));
  StatsReport r;
  r.timestamp = Timestamp::Millis(7);
  SM_EXPECT_TRUE(s.GetStats(Timestamp::Millis(5), &r) == ObsStatus::kNotSupported);
  SM_EXPECT_EQ(r.timestamp.ms(), 7);  // Untouched.
}

#endif
