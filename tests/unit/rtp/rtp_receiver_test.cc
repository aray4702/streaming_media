#include <algorithm>
#include <memory>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/cc/transport_feedback.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/sps_vui_rewriter.h"
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
// Without H.264's `missing_packets_` hold (on by default since #580): a delta frame behind a hole is
// handed to the frame buffer, which these NACK tests count.
RtpReceiverConfig NoHoldCfg() {
  RtpReceiverConfig c = ReceiverCfg();
  c.packet_buffer.missing_packets = false;
  return c;
}

// The sender's queued packets as the pacer releases them: numbered and stored in the RTX history
// (SendSideController::Process), each followed by the FEC packets it completed.
std::vector<SenderPacket> Release(RtpSender& s, Timestamp now) {
  std::vector<SenderPacket> out;
  for (auto& p : s.TakePackets()) {
    s.AssignSequenceNumber(p, now);
    s.OnPacketReleased(p, now);
    out.push_back(std::move(p));
    for (auto& f : s.TakeFecPackets()) {
      s.AssignSequenceNumber(f, now);
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
  // FU-A fragments of 2 header bytes each. The test sends the fake encoder's
  // SPS without the outgoing VUI rewrite, so the receiver adds the VUI's
  // bitstream_restriction as VideoRtpDepacketizerH264 does (+5 bytes).
  SM_EXPECT_TRUE(frames[0].keyframe);
  SM_EXPECT_EQ(frames[0].bitstream.size(), 3005u);
  SM_EXPECT_TRUE(frames[0].bitstream == rtp::ParseOutgoingBitstreamAndRewrite(MakeFakeH264Frame(true, 3000)));
  SM_EXPECT_TRUE(r.TakeFeedback().empty());
}

SM_TEST(RtpReceiver, LostPacketIsNackedAndRtxCompletesTheFrame) {
  RtpSender s(SenderCfg());
  RtpReceiver r(NoHoldCfg());
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

// VideoReceiveStream2::OnCompleteFrame -> RtpVideoStreamReceiver2::FrameContinuous -> NackRequester::ClearUpTo:
// every frame that becomes continuous clears the recovered list up to its last packet, delta frames included.
// So a packet that RTX recovered ahead of the newest is forgotten once a later recovered frame is continuous,
// and the gap that a later packet reveals asks for it again (issue_466_scene_change_very_low_capacity seed 1,
// libwebrtc's second RTX of 6807 at 11.61 s, #893).
SM_TEST(RtpReceiver, AContinuousDeltaFrameClearsTheRecoveredListBeforeIt) {
  RtpSender s(SenderCfg());
  RtpReceiver r(ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  std::vector<std::vector<Bytes>> f;
  for (int64_t i = 0; i < 5; ++i) f.push_back(SendFrame(s, i, i == 0, 300, Timestamp::Millis(33 * i)));
  std::vector<uint16_t> seq;
  for (const auto& frame : f) {
    SM_ASSERT_EQ(frame.size(), 1u);
    seq.push_back(RtpPacket::Parse(frame[0])->seq);
  }
  r.OnPacket(f[0][0], Timestamp::Millis(20));
  r.Process(Timestamp::Millis(20));
  SM_EXPECT_TRUE(r.TakeFeedback().nacks.empty());
  // Frames 1, 2 and 3 are lost. RTX recovers 1 and 2 before any later packet shows the gap.
  const uint16_t early[] = {seq[1], seq[2]};
  s.OnNack(early, Timestamp::Millis(100), TimeDelta::Millis(40));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 2u);
  for (size_t i = 0; i < rtx.size(); ++i) {
    const Timestamp at = Timestamp::Millis(120 + 10 * static_cast<int64_t>(i));
    SM_EXPECT_TRUE(r.OnPacket(rtx[i].packet.Serialize(), at) == ReceivedKind::kRtx);
    r.Process(at);
  }
  SM_EXPECT_EQ(r.stats().frames_scheduled, 3u);
  SM_EXPECT_TRUE(r.TakeFeedback().nacks.empty());
  // Frame 4 reveals the gap from frame 0: frame 2's continuity cleared frame 1 from the recovered list.
  r.OnPacket(f[4][0], Timestamp::Millis(160));
  r.Process(Timestamp::Millis(160));
  ReceiverFeedback fb = r.TakeFeedback();
  SM_ASSERT_EQ(fb.nacks.size(), 2u);
  SM_EXPECT_EQ(fb.nacks[0], seq[1]);
  SM_EXPECT_EQ(fb.nacks[1], seq[3]);
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
  // libwebrtc's default (H26xPacketBuffer without sps-pps-idr-in-keyframe). The SPS/PPS tracker
  // (on by default since #580) would drop an IDR that has had no parameter sets before it.
  RtpReceiverConfig cfg = ReceiverCfg();
  cfg.sps_pps_tracker = false;
  auto frames = DeliverFrame(cfg, IdrOnlyFrame(2500));
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
  // As sent by Session::SendVideo, with the SPS's VUI rewritten: the receive
  // side then finds it already restricted (kVuiOk) and copies it.
  Bytes key = rtp::ParseOutgoingBitstreamAndRewrite(MakeFakeH264Frame(true, 4000));
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

// libwebrtc's default H.264 path (PacketBuffer::FindFrames, packet_buffer.cc;
// H26xPacketBuffer is behind a trial that is off in M157): the IDR start is a
// first packet, so the frame is handed out from it when the parameter-set
// packet before it is lost, and it is a keyframe because it has an IDR slice
// (WebRTC-SpsPpsIdrIsH264Keyframe off). The parameter sets are known from the
// first keyframe, so H264SpsPpsTracker lets the IDR through. The lost packet
// is still NACKed, and its retransmission makes no second frame.
SM_TEST(RtpReceiver, AKeyframeWhoseParameterSetPacketIsLostIsDeliveredFromItsIdr) {
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
  SM_ASSERT_EQ(frames.size(), 2u);
  SM_EXPECT_TRUE(frames[1].keyframe);
  // The retransmission of the parameter-set packet completes no frame.
  r.OnPacket(key2[0], Timestamp::Millis(400));
  for (int64_t t = 400; t < 700; ++t) {
    r.Process(Timestamp::Millis(t));
    for (auto& f : r.TakeFrames()) frames.push_back(f);
  }
  SM_EXPECT_EQ(frames.size(), 2u);
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
  shorty.payload = {0x1C};  // An FU-A without its FU header: ParseFuaNalu drops it.
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
  RtpReceiverConfig rc = NoHoldCfg();
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

// NetEqImpl::InsertPacketInternal with RedPayloadSplitter::SplitRed (issue
// #722): the primary goes in first, then the redundant block, and every block
// reaches NetEq. Packet 2 is lost, so packet 3's redundant copy of frame 2
// arrives after frame 3 and is reordered. So is packet 1's copy of frame 0:
// the first pull's SoftReset cleared frame 0's arrival (#721's t = 50 row).
// The copies of frames that arrived are inserted too: frame 0's is late and
// waits in the buffer until the next pull's DiscardOldPackets (#783), the
// copies of frames 3 and 4 are duplicates.
SM_TEST(RtpReceiver, RedBlocksReachNetEqAsSplitRedOrders) {
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
  std::vector<float> out(80);
  for (uint16_t i = 0; i < 6; ++i) {
    if (i == 2) {
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
    if (i == 0) r.GetAudio(out, Timestamp::Millis(6));  // The first pull's SoftReset, as NetEq's new codec.
    if (i == 3) SM_EXPECT_EQ(dec.last_ts, 320u);  // Frame 2 (redundant) after frame 3 (primary).
  }
  SM_EXPECT_EQ(dec.calls, 9);  // Five primaries and four redundant blocks.
  SM_EXPECT_EQ(r.red().stats().recovered_frames, 1u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_received, 9u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_duplicate, 2u);
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_discarded_late, 0u);
  SM_EXPECT_EQ(CollectMetrics(r.jitter_buffer().neteq_delay()).at("reordered"), 2.0);
  r.GetAudio(out, Timestamp::Millis(126));
  SM_EXPECT_EQ(r.jitter_buffer().stats().packets_discarded_late, 1u);
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

RtpReceiverConfig FecAudioCfg(AudioDecoder* dec) {
  RtpReceiverConfig c;
  c.ssrc = 7;
  c.payload_type = 111;
  c.clock_rate = 8000;
  c.video = false;
  c.decoder = dec;
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
// duration, which fills a lost packet's slot.
SM_TEST(RtpReceiver, InBandFecFillsTheLostFrameWithNetEqV2) {
  FecDecoder dec;
  RtpReceiver r(FecAudioCfg(&dec));
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
  RtpReceiver all(FecAudioCfg(&all_dec));
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

namespace {
// Gives each frame's duration (AudioDecoder::PacketDuration), so the jitter
// buffer decodes it at playout; records the order of the decodes.
class PlayoutDecoder : public FecDecoder {
 public:
  uint32_t PacketDuration(std::span<const uint8_t>) const override { return 160; }
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t ts) override {
    order.push_back(ts);
    return FecDecoder::Decode(frame, ts);
  }
  std::vector<uint32_t> order;
};
}  // namespace

// NetEqImpl::DecodeLoop decodes each packet when it extracts it, in timestamp
// order (#783): a RED packet's primary goes into NetEq first, but the
// redundant (older) frame is decoded first, and a stateful decoder (Opus) sees
// NetEq's sequence. A copy that a higher priority replaced is never decoded.
SM_TEST(RtpReceiver, NetEqV2DecodesFramesAtPlayoutInTimestampOrder) {
  PlayoutDecoder dec;
  RtpReceiverConfig c = FecAudioCfg(&dec);
  c.payload_type = 63;
  c.red = true;
  RtpReceiver r(c);
  loss::RedEncoder red(1);
  red.Encode(111, 0, Bytes(40, 10));  // Frame 0 is lost; packet 1 repeats it.
  for (uint16_t i = 1; i < 4; ++i) {
    RtpPacket p;
    p.ssrc = 7;
    p.payload_type = 63;
    p.seq = i;
    p.timestamp = 160u * i;
    Bytes frame(40, static_cast<uint8_t>(10 + i));
    frame[1] = 1;  // In-band FEC of the frame before, which the RED copy or the frame itself replaces.
    p.payload = red.Encode(111, p.timestamp, frame);
    r.OnPacket(p.Serialize(), Timestamp::Millis(i * 20 + 5));
  }
  SM_EXPECT_TRUE(dec.order.empty());  // Nothing is decoded on arrival.
  SM_EXPECT_EQ(r.stats().audio_frames, 6u);
  std::vector<float> out(80);
  for (int t = 0; t < 100; t += 10) r.GetAudio(out, Timestamp::Millis(100 + t));
  SM_EXPECT_EQ(dec.order.size(), 4u);
  for (size_t i = 0; i < dec.order.size(); ++i) SM_EXPECT_EQ(dec.order[i], 160u * i);
  SM_EXPECT_TRUE(dec.fec_ts.empty());
}

SM_TEST(RtpReceiver, StatisticsFeedReportBlocksAndFrameMetadataIsBounded) {
  RtpSender s(SenderCfg());
  // Timing v1's 1024-entry bound; timing v2's is the 800-frame buffer (#583, tested below).
  RtpReceiverConfig rc = ReceiverCfg();
  rc.timing.version = 1;
  RtpReceiver r(rc);
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

// Video orientation (CVO, #567): a frame takes its rotation from its last packet's extension,
// 0 when it has none (RtpVideoStreamReceiver2::ReceivePacket and OnAssembledFrame,
// compare/webrtc/reference/video/rtp_video_stream_receiver2.cc:797-802, 1108).
SM_TEST(RtpReceiver, FrameRotationComesFromTheLastPacketsCvo) {
  sync::ExtensionMap map;
  map.Register(5, sync::uri::kVideoOrientation);
  RtpSenderConfig sc = SenderCfg();
  sc.extensions = &map;
  RtpSender s(sc);
  RtpReceiverConfig rc = ReceiverCfg();
  rc.extensions = &map;
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  const video::Rotation rotations[] = {video::Rotation::k90, video::Rotation::k90, video::Rotation::k0,
                                       video::Rotation::k0, video::Rotation::k270};
  for (int64_t i = 0; i < 5; ++i) {
    const Timestamp at = Timestamp::Millis(i * 33);
    auto chunks = test::MakeTestFrameChunks(i == 0, i == 0 ? 3000 : 800, s.config().max_payload_size);
    s.SendVideoFrame(chunks, static_cast<uint32_t>(i * 3000), at, i == 0, rotations[i]);
    for (auto& p : Release(s, at)) r.OnPacket(p.packet.Serialize(), at + TimeDelta::Millis(20));
    r.Process(at + TimeDelta::Millis(20));
  }
  auto frames = Run(r, 200, 800);
  SM_ASSERT_EQ(frames.size(), 5u);
  // The fourth frame carries no CVO (0, unchanged): rotation 0, as libwebrtc resets it per packet.
  for (size_t i = 0; i < frames.size(); ++i) SM_EXPECT_TRUE(frames[i].rotation == rotations[i]);
}

SM_TEST(RtpReceiver, SpsPpsTrackerDropsAParameterSetWithoutIds) {
  // An SPS sent as FU-A carries no SPS id (ParseFuaNalu), so
  // H264SpsPpsTracker::CopyAndFixBitstream returns kDrop: the packet is
  // ignored without a keyframe request (rtp_video_stream_receiver2.cc:930-936).
  RtpReceiverConfig cfg = ReceiverCfg();
  cfg.sps_pps_tracker = true;
  RtpReceiver r(cfg);
  r.Start(Timestamp::Millis(0));
  RtpPacket p;
  p.ssrc = cfg.ssrc;
  p.payload_type = 96;
  p.seq = 10;
  p.timestamp = 9000;
  Bytes sps = {0x67};
  sps.insert(sps.end(), kFakeH264Sps, kFakeH264Sps + sizeof(kFakeH264Sps));
  p.payload = {static_cast<uint8_t>(0x60 | kH264NalFuA), static_cast<uint8_t>(0x80 | kH264NalSps)};
  p.payload.insert(p.payload.end(), sps.begin() + 1, sps.end());
  r.OnPacket(p.Serialize(), Timestamp::Millis(10));
  r.Process(Timestamp::Millis(20));
  SM_EXPECT_EQ(r.stats().parameter_sets_without_ids, 1u);
  SM_EXPECT_EQ(r.stats().idr_without_parameter_sets, 0u);
  SM_EXPECT_EQ(r.stats().invalid_payload, 0u);
  int plis = 0;
  for (const auto& m : r.TakeFeedback().keyframe) plis += m.type == loss::FeedbackType::kPli;
  SM_EXPECT_EQ(plis, 0);
}

// receive_statistics_v2 (issue #575): the statisticians follow libwebrtc's receive streams:
// video with NACK 450 (kMaxPacketAgeToNack, rtp_video_stream_receiver2.cc:365-367), video
// without NACK, RTX and audio kDefaultMaxReorderingThreshold (50). On by default, as
// libwebrtc; off keeps the v1 counts.
// The receivers are on the heap: each is about 17 KB, and four on the stack overflow the 64 KB
// default stack of the wasm test binaries that don't link Opus.
SM_TEST(RtpReceiver, ReceiveStatisticsV2Thresholds) {
  RtpReceiverConfig c = ReceiverCfg();
  c.receive_statistics_v2 = false;
  auto v1 = std::make_unique<RtpReceiver>(c);
  SM_EXPECT_FALSE(v1->receive_statistics()->v2());
  SM_EXPECT_FALSE(v1->rtx_receive_statistics()->v2());
  c = ReceiverCfg();
  auto nack = std::make_unique<RtpReceiver>(c);
  SM_EXPECT_TRUE(nack->receive_statistics()->v2());
  SM_EXPECT_EQ(nack->receive_statistics()->max_reordering_threshold(), timing::kMaxPacketAgeToNack);
  SM_EXPECT_TRUE(nack->rtx_receive_statistics()->v2());
  SM_EXPECT_EQ(nack->rtx_receive_statistics()->max_reordering_threshold(), timing::kDefaultMaxReorderingThreshold);
  c.nack = false;
  auto no_nack = std::make_unique<RtpReceiver>(c);
  SM_EXPECT_EQ(no_nack->receive_statistics()->max_reordering_threshold(), timing::kDefaultMaxReorderingThreshold);
  RtpReceiverConfig a;
  a.ssrc = kSsrc;
  a.payload_type = 111;
  a.clock_rate = 48000;
  a.video = false;
  auto audio = std::make_unique<RtpReceiver>(a);
  SM_EXPECT_TRUE(audio->receive_statistics()->v2());
  SM_EXPECT_EQ(audio->receive_statistics()->max_reordering_threshold(), timing::kDefaultMaxReorderingThreshold);
}

// Frame buffer cap (#583), timing v2: libwebrtc's FrameBuffer holds at most kMaxFramesBuffered = 800
// frames (compare/webrtc/reference/video/video_stream_buffer_controller.cc:46); in a full buffer a
// delta frame is refused and a keyframe clears it (api/video/frame_buffer.cc:98-106).
namespace {
RtpReceiverConfig V2ReceiverCfg() {
  RtpReceiverConfig rc = NoHoldCfg();  // Delta frames behind the lost one reach the frame buffer.
  rc.timing.version = 2;
  rc.nack = false;
  return rc;
}
}  // namespace

SM_TEST(RtpReceiver, AFullFrameBufferRefusesADeltaFrameAndAKeyframeClearsIt) {
  RtpSender s(SenderCfg());
  RtpReceiver r(V2ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  for (auto& b : SendFrame(s, 0, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  auto first = Run(r, 10, 12);
  SM_ASSERT_EQ(first.size(), 1u);  // The keyframe is released at once.
  SendFrame(s, 1, false, 100, Timestamp::Millis(33));  // Lost: every later delta frame waits for it.
  Timestamp now = Timestamp::Millis(20);
  for (int64_t i = 2; i < 802; ++i) {
    for (auto& b : SendFrame(s, i, false, 100, now)) r.OnPacket(b, now);
    r.Process(now);
    now = now + TimeDelta::Millis(1);
  }
  SM_EXPECT_EQ(r.stats().frames_received, 801u);
  SM_EXPECT_EQ(r.stats().frames_refused, 0u);
  // The 801st buffered frame: a delta frame has no room.
  for (auto& b : SendFrame(s, 802, false, 100, now)) r.OnPacket(b, now);
  r.Process(now);
  SM_EXPECT_EQ(r.stats().frames_refused, 1u);
  SM_EXPECT_EQ(r.stats().frame_buffer_clears, 0u);
  // A keyframe clears the full buffer and is decoded; the delta frame after it follows.
  now = now + TimeDelta::Millis(1);
  for (auto& b : SendFrame(s, 803, true, 800, now)) r.OnPacket(b, now);
  r.Process(now);
  SM_EXPECT_EQ(r.stats().frame_buffer_clears, 1u);
  for (auto& b : SendFrame(s, 804, false, 100, now)) r.OnPacket(b, now);
  std::vector<ReceivedFrame> frames;
  for (int64_t t = now.ms(); t < now.ms() + 3000; ++t) {
    r.Process(Timestamp::Millis(t));
    for (auto& f : r.TakeFrames()) frames.push_back(f);
  }
  SM_ASSERT_EQ(frames.size(), 2u);
  SM_EXPECT_TRUE(frames[0].keyframe && frames[0].render);
  SM_EXPECT_EQ(frames[0].rtp_timestamp, 803u * 3000u);
  SM_EXPECT_TRUE(!frames[1].keyframe && frames[1].render);
  SM_EXPECT_EQ(frames[1].rtp_timestamp, 804u * 3000u);
  SM_EXPECT_EQ(r.stats().frames_refused, 1u);
}

SM_TEST(RtpReceiver, AfterAPacketBufferResetAnOlderIdIsRefusedUnlessItIsANewerKeyframe) {
  // frame_buffer.cc:84-96. Packets of frames at or below the last decoded one are normally dropped by
  // the packet buffer (ClearTo); after it overflows and is cleared, a sender restarted at lower
  // sequence numbers gets through: its delta frame is refused, and its keyframe, with an older frame
  // id but a newer RTP timestamp, clears the frame buffer and the decoded history and is decoded.
  RtpSender s(SenderCfg());
  RtpReceiver r(V2ReceiverCfg());
  r.Start(Timestamp::Millis(0));
  for (auto& b : SendFrame(s, 0, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  SM_ASSERT_EQ(Run(r, 10, 12).size(), 1u);
  // Frames whose last packet is lost stay in the packet buffer until it overflows and is cleared.
  Timestamp now = Timestamp::Millis(20);
  for (int64_t i = 1; i < 1100 && r.packet_buffer().stats().cleared == 0; ++i) {
    auto packets = SendFrame(s, i, false, 3000, now);
    SM_ASSERT_GE(packets.size(), 3u);
    for (size_t k = 0; k + 1 < packets.size(); ++k) r.OnPacket(packets[k], now);
    r.Process(now);
    r.TakeFeedback();
  }
  SM_ASSERT_GT(r.packet_buffer().stats().cleared, 0u);
  const uint64_t refused = r.stats().frames_refused;
  const uint64_t clears = r.stats().frame_buffer_clears;
  RtpSenderConfig restarted = SenderCfg();
  restarted.initial_seq = 65530 - 300;
  RtpSender s2(restarted);
  now = now + TimeDelta::Millis(1);
  for (auto& b : SendFrame(s2, 3000, false, 100, now)) r.OnPacket(b, now);
  r.Process(now);
  SM_EXPECT_EQ(r.stats().frames_refused, refused + 1);
  SM_EXPECT_EQ(r.stats().frame_buffer_clears, clears);
  for (auto& b : SendFrame(s2, 3001, true, 800, now)) r.OnPacket(b, now);
  std::vector<ReceivedFrame> frames;
  for (int64_t t = now.ms(); t < now.ms() + 3000; ++t) {
    r.Process(Timestamp::Millis(t));
    for (auto& f : r.TakeFrames()) frames.push_back(f);
  }
  SM_EXPECT_EQ(r.stats().frame_buffer_clears, clears + 1);
  SM_ASSERT_GE(frames.size(), 1u);
  SM_EXPECT_TRUE(frames[0].keyframe && frames[0].render);
  SM_EXPECT_EQ(frames[0].rtp_timestamp, 3001u * 3000u);
}

// Absolute capture time (#567 F8.3 part 6, receive half): RtpVideoStreamReceiver2::OnReceivedPayloadData
// interpolates a packet without the extension from the last one at 90 kHz, and the frame takes its first
// packet's (compare/webrtc/reference/video/rtp_video_stream_receiver2.cc:780-795, 1046-1076).
SM_TEST(RtpReceiver, AFrameWithoutTheExtensionGetsTheInterpolatedCaptureTime) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpSenderConfig sc = SenderCfg();
  sc.extensions = &map;
  RtpSender s(sc);
  RtpReceiverConfig rc = ReceiverCfg();
  rc.extensions = &map;
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  const uint64_t first_ntp = sync::Int64MsToUQ32x32(3'900'000'000'000);
  for (int64_t i = 0; i < 4; ++i) {
    const Timestamp at = Timestamp::Millis(i * 33);
    auto chunks = test::MakeTestFrameChunks(i == 0, i == 0 ? 3000 : 800, s.config().max_payload_size);
    std::optional<sync::AbsoluteCaptureTime> abs;
    if (i == 0) abs = sync::AbsoluteCaptureTime{first_ntp, 0};
    s.SendVideoFrame(chunks, static_cast<uint32_t>(i * 3000), at, i == 0, video::Rotation::k0, abs);
    for (auto& p : Release(s, at)) r.OnPacket(p.packet.Serialize(), at + TimeDelta::Millis(20));
    r.Process(at + TimeDelta::Millis(20));
  }
  auto frames = Run(r, 200, 800);
  SM_ASSERT_EQ(frames.size(), 4u);
  for (size_t i = 0; i < frames.size(); ++i) {
    SM_ASSERT_TRUE(frames[i].absolute_capture_time.has_value());
    SM_EXPECT_EQ(frames[i].absolute_capture_time->capture_ntp,
                 sync::AbsoluteCaptureTimeInterpolator::Interpolate(static_cast<uint32_t>(i * 3000), 90000, 0, first_ntp));
    SM_EXPECT_TRUE(frames[i].absolute_capture_time->clock_offset == std::optional<int64_t>(0));
    // No SR yet: neither the local capture clock offset nor the NTP estimate is known.
    SM_EXPECT_FALSE(frames[i].local_capture_clock_offset.has_value());
    SM_EXPECT_EQ(frames[i].ntp_time_ms, -1);
  }
  SM_EXPECT_EQ(r.stats().abs_capture_time_received, 1u);
  SM_EXPECT_GE(r.stats().abs_capture_time_interpolated, 3u);
}

namespace {
// A sender whose clock is 5 s ahead of ours sends an SR at our time `t`; it arrives 50 ms later (RTT 100 ms).
timing::SenderReport RemoteSr(uint32_t ssrc, int64_t t_ms, int clock_rate) {
  timing::SenderReport sr;
  sr.ssrc = ssrc;
  sr.ntp = timing::NtpTime::FromTimestamp(Timestamp::Millis(t_ms + 5000));
  sr.rtp_timestamp = static_cast<uint32_t>(t_ms * (clock_rate / 1000));
  return sr;
}
}  // namespace

// RemoteNtpTimeEstimator from SRs (rtp_video_stream_receiver2.cc:1533-1569): once 3 SRs are in, the
// remote-to-local clock offset (-5 s) goes to CaptureClockOffsetUpdater, and a frame's ntp_time_ms is its
// capture time on our NTP clock. An SR older than 1 ms when the RTCP packet is handled is not used.
SM_TEST(RtpReceiver, SenderReportsTrainTheRemoteNtpEstimateAndTheCaptureClockOffset) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpSenderConfig sc = SenderCfg();
  sc.extensions = &map;
  RtpSender s(sc);
  RtpReceiverConfig rc = ReceiverCfg();
  rc.extensions = &map;
  RtpReceiver r(rc);
  r.Start(Timestamp::Millis(0));
  // Without the module's RTT nothing is used; CallStats' RTT (OnRtt) is not it.
  r.OnRtt(TimeDelta::Millis(100));
  r.OnSenderReport(RemoteSr(kSsrc, 0, 90000), Timestamp::Millis(50));
  r.OnRtcpPacket(Timestamp::Millis(50));
  SM_EXPECT_EQ(r.stats().remote_ntp_updates, 0u);
  r.OnPeriodicRtt(TimeDelta::Millis(100));
  for (int64_t t : {1000, 2000, 3000}) {
    r.OnSenderReport(RemoteSr(kSsrc, t, 90000), Timestamp::Millis(t + 50));
    r.OnRtcpPacket(Timestamp::Millis(t + 50));
  }
  SM_EXPECT_EQ(r.stats().remote_ntp_updates, 3u);
  // A stale SR: the RTCP packet that brought it was handled 2 ms later.
  r.OnSenderReport(RemoteSr(kSsrc, 4000, 90000), Timestamp::Millis(4050));
  r.OnRtcpPacket(Timestamp::Millis(4052));
  SM_EXPECT_EQ(r.stats().remote_ntp_updates, 3u);

  const int64_t t0 = 5000;
  auto chunks = test::MakeTestFrameChunks(true, 3000, s.config().max_payload_size);
  s.SendVideoFrame(chunks, static_cast<uint32_t>(t0 * 90), Timestamp::Millis(t0), true, video::Rotation::k0,
                   sync::AbsoluteCaptureTime{sync::Int64MsToUQ32x32(123'000), 0});
  for (auto& p : Release(s, Timestamp::Millis(t0))) r.OnPacket(p.packet.Serialize(), Timestamp::Millis(t0 + 20));
  auto frames = Run(r, t0 + 20, t0 + 600);
  SM_ASSERT_EQ(frames.size(), 1u);
  SM_ASSERT_TRUE(frames[0].local_capture_clock_offset.has_value());
  SM_EXPECT_EQ(frames[0].local_capture_clock_offset->ms(), -5000);
  const int64_t expected = sync::NtpToMs(timing::NtpTime::FromTimestamp(Timestamp::Millis(t0)).value);
  SM_EXPECT_NEAR(static_cast<double>(frames[0].ntp_time_ms), static_cast<double>(expected), 1.0);
  SM_EXPECT_EQ(r.EstimateNtpTimeMs(static_cast<uint32_t>(t0 * 90)), frames[0].ntp_time_ms);
}

// Audio (channel_receive.cc:747-754, 818-847): every packet is interpolated at the stream's clock rate,
// and an RTCP packet without a new SR gives the last SR again (no recency check).
SM_TEST(RtpReceiver, AudioInterpolatesEachPacketAndReusesTheLastSenderReport) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpReceiverConfig rc;
  rc.ssrc = kSsrc;
  rc.payload_type = 111;
  rc.clock_rate = 48000;
  rc.video = false;
  rc.extensions = &map;
  RtpReceiver r(rc);
  r.OnReportBlockRtt(TimeDelta::Millis(100));
  // An SR arrives 2 ms before the packet is handled: audio still uses it.
  r.OnSenderReport(RemoteSr(kSsrc, 0, 48000), Timestamp::Millis(50));
  r.OnRtcpPacket(Timestamp::Millis(52));
  SM_EXPECT_EQ(r.stats().remote_ntp_updates, 1u);
  const uint64_t first_ntp = sync::Int64MsToUQ32x32(77'000);
  for (uint32_t i = 0; i < 3; ++i) {
    RtpPacket p;
    p.ssrc = kSsrc;
    p.payload_type = 111;
    p.seq = static_cast<uint16_t>(100 + i);
    p.timestamp = 960 * i;
    p.payload = {1, 2, 3};
    if (i == 0) {
      sync::ExtensionAccessor(map).SetRaw(p, sync::uri::kAbsCaptureTime,
                                          sync::SerializeAbsoluteCaptureTime({first_ntp, std::nullopt}));
    }
    r.OnPacket(p.Serialize(), Timestamp::Millis(100 + 20 * i));
  }
  const auto& last = r.last_audio_capture_time();
  SM_ASSERT_TRUE(last.has_value());
  SM_EXPECT_EQ(last->rtp_timestamp, 1920u);
  SM_EXPECT_EQ(last->absolute_capture_time.capture_ntp,
               sync::AbsoluteCaptureTimeInterpolator::Interpolate(1920, 48000, 0, first_ntp));
  SM_EXPECT_FALSE(last->local_capture_clock_offset.has_value());  // The extension has no offset.
  SM_EXPECT_EQ(r.stats().abs_capture_time_received, 1u);
  SM_EXPECT_EQ(r.stats().abs_capture_time_interpolated, 2u);
}

// ModuleRtpRtcpImpl2::LastRtt (rtp_rtcp_impl2.cc:482-491): the last report-block RTT once there is one,
// else the periodic update's RTT when it is over 0 ms (rtt_ms_ > 0).
SM_TEST(RtpReceiver, LastRttIsTheReportBlockRttElseThePeriodicOne) {
  RtpReceiver r(ReceiverCfg());
  SM_EXPECT_FALSE(r.LastRtt().has_value());
  r.OnRtt(TimeDelta::Millis(70));  // CallStats' RTT: not the module's.
  SM_EXPECT_FALSE(r.LastRtt().has_value());
  // 0 ms after set_rtt_ms(rtt->ms()); ms() rounds to the nearest ms as libwebrtc's unit_base.h (#686), so 0.4 ms.
  r.OnPeriodicRtt(TimeDelta::Micros(400));
  SM_EXPECT_FALSE(r.LastRtt().has_value());
  r.OnPeriodicRtt(TimeDelta::Millis(80));
  SM_ASSERT_TRUE(r.LastRtt().has_value());
  SM_EXPECT_EQ(r.LastRtt()->ms(), 80);
  r.OnReportBlockRtt(TimeDelta::Millis(30));
  r.OnPeriodicRtt(TimeDelta::Millis(90));
  SM_EXPECT_EQ(r.LastRtt()->ms(), 30);
}

// ChannelReceive::GetAudioFrameWithInfo (channel_receive.cc:472-507) with NetEq's
// last_decoded_packet_infos_ (neteq_impl.cc:674, 1352-1354): each GetAudio gives the packet infos of the
// packets it decoded, in order, and each capture time gets the local capture clock offset known at
// playout, not at arrival.
SM_TEST(RtpReceiver, DecodedAudioCarriesThePacketInfosOfItsPackets) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  FakeDecoder dec;
  RtpReceiverConfig c;
  c.ssrc = kSsrc;
  c.payload_type = 111;
  c.clock_rate = 8000;
  c.video = false;
  c.decoder = &dec;
  c.extensions = &map;
  RtpReceiver r(c);
  const uint64_t first_ntp = sync::Int64MsToUQ32x32(77'000);
  for (uint16_t i = 0; i < 5; ++i) {
    RtpPacket p;
    p.ssrc = kSsrc;
    p.payload_type = 111;
    p.seq = i;
    p.timestamp = static_cast<uint32_t>(i) * 160;
    p.payload = Bytes(40, static_cast<uint8_t>(10 + i));
    if (i == 0) {
      sync::ExtensionAccessor(map).SetRaw(p, sync::uri::kAbsCaptureTime,
                                          sync::SerializeAbsoluteCaptureTime({first_ntp, int64_t{0}}));
    }
    r.OnPacket(p.Serialize(), Timestamp::Millis(i * 20 + 5));
  }
  // The packets arrived before any SR: no offset then.
  SM_ASSERT_TRUE(r.last_audio_capture_time().has_value());
  SM_EXPECT_FALSE(r.last_audio_capture_time()->local_capture_clock_offset.has_value());
  // The sender's clock is 5 s ahead; 3 SRs train the remote-to-local offset before playout.
  r.OnReportBlockRtt(TimeDelta::Millis(100));
  for (int64_t t : {1000, 2000, 3000}) {
    r.OnSenderReport(RemoteSr(kSsrc, t, 8000), Timestamp::Millis(t + 50));
    r.OnRtcpPacket(Timestamp::Millis(t + 50));
  }
  std::vector<AudioPacketInfo> infos;
  std::vector<float> out(80);
  for (int64_t t = 3100; t < 3300; t += 10) {
    r.GetAudio(out, Timestamp::Millis(t));
    for (const auto& i : r.audio_packet_infos()) infos.push_back(i);
  }
  SM_ASSERT_EQ(infos.size(), 5u);
  for (size_t i = 0; i < infos.size(); ++i) {
    SM_EXPECT_EQ(infos[i].ssrc, kSsrc);
    SM_EXPECT_EQ(infos[i].rtp_timestamp, static_cast<uint32_t>(i * 160));
    SM_EXPECT_EQ(infos[i].receive_time.ms(), static_cast<int64_t>(i * 20 + 5));
    SM_ASSERT_TRUE(infos[i].absolute_capture_time.has_value());
    SM_EXPECT_EQ(infos[i].absolute_capture_time->capture_ntp,
                 sync::AbsoluteCaptureTimeInterpolator::Interpolate(static_cast<uint32_t>(i * 160), 8000, 0, first_ntp));
    SM_ASSERT_TRUE(infos[i].local_capture_clock_offset.has_value());
    SM_EXPECT_EQ(infos[i].local_capture_clock_offset->ms(), -5000);
  }
  // A GetAudio that decodes nothing has no packet infos.
  r.GetAudio(out, Timestamp::Millis(3300));
  SM_EXPECT_TRUE(r.audio_packet_infos().empty());
}

// RtpVideoStreamReceiver2::UnwrapSequenceNumberOrRecover (video/rtp_video_stream_receiver2.cc:651-753,
// issue #818): a sender that restarts its sequence numbers more than 2048 behind, with newer RTP
// timestamps, is a discontinuity. Its packets are dropped and a keyframe is requested at most every
// max_wait_for_keyframe (200 ms) until a keyframe starts; there the receiver re-anchors, and NACKs go
// out on the restarted sender's sequence numbers. libwebrtc runs this for codecs other than H.264 and
// H.265 (rtp::Session's switch); the H.264 test payload stands in for one here.
SM_TEST(RtpReceiver, ASequenceNumberDiscontinuityRequestsKeyframesUntilANewerKeyframeReanchors) {
  RtpReceiverConfig cfg = NoHoldCfg();
  cfg.seq_num_discontinuity = true;
  RtpSender s(SenderCfg());
  RtpReceiver r(cfg);
  for (auto& b : SendFrame(s, 0, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
  for (auto& b : SendFrame(s, 1, false, 800, Timestamp::Millis(33))) r.OnPacket(b, Timestamp::Millis(43));
  SM_ASSERT_EQ(Run(r, 10, 300).size(), 2u);
  SM_ASSERT_TRUE(r.TakeFeedback().keyframe.empty());

  RtpSenderConfig restarted = SenderCfg();
  restarted.initial_seq = static_cast<uint16_t>(65530 - 5000);
  RtpSender s2(restarted);
  const auto count_requests = [&](int64_t t) {
    r.Process(Timestamp::Millis(t));
    return r.TakeFeedback().keyframe.size();
  };
  for (auto& b : SendFrame(s2, 10, false, 800, Timestamp::Millis(300))) r.OnPacket(b, Timestamp::Millis(310));
  SM_EXPECT_EQ(r.stats().seq_num_discontinuities, 1u);
  SM_EXPECT_GT(r.stats().seq_num_discontinuity_dropped, 0u);
  SM_EXPECT_EQ(count_requests(310), 1u);
  // Within max_wait_for_keyframe of the request: dropped, no new request.
  for (auto& b : SendFrame(s2, 11, false, 800, Timestamp::Millis(400))) r.OnPacket(b, Timestamp::Millis(410));
  SM_EXPECT_EQ(count_requests(410), 0u);
  // After it: requested again.
  for (auto& b : SendFrame(s2, 12, false, 800, Timestamp::Millis(520))) r.OnPacket(b, Timestamp::Millis(520));
  SM_EXPECT_EQ(count_requests(520), 1u);
  SM_EXPECT_EQ(r.stats().seq_num_reanchors, 0u);
  SM_EXPECT_EQ(r.stats().frames_received, 2u);

  // The keyframe re-anchors, and frames after it are decoded.
  for (auto& b : SendFrame(s2, 13, true, 800, Timestamp::Millis(600))) r.OnPacket(b, Timestamp::Millis(610));
  SM_EXPECT_EQ(r.stats().seq_num_reanchors, 1u);
  SM_EXPECT_EQ(r.stats().seq_num_discontinuities, 1u);
  // A lost packet after the re-anchor is NACKed with the restarted sender's sequence number, and its RTX
  // copy completes the frame.
  auto f14 = SendFrame(s2, 14, false, 3000, Timestamp::Millis(633));
  SM_ASSERT_GE(f14.size(), 3u);
  auto lost = RtpPacket::Parse(f14[1]);
  SM_ASSERT_TRUE(lost.has_value());
  for (size_t i = 0; i < f14.size(); ++i)
    if (i != 1) r.OnPacket(f14[i], Timestamp::Millis(643));
  r.Process(Timestamp::Millis(643));
  ReceiverFeedback fb = r.TakeFeedback();
  SM_ASSERT_EQ(fb.nacks.size(), 1u);
  SM_EXPECT_EQ(fb.nacks[0], lost->seq);
  SM_EXPECT_TRUE(fb.keyframe.empty());
  s2.OnNack(fb.nacks, Timestamp::Millis(650), TimeDelta::Millis(40));
  auto rtx = s2.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  r.OnPacket(rtx[0].packet.Serialize(), Timestamp::Millis(660));
  auto frames = Run(r, 610, 1500);
  SM_ASSERT_EQ(frames.size(), 2u);
  SM_EXPECT_TRUE(frames[0].keyframe && frames[0].render);
  SM_EXPECT_EQ(frames[0].rtp_timestamp, 13u * 3000u);
  SM_EXPECT_EQ(frames[1].rtp_timestamp, 14u * 3000u);
  SM_EXPECT_TRUE(frames[1].frame_id > frames[0].frame_id);
  SM_EXPECT_EQ(r.stats().gaps, 1u);  // The lost packet; the restart is no gap.
  SM_EXPECT_TRUE(r.TakeFeedback().keyframe.empty());
}

// Outside a discontinuity, a packet more than 2048 behind the newest media packet with an older RTP timestamp
// (a severely reordered old one) is dropped and starts nothing. With the switch off (H.264, the receiver's
// default) the restarted sender's packets go to the packet buffer.
SM_TEST(RtpReceiver, AnOldPacketFarBehindIsDroppedAndH264HasNoDiscontinuityRule) {
  for (bool on : {true, false}) {
    RtpReceiverConfig cfg = NoHoldCfg();
    cfg.seq_num_discontinuity = on;
    RtpSender s(SenderCfg());
    RtpReceiver r(cfg);
    for (auto& b : SendFrame(s, 100, true, 800, Timestamp::Millis(0))) r.OnPacket(b, Timestamp::Millis(10));
    SM_ASSERT_EQ(Run(r, 10, 300).size(), 1u);
    RtpSenderConfig old = SenderCfg();
    old.initial_seq = static_cast<uint16_t>(65530 - 5000);
    RtpSender s2(old);
    for (auto& b : SendFrame(s2, 1, false, 800, Timestamp::Millis(300))) r.OnPacket(b, Timestamp::Millis(310));
    SM_EXPECT_EQ(r.stats().seq_num_discontinuities, 0u);
    SM_EXPECT_EQ(r.stats().seq_num_discontinuity_dropped > 0, on);
    for (auto& b : SendFrame(s2, 101, false, 800, Timestamp::Millis(310))) r.OnPacket(b, Timestamp::Millis(320));
    SM_EXPECT_EQ(r.stats().seq_num_discontinuities, on ? 1u : 0u);
  }
}

// Issue #899: a copy of a one-packet frame's packet that arrives after the
// frame was assembled (a spurious RTX, a duplicating link) is taken again by
// the packet buffer, as libwebrtc's empty slot takes it, and the frame is
// assembled a second time. With `timing v2` the frame buffer drops the repeat
// (FrameBuffer::InsertFrame: already inserted), so every frame is counted
// (ReceiveStatisticsProxy::OnCompleteFrame) and rendered once.
SM_TEST(RtpReceiver, ACopyAfterAssemblyIsAssembledAgainAndTheFrameBufferDropsIt) {
  RtpSender s(SenderCfg());
  RtpReceiver r(NoHoldCfg());
  r.Start(Timestamp::Millis(0));
  for (int64_t i = 0; i < 3; ++i) {
    Timestamp at = Timestamp::Millis(i * 33);
    auto pkts = SendFrame(s, i, i == 0, 300, at);
    SM_ASSERT_EQ(pkts.size(), 1u);
    r.OnPacket(pkts[0], at + TimeDelta::Millis(20));
    r.Process(at + TimeDelta::Millis(20));
    r.OnPacket(pkts[0], at + TimeDelta::Millis(21));  // The copy, after the frame was assembled.
    r.Process(at + TimeDelta::Millis(21));
  }
  auto frames = Run(r, 100, 800);
  // A copy is taken again while its frame waits in the frame buffer; one that lands after its frame
  // was decoded is behind ClearTo and old (RtpVideoStreamReceiver2::FrameDecoded).
  const auto& pb = r.packet_buffer().stats();
  SM_EXPECT_EQ(pb.reinserted + pb.too_old, 3u);
  SM_EXPECT_GT(pb.reinserted, 0u);
  SM_EXPECT_EQ(pb.frames_assembled, 3u + pb.reinserted);
  SM_EXPECT_EQ(r.stats().frames_refused, pb.reinserted);
  SM_EXPECT_EQ(r.stats().frames_received, 3u);
  SM_ASSERT_EQ(frames.size(), 3u);
  for (size_t i = 0; i + 1 < frames.size(); ++i) SM_EXPECT_TRUE(frames[i].frame_id < frames[i + 1].frame_id);
}

// `timing v1` has no frame buffer behind the packet buffer, so the packet
// buffer keeps rejecting a copy of a packet already handed out (a recorded
// deviation of the non-default path, issue #899).
SM_TEST(RtpReceiver, TimingV1DropsACopyAfterAssemblyInThePacketBuffer) {
  RtpSender s(SenderCfg());
  RtpReceiverConfig c = NoHoldCfg();
  c.timing.version = 1;
  RtpReceiver r(c);
  r.Start(Timestamp::Millis(0));
  for (int64_t i = 0; i < 3; ++i) {
    Timestamp at = Timestamp::Millis(i * 33);
    auto pkts = SendFrame(s, i, i == 0, 300, at);
    SM_ASSERT_EQ(pkts.size(), 1u);
    r.OnPacket(pkts[0], at + TimeDelta::Millis(20));
    r.Process(at + TimeDelta::Millis(20));
    r.OnPacket(pkts[0], at + TimeDelta::Millis(21));
    r.Process(at + TimeDelta::Millis(21));
  }
  auto frames = Run(r, 100, 800);
  SM_EXPECT_EQ(r.packet_buffer().stats().duplicates, 3u);
  SM_EXPECT_EQ(r.packet_buffer().stats().reinserted, 0u);
  SM_EXPECT_EQ(r.stats().frames_received, 3u);
  SM_EXPECT_EQ(frames.size(), 3u);
}
