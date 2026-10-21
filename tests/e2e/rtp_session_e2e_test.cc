// Two rtp::Session objects exchanging bytes over sim::NetworkLink (plan PR 5 of
// docs/design/rtp-session.md): audio and video both ways of one call, loss,
// an outage, NACK recovery, keyframe requests and the rate feedback loop.
#include <algorithm>
#include <cmath>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/rtp/session.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
constexpr uint32_t kAudioSsrc = 1, kVideoSsrc = 2, kRtxSsrc = 3;

// Decoder that turns every frame into 20 ms of samples at 48 kHz.
class FakeDecoder : public AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t) override {
    ++decoded;
    return std::vector<float>(960, 0.1f);
  }
  int decoded = 0;
};

struct Endpoint {
  explicit Endpoint(SessionConfig c, const test::TestVideoParser* parser, FakeDecoder* dec, bool red)
      : session(std::move(c)) {
    AudioStreamConfig a;
    a.ssrc = kAudioSsrc;
    a.nominal_rate = DataRate::KilobitsPerSec(40);
    if (red) a.red_payload_type = 63;
    VideoStreamConfig v;
    v.ssrc = kVideoSsrc;
    v.rtx_ssrc = kRtxSsrc;
    v.parser = parser;
    audio_send = session.AddAudioSend(a);
    video_send = session.AddVideoSend(v);
    audio_recv = session.AddAudioReceive(a, dec);
    video_recv = session.AddVideoReceive(v);
  }
  Session session;
  AudioSendId audio_send;
  VideoSendId video_send;
  AudioRecvId audio_recv;
  VideoRecvId video_recv;
};

SessionConfig MakeConfig(uint32_t ssrc, const char* cname, bool mid) {
  SessionConfig c;
  c.local_ssrc = ssrc;
  c.cname = cname;
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.extensions.Register(2, sync::uri::kAbsSendTime);
  c.extensions.Register(3, sync::uri::kMid);
  if (mid) {
    c.mid_audio = "a";
    c.mid_video = "v";
  }
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(800);
  return c;
}

struct Options {
  double loss = 0;
  bool outage = false;
  bool mid = true;
  bool red = false;
  uint64_t seed = 1;
};

struct Result {
  int frames_sent = 0, keyframes_sent = 0, rendered = 0, audio_pulled = 0, concealed = 0;
  uint64_t keyframe_events = 0, rtt_events = 0, malformed_a = 0, malformed_b = 0, unroutable = 0;
  uint64_t rtx_received = 0, nack_seqs = 0, rtcp_a_to_b = 0, rtcp_b_to_a = 0;
  uint64_t padding_out = 0, audio_decoded = 0;
  int64_t longest_gap_ms = 0;
  double rtt_ms = 0;
  double target_kbps = 0;
};

Result Run(const Options& o) {
  const test::TestVideoParser parser;
  FakeDecoder dec_a, dec_b;
  Endpoint a(MakeConfig(10, "alice", o.mid), &parser, &dec_a, o.red);
  Endpoint b(MakeConfig(20, "bob", o.mid), &parser, &dec_b, o.red);

  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(25);
  lc.capacity = DataRate::KilobitsPerSec(3000);
  lc.queue_limit = DataSize::Bytes(60000);
  lc.loss_rate = o.loss;
  sim::NetworkLink<std::vector<uint8_t>> ab(lc, o.seed);
  sim::NetworkLink<std::vector<uint8_t>> ba(lc, o.seed + 100);

  Result res;
  bool key_needed = true;
  int64_t frame_id = 0, last_render = -1;
  const int64_t end_ms = 10000;
  std::vector<float> pcm(480);
  for (int64_t t = 0; t < end_ms; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    const bool down = o.outage && t >= 4000 && t < 5500;
    // Alice sends audio and video; Bob sends audio so he has RTP to report on.
    if (t % 20 == 0) {
      std::vector<uint8_t> frame(60, static_cast<uint8_t>(t / 20));
      a.session.SendAudio(a.audio_send, {static_cast<uint32_t>(t * 48), frame, false}, now);
      b.session.SendAudio(b.audio_send, {static_cast<uint32_t>(t * 48), frame, false}, now);
    }
    if (t % 33 == 0 && t < end_ms - 1000) {
      const bool key = key_needed;
      key_needed = false;
      auto chunks = test::MakeTestFrameChunks(key, key ? 6000 : 3000, 1188);
      a.session.SendVideoChunks(a.video_send, chunks, static_cast<uint32_t>(t * 90), now);
      ++frame_id;
      ++res.frames_sent;
      res.keyframes_sent += key;
    }
    a.session.Process(now);
    b.session.Process(now);
    for (auto& p : a.session.TakeOutgoing()) {
      res.padding_out += p.kind == OutgoingKind::kPadding;
      res.rtcp_a_to_b += p.kind == OutgoingKind::kRtcp;
      ab.Send(now, std::move(p.bytes), p.wire_size);
    }
    for (auto& p : b.session.TakeOutgoing()) {
      res.rtcp_b_to_a += p.kind == OutgoingKind::kRtcp;
      ba.Send(now, std::move(p.bytes), p.wire_size);
    }
    ab.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) {
      if (!down) b.session.OnPacket(bytes, at);
    });
    ba.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) {
      if (!down) a.session.OnPacket(bytes, at);
    });
    for (const auto& f : b.session.TakeVideoFrames(b.video_recv)) {
      if (!f.render) continue;
      ++res.rendered;
      if (last_render >= 0) res.longest_gap_ms = std::max(res.longest_gap_ms, t - last_render);
      last_render = t;
    }
    for (const auto& e : a.session.TakeEvents()) {
      if (e.type == SessionEventType::kKeyframeRequested) {
        key_needed = true;
        ++res.keyframe_events;
      }
      res.rtt_events += e.type == SessionEventType::kRttUpdated;
    }
    b.session.TakeEvents();
    if (t % 10 == 0) {
      auto op = b.session.GetAudio(b.audio_recv, pcm, now);
      ++res.audio_pulled;
      res.concealed += op != timing::AudioOperation::kNormal;
    }
  }
  res.malformed_a = a.session.stats().malformed;
  res.malformed_b = b.session.stats().malformed;
  res.unroutable = a.session.stats().unroutable + b.session.stats().unroutable;
  res.rtx_received = b.session.video_receiver(b.video_recv).stats().rtx;
  res.nack_seqs = b.session.video_receiver(b.video_recv).nack().stats().nack_requests;
  res.audio_decoded = static_cast<uint64_t>(dec_b.decoded);
  res.rtt_ms = static_cast<double>(a.session.rtt().us()) / 1000.0;
  res.target_kbps = a.session.rates().target.kbps();
  return res;
}
}  // namespace

SM_TEST(E2E_RtpSession, CleanCallDeliversAudioAndVideoBothWays) {
  Result r = Run({});
  SM_EXPECT_EQ(r.malformed_a + r.malformed_b, 0u);
  SM_EXPECT_EQ(r.unroutable, 0u);
  SM_EXPECT_GE(r.rendered, r.frames_sent - 10);
  // Was <= 100. With the first transport feedback after 100 ms (#217) and timing v2 (#24), the
  // second frame waits in the pacer behind the debt of the keyframe and the initial probes
  // (880 kbps pacing, 12 KB debt) until the probe result arrives: 118 ms at t = 167 ms, the
  // longest gap of the call. libwebrtc does the same: its `clean_link` seed-1 forward trace
  // (parity-data) sends no video from 61 ms to 125 ms after its probes. Owner rule on PR #226
  // (program.md 0.2): just above our value.
  SM_EXPECT_LE(r.longest_gap_ms, 120);
  SM_EXPECT_GT(r.audio_decoded, 400u);
  SM_EXPECT_GT(r.rtcp_a_to_b, 5u);
  SM_EXPECT_GT(r.rtcp_b_to_a, 5u);
  SM_EXPECT_GT(r.rtt_events, 0u);
  // The round trip is two 25 ms legs plus serialization and report timing.
  SM_EXPECT_GT(r.rtt_ms, 45.0);
  SM_EXPECT_LT(r.rtt_ms, 120.0);
  SM_EXPECT_GT(r.target_kbps, 0.0);
}

SM_TEST(E2E_RtpSession, RoutesWithoutMidByPayloadTypeAndSsrc) {
  Options o;
  o.mid = false;
  Result r = Run(o);
  SM_EXPECT_EQ(r.unroutable, 0u);
  SM_EXPECT_GE(r.rendered, r.frames_sent - 10);
  SM_EXPECT_GT(r.audio_decoded, 400u);
}

SM_TEST(E2E_RtpSession, AudioRedIsUnwrappedThroughTheSession) {
  Options o;
  o.red = true;
  o.loss = 0.1;
  Result r = Run(o);
  SM_EXPECT_EQ(r.malformed_b, 0u);
  // RED repeats the previous frame, so more frames reach the decoder than
  // packets survive.
  SM_EXPECT_GT(r.audio_decoded, 400u);
}

SM_TEST(E2E_RtpSession, NackAndRtxRecoverLossThroughTheSession) {
  for (uint64_t seed : {1, 2, 3}) {
    Options o;
    o.loss = 0.05;
    o.seed = seed;
    Result r = Run(o);
    SM_EXPECT_EQ(r.malformed_b, 0u);
    SM_EXPECT_GT(r.nack_seqs, 0u);
    SM_EXPECT_GT(r.rtx_received, 0u);
    SM_EXPECT_GE(r.rendered, r.frames_sent * 9 / 10);
  }
}

SM_TEST(E2E_RtpSession, OutageIsRepairedByNackAndVideoResumes) {
  // libwebrtc's receiver (#167 deleted the v1 requester, which asked for a keyframe right after the outage):
  // a blocked frame is left to NACK, and RTX repairs the 1.5 s outage from the packet history, so no
  // keyframe is needed. The decode time-out (3 s) is the backstop and does not fire.
  Options o;
  o.outage = true;
  Result r = Run(o);
  SM_EXPECT_EQ(r.keyframe_events, 0u);
  SM_EXPECT_EQ(r.keyframes_sent, 1);
  SM_EXPECT_GT(r.rtx_received, 0u);
  SM_EXPECT_GT(r.rendered, r.frames_sent / 2);
  SM_EXPECT_LE(r.longest_gap_ms, 3000);
}

// #256: an audio stream at a clock rate NetEq does not run at crashed the
// receiver in concealment. Its packets are now discarded, as libwebrtc does.
SM_TEST(E2E_RtpSession, AudioAtAClockRateNetEqDoesNotRunAtIsDiscarded) {
  struct AnyRateDecoder : AudioDecoder {
    std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t) override {
      return std::vector<float>(2000, 0.1f);
    }
  };
  for (int rate : {11025, 12000, 22050, 24000, 44100, 48000}) {
    AnyRateDecoder dec;
    AudioStreamConfig ac;
    ac.ssrc = kAudioSsrc;
    ac.clock_rate = rate;
    ac.nominal_rate = DataRate::KilobitsPerSec(32);
    SessionConfig sc;
    Session tx(sc), rx(sc);
    const AudioSendId as = tx.AddAudioSend(ac);
    const AudioRecvId ar = rx.AddAudioReceive(ac, &dec);
    std::vector<float> out(static_cast<size_t>(rate / 100));
    for (int t = 0; t < 3000; ++t) {
      const Timestamp now = Timestamp::Millis(t);
      if (t % 20 == 0 && t < 1000) {  // 1 s of speech, then the sender goes quiet.
        std::vector<uint8_t> frame(80, 1);
        tx.SendAudio(as, {static_cast<uint32_t>(int64_t{t} * rate / 1000), frame, false}, now);
      }
      tx.Process(now);
      for (auto& p : tx.TakeOutgoing()) rx.OnPacket(p.bytes, now);
      rx.Process(now);
      if (t % 10 == 0) rx.GetAudio(ar, out, now);
    }
    const auto& jb = rx.audio_receiver(ar).jitter_buffer().stats();
    SM_EXPECT_EQ(jb.packets_discarded_unsupported_rate, rate == 48000 ? 0u : 50u);
    SM_EXPECT_EQ(jb.packets_received, rate == 48000 ? 50u : 0u);
  }
}
