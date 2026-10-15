#include <algorithm>
#include <set>
#include <vector>

#include "sm/loss/red.h"
#include "sm/loss/rtx.h"
#include "sm/rtp/rtp_sender.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {
RtpSenderConfig VideoCfg() {
  RtpSenderConfig c;
  c.ssrc = 0x1111;
  c.payload_type = 96;
  c.rtx_ssrc = 0x2222;
  c.rtx_payload_type = 97;
  c.initial_seq = 65534;  // Wraps on the third packet.
  return c;
}
std::vector<Bytes> Chunks(std::initializer_list<size_t> sizes) {
  std::vector<Bytes> out;
  uint8_t v = 1;
  for (size_t s : sizes) out.emplace_back(s, v++);
  return out;
}
}  // namespace

SM_TEST(RtpSender, SplitEvenlyKeepsChunksEqualAndBounded) {
  for (size_t bytes : {size_t{1}, size_t{1188}, size_t{1189}, size_t{5000}, size_t{0}}) {
    auto parts = SplitEvenly(bytes, 1188);
    size_t sum = 0;
    for (size_t p : parts) {
      SM_EXPECT_LE(p, 1188u);
      sum += p;
    }
    SM_EXPECT_EQ(sum, bytes);
    SM_EXPECT_LE(*std::max_element(parts.begin(), parts.end()) - *std::min_element(parts.begin(), parts.end()), 1u);
  }
  SM_EXPECT_EQ(SplitEvenly(2400, 1188).size(), 3u);
}

SM_TEST(RtpSender, VideoFrameNumbersPacketsAndMarksTheLastOne) {
  RtpSender s(VideoCfg());
  auto chunks = Chunks({100, 200, 50});
  s.SendVideoFrame(chunks, 9000, Timestamp::Millis(10));
  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 3u);
  uint16_t expect_seq[] = {65534, 65535, 0};
  for (size_t i = 0; i < 3; ++i) {
    SM_EXPECT_EQ(out[i].packet.seq, expect_seq[i]);
    SM_EXPECT_EQ(out[i].packet.ssrc, 0x1111u);
    SM_EXPECT_EQ(out[i].packet.payload_type, 96);
    SM_EXPECT_EQ(out[i].packet.timestamp, 9000u);
    SM_EXPECT_EQ(out[i].packet.marker, i == 2);
    SM_EXPECT_TRUE(out[i].kind == PacketKind::kVideo);
    SM_EXPECT_TRUE(out[i].priority == cc::PacketPriority::kVideo);
    SM_EXPECT_TRUE(out[i].packet.payload == chunks[i]);
  }
  SM_EXPECT_TRUE(s.TakePackets().empty());
  RtcpSenderState st = s.SenderState();
  SM_EXPECT_EQ(st.ssrc, 0x1111u);
  SM_EXPECT_EQ(st.packet_count, 3u);
  SM_EXPECT_EQ(st.octet_count, 350u);
  SM_EXPECT_EQ(st.rtp_timestamp, 9000u);
  SM_EXPECT_EQ(st.clock_rate, 90000);
  s.SendVideoFrame({}, 9003, Timestamp::Millis(11));  // An empty frame sends nothing.
  SM_EXPECT_TRUE(s.TakePackets().empty());
}

SM_TEST(RtpSender, NackIsAnsweredWithRtxAtRetransmissionPriority) {
  RtpSender s(VideoCfg());
  s.SendVideoFrame(Chunks({10, 20, 30}), 1, Timestamp::Millis(0));
  s.TakePackets();
  uint16_t want[] = {65535, 0, 77};
  s.OnNack(want, Timestamp::Millis(50), TimeDelta::Millis(20));
  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 2u);  // 77 was never sent.
  for (auto& o : out) {
    SM_EXPECT_TRUE(o.kind == PacketKind::kRtx);
    SM_EXPECT_TRUE(o.priority == cc::PacketPriority::kRetransmission);
    SM_EXPECT_EQ(o.packet.ssrc, 0x2222u);
    SM_EXPECT_EQ(o.packet.payload_type, 97);
  }
  auto restored = loss::RestoreFromRtx(out[0].packet, 0x1111, 96);
  SM_ASSERT_TRUE(restored.has_value());
  SM_EXPECT_EQ(restored->seq, 65535);
  SM_EXPECT_EQ(restored->payload.size(), 20u);
  SM_EXPECT_EQ(restored->timestamp, 1u);
  SM_EXPECT_EQ(s.stats().rtx_packets, 2u);
  // The same NACK again inside the RTT is throttled by the history.
  s.OnNack(want, Timestamp::Millis(55), TimeDelta::Millis(20));
  SM_EXPECT_TRUE(s.TakePackets().empty());
}

SM_TEST(RtpSender, NackWithoutRtxIsCountedNotAnswered) {
  RtpSenderConfig c = VideoCfg();
  c.rtx_ssrc.reset();
  RtpSender s(c);
  s.SendVideoFrame(Chunks({10}), 1, Timestamp::Millis(0));
  s.TakePackets();
  uint16_t want[] = {65534};
  s.OnNack(want, Timestamp::Millis(50), TimeDelta::Millis(20));
  SM_EXPECT_TRUE(s.TakePackets().empty());
  SM_EXPECT_EQ(s.stats().nacks_without_rtx, 1u);
}

SM_TEST(RtpSender, AudioRedCarriesThePreviousFrame) {
  RtpSenderConfig c;
  c.ssrc = 5;
  c.payload_type = 111;
  c.video = false;
  c.clock_rate = 48000;
  c.red_payload_type = 63;
  RtpSender s(c);
  Bytes f1(30, 0xA1), f2(30, 0xB2);
  s.SendAudio(f1, 960, false, Timestamp::Millis(0));
  s.SendAudio(f2, 1920, true, Timestamp::Millis(20));
  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 2u);
  SM_EXPECT_TRUE(out[1].kind == PacketKind::kAudio);
  SM_EXPECT_TRUE(out[1].priority == cc::PacketPriority::kAudio);
  SM_EXPECT_EQ(out[1].packet.payload_type, 63);
  SM_EXPECT_TRUE(out[1].packet.marker);
  auto blocks = loss::ParseRed(out[1].packet.payload, out[1].packet.timestamp);
  SM_ASSERT_TRUE(blocks.has_value());
  SM_ASSERT_EQ(blocks->size(), 2u);
  SM_EXPECT_EQ((*blocks)[0].payload_type, 111);
  SM_EXPECT_TRUE((*blocks)[0].payload == f1);
  SM_EXPECT_TRUE((*blocks)[1].primary);
  SM_EXPECT_TRUE((*blocks)[1].payload == f2);
  SM_EXPECT_EQ(s.SenderState().clock_rate, 48000);
}

SM_TEST(RtpSender, AudioWithoutRedIsOnePlainPacket) {
  RtpSenderConfig c;
  c.video = false;
  c.payload_type = 111;
  RtpSender s(c);
  Bytes f(40, 7);
  s.SendAudio(f, 960, false, Timestamp::Millis(0));
  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.payload_type, 111);
  SM_EXPECT_TRUE(out[0].packet.payload == f);
}

SM_TEST(RtpSender, FecPacketsComeBeforeTheLastMediaPacketOfTheFrame) {
  RtpSenderConfig c = VideoCfg();
  c.fec = true;
  c.fec_config.fec_ssrc = 0x3333;
  RtpSender s(c);
  s.SetFecParams({0.5, loss::FecMaskType::kInterleaved});
  s.SendVideoFrame(Chunks({100, 100, 100, 100}), 1, Timestamp::Millis(0));
  auto out = s.TakePackets();
  size_t media = 0, fec = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i].kind == PacketKind::kFec) {
      ++fec;
      SM_EXPECT_TRUE(out[i].priority == cc::PacketPriority::kFec);
      SM_EXPECT_EQ(out[i].packet.ssrc, 0x3333u);
      SM_EXPECT_EQ(media, 4u - 1);  // Parity is queued with the frame's last packet, ahead of it.
    } else {
      ++media;
    }
  }
  SM_EXPECT_EQ(media, 4u);
  SM_EXPECT_EQ(fec, 2u);
  SM_EXPECT_EQ(s.stats().fec_packets, 2u);
  // FEC packets are not in the sender report counts.
  SM_EXPECT_EQ(s.SenderState().packet_count, 4u);
}

SM_TEST(RtpSender, MidIsWrittenOnTheFirstPacketsUntilAcknowledged) {
  sync::ExtensionMap map;
  map.Register(4, sync::uri::kMid);
  RtpSenderConfig c = VideoCfg();
  c.mid = "v0";
  c.mid_packets = 2;
  c.extensions = &map;
  RtpSender s(c);
  sync::ExtensionAccessor access(map);
  s.SendVideoFrame(Chunks({10, 10, 10}), 1, Timestamp::Millis(0));
  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 3u);
  SM_EXPECT_TRUE(access.GetString(out[0].packet, sync::uri::kMid) == std::optional<std::string>("v0"));
  SM_EXPECT_TRUE(access.GetString(out[1].packet, sync::uri::kMid) == std::optional<std::string>("v0"));
  SM_EXPECT_FALSE(access.GetString(out[2].packet, sync::uri::kMid).has_value());
  RtpSender s2(c);
  s2.AcknowledgeMid();
  s2.SendVideoFrame(Chunks({10}), 1, Timestamp::Millis(0));
  SM_EXPECT_FALSE(access.GetString(s2.TakePackets()[0].packet, sync::uri::kMid).has_value());
  SM_EXPECT_EQ(s.stats().mid_packets, 2u);
}

SM_TEST(RtpSender, PaddingUsesTheRtxStreamAndKeepsSequenceNumbersApart) {
  RtpSender s(VideoCfg());
  s.SendVideoFrame(Chunks({10}), 1, Timestamp::Millis(0));
  s.TakePackets();
  SenderPacket pad = s.MakePadding(224 - 28, 77, Timestamp::Millis(1));
  SM_EXPECT_TRUE(pad.kind == PacketKind::kPadding);
  SM_EXPECT_TRUE(pad.priority == cc::PacketPriority::kPadding);
  SM_EXPECT_EQ(pad.packet.ssrc, 0x2222u);
  SM_EXPECT_EQ(pad.packet.payload_type, 97);
  SM_EXPECT_TRUE(pad.packet.payload.empty());
  SM_EXPECT_EQ(pad.packet.size(), 196u);
  // A resend after the padding takes the next RTX sequence number.
  uint16_t want[] = {65534};
  s.OnNack(want, Timestamp::Millis(50), TimeDelta::Millis(10));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_EQ(static_cast<uint16_t>(rtx[0].packet.seq - pad.packet.seq), 1u);
  // Sizes are clamped to what padding can express.
  SM_EXPECT_EQ(s.MakePadding(5000, 0, Timestamp::Millis(2)).packet.padding_size, 255);
  SM_EXPECT_EQ(s.MakePadding(0, 0, Timestamp::Millis(2)).packet.padding_size, 1);
  // Without RTX, padding rides the media SSRC and sequence space.
  RtpSenderConfig c = VideoCfg();
  c.rtx_ssrc.reset();
  RtpSender m(c);
  SenderPacket mp = m.MakePadding(100, 0, Timestamp::Millis(0));
  SM_EXPECT_EQ(mp.packet.ssrc, 0x1111u);
  SM_EXPECT_EQ(mp.packet.seq, 65534);
  m.SendVideoFrame(Chunks({5}), 1, Timestamp::Millis(1));
  SM_EXPECT_EQ(m.TakePackets()[0].packet.seq, 65535);
}
