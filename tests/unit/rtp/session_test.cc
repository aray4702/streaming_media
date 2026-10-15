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
  auto chunks = test::MakeTestFrameChunks(0, true, 3000, 1188);
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

SM_TEST(Session, SendVideoSplitsTheFrameEvenlyWithinTheMtu) {
  Pair p;
  Bytes data(5000, 1);
  p.a.SendVideo(p.a_video, {0, data}, Timestamp::Millis(0));
  size_t total = 0, n = 0;
  for (auto& o : Pump(p.a, 0, 500)) {
    if (o.kind != OutgoingKind::kVideo) continue;
    SM_EXPECT_LE(o.bytes.size(), 1200u);
    total += RtpPacket::Parse(o.bytes)->payload.size();
    ++n;
  }
  SM_EXPECT_EQ(n, 5u);
  SM_EXPECT_EQ(total, 5000u);
  // Full-size chunks leave room for the header extensions.
  Bytes big(1188 * 3, 2);
  p.a.SendVideo(p.a_video, {3000, big}, Timestamp::Millis(500));
  for (auto& o : Pump(p.a, 500, 500)) {
    if (o.kind == OutgoingKind::kVideo) SM_EXPECT_LE(o.bytes.size(), 1200u);
  }
}

SM_TEST(Session, NackFromThePeerIsAnsweredWithRtxOnTheRtxSsrc) {
  Pair p;
  auto chunks = test::MakeTestFrameChunks(0, true, 3000, 1188);
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
  p.a.SendVideoChunks(p.a_video, test::MakeTestFrameChunks(0, true, 2500, 1188), 0, Timestamp::Millis(0));
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
