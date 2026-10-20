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
  p.a.SendVideo(p.a_video, {0, frame}, Timestamp::Millis(0));
  std::vector<RtpPacket> sent;
  for (auto& o : Pump(p.a, 0, 500)) {
    if (o.kind != OutgoingKind::kVideo) continue;
    SM_EXPECT_LE(o.bytes.size(), 1200u);
    sent.push_back(*RtpPacket::Parse(o.bytes));
  }
  // SPS+PPS in a STAP-A, then the IDR slice in FU-A fragments; the marker is
  // on the last packet only, and the payloads rebuild the access unit.
  SM_ASSERT_GE(sent.size(), 5u);
  SM_EXPECT_EQ(sent[0].payload[0] & 0x1F, kH264NalStapA);
  for (size_t i = 0; i < sent.size(); ++i) SM_EXPECT_EQ(sent[i].marker, i + 1 == sent.size());
  Bytes rebuilt;
  SM_EXPECT_TRUE(H264Depacketizer::Reassemble(sent, rebuilt));
  SM_EXPECT_TRUE(rebuilt == frame);
  // Full-size chunks leave room for the header extensions.
  p.a.SendVideo(p.a_video, {3000, MakeFakeH264Frame(false, 1188 * 3)}, Timestamp::Millis(500));
  for (auto& o : Pump(p.a, 500, 500)) {
    if (o.kind == OutgoingKind::kVideo) SM_EXPECT_LE(o.bytes.size(), 1200u);
  }
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
uint64_t FeedbacksIn2s(int version, DataRate start_rate) {
  SessionConfig ca = Cfg(1), cb = Cfg(2);
  cb.transport_feedback_version = version;
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

SM_TEST(Session, TransportFeedbackV2IntervalFollowsTheLocalSendEstimate) {
  const uint64_t v1 = FeedbacksIn2s(1, DataRate::KilobitsPerSec(300));
  const uint64_t fast = FeedbacksIn2s(2, DataRate::KilobitsPerSec(300));  // 50 ms.
  const uint64_t slow = FeedbacksIn2s(2, DataRate::KilobitsPerSec(40));   // 250 ms.
  SM_EXPECT_NEAR(static_cast<double>(v1), 40, 2);
  SM_EXPECT_NEAR(static_cast<double>(fast), 40, 2);
  SM_EXPECT_NEAR(static_cast<double>(slow), 8, 2);
}

// Issue #217: Call::EnsureStarted starts ReceiveSideCongestionController's task
// before the Call hears its start rate, so the generator's first Process runs on
// its default 100 ms interval: libwebrtc's first feedback goes out at 100 ms,
// then every 50 ms (at 300 kbps). Seed 1 of issue_120_rtx_payload_padding: the
// first delay-based update is at 115 ms, then every 50 ms.
SM_TEST(Session, TransportFeedbackV2FirstReportWaitsTheDefaultInterval) {
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

// RtpRtcpImpl2::OnSendingRtpFrame (#101): with `rtcp v2` a report due within
// 100 ms is sent ahead of a keyframe, not on the next Process tick.
SM_TEST(Session, V2SendsADueSenderReportAheadOfAKeyframe) {
  auto run = [](int version, bool keyframe) {
    SessionConfig c = Cfg(1);
    c.rtcp.version = version;
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
  SM_EXPECT_EQ(run(2, true), 1u);
  SM_EXPECT_EQ(run(2, false), 0u);
  SM_EXPECT_EQ(run(1, true), 0u);
}

// VideoSendStreamImpl (#187): a video stream with the transport-cc extension
// sets StreamsConfig::pacing_factor to 1.1 (kStrictPacingMultiplier), so GoogCC
// v2 paces at 1.1 x the target from the start, not 2.5 x until the first feedback.
SM_TEST(Session, AVideoStreamWithTransportCcPacesAtTheStrictFactorFromTheStart) {
  auto pacing_bps = [](bool video, bool transport_cc) {
    SessionConfig c = Cfg(1);
    if (!transport_cc) c.extensions = sync::ExtensionMap();
    c.send_side.gcc.version = 2;
    c.send_side.pacer.version = cc::PacerVersion::kV2;
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

// #61: a session runs libwebrtc's components unless a field selects v1. The
// component configs on their own keep v1, so their unit tests test what they name.
SM_TEST(Session, DefaultsToLibwebrtcsComponents) {
  const SessionConfig c;
  SM_EXPECT_EQ(c.send_side.gcc.version, 2);
  SM_EXPECT_TRUE(c.send_side.pacer.version == cc::PacerVersion::kV2);
  SM_EXPECT_EQ(c.send_side.allocator_version, 2);
  SM_EXPECT_EQ(c.transport_feedback_version, 2);
  SM_EXPECT_EQ(c.rtcp.version, 2);
  SM_EXPECT_EQ(c.rtx_version, 2);
  SM_EXPECT_EQ(c.packetization_version, 2);
  SM_EXPECT_EQ(VideoStreamConfig{}.keyframe.version, 2);
  SM_EXPECT_EQ(cc::GccConfig{}.version, 1);
  SM_EXPECT_TRUE(cc::PacerConfig{}.version == cc::PacerVersion::kV1);
  SM_EXPECT_EQ(RtcpConfig{}.version, 1);
  SM_EXPECT_EQ(loss::KeyframeRequestConfig{}.version, 1);
}
