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
constexpr uint32_t kSsrc = 0x1111, kRtxSsrc = 0x2222;
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

// The sender's queued packets as the pacer releases them: numbered and stored in the RTX history
// (SendSideController::Process), each followed by the FEC packets it completed.
std::vector<SenderPacket> Release(RtpSender& s, Timestamp now) {
  std::vector<SenderPacket> out;
  for (auto& p : s.TakePackets()) {
    s.AssignSequenceNumber(p);
    s.OnPacketReleased(p, now);
    out.push_back(std::move(p));
    for (auto& f : s.TakeFecPackets()) {
      s.AssignSequenceNumber(f);
      s.OnPacketReleased(f, now);
      out.push_back(std::move(f));
    }
  }
  return out;
}

// One frame through the sender: the serialized packets of it.
std::vector<Bytes> SendFrame(RtpSender& s, int64_t id, bool key, size_t bytes, Timestamp now) {
  auto chunks = test::MakeTestFrameChunks(key, bytes, s.config().max_payload_size);
  s.SendVideoFrame(chunks, static_cast<uint32_t>(id * 3000), now);
  std::vector<Bytes> out;
  for (auto& p : Release(s, now)) out.push_back(p.packet.Serialize());
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
    if (i > 0) SM_EXPECT_TRUE(frames[i].frame_id > frames[i - 1].frame_id);
    SM_EXPECT_EQ(frames[i].keyframe, i == 0);
    SM_EXPECT_TRUE(frames[i].render);
    SM_EXPECT_EQ(frames[i].rtp_timestamp, static_cast<uint32_t>(i * 3000));
  }
  // The keyframe is SPS + PPS + IDR in 3000 Annex-B bytes; the payloads carry
  // the NAL units without start codes (3 x 4 bytes) and the IDR split into
  // FU-A fragments of 2 header bytes each.
  SM_EXPECT_TRUE(frames[0].keyframe);
  SM_EXPECT_EQ(frames[0].bitstream.size(), 3000u);
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
  SM_EXPECT_TRUE(frames[2].frame_id > frames[1].frame_id);
  SM_EXPECT_EQ(frames[1].rtp_timestamp, 3000u);
  SM_EXPECT_TRUE(r.TakeFeedback().keyframe.empty());
}

// RtpVideoStreamReceiver2::OnReceivedPayloadData: the NACK for a gap leaves with the packet that revealed it
// (NackRequester::OnReceivedPacket, then SendBufferedRtcpFeedback), before the packet buffer or the next
// process call. A packet reordered within the same millisecond is then NACKed too, as in libwebrtc (#411).
SM_TEST(RtpReceiver, AGapIsNackedWhenItIsSeenEvenIfThePacketArrivesInTheSameTick) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  auto f0 = SendFrame(s, 0, true, 3000, Timestamp::Millis(0));
  SM_ASSERT_GE(f0.size(), 3u);
  auto late = RtpPacket::Parse(f0[1]);
  SM_ASSERT_TRUE(late.has_value());
  r.OnPacket(f0[0], Timestamp::Micros(20100));
  r.OnPacket(f0[2], Timestamp::Micros(20300));  // Reveals the gap.
  r.OnPacket(f0[1], Timestamp::Micros(20600));  // Reordered by 0.3 ms.
  for (size_t i = 3; i < f0.size(); ++i) r.OnPacket(f0[i], Timestamp::Micros(20800));
  r.Process(Timestamp::Millis(21));
  ReceiverFeedback fb = r.TakeFeedback();
  SM_ASSERT_EQ(fb.nacks.size(), 1u);
  SM_EXPECT_EQ(fb.nacks[0], late->seq);
  SM_EXPECT_EQ(r.nack().pending(), 0u);
  SM_EXPECT_EQ(r.stats().frames_received, 1u);
  r.Process(Timestamp::Millis(200));
  SM_EXPECT_TRUE(r.TakeFeedback().nacks.empty());
}

SM_TEST(RtpReceiver, MissingFrameReferenceIsLeftToNack) {
  // libwebrtc keeps NACKing and asks for no keyframe until the decode time-out.
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  for (auto& b : SendFrame(s, 0, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  SendFrame(s, 1, false, 800, Timestamp::Millis(33));  // Frame 1 never arrives.
  for (auto& b : SendFrame(s, 2, false, 800, Timestamp::Millis(66))) r.OnPacket(b, Timestamp::Millis(80));
  bool asked = false;
  for (int64_t t = 80; t < 2000; ++t) {
    r.Process(Timestamp::Millis(t));
    ReceiverFeedback fb = r.TakeFeedback();
    for (const auto& m : fb.keyframe)
      if (m.type == loss::FeedbackType::kPli || m.type == loss::FeedbackType::kFir) asked = true;
  }
  SM_EXPECT_FALSE(asked);
}

namespace {
// An access unit with only an IDR slice (no SPS or PPS).
Bytes IdrOnlyFrame(size_t bytes) {
  Bytes full = MakeFakeH264Frame(true, bytes);
  auto nals = SplitAnnexB(full);
  Bytes out = {0, 0, 0, 1};
  out.insert(out.end(), nals.back().begin(), nals.back().end());
  return out;
}
std::vector<ReceivedFrame> DeliverFrame(const RtpReceiverConfig& cfg, const Bytes& annexb) {
  RtpSender s(SenderCfg());
  RtpReceiver r(cfg);
  r.Start(Timestamp::Millis(0));
  s.SendVideoFrame(H264Packetizer::Packetize(annexb, s.config().max_payload_size), 0, Timestamp::Millis(0));
  for (auto& p : Release(s, Timestamp::Millis(0))) r.OnPacket(p.packet.Serialize(), Timestamp::Millis(10));
  return Run(r, 10, 400);
}
}  // namespace

SM_TEST(RtpReceiver, AnIdrAloneIsAKeyframeByDefault) {
  // libwebrtc's default (H26xPacketBuffer without sps-pps-idr-in-keyframe).
  auto frames = DeliverFrame(ReceiverCfg(), IdrOnlyFrame(2500));
  SM_ASSERT_EQ(frames.size(), 1u);
  SM_EXPECT_TRUE(frames[0].keyframe);
  SM_EXPECT_TRUE(frames[0].render);
}

SM_TEST(RtpReceiver, StrictModeNeedsParameterSetsForAKeyframe) {
  RtpReceiverConfig strict = ReceiverCfg();
  strict.keyframe_needs_parameter_sets = true;
  SM_EXPECT_TRUE(DeliverFrame(strict, IdrOnlyFrame(2500)).empty());  // Not decodable: waits for a keyframe.
  auto frames = DeliverFrame(strict, MakeFakeH264Frame(true, 2500));
  SM_ASSERT_EQ(frames.size(), 1u);
  SM_EXPECT_TRUE(frames[0].keyframe);
}

SM_TEST(RtpReceiver, TheFrameBitstreamIsRebuiltFromThePackets) {
  Bytes key = MakeFakeH264Frame(true, 4000);
  auto frames = DeliverFrame(ReceiverCfg(), key);
  SM_ASSERT_EQ(frames.size(), 1u);
  SM_EXPECT_TRUE(frames[0].bitstream == key);
  RtpReceiverConfig off = ReceiverCfg();
  off.deliver_bitstream = false;
  frames = DeliverFrame(off, key);
  SM_ASSERT_EQ(frames.size(), 1u);
  SM_EXPECT_TRUE(frames[0].bitstream.empty());
}

SM_TEST(RtpReceiver, FrameIdsAndReferencesComeFromSequenceNumbers) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  std::vector<int64_t> last_seq;
  for (int64_t i = 0; i < 4; ++i) {
    auto pkts = SendFrame(s, i, i == 0, 3000, Timestamp::Millis(i * 33));
    last_seq.push_back(RtpPacket::Parse(pkts.back())->seq);
    for (auto& b : pkts) r.OnPacket(b, Timestamp::Millis(i * 33 + 10));
  }
  auto frames = Run(r, 140, 800);
  SM_ASSERT_EQ(frames.size(), 4u);
  // The initial sequence number 65530 wraps: ids keep increasing across it, and
  // each is the unwrapped sequence number of the frame's last packet.
  for (size_t i = 1; i < frames.size(); ++i) {
    SM_EXPECT_EQ(frames[i].frame_id - frames[i - 1].frame_id, (last_seq[i] - last_seq[i - 1] + 65536) % 65536);
  }
}

SM_TEST(RtpReceiver, ALostParameterSetPacketIsNackedAndNoKeyframeWithoutItIsDelivered) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  for (auto& b : SendFrame(s, 0, true, 3000, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  // The second keyframe loses its SPS+PPS packet (the first one).
  auto key2 = SendFrame(s, 1, true, 3000, Timestamp::Millis(33));
  auto lost = RtpPacket::Parse(key2[0]);
  SM_ASSERT_TRUE(lost.has_value());
  SM_EXPECT_EQ(lost->payload[0] & 0x1F, kH264NalStapA);
  for (size_t i = 1; i < key2.size(); ++i) r.OnPacket(key2[i], Timestamp::Millis(50));
  bool nacked = false;
  std::vector<ReceivedFrame> frames;
  for (int64_t t = 50; t < 400; ++t) {
    r.Process(Timestamp::Millis(t));
    for (auto& f : r.TakeFrames()) frames.push_back(f);
    for (uint16_t n : r.TakeFeedback().nacks) nacked = nacked || n == lost->seq;
  }
  SM_EXPECT_TRUE(nacked);
  SM_ASSERT_EQ(frames.size(), 1u);  // Only the first keyframe: the IDR alone is not handed over.
  // The retransmission completes it.
  r.OnPacket(key2[0], Timestamp::Millis(400));
  for (int64_t t = 400; t < 700; ++t) {
    r.Process(Timestamp::Millis(t));
    for (auto& f : r.TakeFrames()) frames.push_back(f);
  }
  SM_ASSERT_EQ(frames.size(), 2u);
  SM_EXPECT_TRUE(frames[1].keyframe);
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

SM_TEST(RtpReceiver, PaddingOnTheMediaSsrcIsReceivedNotLost) {
  RtpSenderConfig sc = SenderCfg();
  sc.rtx_ssrc.reset();  // Padding then uses the media SSRC and a media sequence number.
  RtpSender s(sc);
  RtpReceiverConfig rc = ReceiverCfg();
  rc.rtx_ssrc.reset();
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  auto f0 = SendFrame(s, 0, true, 800, Timestamp::Millis(0));
  SenderPacket pad = s.MakePadding(200, 3000, Timestamp::Millis(10));
  SM_EXPECT_EQ(pad.packet.ssrc, kSsrc);
  auto f1 = SendFrame(s, 1, false, 800, Timestamp::Millis(33));
  SM_ASSERT_EQ(f0.size(), 1u);
  SM_ASSERT_EQ(f1.size(), 1u);
  SM_EXPECT_EQ(pad.packet.seq, static_cast<uint16_t>(RtpPacket::Parse(f0[0])->seq + 1));
  r.OnPacket(f0[0], Timestamp::Millis(20));
  SM_EXPECT_TRUE(r.OnPacket(pad.packet.Serialize(), Timestamp::Millis(21)) == ReceivedKind::kPadding);
  r.OnPacket(f1[0], Timestamp::Millis(50));
  r.Process(Timestamp::Millis(60));
  SM_EXPECT_EQ(r.stats().gaps, 0u);
  SM_EXPECT_EQ(r.stats().padding, 1u);
  SM_EXPECT_EQ(r.nack().stats().missing_detected, 0u);
  SM_EXPECT_EQ(r.stats().frames_received, 2u);
  SM_EXPECT_EQ(r.packet_buffer().stats().padding, 1u);
  SM_EXPECT_TRUE(r.TakeFeedback().empty());
  // The report block counts the padding as received: three packets, none lost.
  auto rb = r.receive_statistics()->MakeReportBlock(Timestamp::Millis(1000));
  SM_EXPECT_EQ(rb.cumulative_lost, 0);
  SM_EXPECT_EQ(rb.extended_highest_seq & 0xffff, static_cast<uint32_t>(static_cast<uint16_t>(65530 + 2)));
  // A later retransmission-less run stays quiet: no NACK for the padding seq.
  for (int64_t t = 60; t < 600; ++t) {
    r.Process(Timestamp::Millis(t));
    SM_EXPECT_TRUE(r.TakeFeedback().nacks.empty());
  }
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
  shorty.payload = {0x19, 1, 2};  // STAP-B (type 25) is mode 2: not accepted.
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

// RED + ULPFEC on the media SSRC (RtpVideoStreamReceiver2::ParseAndHandleEncapsulatingHeader):
// the lost media packet is recovered from FEC, and the FEC packets are not NACKed. The lost packet is
// NACKed once, by the media packet after it, before its frame's FEC arrives (NackRequester::OnReceivedPacket
// sends the request at once, #411); FEC recovers it, so it is not requested again.
SM_TEST(RtpReceiver, UlpfecRecoversALostMediaPacket) {
  RtpSenderConfig sc = SenderCfg();
  sc.fec = true;
  RtpSender s(sc);
  s.SetFecParams({.fec_rate = 255, .max_fec_frames = 1});
  RtpReceiverConfig rc = ReceiverCfg();
  rc.fec = true;
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  auto chunks = test::MakeTestFrameChunks(true, 4000, s.config().max_payload_size);
  s.SendVideoFrame(chunks, 0, Timestamp::Millis(0));
  size_t media = 0, fec = 0;
  uint16_t lost_seq = 0;
  for (auto& p : Release(s, Timestamp::Millis(0))) {
    if (p.kind == PacketKind::kFec) {
      SM_EXPECT_TRUE(r.OnPacket(p.packet.Serialize(), Timestamp::Millis(30)) == ReceivedKind::kFec);
      ++fec;
    } else {
      if (media++ == 1) {  // The second media packet is lost.
        lost_seq = p.packet.seq;
        continue;
      }
      SM_EXPECT_TRUE(r.OnPacket(p.packet.Serialize(), Timestamp::Millis(20)) == ReceivedKind::kMedia);
    }
  }
  SM_ASSERT_GE(media, 3u);
  SM_ASSERT_GE(fec, 1u);
  // The next frame shows any sequence number left missing.
  for (auto& b : SendFrame(s, 1, false, 800, Timestamp::Millis(33))) r.OnPacket(b, Timestamp::Millis(53));
  r.Process(Timestamp::Millis(60));
  SM_ASSERT_TRUE(r.ulpfec() != nullptr);
  SM_EXPECT_EQ(r.ulpfec()->GetPacketCounter().num_recovered_packets, 1u);
  SM_EXPECT_EQ(r.stats().fec, fec + 1);  // The second frame's FEC too.
  SM_EXPECT_EQ(r.stats().frames_received, 2u);  // The first complete with the recovered packet.
  auto nacks = r.TakeFeedback().nacks;
  SM_ASSERT_EQ(nacks.size(), 1u);
  SM_EXPECT_EQ(nacks[0], lost_seq);
  SM_EXPECT_EQ(r.nack().pending(), 0u);
  auto frames = Run(r, 61, 600);
  SM_EXPECT_EQ(frames.size(), 2u);
  SM_EXPECT_TRUE(r.TakeFeedback().nacks.empty());
}

// A RED packet lost with no FEC to recover it is NACKed and resent on RTX with the RTX payload type
// associated with RED; the restored RED packet completes the frame.
SM_TEST(RtpReceiver, RedPacketRestoredFromRtx) {
  RtpSenderConfig sc = SenderCfg();
  sc.fec = true;
  RtpSender s(sc);  // FEC rate 0: RED only.
  RtpReceiverConfig rc = ReceiverCfg();
  rc.fec = true;
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  auto f0 = SendFrame(s, 0, true, 3000, Timestamp::Millis(0));
  auto f1 = SendFrame(s, 1, false, 800, Timestamp::Millis(33));
  SM_ASSERT_GE(f0.size(), 3u);
  for (size_t i = 0; i < f0.size(); ++i)
    if (i != 1) r.OnPacket(f0[i], Timestamp::Millis(20));
  for (auto& b : f1) r.OnPacket(b, Timestamp::Millis(53));
  r.Process(Timestamp::Millis(60));
  SM_EXPECT_EQ(r.stats().frames_received, 1u);
  ReceiverFeedback fb = r.TakeFeedback();
  SM_ASSERT_EQ(fb.nacks.size(), 1u);
  s.OnNack(fb.nacks, Timestamp::Millis(80), TimeDelta::Millis(40));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_EQ(rtx[0].packet.payload_type, sc.ulpfec.red_rtx_payload_type);
  SM_EXPECT_TRUE(r.OnPacket(rtx[0].packet.Serialize(), Timestamp::Millis(100)) == ReceivedKind::kRtx);
  r.Process(Timestamp::Millis(101));
  SM_EXPECT_EQ(r.stats().frames_received, 2u);
  SM_EXPECT_EQ(r.nack().stats().recovered, 1u);
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

namespace {
// A frame whose second byte is 1 also carries the previous frame as in-band
// FEC (160 samples), as an Opus packet with LBRR.
class FecDecoder : public FakeDecoder {
 public:
  uint32_t FecDuration(std::span<const uint8_t> frame) const override { return frame.size() > 1 && frame[1] == 1 ? 160 : 0; }
  std::optional<std::vector<float>> DecodeFec(std::span<const uint8_t>, uint32_t ts) override {
    fec_ts.push_back(ts);
    return std::vector<float>(160, 0.5f);
  }
  std::vector<uint32_t> fec_ts;
};

RtpReceiverConfig FecAudioCfg(AudioDecoder* dec, int jitter_buffer_version) {
  RtpReceiverConfig c;
  c.ssrc = 7;
  c.payload_type = 111;
  c.clock_rate = 8000;
  c.video = false;
  c.decoder = dec;
  c.jitter_buffer.version = jitter_buffer_version;
  return c;
}

void SendFecAudio(RtpReceiver& r, uint16_t i, bool fec) {
  RtpPacket p;
  p.ssrc = 7;
  p.payload_type = 111;
  p.seq = i;
  p.timestamp = 160u * i;
  p.payload = Bytes(40, static_cast<uint8_t>(10 + i));
  p.payload[1] = fec ? 1 : 0;
  r.OnPacket(p.Serialize(), Timestamp::Millis(i * 20 + 5));
}
}  // namespace

// AudioDecoderOpusImpl::ParsePayload (audio_decoder_opus.cc): a packet with
// in-band FEC gives NetEq a second frame at its timestamp less the FEC's
// duration, which fills a lost packet's slot (neteq v2 only).
SM_TEST(RtpReceiver, InBandFecFillsTheLostFrameWithNetEqV2) {
  FecDecoder dec;
  RtpReceiver r(FecAudioCfg(&dec, 2));
  for (uint16_t i = 0; i < 6; ++i) {
    if (i != 2) SendFecAudio(r, i, i > 0);  // Packet 2 is lost; packet 3 carries it.
  }
  // Packets 1, 3, 4 and 5 carry FEC for 0, 2, 3 and 4.
  SM_EXPECT_EQ(dec.fec_ts.size(), 4u);
  SM_EXPECT_EQ(dec.fec_ts[1], 320u);
  SM_EXPECT_EQ(r.stats().audio_fec_frames, 4u);
  SM_EXPECT_EQ(r.stats().audio_frames, 5u);
  // Every slot 0-5 is held once: the FEC copies of 0, 3 and 4 lose to the
  // frames themselves (PacketBuffer::InsertPacket), and slot 2 is the FEC.
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_received, 9u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_duplicate, 3u);
  // Playout conceals exactly as much as a receiver that got every packet.
  FecDecoder all_dec;
  RtpReceiver all(FecAudioCfg(&all_dec, 2));
  for (uint16_t i = 0; i < 6; ++i) SendFecAudio(all, i, false);
  std::vector<float> out(80);
  for (int t = 0; t < 200; t += 10) {
    r.GetAudio(out, Timestamp::Millis(120 + t));
    all.GetAudio(out, Timestamp::Millis(120 + t));
  }
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_lost, 0u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().concealed_samples, all.jitter_buffer().stats().concealed_samples);
  SM_EXPECT_EQ(r.jitter_buffer().stats().concealment_events, all.jitter_buffer().stats().concealment_events);
}

SM_TEST(RtpReceiver, InBandFecIsIgnoredWithNetEqV1) {
  FecDecoder dec;
  RtpReceiver r(FecAudioCfg(&dec, 1));
  for (uint16_t i = 0; i < 6; ++i) {
    if (i != 2) SendFecAudio(r, i, i > 0);
  }
  SM_EXPECT_TRUE(dec.fec_ts.empty());
  SM_EXPECT_EQ(r.stats().audio_fec_frames, 0u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_received, 5u);
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
  SendFrame(s, 1, false, 100, Timestamp::Millis(33));  // Lost on the way.
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

namespace {
class SeqLog : public VideoMediaObserver {
 public:
  void OnVideoMediaPacket(uint16_t seq, Timestamp) override { seqs.push_back(seq); }
  std::vector<uint16_t> seqs;
};
}  // namespace

SM_TEST(RtpReceiver, MediaObserverSeesOriginalsAndRtxRestoresButNotPadding) {
  SeqLog log;
  RtpReceiverConfig rc = ReceiverCfg();
  rc.media_observer = &log;
  RtpSender s(SenderCfg());
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  auto f0 = SendFrame(s, 0, true, 3000, Timestamp::Millis(0));
  SM_ASSERT_GE(f0.size(), 3u);
  for (size_t i = 0; i < f0.size(); ++i)
    if (i != 1) r.OnPacket(f0[i], Timestamp::Millis(20));
  SM_EXPECT_EQ(log.seqs.size(), f0.size() - 1);
  auto lost = RtpPacket::Parse(f0[1]);
  s.OnNack(std::vector<uint16_t>{lost->seq}, Timestamp::Millis(60), TimeDelta::Millis(40));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  r.OnPacket(rtx[0].packet.Serialize(), Timestamp::Millis(80));
  SM_ASSERT_EQ(log.seqs.size(), f0.size());
  SM_EXPECT_EQ(log.seqs.back(), lost->seq);  // The restored packet carries the original sequence number.
  // A padding-only packet on the RTX SSRC is not media.
  SenderPacket pad = s.MakePadding(200, 0, Timestamp::Millis(90));
  r.OnPacket(pad.packet.Serialize(), Timestamp::Millis(90));
  SM_EXPECT_EQ(log.seqs.size(), f0.size());
}

// Lip sync (issue #77): RtpVideoStreamReceiver2::UpdatePacketReceiveTimestamps
// runs only for packets that are not `recovered()`. A late RTX restore of an
// old frame leaves the latest received RTP timestamp and receive time alone,
// so it doesn't look like a 100 ms jump in video delay to the synchronizer.
SM_TEST(RtpReceiver, SyncInfoIgnoresPacketsRestoredFromRtx) {
  RtpReceiverConfig rc = ReceiverCfg();
  rc.timing.version = 2;
  RtpSender s(SenderCfg());
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  timing::SenderReport sr;
  sr.ssrc = kSsrc;
  sr.ntp = timing::NtpTime{uint64_t{1} << 32};
  sr.rtp_timestamp = 0;
  r.OnSenderReport(sr, Timestamp::Millis(10));
  auto f0 = SendFrame(s, 0, true, 3000, Timestamp::Millis(0));
  auto f1 = SendFrame(s, 1, false, 800, Timestamp::Millis(33));
  SM_ASSERT_GE(f0.size(), 3u);
  for (size_t i = 0; i < f0.size(); ++i)
    if (i != 1) r.OnPacket(f0[i], Timestamp::Millis(20));
  for (auto& b : f1) r.OnPacket(b, Timestamp::Millis(53));
  auto before = r.SyncInfo();
  SM_ASSERT_TRUE(before.has_value());
  SM_EXPECT_EQ(before->latest_received_capture_rtp_timestamp, 3000u);
  SM_EXPECT_EQ(before->latest_receive_time.ms(), 53);
  // Frame 0's lost packet arrives through RTX 100 ms later.
  auto lost = RtpPacket::Parse(f0[1]);
  s.OnNack(std::vector<uint16_t>{lost->seq}, Timestamp::Millis(130), TimeDelta::Millis(40));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_TRUE(r.OnPacket(rtx[0].packet.Serialize(), Timestamp::Millis(153)) == ReceivedKind::kRtx);
  auto after = r.SyncInfo();
  SM_ASSERT_TRUE(after.has_value());
  SM_EXPECT_EQ(after->latest_received_capture_rtp_timestamp, 3000u);
  SM_EXPECT_EQ(after->latest_receive_time.ms(), 53);
  // A packet received as it was sent still moves it.
  auto f2 = SendFrame(s, 2, false, 800, Timestamp::Millis(166));
  for (auto& b : f2) r.OnPacket(b, Timestamp::Millis(186));
  SM_EXPECT_EQ(r.SyncInfo()->latest_received_capture_rtp_timestamp, 6000u);
  SM_EXPECT_EQ(r.SyncInfo()->latest_receive_time.ms(), 186);
}

namespace {
// A keyframe whose SPS+PPS packet (the first) arrives after the packet that starts the IDR slice, as
// reordering can do at the start of a call. Returns the frames and counts the PLIs sent.
std::vector<ReceivedFrame> DeliverReordered(const RtpReceiverConfig& cfg, int* plis, uint64_t* dropped) {
  RtpSender s(SenderCfg());
  RtpReceiver r(cfg);
  r.Start(Timestamp::Millis(0));
  s.SendVideoFrame(H264Packetizer::Packetize(MakeFakeH264Frame(true, 3000), s.config().max_payload_size), 0,
                   Timestamp::Millis(0));
  auto pkts = Release(s, Timestamp::Millis(0));
  SM_EXPECT_TRUE(pkts.size() >= 3u);
  std::vector<Bytes> order;
  for (size_t i = 1; i < pkts.size(); ++i) order.push_back(pkts[i].packet.Serialize());
  order.push_back(pkts[0].packet.Serialize());
  *plis = 0;
  std::vector<ReceivedFrame> out;
  for (int64_t t = 0; t < 400; ++t) {
    if (t >= 10 && t - 10 < static_cast<int64_t>(order.size())) r.OnPacket(order[t - 10], Timestamp::Millis(t));
    r.Process(Timestamp::Millis(t));
    for (const auto& m : r.TakeFeedback().keyframe) *plis += m.type == loss::FeedbackType::kPli;
    for (auto& f : r.TakeFrames()) out.push_back(f);
  }
  *dropped = r.stats().idr_without_parameter_sets;
  return out;
}
}  // namespace

SM_TEST(RtpReceiver, SpsPpsTrackerDropsAnIdrStartBeforeItsParameterSetsAndAsksForAKeyframe) {
  // H264SpsPpsTracker::CopyAndFixBitstream returns kRequestKeyframe for the first packet of an IDR
  // slice whose PPS and SPS have not been seen; RtpVideoStreamReceiver2 drops it and sends a PLI.
  RtpReceiverConfig on = ReceiverCfg();
  on.sps_pps_tracker = true;
  int plis_on = 0, plis_off = 0;
  uint64_t dropped_on = 0, dropped_off = 0;
  auto frames_on = DeliverReordered(on, &plis_on, &dropped_on);
  SM_EXPECT_TRUE(frames_on.empty());  // The frame misses its IDR start: no keyframe until a new one.
  SM_EXPECT_EQ(dropped_on, 1u);
  RtpReceiverConfig off = on;
  off.sps_pps_tracker = false;  // Today's default: the reordered keyframe is assembled.
  auto frames_off = DeliverReordered(off, &plis_off, &dropped_off);
  SM_ASSERT_EQ(frames_off.size(), 1u);
  SM_EXPECT_TRUE(frames_off[0].keyframe);
  SM_EXPECT_EQ(dropped_off, 0u);
  SM_EXPECT_TRUE(plis_on > plis_off);
}

SM_TEST(RtpReceiver, SpsPpsTrackerPassesAKeyframeInOrder) {
  RtpReceiverConfig on = ReceiverCfg();
  on.sps_pps_tracker = true;
  auto frames = DeliverFrame(on, MakeFakeH264Frame(true, 3000));
  SM_ASSERT_EQ(frames.size(), 1u);
  SM_EXPECT_TRUE(frames[0].keyframe);
}
