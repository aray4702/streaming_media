#include <algorithm>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/cc/transport_feedback.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {
constexpr uint32_t kSsrc = 0x1111, kRtxSsrc = 0x2222, kFecSsrc = 0x3333;
const test::TestVideoParser kParser;

RtpSenderConfig SenderCfg() {
  RtpSenderConfig c;
  c.ssrc = kSsrc;
  c.payload_type = 96;
  c.rtx_ssrc = kRtxSsrc;
  c.rtx_payload_type = 97;
  c.initial_seq = 65530;  // Wraps during the tests.
  return c;
}

RtpReceiverConfig ReceiverCfg() {
  RtpReceiverConfig c;
  c.ssrc = kSsrc;
  c.payload_type = 96;
  c.rtx_ssrc = kRtxSsrc;
  c.parser = &kParser;
  return c;
}

// One frame through the sender: the serialized packets of it.
std::vector<Bytes> SendFrame(RtpSender& s, int64_t id, bool key, size_t bytes, Timestamp now) {
  auto chunks = test::MakeTestFrameChunks(id, key, bytes, s.config().max_payload_size);
  s.SendVideoFrame(chunks, static_cast<uint32_t>(id * 3000), now);
  std::vector<Bytes> out;
  for (auto& p : s.TakePackets()) out.push_back(p.packet.Serialize());
  return out;
}

// Runs the receiver's clock from `from` to `to` in 1 ms steps and collects frames.
std::vector<ReceivedFrame> Run(RtpReceiver& r, int64_t from_ms, int64_t to_ms) {
  std::vector<ReceivedFrame> out;
  for (int64_t t = from_ms; t < to_ms; ++t) {
    r.Process(Timestamp::Millis(t));
    for (auto& f : r.TakeFrames()) out.push_back(f);
  }
  return out;
}
}  // namespace

SM_TEST(RtpReceiver, MalformedDatagramIsCountedAndDropped) {
  RtpReceiver r(ReceiverCfg());
  Bytes junk = {0x80, 0x60, 0x00};
  SM_EXPECT_TRUE(r.OnPacket(junk, Timestamp::Millis(1)) == ReceivedKind::kMalformed);
  SM_EXPECT_TRUE(r.OnPacket(Bytes{}, Timestamp::Millis(1)) == ReceivedKind::kMalformed);
  SM_EXPECT_EQ(r.stats().malformed, 2u);
  SM_EXPECT_EQ(r.stats().packets, 0u);
}

SM_TEST(RtpReceiver, FramesInOrderAreAssembledScheduledAndRendered) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  size_t packets = 0;
  for (int64_t i = 0; i < 6; ++i) {
    Timestamp at = Timestamp::Millis(i * 33);
    for (auto& b : SendFrame(s, i, i == 0, i == 0 ? 3000 : 800, at)) {
      SM_EXPECT_TRUE(r.OnPacket(b, at + TimeDelta::Millis(20)) == ReceivedKind::kMedia);
      ++packets;
    }
    r.Process(at + TimeDelta::Millis(20));
  }
  auto frames = Run(r, 200, 800);
  SM_EXPECT_EQ(r.stats().media, packets);
  SM_EXPECT_EQ(r.stats().frames_received, 6u);
  SM_EXPECT_EQ(r.stats().gaps, 0u);
  SM_ASSERT_EQ(frames.size(), 6u);
  for (size_t i = 0; i < frames.size(); ++i) {
    SM_EXPECT_EQ(frames[i].frame_id, static_cast<int64_t>(i));
    SM_EXPECT_EQ(frames[i].keyframe, i == 0);
    SM_EXPECT_TRUE(frames[i].render);
    SM_EXPECT_EQ(frames[i].rtp_timestamp, static_cast<uint32_t>(i * 3000));
  }
  SM_EXPECT_EQ(frames[0].size_bytes, 3000u);
  SM_EXPECT_TRUE(r.TakeFeedback().empty());
}

SM_TEST(RtpReceiver, LostPacketIsNackedAndRtxCompletesTheFrame) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  auto f0 = SendFrame(s, 0, true, 800, Timestamp::Millis(0));
  auto f1 = SendFrame(s, 1, false, 3000, Timestamp::Millis(33));
  auto f2 = SendFrame(s, 2, false, 800, Timestamp::Millis(66));
  SM_ASSERT_GE(f1.size(), 3u);
  for (auto& b : f0) r.OnPacket(b, Timestamp::Millis(20));
  // The middle packet of frame 1 is lost.
  auto lost = RtpPacket::Parse(f1[1]);
  SM_ASSERT_TRUE(lost.has_value());
  for (size_t i = 0; i < f1.size(); ++i)
    if (i != 1) r.OnPacket(f1[i], Timestamp::Millis(53));
  for (auto& b : f2) r.OnPacket(b, Timestamp::Millis(86));
  r.Process(Timestamp::Millis(90));
  SM_EXPECT_EQ(r.stats().gaps, 1u);
  SM_EXPECT_EQ(r.stats().missing, 1u);
  ReceiverFeedback fb = r.TakeFeedback();
  SM_ASSERT_EQ(fb.nacks.size(), 1u);
  SM_EXPECT_EQ(fb.nacks[0], lost->seq);
  // Frame 1 is incomplete. Frame 2 is assembled but waits for its reference.
  SM_EXPECT_EQ(r.stats().frames_received, 2u);
  SM_EXPECT_EQ(r.stats().frames_scheduled, 1u);

  // The sender answers the NACK; the RTX packet restores the original.
  s.OnNack(fb.nacks, Timestamp::Millis(120), TimeDelta::Millis(40));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_EQ(rtx[0].packet.ssrc, kRtxSsrc);
  SM_EXPECT_TRUE(r.OnPacket(rtx[0].packet.Serialize(), Timestamp::Millis(140)) == ReceivedKind::kRtx);
  r.Process(Timestamp::Millis(141));
  SM_EXPECT_EQ(r.stats().frames_received, 3u);
  SM_EXPECT_EQ(r.nack().stats().recovered, 1u);
  auto frames = Run(r, 142, 800);
  SM_ASSERT_EQ(frames.size(), 3u);
  SM_EXPECT_EQ(frames[2].frame_id, 2);
  SM_EXPECT_TRUE(r.TakeFeedback().keyframe.empty());
}

SM_TEST(RtpReceiver, MissingFrameReferenceAsksForAKeyframe) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  for (auto& b : SendFrame(s, 0, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  SendFrame(s, 1, false, 800, Timestamp::Millis(33));  // Frame 1 never arrives.
  for (auto& b : SendFrame(s, 2, false, 800, Timestamp::Millis(66))) r.OnPacket(b, Timestamp::Millis(80));
  bool asked = false;
  for (int64_t t = 80; t < 1000 && !asked; ++t) {
    r.Process(Timestamp::Millis(t));
    ReceiverFeedback fb = r.TakeFeedback();
    for (const auto& m : fb.keyframe)
      if (m.type == loss::FeedbackType::kPli || m.type == loss::FeedbackType::kFir) asked = true;
    asked = asked || fb.pli;
  }
  SM_EXPECT_TRUE(asked);
}

SM_TEST(RtpReceiver, PaddingOnlyPacketsAreNotMedia) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  SenderPacket pad = s.MakePadding(200, 0, Timestamp::Millis(1));  // On the RTX SSRC.
  SM_EXPECT_EQ(pad.packet.ssrc, kRtxSsrc);
  SM_EXPECT_TRUE(r.OnPacket(pad.packet.Serialize(), Timestamp::Millis(5)) == ReceivedKind::kPadding);
  RtpPacket own;
  own.ssrc = kSsrc;
  own.payload_type = 96;
  own.padding_size = 50;
  SM_EXPECT_TRUE(r.OnPacket(own.Serialize(), Timestamp::Millis(5)) == ReceivedKind::kPadding);
  SM_EXPECT_EQ(r.stats().padding, 2u);
  SM_EXPECT_EQ(r.stats().media, 0u);
  SM_EXPECT_EQ(r.stats().gaps, 0u);
  SM_EXPECT_EQ(r.stats().frames_received, 0u);
}

SM_TEST(RtpReceiver, UnknownSsrcAndInvalidPayloadAreCountedNotUsed) {
  RtpReceiver r(ReceiverCfg());
  RtpPacket other;
  other.ssrc = 0x9999;
  other.payload_type = 96;
  other.payload = Bytes(20, 1);
  SM_EXPECT_TRUE(r.OnPacket(other.Serialize(), Timestamp::Millis(1)) == ReceivedKind::kUnknown);
  RtpPacket shorty;
  shorty.ssrc = kSsrc;
  shorty.payload_type = 96;
  shorty.payload = Bytes(3, 1);  // Shorter than the test header.
  SM_EXPECT_TRUE(r.OnPacket(shorty.Serialize(), Timestamp::Millis(1)) == ReceivedKind::kMedia);
  SM_EXPECT_EQ(r.stats().unknown_ssrc, 1u);
  SM_EXPECT_EQ(r.stats().invalid_payload, 1u);
  SM_EXPECT_EQ(r.packet_buffer().buffered_packets(), 0u);
  SM_EXPECT_EQ(r.stats().gaps, 0u);
}

SM_TEST(RtpReceiver, TransportSequenceNumbersOfEveryPacketReachTheFeedbackBuilder) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  sync::ExtensionAccessor access(map);
  cc::TransportFeedbackBuilder fb(2, 1);
  RtpReceiverConfig c = ReceiverCfg();
  c.extensions = &map;
  c.feedback = &fb;
  RtpReceiver r(c);
  RtpSender s(SenderCfg());
  uint16_t tseq = 100;
  for (auto& b : SendFrame(s, 0, true, 2500, Timestamp::Millis(0))) {
    auto p = RtpPacket::Parse(b);
    access.SetTransportSeq(*p, tseq++);
    r.OnPacket(p->Serialize(), Timestamp::Millis(10 + tseq));
  }
  SenderPacket pad = s.MakePadding(200, 0, Timestamp::Millis(1));
  access.SetTransportSeq(pad.packet, tseq++);
  r.OnPacket(pad.packet.Serialize(), Timestamp::Millis(30));
  auto reports = fb.Build();
  SM_ASSERT_EQ(reports.size(), 1u);
  size_t received = 0;
  for (const auto& p : reports[0].packets) received += p.received;
  SM_EXPECT_EQ(received, static_cast<size_t>(tseq - 100));
  SM_EXPECT_EQ(reports[0].base_seq, 100);
}

SM_TEST(RtpReceiver, FecRecoversALostMediaPacket) {
  RtpSenderConfig sc = SenderCfg();
  sc.fec = true;
  sc.fec_config.fec_ssrc = kFecSsrc;
  RtpSender s(sc);
  s.SetFecParams({.protection = 1.0});
  RtpReceiverConfig rc = ReceiverCfg();
  rc.fec = true;
  rc.fec_config.fec_ssrc = kFecSsrc;
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  auto chunks = test::MakeTestFrameChunks(0, true, 4000, s.config().max_payload_size);
  s.SendVideoFrame(chunks, 0, Timestamp::Millis(0));
  auto out = s.TakePackets();
  size_t media = 0, fec = 0;
  for (auto& p : out) {
    if (p.kind == PacketKind::kFec) {
      SM_EXPECT_TRUE(r.OnPacket(p.packet.Serialize(), Timestamp::Millis(30)) == ReceivedKind::kFec);
      ++fec;
    } else {
      if (media++ == 1) continue;  // The second media packet is lost.
      r.OnPacket(p.packet.Serialize(), Timestamp::Millis(20));
    }
  }
  SM_ASSERT_GE(media, 3u);
  SM_ASSERT_GE(fec, 1u);
  r.Process(Timestamp::Millis(35));
  SM_EXPECT_GE(r.fec().stats().recovered, 1u);
  SM_EXPECT_EQ(r.stats().frames_received, 1u);  // Complete with the recovered packet.
}

namespace {
// Decodes a frame to 20 ms of samples whose value is the first byte.
class FakeDecoder : public AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t ts) override {
    ++calls;
    last_ts = ts;
    if (frame.empty()) return std::nullopt;
    return std::vector<float>(160, static_cast<float>(frame[0]) / 255.f);
  }
  int calls = 0;
  uint32_t last_ts = 0;
};
}  // namespace

SM_TEST(RtpReceiver, AudioFramesAreDecodedIntoTheJitterBuffer) {
  FakeDecoder dec;
  RtpReceiverConfig c;
  c.ssrc = 7;
  c.payload_type = 111;
  c.clock_rate = 8000;
  c.video = false;
  c.decoder = &dec;
  RtpReceiver r(c);
  for (uint16_t i = 0; i < 5; ++i) {
    RtpPacket p;
    p.ssrc = 7;
    p.payload_type = 111;
    p.seq = i;
    p.timestamp = static_cast<uint32_t>(i) * 160;
    p.payload = Bytes(40, static_cast<uint8_t>(10 + i));
    SM_EXPECT_TRUE(r.OnPacket(p.Serialize(), Timestamp::Millis(i * 20 + 5)) == ReceivedKind::kMedia);
  }
  SM_EXPECT_EQ(dec.calls, 5);
  SM_EXPECT_EQ(dec.last_ts, 640u);
  SM_EXPECT_EQ(r.stats().audio_frames, 5u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_received, 5u);
  SM_EXPECT_EQ(r.stats().frames_received, 0u);
  // An empty frame is a decode failure, not a crash.
  RtpPacket e;
  e.ssrc = 7;
  e.payload_type = 111;
  e.seq = 5;
  e.timestamp = 800;
  r.OnPacket(e.Serialize(), Timestamp::Millis(110));
  SM_EXPECT_EQ(r.stats().audio_decode_failures, 1u);
  std::vector<float> out(80);
  r.GetAudio(out, Timestamp::Millis(120));  // Playout does not crash.
}

SM_TEST(RtpReceiver, RedBlocksAreUnwrappedAndEachFrameDecodedOnce) {
  FakeDecoder dec;
  RtpReceiverConfig c;
  c.ssrc = 7;
  c.payload_type = 63;
  c.clock_rate = 8000;
  c.video = false;
  c.red = true;
  c.decoder = &dec;
  RtpReceiver r(c);
  loss::RedEncoder red(1);
  for (uint16_t i = 0; i < 6; ++i) {
    if (i == 2) {  // Packet 2 is lost; packet 3 repeats its frame.
      red.Encode(111, 160u * i, Bytes(40, 99));
      continue;
    }
    RtpPacket p;
    p.ssrc = 7;
    p.payload_type = 63;
    p.seq = i;
    p.timestamp = 160u * i;
    p.payload = red.Encode(111, p.timestamp, Bytes(40, static_cast<uint8_t>(10 + i)));
    r.OnPacket(p.Serialize(), Timestamp::Millis(i * 20 + 5));
  }
  SM_EXPECT_EQ(dec.calls, 6);  // Five received plus the one recovered from RED.
  SM_EXPECT_EQ(r.red().stats().recovered_frames, 1u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_received, 6u);
}

SM_TEST(RtpReceiver, StatisticsFeedReportBlocksAndFrameMetadataIsBounded) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  for (auto& b : SendFrame(s, 0, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  auto rb = r.receive_statistics()->MakeReportBlock(Timestamp::Millis(1000));
  SM_EXPECT_EQ(rb.ssrc, kSsrc);
  SM_EXPECT_EQ(rb.extended_highest_seq & 0xffff, 65530u);
  // Delta frames that depend on a frame that never arrives pile up in the
  // buffer; the receiver forgets old metadata instead of growing.
  for (int64_t i = 2; i < 2400; ++i) {
    for (auto& b : SendFrame(s, i, false, 100, Timestamp::Millis(i * 33))) r.OnPacket(b, Timestamp::Millis(i * 33 + 10));
    r.Process(Timestamp::Millis(i * 33 + 10));
    r.TakeFeedback();
  }
  MapMetricsWriter w;
  r.ExportMetrics(w);
  SM_EXPECT_GT(w.Get("frames_received"), 1000.0);
  SM_EXPECT_GT(w.Get("frame_meta_evicted"), 0.0);
}
