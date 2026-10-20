// Ports the intent of libwebrtc's modules/pacing/packet_router_unittest.cc (issue #166). The
// PacketRouter's work is split here: rtp::SendSideController::Process numbers each released packet
// (transport-wide sequence number) and reports it to GCC, rtp::RtpSender assigns the RTP sequence
// number at release (`rtx v2`) and makes the padding, and rtp::Session picks the stream that pads.
// The cases run with `pacer v2` (and `rtx v2` where the case needs it), the switches that follow
// libwebrtc. Cases not ported, with the reason, are listed in tools/catalog.py (5.5).
#include <optional>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/rtp/session.h"
#include "sm/rtp/send_side_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
SendSideConfig V2() {
  SendSideConfig cfg;
  cfg.pacer.version = cc::PacerVersion::kV2;
  return cfg;
}

SenderPacket Media(uint16_t seq, PacketKind kind = PacketKind::kVideo, cc::PacketPriority prio = cc::PacketPriority::kVideo) {
  SenderPacket p;
  p.packet.ssrc = 1;
  p.packet.payload_type = 96;
  p.packet.seq = seq;
  p.packet.payload.assign(100, 0);
  p.kind = kind;
  p.priority = prio;
  return p;
}

std::vector<ReleasedPacket> Run(SendSideController& c, int from_ms, int to_ms) {
  std::vector<ReleasedPacket> all;
  for (int t = from_ms; t < to_ms; ++t) {
    auto r = c.Process(Timestamp::Millis(t));
    all.insert(all.end(), r.begin(), r.end());
  }
  return all;
}

const test::TestVideoParser kParser;

SessionConfig SessionCfg() {
  SessionConfig c;
  c.local_ssrc = 1;
  c.cname = "test";
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.send_side.pacer.version = cc::PacerVersion::kV2;
  c.rtx_version = 2;
  return c;
}

AudioStreamConfig Audio() {
  AudioStreamConfig a;
  a.ssrc = 0x100;
  return a;
}

VideoStreamConfig Video(bool rtx) {
  VideoStreamConfig v;
  v.ssrc = 0x200;
  if (rtx) v.rtx_ssrc = 0x300;
  v.parser = &kParser;
  return v;
}

// SSRCs of the padding-only packets `s` sends in its first second, after one frame of each stream
// at the start: the start-up probes go out as padding once the frame has left.
std::vector<uint32_t> PaddingSsrcs(Session& s, std::optional<AudioSendId> audio, std::optional<VideoSendId> video) {
  const std::vector<uint8_t> opus(300, 7);
  if (audio) s.SendAudio(*audio, {960, opus, false}, Timestamp::Millis(0));
  if (video) s.SendVideoChunks(*video, test::MakeTestFrameChunks(true, 3000, 1188), 0, Timestamp::Millis(0));
  std::vector<uint32_t> out;
  for (int64_t t = 0; t < 1000; ++t) {
    s.Process(Timestamp::Millis(t));
    for (auto& p : s.TakeOutgoing()) {
      if (p.kind != OutgoingKind::kPadding) continue;
      auto parsed = RtpPacket::Parse(p.bytes);
      if (parsed) out.push_back(parsed->ssrc);
    }
  }
  return out;
}

bool AllEqual(const std::vector<uint32_t>& v, uint32_t ssrc) {
  for (uint32_t s : v)
    if (s != ssrc) return false;
  return true;
}
}  // namespace

// Sanity_NoModuleRegistered_GeneratePadding: no stream to pad on, no padding.
SM_TEST(PacketRouter, NoPaddingSenderSendsNoPadding) {
  SendSideController c(V2());
  c.Start(Timestamp::Millis(0));
  c.pacer().SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(200));
  c.Enqueue(Media(1), Timestamp::Millis(0));
  auto all = Run(c, 0, 500);
  SM_ASSERT_EQ(all.size(), 1u);
  SM_EXPECT_TRUE(all[0].kind == PacketKind::kVideo);
  SM_EXPECT_EQ(c.stats().padding, 0u);
}

// GeneratePaddingPrioritizesRtx, SupportsRtxPayloadPaddingTrueIfRtxSendModule: a video stream with
// RTX (and `rtx v2`) pads with RTX payload padding, the history resent on the RTX SSRC.
SM_TEST(PacketRouter, PaddingIsRtxPayloadOnTheRtxSsrc) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);  // SupportsRtxPayloadPadding needs a BWE extension.
  SendSideController c(V2(), &map);
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  sc.rtx.version = 2;
  sc.extensions = &map;
  RtpSender sender(sc);
  c.SetPaddingSender(&sender);
  c.Start(Timestamp::Millis(0));
  // One packet large enough to start the start-up probes, which then go out as padding.
  const std::vector<uint8_t> chunk(300, 1);
  sender.SendVideoFrame(std::span<const std::vector<uint8_t>>(&chunk, 1), 9000, Timestamp::Millis(0));
  for (auto& p : sender.TakePackets()) c.Enqueue(std::move(p), Timestamp::Millis(0));
  size_t padding = 0;
  for (auto& r : Run(c, 0, 1000)) {
    if (r.kind != PacketKind::kPadding) continue;
    auto p = RtpPacket::Parse(r.bytes);
    SM_ASSERT_TRUE(p.has_value());
    SM_EXPECT_EQ(p->ssrc, 0x20u);
    ++padding;
  }
  SM_EXPECT_GT(padding, 0u);
  SM_EXPECT_GT(sender.stats().payload_padding_packets, 0u);
}

// GeneratePaddingPrioritizesVideo: with only an audio stream, padding goes on the audio SSRC; once a
// video stream is added it pads, whatever the order the streams were added in.
SM_TEST(PacketRouter, PaddingPrefersTheVideoStream) {
  {
    Session s(SessionCfg());
    auto a = s.AddAudioSend(Audio());
    auto ssrcs = PaddingSsrcs(s, a, std::nullopt);
    SM_EXPECT_GT(ssrcs.size(), 0u);
    SM_EXPECT_TRUE(AllEqual(ssrcs, 0x100));
  }
  {
    Session s(SessionCfg());
    auto a = s.AddAudioSend(Audio());
    auto v = s.AddVideoSend(Video(true));
    auto ssrcs = PaddingSsrcs(s, a, v);
    SM_EXPECT_GT(ssrcs.size(), 0u);
    SM_EXPECT_TRUE(AllEqual(ssrcs, 0x300));
  }
  {
    Session s(SessionCfg());
    auto v = s.AddVideoSend(Video(true));
    auto a = s.AddAudioSend(Audio());
    auto ssrcs = PaddingSsrcs(s, a, v);
    SM_EXPECT_GT(ssrcs.size(), 0u);
    SM_EXPECT_TRUE(AllEqual(ssrcs, 0x300));
  }
}

// SupportsRtxPayloadPaddingFalseIfNoRtxSendModule: a video stream without RTX pads on its media SSRC.
SM_TEST(PacketRouter, VideoWithoutRtxPadsOnTheMediaSsrc) {
  Session s(SessionCfg());
  auto a = s.AddAudioSend(Audio());
  auto v = s.AddVideoSend(Video(false));
  auto ssrcs = PaddingSsrcs(s, a, v);
  SM_EXPECT_GT(ssrcs.size(), 0u);
  SM_EXPECT_TRUE(AllEqual(ssrcs, 0x200));
}

// AllocatesRtpSequenceNumbersIfPacketCanBeSent: with `rtx v2` the media packets carry no RTP
// sequence number until the pacer releases them, and get consecutive numbers in release order.
// (DoNotAllocatesRtpSequenceNumbersIfPacketCanNotBeSent is SendSideController.
// FlushedPacketsLeaveNoSequenceGapUnderRtxV2: a packet that is never sent takes no number.)
SM_TEST(PacketRouter, AssignsRtpSequenceNumbersAtRelease) {
  SendSideController c(V2());
  c.Start(Timestamp::Millis(0));
  RtpSenderConfig sc;
  sc.ssrc = 0x10;
  sc.rtx_ssrc = 0x20;
  sc.rtx.version = 2;
  sc.initial_seq = 100;
  RtpSender sender(sc);
  const std::vector<std::vector<uint8_t>> frame(3, std::vector<uint8_t>(500, 1));
  sender.SendVideoFrame(frame, 9000, Timestamp::Millis(0));
  auto packets = sender.TakePackets();
  SM_ASSERT_EQ(packets.size(), 3u);
  for (const auto& p : packets) SM_EXPECT_EQ(p.packet.seq, 0u);
  for (auto& p : packets) c.Enqueue(std::move(p), Timestamp::Millis(0));
  std::vector<uint16_t> seqs;
  for (auto& r : Run(c, 0, 200)) {
    auto p = RtpPacket::Parse(r.bytes);
    SM_ASSERT_TRUE(p.has_value());
    seqs.push_back(p->seq);
  }
  SM_EXPECT_TRUE(seqs == (std::vector<uint16_t>{100, 101, 102}));
}

// AllocatesTransportSequenceNumbers: transport-wide sequence numbers start at 1 ("for historical
// reasons", packet_router.cc transport_seq_(1)) and are written to the packet and reported to GCC.
SM_TEST(PacketRouter, TransportSequenceNumbersStartAtOne) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  SendSideController c(V2(), &map);
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 3; ++i) c.Enqueue(Media(i), Timestamp::Millis(0));
  auto all = Run(c, 0, 100);
  SM_ASSERT_EQ(all.size(), 3u);
  sync::ExtensionAccessor access(map);
  for (size_t i = 0; i < all.size(); ++i) {
    SM_EXPECT_EQ(all[i].transport_seq, i + 1);
    auto p = RtpPacket::Parse(all[i].bytes);
    SM_ASSERT_TRUE(p.has_value());
    SM_EXPECT_TRUE(access.GetTransportSeq(*p) == std::optional<uint16_t>(static_cast<uint16_t>(i + 1)));
  }
  SM_EXPECT_GT(c.gcc().history().InFlight().bytes(), 0);
}

// DoesNotAllocateTransportSequenceNumberWithoutExtension, SendPacketWithoutTransportSequenceNumbers:
// without the extension the packet has no transport-wide sequence number, and the bandwidth
// estimator is not told about it (NotifyBweOfPacedSentPacket returns without one).
SM_TEST(PacketRouter, NoTransportSequenceNumberWithoutTheExtension) {
  sync::ExtensionMap map;  // Nothing mapped.
  SendSideController c(V2(), &map);
  c.Start(Timestamp::Millis(0));
  for (uint16_t i = 0; i < 3; ++i) c.Enqueue(Media(i), Timestamp::Millis(0));
  auto all = Run(c, 0, 100);
  SM_ASSERT_EQ(all.size(), 3u);
  for (const auto& r : all) {
    SM_EXPECT_EQ(r.transport_seq, 0u);
    auto p = RtpPacket::Parse(r.bytes);
    SM_ASSERT_TRUE(p.has_value());
    SM_EXPECT_TRUE(p->extension.empty());
  }
  SM_EXPECT_EQ(c.gcc().history().InFlight().bytes(), 0);
  SM_EXPECT_EQ(c.stats().released, 3u);
}

// DoesNotIncrementTransportSequenceNumberOnSendFailure: a slot the router cannot send (here, a padding
// slot with no stream to pad on) does not use up a transport-wide sequence number.
SM_TEST(PacketRouter, AnUnsentSlotTakesNoTransportSequenceNumber) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  SendSideController c(V2(), &map);
  c.Start(Timestamp::Millis(0));
  c.pacer().SetPacingRates(DataRate::KilobitsPerSec(1000), DataRate::KilobitsPerSec(500));
  c.Enqueue(Media(1), Timestamp::Millis(0));
  auto first = Run(c, 0, 300);
  c.Enqueue(Media(2), Timestamp::Millis(300));
  auto second = Run(c, 300, 400);
  SM_ASSERT_EQ(first.size(), 1u);
  SM_ASSERT_EQ(second.size(), 1u);
  SM_EXPECT_EQ(first[0].transport_seq, 1u);
  SM_EXPECT_EQ(second[0].transport_seq, 2u);
}

// `pacer v1` keeps its numbering from 0 and numbers every packet, so the v1 runs are unchanged.
SM_TEST(PacketRouter, PacerV1NumbersFromZero) {
  SendSideController c;
  c.Start(Timestamp::Millis(0));
  c.Enqueue(Media(1), Timestamp::Millis(0));
  auto all = Run(c, 0, 100);
  SM_ASSERT_EQ(all.size(), 1u);
  SM_EXPECT_EQ(all[0].transport_seq, 0u);
  SM_EXPECT_GT(c.gcc().history().InFlight().bytes(), 0);
}
