#include <algorithm>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/core/random.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/session.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

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

struct Pair {
  Pair() : a(Cfg(1)), b(Cfg(2)) {
    AudioStreamConfig au;
    au.ssrc = 0x100;
    a_audio = a.AddAudioSend(au);
    a_video = a.AddVideoSend(Video());
    b_audio = b.AddAudioReceive(au, &dec);
    b_video = b.AddVideoReceive(Video());
  }
  NullDecoder dec;
  Session a, b;
  AudioSendId a_audio;
  VideoSendId a_video;
  AudioRecvId b_audio;
  VideoRecvId b_video;
};

// Runs `a` for `ms` and returns what it sent.
std::vector<OutgoingPacket> Pump(Session& s, int64_t from_ms, int64_t ms) {
  std::vector<OutgoingPacket> out;
  for (int64_t t = from_ms; t < from_ms + ms; ++t) {
    s.Process(Timestamp::Millis(t));
    for (auto& p : s.TakeOutgoing()) out.push_back(std::move(p));
  }
  return out;
}
}  // namespace

SM_TEST(Session, MediaIsRoutedToTheMatchingReceiverAndRtcpGoesToTheEngine) {
  Pair p;
  Bytes frame(60, 7);
  p.a.SendAudio(p.a_audio, {960, frame, false}, Timestamp::Millis(0));
  auto chunks = test::MakeTestFrameChunks(true, 3000, 1188);
  p.a.SendVideoChunks(p.a_video, chunks, 0, Timestamp::Millis(0));
  size_t audio = 0, video = 0, rtcp = 0;
  for (auto& o : Pump(p.a, 0, 1200)) {
    audio += o.kind == OutgoingKind::kAudio;
    video += o.kind == OutgoingKind::kVideo;
    rtcp += o.kind == OutgoingKind::kRtcp;
    p.b.OnPacket(o.bytes, Timestamp::Millis(50));
  }
  SM_EXPECT_EQ(audio, 1u);
  SM_EXPECT_EQ(video, chunks.size());
  SM_EXPECT_GT(rtcp, 0u);  // A sender report with SDES.
  SM_EXPECT_EQ(p.b.audio_receiver(p.b_audio).stats().media, 1u);
  SM_EXPECT_EQ(p.b.video_receiver(p.b_video).stats().media, chunks.size());
  SM_EXPECT_EQ(p.b.stats().unroutable, 0u);
  SM_EXPECT_GT(p.b.rtcp().stats().sr_received, 0u);
}

SM_TEST(Session, ZeroByteVideoFrameSendsNothingAndIsCounted) {
  Pair p;
  p.a.SendVideo(p.a_video, {0, {}}, Timestamp::Millis(0));
  SM_EXPECT_EQ(p.a.stats().empty_video_frames, 1u);
  SM_EXPECT_EQ(p.a.video_sender(p.a_video).stats().media_packets, 0u);
  p.a.SendVideoChunks(p.a_video, {}, 0, Timestamp::Millis(0));
  SM_EXPECT_EQ(p.a.stats().empty_video_frames, 2u);
}

SM_TEST(Session, SendVideoPacketizesAnAnnexBFrameWithinTheMtu) {
  Pair p;
  const Bytes frame = MakeFakeH264Frame(true, 5000);
  p.a.SendVideo(p.a_video, {0, frame, true}, Timestamp::Millis(0));
  std::vector<RtpPacket> sent;
  for (auto& o : Pump(p.a, 0, 500)) {
    if (o.kind != OutgoingKind::kVideo) continue;
    SM_EXPECT_LE(o.bytes.size(), 1200u);
    sent.push_back(*RtpPacket::Parse(o.bytes));
  }
  // SPS+PPS in a STAP-A, then the IDR slice in FU-A fragments; the marker is
  // on the last packet only, and the payloads rebuild the access unit (with
  // the keyframe's SPS VUI rewritten, #342).
  SM_ASSERT_GE(sent.size(), 5u);
  SM_EXPECT_EQ(sent[0].payload[0] & 0x1F, kH264NalStapA);
  for (size_t i = 0; i < sent.size(); ++i) SM_EXPECT_EQ(sent[i].marker, i + 1 == sent.size());
  Bytes rebuilt;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(sent, rebuilt));
  SM_EXPECT_TRUE(rebuilt == ParseOutgoingBitstreamAndRewrite(frame));
  // Full-size chunks leave room for the header extensions.
  p.a.SendVideo(p.a_video, {3000, MakeFakeH264Frame(false, 1188 * 3)}, Timestamp::Millis(500));
  for (auto& o : Pump(p.a, 500, 500)) {
    if (o.kind == OutgoingKind::kVideo) SM_EXPECT_LE(o.bytes.size(), 1200u);
  }
}

// FrameEncodeMetadataWriter::UpdateBitstream (#342): a keyframe's SPS gets
// libwebrtc's VUI, 5 bytes more on the fake encoder's SPS (the test bench's
// STAP-A [SPS, PPS] packet grows from 72 to 77 bytes, as libwebrtc's); delta
// frames are sent as given.
SM_TEST(Session, KeyframeSpsGetsLibwebrtcsVui) {
  Pair p;
  p.a.SendVideo(p.a_video, {0, MakeFakeH264Frame(true, 3000), true}, Timestamp::Millis(0));
  std::vector<OutgoingPacket> sent;
  for (auto& o : Pump(p.a, 0, 300)) {
    if (o.kind == OutgoingKind::kVideo) sent.push_back(std::move(o));
  }
  SM_ASSERT_GE(sent.size(), 2u);
  const RtpPacket stap = *RtpPacket::Parse(sent[0].bytes);
  SM_EXPECT_EQ(stap.payload[0] & 0x1F, kH264NalStapA);
  // STAP-A header, then [size, SPS] and [size, PPS].
  SM_ASSERT_GE(stap.payload.size(), 3u + 13u + 2u + 11u);
  SM_EXPECT_EQ((stap.payload[1] << 8) | stap.payload[2], 13);
  SM_EXPECT_EQ(stap.payload.size(), 1u + 2u + 13u + 2u + 11u);
  SM_EXPECT_EQ(p.a.stats().sps.rewritten, 1u);
  SM_EXPECT_EQ(p.a.stats().sps.parse_failures, 0u);

  const Bytes delta = MakeFakeH264Frame(false, 500);
  p.a.SendVideo(p.a_video, {3000, delta, false}, Timestamp::Millis(300));
  std::vector<RtpPacket> delta_sent;
  for (auto& o : Pump(p.a, 300, 300)) {
    if (o.kind == OutgoingKind::kVideo) delta_sent.push_back(*RtpPacket::Parse(o.bytes));
  }
  Bytes rebuilt;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(delta_sent, rebuilt));
  SM_EXPECT_TRUE(rebuilt == delta);
  SM_EXPECT_EQ(p.a.stats().sps.rewritten, 1u);
}

SM_TEST(Session, NackFromThePeerIsAnsweredWithRtxOnTheRtxSsrc) {
  Pair p;
  auto chunks = test::MakeTestFrameChunks(true, 3000, 1188);
  p.a.SendVideoChunks(p.a_video, chunks, 0, Timestamp::Millis(0));
  std::vector<uint16_t> seqs;
  for (auto& o : Pump(p.a, 0, 300)) {
    if (o.kind == OutgoingKind::kVideo) seqs.push_back(RtpPacket::Parse(o.bytes)->seq);
  }
  SM_ASSERT_EQ(seqs.size(), chunks.size());
  // The peer's RTCP engine builds the NACK.
  RtcpConfig rc;
  rc.local_ssrc = 2;
  RtcpEngine peer(rc);
  peer.QueueNack(0x200, std::vector<uint16_t>{seqs[1]});
  for (auto& r : peer.Process(Timestamp::Millis(400))) p.a.OnPacket(r.bytes, Timestamp::Millis(400));
  size_t rtx = 0;
  for (auto& o : Pump(p.a, 400, 300)) {
    if (o.kind != OutgoingKind::kRtx) continue;
    ++rtx;
    SM_EXPECT_EQ(RtpPacket::Parse(o.bytes)->ssrc, 0x300u);
  }
  SM_EXPECT_EQ(rtx, 1u);
}

// ModuleRtpRtcpImpl2 (rtp_rtcp_impl2.cc PeriodicUpdate, OnReceivedNack; issue #217):
// the packet history takes the RTT once a second from the last sample, and
// 5 ms + that RTT on each NACK, not every sample as it arrives.
SM_TEST(Session, PacketHistoryRttFollowsThePeriodicUpdateAndNacks) {
  Pair p;
  constexpr int64_t kOneWayMs = 40;
  struct InFlight {
    int64_t at;
    bool to_b;
    Bytes bytes;
  };
  std::vector<InFlight> wire;
  const loss::RtxSender& history = p.a.video_sender(p.a_video).rtx();
  int64_t first_sample_ms = -1;
  TimeDelta history_at_first_sample = TimeDelta::Zero();
  for (int64_t t = 0; t < 3100; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 33 == 0) {
      auto chunks = test::MakeTestFrameChunks(t == 0, 1500, 1188);
      p.a.SendVideoChunks(p.a_video, chunks, static_cast<uint32_t>(t * 90), now);
    }
    for (size_t i = 0; i < wire.size();) {
      if (wire[i].at > t) {
        ++i;
        continue;
      }
      (wire[i].to_b ? p.b : p.a).OnPacket(wire[i].bytes, now);
      wire.erase(wire.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for (const auto& e : p.a.TakeEvents()) {
      if (e.type == SessionEventType::kRttUpdated && first_sample_ms < 0) {
        first_sample_ms = t;
        history_at_first_sample = history.rtt();
      }
    }
    p.a.Process(now);
    for (auto& o : p.a.TakeOutgoing()) wire.push_back({t + kOneWayMs, true, std::move(o.bytes)});
    p.b.Process(now);
    for (auto& o : p.b.TakeOutgoing()) wire.push_back({t + kOneWayMs, false, std::move(o.bytes)});
    p.b.TakeEvents();
  }
  // The first sample doesn't reach the history; the next update (1 s ticks from the start) does.
  SM_ASSERT_GE(first_sample_ms, 0);
  SM_EXPECT_LT(first_sample_ms, 2000);
  SM_EXPECT_TRUE(history_at_first_sample == TimeDelta::MinusInfinity());
  SM_EXPECT_EQ(history.rtt().ms(), p.a.rtt().ms());
  const int64_t periodic_ms = history.rtt().ms();
  SM_EXPECT_GE(periodic_ms, 2 * kOneWayMs);
  // A NACK sets 5 ms + the periodic RTT (RTPSender::OnReceivedNack).
  RtcpConfig rc;
  rc.local_ssrc = 2;
  RtcpEngine peer(rc);
  peer.QueueNack(0x200, std::vector<uint16_t>{1});
  for (auto& r : peer.Process(Timestamp::Millis(3100))) p.a.OnPacket(r.bytes, Timestamp::Millis(3100));
  SM_EXPECT_EQ(history.rtt().ms(), 5 + periodic_ms);
}

SM_TEST(Session, PliFromThePeerBecomesAKeyframeEvent) {
  Pair p;
  RtcpConfig rc;
  rc.local_ssrc = 2;
  RtcpEngine peer(rc);
  peer.QueuePli(0x200);
  for (auto& r : peer.Process(Timestamp::Millis(10))) p.a.OnPacket(r.bytes, Timestamp::Millis(10));
  bool seen = false;
  for (const auto& e : p.a.TakeEvents()) {
    if (e.type == SessionEventType::kKeyframeRequested) {
      seen = true;
      SM_EXPECT_EQ(e.ssrc, 0x200u);
    }
  }
  SM_EXPECT_TRUE(seen);
}

SM_TEST(Session, DemuxesRtcpAndRtpByTheSecondByte) {
  Pair p;
  // A one-byte, a truncated RTP header and an RTCP-looking runt.
  for (Bytes junk : {Bytes{0x80}, Bytes{0x80, 96, 0, 1}, Bytes{0x81, 201, 0}}) p.b.OnPacket(junk, Timestamp::Millis(0));
  SM_EXPECT_EQ(p.b.stats().datagrams_in, 3u);
  SM_EXPECT_EQ(p.b.stats().malformed, 3u);
}

SM_TEST(Session, RandomAndTruncatedDatagramsNeverCrashAndAreCounted) {
  Pair p;
  Random rng(7);
  // Real packets, truncated at every length and corrupted.
  p.a.SendAudio(p.a_audio, {0, Bytes(60, 1), false}, Timestamp::Millis(0));
  p.a.SendVideoChunks(p.a_video, test::MakeTestFrameChunks(true, 2500, 1188), 0, Timestamp::Millis(0));
  auto real = Pump(p.a, 0, 1200);
  SM_ASSERT_TRUE(!real.empty());
  uint64_t fed = 0;
  for (const auto& o : real) {
    for (size_t n = 0; n <= o.bytes.size(); ++n) {
      p.b.OnPacket(std::span<const uint8_t>(o.bytes.data(), n), Timestamp::Millis(100));
      ++fed;
    }
    for (int k = 0; k < 50; ++k) {
      Bytes c = o.bytes;
      c[static_cast<size_t>(rng.Uniform(0, static_cast<double>(c.size()) - 0.001))] = static_cast<uint8_t>(rng.Uniform(0, 255.9));
      p.b.OnPacket(c, Timestamp::Millis(100));
      ++fed;
    }
  }
  for (int k = 0; k < 2000; ++k) {
    Bytes junk(static_cast<size_t>(rng.Uniform(0, 80)));
    for (auto& b : junk) b = static_cast<uint8_t>(rng.Uniform(0, 255.9));
    p.b.OnPacket(junk, Timestamp::Millis(100));
    ++fed;
  }
  p.b.Process(Timestamp::Millis(200));
  SM_EXPECT_EQ(p.b.stats().datagrams_in, fed);
  SM_EXPECT_GT(p.b.stats().malformed, 0u);
}

SM_TEST(Session, MidIsRepeatedUntilThePeerAnswersWithOne) {
  Pair p;
  for (int i = 0; i < 12; ++i) p.a.SendAudio(p.a_audio, {static_cast<uint32_t>(i * 960), Bytes(40, 1), false}, Timestamp::Millis(i * 20));
  SM_EXPECT_EQ(p.a.audio_sender(p.a_audio).stats().mid_packets, 8u);  // mid_packets default.

  Pair q;
  // Packets from a peer that already sends a MID acknowledge ours at once.
  Pair peer;
  peer.a.SendAudio(peer.a_audio, {0, Bytes(40, 1), false}, Timestamp::Millis(0));
  for (auto& o : Pump(peer.a, 0, 100)) q.a.OnPacket(o.bytes, Timestamp::Millis(100));
  for (int i = 0; i < 12; ++i) q.a.SendAudio(q.a_audio, {static_cast<uint32_t>(i * 960), Bytes(40, 1), false}, Timestamp::Millis(200 + i * 20));
  SM_EXPECT_EQ(q.a.audio_sender(q.a_audio).stats().mid_packets, 0u);
}

SM_TEST(Session, RatesSplitTheTargetBetweenAudioAndVideo) {
  SessionConfig c = Cfg(1);
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  Session s(c);
  AudioStreamConfig a;
  a.ssrc = 1;
  a.nominal_rate = DataRate::KilobitsPerSec(100);
  s.AddAudioSend(a);
  s.AddVideoSend(Video());
  s.Process(Timestamp::Millis(0));
  SendRates r = s.rates();
  SM_EXPECT_EQ(r.audio_rate.bps(), 100000);
  SM_EXPECT_LE(r.video_target.bps(), r.target.bps() - 100000);
  SM_EXPECT_GT(r.video_target.bps(), 0);
}

SM_TEST(Session, ExportsMetrics) {
  Pair p;
  MapMetricsWriter w;
  p.a.ExportMetrics(w);
  SM_EXPECT_TRUE(w.values().count("datagrams_in") == 1);
  SM_EXPECT_TRUE(w.values().count("target_kbps") == 1);
}

namespace {
// Runs two sessions back to back (no loss, no delay) for `ms`, sending one
// video frame every 33 ms from `a`, and returns a's events.
std::vector<SessionEvent> Exchange(Pair& p, int64_t ms) {
  std::vector<SessionEvent> events;
  for (int64_t t = 0; t < ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 33 == 0) {
      auto chunks = test::MakeTestFrameChunks(t == 0, 1500, 1188);
      p.a.SendVideoChunks(p.a_video, chunks, static_cast<uint32_t>(t * 90), now);
    }
    p.a.Process(now);
    for (auto& o : p.a.TakeOutgoing()) p.b.OnPacket(o.bytes, now);
    p.b.Process(now);
    for (auto& o : p.b.TakeOutgoing()) p.a.OnPacket(o.bytes, now);
    for (auto& e : p.a.TakeEvents()) events.push_back(e);
    p.b.TakeEvents();
  }
  return events;
}
}  // namespace

SM_TEST(Session, ReportBlocksFromThePeerBecomeEventsWithTheLossFraction) {
  Pair p;
  auto events = Exchange(p, 1600);
  size_t blocks = 0;
  for (const auto& e : events) {
    if (e.type != SessionEventType::kReportBlock) continue;
    ++blocks;
    SM_EXPECT_TRUE(e.ssrc == 0x100 || e.ssrc == 0x200);
    SM_EXPECT_EQ(e.loss_fraction, 0.0);
  }
  SM_EXPECT_GE(blocks, 2u);  // One receiver report: an audio and a video block.
}

SM_TEST(Session, SetRttFeedsTheSessionWithoutRaisingAnEvent) {
  Pair p;
  p.b.SetRtt(TimeDelta::Millis(250));
  SM_EXPECT_EQ(p.b.rtt().ms(), 250);
  SM_EXPECT_TRUE(p.b.TakeEvents().empty());
}

// A receive-only video stream takes its NACK RTT from CallStats, as
// VideoReceiveStream2::OnRttUpdate does: a measurement reaches it at the next
// 1 s periodic update (ModuleRtpRtcpImpl2::PeriodicUpdate), and the largest
// report of the last 1.5 s wins (call_stats2.cc).
SM_TEST(Session, AReceiveOnlyVideoStreamTakesTheMaximumRttFromCallStats) {
  Session s(Cfg(2));
  VideoRecvId v = s.AddVideoReceive(Video());
  const auto nack_rtt = [&] { return s.video_receiver(v).nack().rtt().ms(); };
  Pump(s, 0, 100);
  const int64_t initial = nack_rtt();
  s.SetRtt(TimeDelta::Millis(250));
  Pump(s, 100, 899);  // Up to 998 ms: before the first periodic update.
  SM_EXPECT_EQ(nack_rtt(), initial);
  Pump(s, 999, 2);  // The update at 1000 ms.
  SM_EXPECT_EQ(nack_rtt(), 250);
  s.SetRtt(TimeDelta::Millis(50));
  Pump(s, 1001, 1000);  // 2000 ms: the 250 ms report is 1 s old and still the maximum.
  SM_EXPECT_EQ(nack_rtt(), 250);
  s.SetRtt(TimeDelta::Millis(60));
  Pump(s, 2001, 1000);  // 3000 ms: 250 ms has expired; 60 is the largest left.
  SM_EXPECT_EQ(nack_rtt(), 60);
}

// The session exports CallStats::LastProcessedRtt() as `call_stats_rtt_ms`,
// what libwebrtc's Call::GetStats().rtt_ms reads (call.cc): -1 until the
// first 1 s update, then the 70/30 smoothed average of the reports, not the
// last raw RTT that rtt() returns (issue #370).
SM_TEST(Session, ExportsCallStatsLastProcessedRtt) {
  Session s(Cfg(2));
  s.AddVideoReceive(Video());
  const auto gauge = [&] {
    MapMetricsWriter w;
    s.ExportMetrics(w);
    return w.Get("call_stats_rtt_ms", -2);
  };
  Pump(s, 0, 100);
  SM_EXPECT_EQ(gauge(), -1.0);
  s.SetRtt(TimeDelta::Millis(250));
  Pump(s, 100, 899);  // Up to 998 ms: before the first periodic update.
  SM_EXPECT_EQ(gauge(), -1.0);
  Pump(s, 999, 2);  // The update at 1000 ms.
  SM_EXPECT_EQ(gauge(), 250.0);
  s.SetRtt(TimeDelta::Millis(50));
  SM_EXPECT_EQ(s.rtt().ms(), 50);
  SM_EXPECT_EQ(gauge(), 250.0);  // Until the next update.
  Pump(s, 1001, 1000);           // 2000 ms: smoothed toward the 150 ms mean of the reports.
  SM_EXPECT_GT(gauge(), 150.0);
  SM_EXPECT_LT(gauge(), 250.0);
}

SM_TEST(Session, AReceiveOnlyEndpointRunsNoSendSideController) {
  Session s(Cfg(2));
  NullDecoder dec;
  AudioStreamConfig au;
  au.ssrc = 0x100;
  s.AddAudioReceive(au, &dec);
  s.AddVideoReceive(Video());
  // Nothing ever answers: a send-side outage monitor would pause, degrade and
  // ask for a keyframe within a few seconds.
  std::vector<OutgoingPacket> out = Pump(s, 0, 6000);
  for (const auto& o : out) SM_EXPECT_TRUE(o.kind == OutgoingKind::kRtcp);
  SM_EXPECT_EQ(static_cast<int>(s.send_side().outage().state()), static_cast<int>(cc::LinkState::kUp));
  SM_EXPECT_EQ(s.send_side().stats().released, 0u);
  for (const auto& e : s.TakeEvents()) {
    SM_EXPECT_TRUE(e.type != SessionEventType::kEncoderPause && e.type != SessionEventType::kKeyframeRequested);
  }
}

SM_TEST(Session, AnUnknownAudioReceiveIdIsIgnored) {
  Session s(Cfg(2));
  std::vector<float> pcm(480);
  SM_EXPECT_TRUE(s.GetAudio(AudioRecvId{}, pcm, Timestamp::Millis(0)) == timing::AudioOperation::kSilence);
  SM_EXPECT_TRUE(s.GetAudio(AudioRecvId{7}, pcm, Timestamp::Millis(0)) == timing::AudioOperation::kSilence);
}

// libwebrtc call/rtp_video_sender.cc ShouldDisableRedAndUlpfec, the cases of
// rtp_video_sender_unittest.cc that apply (NACK with and without a picture id,
// FlexFEC priority, a single payload type, the disabling experiment).
namespace {
RedUlpfecDecisionInput Both(VideoCodecType codec, bool nack) {
  RedUlpfecDecisionInput in;
  in.codec = codec;
  in.nack_enabled = nack;
  in.red_enabled = true;
  in.ulpfec_enabled = true;
  return in;
}
}  // namespace

SM_TEST(Session, VideoFecIsOffBesideNackForH264AndGeneric) {
  SM_EXPECT_TRUE(ShouldDisableRedAndUlpfec(Both(VideoCodecType::kH264, true)));
  SM_EXPECT_TRUE(ShouldDisableRedAndUlpfec(Both(VideoCodecType::kGeneric, true)));
  auto g = Both(VideoCodecType::kGeneric, true);
  g.generic_picture_id_trial = true;
  SM_EXPECT_TRUE(!ShouldDisableRedAndUlpfec(g));
}

SM_TEST(Session, VideoFecStaysOnBesideNackForVp8AndVp9) {
  SM_EXPECT_TRUE(!ShouldDisableRedAndUlpfec(Both(VideoCodecType::kVP8, true)));
  SM_EXPECT_TRUE(!ShouldDisableRedAndUlpfec(Both(VideoCodecType::kVP9, true)));
}

SM_TEST(Session, VideoFecStaysOnWithoutNack) {
  SM_EXPECT_TRUE(!ShouldDisableRedAndUlpfec(Both(VideoCodecType::kH264, false)));
  SM_EXPECT_TRUE(!ShouldDisableRedAndUlpfec(Both(VideoCodecType::kGeneric, false)));
}

SM_TEST(Session, VideoFecOffForFlexfecExperimentOrOnlyOneOfRedAndUlpfec) {
  auto f = Both(VideoCodecType::kVP8, false);
  f.flexfec_enabled = true;
  SM_EXPECT_TRUE(ShouldDisableRedAndUlpfec(f));
  auto e = Both(VideoCodecType::kVP8, false);
  e.disable_ulpfec_experiment = true;
  SM_EXPECT_TRUE(ShouldDisableRedAndUlpfec(e));
  auto only_red = Both(VideoCodecType::kVP8, false);
  only_red.ulpfec_enabled = false;
  SM_EXPECT_TRUE(ShouldDisableRedAndUlpfec(only_red));
  auto only_fec = Both(VideoCodecType::kVP8, false);
  only_fec.red_enabled = false;
  SM_EXPECT_TRUE(ShouldDisableRedAndUlpfec(only_fec));
  auto none = Both(VideoCodecType::kH264, true);
  none.red_enabled = none.ulpfec_enabled = false;
  SM_EXPECT_TRUE(!ShouldDisableRedAndUlpfec(none));
}

SM_TEST(Session, VideoFecEnabledFollowsTheDecision) {
  VideoStreamConfig v = Video();
  v.fec = true;
  v.nack = true;
  SM_EXPECT_TRUE(!VideoFecEnabled(v));  // H.264 + NACK.
  v.nack = false;
  SM_EXPECT_TRUE(VideoFecEnabled(v));
  v.nack = true;
  v.codec = VideoCodecType::kVP8;
  SM_EXPECT_TRUE(VideoFecEnabled(v));
  v.fec = false;
  SM_EXPECT_TRUE(!VideoFecEnabled(v));
}

namespace {
// `a` sends one audio frame every 20 ms to a receive-only `b`; returns how many
// transport-cc messages `b` built in 2 s.
uint64_t FeedbacksIn2s(DataRate start_rate) {
  SessionConfig ca = Cfg(1), cb = Cfg(2);
  cb.send_side.gcc.start_rate = start_rate;
  Session a(ca), b(cb);
  NullDecoder dec;
  AudioStreamConfig au;
  au.ssrc = 0x100;
  auto a_audio = a.AddAudioSend(au);
  b.AddAudioReceive(au, &dec);
  Bytes frame(60, 7);
  for (int64_t t = 0; t < 2000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 20 == 0) a.SendAudio(a_audio, {960, frame, false}, now);
    a.Process(now);
    for (auto& o : a.TakeOutgoing()) b.OnPacket(o.bytes, now);
    b.Process(now);
    b.TakeOutgoing();
  }
  return b.transport_feedback().stats().feedbacks;
}
}  // namespace

SM_TEST(Session, TransportFeedbackIntervalFollowsTheLocalSendEstimate) {
  const uint64_t fast = FeedbacksIn2s(DataRate::KilobitsPerSec(300));  // 50 ms.
  const uint64_t slow = FeedbacksIn2s(DataRate::KilobitsPerSec(40));   // 250 ms.
  SM_EXPECT_NEAR(static_cast<double>(fast), 40, 2);
  SM_EXPECT_NEAR(static_cast<double>(slow), 8, 2);
}

// Issue #217: Call::EnsureStarted starts ReceiveSideCongestionController's task
// before the Call hears its start rate, so the generator's first Process runs on
// its default 100 ms interval: libwebrtc's first feedback goes out at 100 ms,
// then every 50 ms (at 300 kbps). Seed 1 of issue_120_rtx_payload_padding: the
// first delay-based update is at 115 ms, then every 50 ms.
SM_TEST(Session, TransportFeedbackFirstReportWaitsTheDefaultInterval) {
  SessionConfig ca = Cfg(1), cb = Cfg(2);
  Session a(ca), b(cb);
  NullDecoder dec;
  AudioStreamConfig au;
  au.ssrc = 0x100;
  auto a_audio = a.AddAudioSend(au);
  b.AddAudioReceive(au, &dec);
  Bytes frame(60, 7);
  std::vector<int64_t> sent_at;
  std::vector<std::pair<int64_t, Bytes>> link;  // 15 ms one-way delay.
  uint64_t seen = 0;
  for (int64_t t = 0; t < 400; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 20 == 0) a.SendAudio(a_audio, {960, frame, false}, now);
    a.Process(now);
    for (auto& o : a.TakeOutgoing()) link.emplace_back(t + 15, std::move(o.bytes));
    for (auto& [at, bytes] : link) {
      if (at == t) b.OnPacket(bytes, now);
    }
    b.Process(now);
    b.TakeOutgoing();
    if (b.transport_feedback().stats().feedbacks > seen) {
      seen = b.transport_feedback().stats().feedbacks;
      sent_at.push_back(t);
    }
  }
  SM_EXPECT_GE(sent_at.size(), static_cast<size_t>(6));
  if (sent_at.size() < 6) return;
  SM_EXPECT_EQ(sent_at[0], 100);
  for (size_t i = 1; i < 6; ++i) SM_EXPECT_EQ(sent_at[i] - sent_at[i - 1], 50);
}

// RtpRtcpImpl2::OnSendingRtpFrame (#101): a report due within 100 ms is sent
// ahead of a keyframe, not on the next Process tick.
SM_TEST(Session, SendsADueSenderReportAheadOfAKeyframe) {
  auto run = [](bool keyframe) {
    SessionConfig c = Cfg(1);
    c.rtcp.first_report_delay = TimeDelta::Millis(1000);
    Session s(c);
    AudioStreamConfig au;
    au.ssrc = 0x100;
    s.AddAudioSend(au);
    VideoSendId v = s.AddVideoSend(Video());
    s.Process(Timestamp::Millis(0));
    s.TakeOutgoing();
    auto chunks = test::MakeTestFrameChunks(keyframe, 3000, 1188);
    s.SendVideoChunks(v, chunks, 0, Timestamp::Millis(950), keyframe);
    size_t rtcp = 0;
    for (auto& o : s.TakeOutgoing()) rtcp += o.kind == OutgoingKind::kRtcp;
    return rtcp;
  };
  SM_EXPECT_EQ(run(true), 1u);
  SM_EXPECT_EQ(run(false), 0u);
}

// VideoSendStreamImpl (#187): a video stream with the transport-cc extension
// sets StreamsConfig::pacing_factor to 1.1 (kStrictPacingMultiplier), so GoogCC
// paces at 1.1 x the target from the start, not 2.5 x until the first feedback.
SM_TEST(Session, AVideoStreamWithTransportCcPacesAtTheStrictFactorFromTheStart) {
  auto pacing_bps = [](bool video, bool transport_cc) {
    SessionConfig c = Cfg(1);
    if (!transport_cc) c.extensions = sync::ExtensionMap();
    c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(300);
    Session s(c);
    AudioStreamConfig au;
    au.ssrc = 0x100;
    s.AddAudioSend(au);
    if (video) s.AddVideoSend(Video());
    s.Process(Timestamp::Millis(0));
    return s.send_side().gcc().PacingRate().bps();
  };
  SM_EXPECT_EQ(pacing_bps(true, true), 330000);
  SM_EXPECT_EQ(pacing_bps(false, true), 750000);  // Audio only: GoogCC's 2.5 x.
  SM_EXPECT_EQ(pacing_bps(true, false), 750000);  // No transport-cc: no override.
}

// #62: an audio stream receives through libwebrtc's NetEq (jitter buffer version 2)
// unless the config selects v1; AudioJitterBufferConfig on its own keeps v1.
SM_TEST(Session, AudioStreamDefaultsToLibwebrtcsNetEq) {
  SM_EXPECT_EQ(AudioStreamConfig{}.jitter_buffer.version, 2);
  SM_EXPECT_EQ(timing::AudioJitterBufferConfig{}.version, 1);
}

// #24: a session's video stream runs libwebrtc's receive timing (VCMTiming and
// frame release); timing::VideoTimingConfig on its own keeps version 1.
SM_TEST(Session, VideoStreamDefaultsToLibwebrtcsReceiveTiming) {
  SM_EXPECT_EQ(VideoStreamConfig{}.timing.version, 2);
  SM_EXPECT_EQ(timing::VideoTimingConfig{}.version, 1);
}

// #453: libwebrtc runs H264SpsPpsTracker (rtp_video_stream_receiver2.cc OnReceivedPayloadData) and
// PacketBuffer's missing_packets_ hold (packet_buffer.cc FindFrames) on every H.264 stream, and on
// no other codec. A session's video stream does the same; the building blocks keep both off.
SM_TEST(Session, H264StreamsRunTheSpsPpsTrackerAndTheMissingPacketsHold) {
  SM_EXPECT_TRUE(VideoStreamConfig{}.sps_pps_tracker);
  SM_EXPECT_TRUE(VideoStreamConfig{}.packet_buffer.missing_packets);
  SM_EXPECT_TRUE(!RtpReceiverConfig{}.sps_pps_tracker);
  SM_EXPECT_TRUE(!timing::PacketBufferConfig{}.missing_packets);
  Session s(Cfg(2));
  VideoStreamConfig h264 = Video();
  VideoStreamConfig vp8 = Video();
  vp8.codec = VideoCodecType::kVP8;
  vp8.ssrc = 0x400;
  vp8.rtx_ssrc = 0x500;
  const RtpReceiverConfig& a = s.video_receiver(s.AddVideoReceive(h264)).config();
  SM_EXPECT_TRUE(a.sps_pps_tracker);
  SM_EXPECT_TRUE(a.packet_buffer.missing_packets);
  const RtpReceiverConfig& b = s.video_receiver(s.AddVideoReceive(vp8)).config();
  SM_EXPECT_TRUE(!b.sps_pps_tracker);
  SM_EXPECT_TRUE(!b.packet_buffer.missing_packets);
}

// #62: AudioSendStream::ConfigureStream asks for periodic ALR probing when the audio stream
// has transport-cc (audio_send_stream.cc:297-308). Without transport-cc it does not.
// #355: VideoSendStreamImpl's constructor then sets it to RateControlSettings::UseAlrProbing(),
// off without field trials, when the video stream has transport-cc
// (video_send_stream_impl.cc:487-523). So a call that adds audio and then video does not
// probe periodically in ALR (yardstick #359: libwebrtc sends none on cross traffic), while
// audio alone, or audio added after the video, does.
SM_TEST(Session, AnAudioStreamWithTransportCcRequestsAlrProbing) {
  enum class Order { kAudioOnly, kAudioThenVideo, kVideoThenAudio, kVideoOnly };
  auto alr_probing = [](Order order, bool transport_cc) {
    SessionConfig c = Cfg(1);
    if (!transport_cc) c.extensions = sync::ExtensionMap();
    Session s(c);
    AudioStreamConfig au;
    au.ssrc = 0x100;
    if (order == Order::kVideoThenAudio || order == Order::kVideoOnly) s.AddVideoSend(Video());
    if (order != Order::kVideoOnly) s.AddAudioSend(au);
    if (order == Order::kAudioThenVideo) s.AddVideoSend(Video());
    s.Process(Timestamp::Millis(0));
    return s.send_side().gcc().probe_controller_v2().periodic_alr_probing();
  };
  SM_EXPECT_TRUE(alr_probing(Order::kAudioOnly, true));
  SM_EXPECT_FALSE(alr_probing(Order::kAudioThenVideo, true));  // The video stream turns it off.
  SM_EXPECT_TRUE(alr_probing(Order::kVideoThenAudio, true));
  SM_EXPECT_FALSE(alr_probing(Order::kVideoOnly, true));  // Video alone (not screen share) does not ask.
  SM_EXPECT_FALSE(alr_probing(Order::kAudioThenVideo, false));
}

// #62: the initial exponential probes of an audio-only call are 3x and 6x the start rate,
// not capped at twice the audio allocation: libwebrtc's GoogCC is created when the network
// comes up, before AudioSendStream::Start registers its bitrate, so ProbeController knows no
// max_total_allocated_bitrate when it probes (rtp_transport_controller_send.cc:413-428, 747-773).
SM_TEST(Session, AudioOnlyInitialProbesAreNotCappedByTheAllocation) {
  SessionConfig c = Cfg(1);
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(300);
  Session s(c);
  AudioStreamConfig au;
  au.ssrc = 0x100;
  s.AddAudioSend(au);
  s.Process(Timestamp::Millis(0));
  const auto& pc = s.send_side().gcc().probe_controller_v2();
  SM_EXPECT_TRUE(pc.state() == cc::ProbeControllerV2::State::kWaitingForProbingResult);
}
