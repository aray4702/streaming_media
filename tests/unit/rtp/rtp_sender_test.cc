#include <algorithm>
#include <optional>
#include <set>
#include <vector>

#include "sm/audio/audio_level.h"
#include "sm/core/audio_frame_operations.h"
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
// The queued packets as the pacer releases them: numbered and stored in the RTX history
// (SendSideController::Process).
std::vector<SenderPacket> Release(RtpSender& s, Timestamp now = Timestamp::Millis(0)) {
  auto out = s.TakePackets();
  for (auto& p : out) {
    s.AssignSequenceNumber(p, now);
    s.OnPacketReleased(p, now);
  }
  return out;
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
  auto out = Release(s);
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
  RtcpSenderState st = s.SenderState(Timestamp::Millis(0));
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
  Release(s);
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
  // The same NACK again while the resends are still queued is ignored by the history.
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
  // Every frame carries RED's payload type, which has no registered rate: the
  // sender report extrapolates at kBogusRtpRateForAudioRtcp (issue #895).
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(0)).clock_rate, 8000);
}

// RTCPSender::BuildSR reads the rate registered for the last frame's payload type
// (rtcp_sender.cc:355-360). ChannelSend::SetEncoder registers only the codec's
// (channel_send.cc:645), so audio sent as RED reports at kBogusRtpRateForAudioRtcp,
// 8 kHz, and audio without RED at the codec's 48 kHz (issue #895).
SM_TEST(RtpSender, SenderReportRateIsTheLastPayloadTypesRegisteredRate) {
  RtpSenderConfig c;
  c.ssrc = 5;
  c.payload_type = 111;
  c.video = false;
  c.clock_rate = 48000;
  RtpSender plain(c);
  plain.SendAudio(Bytes(30, 0xA1), 960, false, Timestamp::Millis(0));
  SM_EXPECT_EQ(plain.SenderState(Timestamp::Millis(0)).clock_rate, 48000);
  c.red_payload_type = 63;
  RtpSender red(c);
  red.SendAudio(Bytes(30, 0xA1), 960, false, Timestamp::Millis(0));
  SM_EXPECT_EQ(red.SenderState(Timestamp::Millis(0)).clock_rate, RtpSender::kBogusRtpRateForAudioRtcp);
  SM_EXPECT_EQ(RtpSender::kBogusRtpRateForAudioRtcp, 8000);
}

// RTPSenderAudio::MarkerBit (rtp_sender_audio.cc:88-128): the marker goes on the stream's first
// packet, and on the first speech frame after a DTX frame (ACM's kAudioFrameCN, inband VAD); a stream
// that starts in DTX marks its first speech frame (issue #514).
SM_TEST(RtpSender, AudioMarkerOnTheFirstPacketAndAfterDtx) {
  RtpSenderConfig c;
  c.video = false;
  c.payload_type = 111;
  const Bytes speech(40, 7), dtx(1, 0);
  auto markers = [&](RtpSender& s, std::initializer_list<const Bytes*> frames) {
    uint32_t ts = 0;
    for (const Bytes* f : frames) s.SendAudio(*f, ts += 960, false, Timestamp::Millis(ts / 48));
    std::vector<bool> out;
    for (const SenderPacket& p : s.TakePackets()) out.push_back(p.packet.marker);
    return out;
  };
  RtpSender a(c);
  SM_EXPECT_TRUE(markers(a, {&speech, &speech, &dtx, &dtx, &speech, &speech}) ==
                 (std::vector<bool>{true, false, false, false, true, false}));
  RtpSender b(c);
  SM_EXPECT_TRUE(markers(b, {&dtx, &speech, &speech}) == (std::vector<bool>{false, true, false}));
  // The caller's marker still sets it.
  RtpSender d(c);
  d.SendAudio(speech, 960, false, Timestamp::Millis(0));
  d.SendAudio(speech, 1920, true, Timestamp::Millis(20));
  auto out = d.TakePackets();
  SM_ASSERT_EQ(out.size(), 2u);
  SM_EXPECT_TRUE(out[1].packet.marker);
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

// RTPSenderVideo with RED and ULPFEC: every media packet is wrapped in RED (BuildRedPayload), the
// FEC is generated as the pacer sends the media (RtpSenderEgress::SendPacket) and the FEC packets
// are RED packets on the media SSRC that take their sequence numbers when the pacer sends them.
SM_TEST(RtpSender, UlpfecOverRedOnTheMediaSsrc) {
  RtpSenderConfig c = VideoCfg();
  c.fec = true;
  RtpSender s(c);
  s.SetFecParams({.fec_rate = 128, .max_fec_frames = 1});
  s.SendVideoFrame(Chunks({100, 100, 100, 100}), 1, Timestamp::Millis(0));
  auto media = s.TakePackets();
  SM_ASSERT_EQ(media.size(), 4u);
  for (size_t i = 0; i < media.size(); ++i) {
    SM_EXPECT_TRUE(media[i].kind == PacketKind::kVideo);
    SM_EXPECT_EQ(media[i].packet.payload_type, c.ulpfec.red_payload_type);
    SM_ASSERT_EQ(media[i].packet.payload.size(), 101u);
    SM_EXPECT_EQ(media[i].packet.payload[0], 96);  // RED block header: the media PT, F bit 0.
    SM_EXPECT_EQ(media[i].packet.payload[1], static_cast<uint8_t>(i + 1));
    s.AssignSequenceNumber(media[i], Timestamp::Millis(1));
    s.OnPacketReleased(media[i], Timestamp::Millis(1));
    // The FEC comes when the frame's last packet is sent.
    if (i < 3) SM_EXPECT_TRUE(s.TakeFecPackets().empty());
  }
  auto fec = s.TakeFecPackets();
  SM_ASSERT_EQ(fec.size(), 2u);  // NumFecPackets(4, 128).
  for (size_t i = 0; i < fec.size(); ++i) {
    SM_EXPECT_TRUE(fec[i].kind == PacketKind::kFec);
    SM_EXPECT_TRUE(fec[i].priority == cc::PacketPriority::kFec);
    SM_EXPECT_TRUE(fec[i].sender == &s);
    SM_EXPECT_EQ(fec[i].packet.ssrc, c.ssrc);
    SM_EXPECT_EQ(fec[i].packet.payload_type, c.ulpfec.red_payload_type);
    SM_EXPECT_EQ(fec[i].packet.payload[0], c.ulpfec.ulpfec_payload_type);
    SM_EXPECT_FALSE(fec[i].packet.marker);
    s.AssignSequenceNumber(fec[i], Timestamp::Millis(1));
    // After the four media packets: 65534, 65535, 0, 1, then 2 and 3.
    SM_EXPECT_EQ(fec[i].packet.seq, static_cast<uint16_t>(2 + i));
    s.OnPacketReleased(fec[i], Timestamp::Millis(1));
  }
  SM_EXPECT_TRUE(s.TakeFecPackets().empty());
  SM_EXPECT_EQ(s.stats().fec_packets, 2u);
  // The FEC packets are in the sender report count, as RtpSenderEgress::UpdateRtpStats adds every
  // packet sent on the SSRC to `transmitted` (rtp_sender_egress.cc:488-493; issue #816).
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(0)).packet_count, 6u);
  // The FEC packets are not kept for retransmission.
  SM_EXPECT_EQ(s.rtx().history_size(), 4u);
}

// Issue #619: SendVideoFrame returns the payload bytes of the frame's video packets, RED headers
// included, as RTPSenderVideo::LogAndSendToNetwork sums them (rtp_sender_video.cc:245-251) for the
// post-encode overhead.
SM_TEST(RtpSender, VideoFrameReturnsItsPacketizedPayloadSize) {
  RtpSender s(VideoCfg());
  SM_EXPECT_EQ(s.SendVideoFrame(Chunks({100, 200, 50}), 1, Timestamp::Millis(0)), 350u);
  SM_EXPECT_EQ(s.SendVideoFrame({}, 2, Timestamp::Millis(1)), 0u);
  RtpSenderConfig c = VideoCfg();
  c.fec = true;
  RtpSender red(c);
  SM_EXPECT_EQ(red.SendVideoFrame(Chunks({100, 100, 100, 100}), 1, Timestamp::Millis(0)), 404u);
}

// rtx_payload_type_map_: a RED packet is resent with the RTX payload type associated with RED.
SM_TEST(RtpSender, RedPacketIsResentWithTheRedRtxPayloadType) {
  RtpSenderConfig c = VideoCfg();
  c.fec = true;
  RtpSender s(c);
  s.SendVideoFrame(Chunks({100}), 1, Timestamp::Millis(0));
  auto sent = Release(s);
  SM_ASSERT_EQ(sent.size(), 1u);
  const uint16_t seq = sent[0].packet.seq;
  s.OnNack(std::vector<uint16_t>{seq}, Timestamp::Millis(100), TimeDelta::Millis(10));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_EQ(rtx[0].packet.ssrc, 0x2222u);
  SM_EXPECT_EQ(rtx[0].packet.payload_type, c.ulpfec.red_rtx_payload_type);
  auto restored = loss::RestoreFromRtx(rtx[0].packet, c.ssrc, c.ulpfec.red_payload_type);
  SM_ASSERT_TRUE(restored.has_value());
  SM_EXPECT_TRUE(restored->payload == sent[0].packet.payload);
}

// RTPSenderVideo::FecPacketOverhead: the ULPFEC header with a 6-byte mask (18), the RED header (1)
// and the registered extensions an FEC packet can carry.
SM_TEST(RtpSender, FecPacketOverhead) {
  RtpSenderConfig c = VideoCfg();
  SM_EXPECT_EQ(RtpSender(c).FecPacketOverhead(), 0u);
  c.fec = true;
  SM_EXPECT_EQ(RtpSender(c).FecPacketOverhead(), 19u);
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  c.extensions = &map;
  // One-byte header extension block (4) plus transport-cc (1 + 2), padded to 4: 8 bytes.
  SM_EXPECT_EQ(RtpSender(c).FecPacketOverhead(), 27u);
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
  Release(s);
  SenderPacket pad = s.MakePadding(224 - 28, 77, Timestamp::Millis(1));
  SM_EXPECT_TRUE(pad.kind == PacketKind::kPadding);
  SM_EXPECT_TRUE(pad.priority == cc::PacketPriority::kPadding);
  SM_EXPECT_EQ(pad.packet.ssrc, 0x2222u);
  SM_EXPECT_EQ(pad.packet.payload_type, 97);
  SM_EXPECT_TRUE(pad.packet.payload.empty());
  SM_EXPECT_EQ(pad.packet.size(), 196u);
  // A resend after the padding takes the next RTX sequence number, at release.
  s.AssignSequenceNumber(pad, Timestamp::Millis(1));
  uint16_t want[] = {65534};
  s.OnNack(want, Timestamp::Millis(50), TimeDelta::Millis(10));
  auto rtx = Release(s, Timestamp::Millis(50));
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
  SM_EXPECT_EQ(Release(m)[0].packet.seq, 65535);
}

SM_TEST(RtpSender, AudioLevelByteIsWrittenWhenTheExtensionIsMapped) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(2, sync::uri::kAudioLevel);
  RtpSenderConfig c;
  c.video = false;
  c.extensions = &map;
  RtpSender s(c);
  const Bytes frame(60, 3);
  s.SendAudio(frame, 960, false, Timestamp::Millis(0), audio::AudioLevel{37, true});
  // No level given and no capture frame analyzed: RmsLevel::Average() of nothing, 127, as
  // ChannelSend::SendData always sets one while the extension is registered (channel_send.cc:389-396).
  s.SendAudio(frame, 1920, false, Timestamp::Millis(20));
  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 2u);
  auto level = sync::ExtensionAccessor(map).GetRaw(out[0].packet, sync::uri::kAudioLevel);
  SM_ASSERT_TRUE(level.has_value());
  SM_EXPECT_EQ((*level)[0], 0x80 | 37);
  level = sync::ExtensionAccessor(map).GetRaw(out[1].packet, sync::uri::kAudioLevel);
  SM_ASSERT_TRUE(level.has_value());
  SM_EXPECT_EQ((*level)[0], 0x80 | 127);
  SM_EXPECT_EQ(s.stats().audio_level_packets, 2u);

  sync::ExtensionMap none;  // Not mapped: nothing is written.
  RtpSenderConfig c2;
  c2.video = false;
  c2.extensions = &none;
  RtpSender s2(c2);
  s2.SendAudio(frame, 960, false, Timestamp::Millis(0), audio::AudioLevel{37, true});
  SM_EXPECT_EQ(s2.stats().audio_level_packets, 0u);
}

// #567 F6.9: the level is computed in the send path, as ChannelSend: each 10 ms capture frame
// feeds RmsLevel (AnalyzeMuted for a muted frame after a muted one, channel_send.cc:909-924), and
// each packet takes Average() and resets it (channel_send.cc:389-396); the voice bit is the
// encoder's `speech`, off for an Opus DTX frame of at most 2 bytes (rtp_sender_audio.cc:248-250,
// opus_interface.cc:179-192, audio_encoder_opus.cc:604).
SM_TEST(RtpSender, AudioLevelIsTheRmsLevelOfTheCaptureFramesSinceTheLastPacket) {
  sync::ExtensionMap map;
  map.Register(2, sync::uri::kAudioLevel);
  RtpSenderConfig c;
  c.video = false;
  c.extensions = &map;
  RtpSender s(c);
  const std::vector<int16_t> loud(480, 1000);  // 10 ms at 48 kHz: -30.3 dBov.
  const std::vector<int16_t> silent(480, 0);
  const Bytes opus(60, 3);
  const Bytes dtx(2, 3);
  auto level_of = [&](const SenderPacket& p) {
    const auto raw = sync::ExtensionAccessor(map).GetRaw(p.packet, sync::uri::kAudioLevel);
    return raw ? int{(*raw)[0]} : -1;
  };

  // Two loud frames: 30.
  s.AnalyzeAudioCapture(loud, false);
  s.AnalyzeAudioCapture(loud, false);
  s.SendAudio(opus, 960, false, Timestamp::Millis(20));
  // One loud, one silent: the mean square halves, 33 (RmsLevel over both, not the last frame).
  s.AnalyzeAudioCapture(loud, false);
  s.AnalyzeAudioCapture(silent, false);
  s.SendAudio(opus, 1920, false, Timestamp::Millis(40));
  // Muted: the first muted frame is analyzed as given (the caller's faded frame); later muted
  // frames count as zeros whatever they hold.
  s.AnalyzeAudioCapture(loud, false);
  s.AnalyzeAudioCapture(loud, true);
  s.SendAudio(opus, 2880, false, Timestamp::Millis(60));
  s.AnalyzeAudioCapture(loud, true);
  s.AnalyzeAudioCapture(loud, true);
  s.SendAudio(dtx, 3840, false, Timestamp::Millis(80));
  // Unmuting analyzes the frame again.
  s.AnalyzeAudioCapture(loud, false);
  s.AnalyzeAudioCapture(loud, false);
  s.SendAudio(opus, 4800, false, Timestamp::Millis(100));
  // An override (ChannelSend's audio_level_dbov_override) wins, and the average is still reset.
  s.AnalyzeAudioCapture(loud, false);
  s.SendAudio(opus, 5760, false, Timestamp::Millis(120), audio::AudioLevel{50, false});
  s.SendAudio(opus, 6720, false, Timestamp::Millis(140));

  auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 7u);
  SM_EXPECT_EQ(level_of(out[0]), 0x80 | 30);
  SM_EXPECT_EQ(level_of(out[1]), 0x80 | 33);
  SM_EXPECT_EQ(level_of(out[2]), 0x80 | 30);
  SM_EXPECT_EQ(level_of(out[3]), 127);  // Muted zeros, and a DTX frame: no voice bit.
  SM_EXPECT_EQ(level_of(out[4]), 0x80 | 30);
  SM_EXPECT_EQ(level_of(out[5]), 50);
  SM_EXPECT_EQ(level_of(out[6]), 0x80 | 127);
  SM_EXPECT_EQ(s.stats().audio_level_packets, 7u);

  // The same frames through libwebrtc's RmsLevel directly.
  audio::RmsLevel r;
  r.Analyze(std::span<const int16_t>(loud));
  r.Analyze(std::span<const int16_t>(silent));
  SM_EXPECT_EQ(r.Average(), 33);

  // Not mapped: nothing is analyzed or written.
  sync::ExtensionMap none;
  RtpSenderConfig c2;
  c2.video = false;
  c2.extensions = &none;
  RtpSender s2(c2);
  s2.AnalyzeAudioCapture(loud, false);
  s2.SendAudio(opus, 960, false, Timestamp::Millis(20));
  SM_EXPECT_EQ(s2.stats().audio_level_packets, 0u);
}

// #879: ProcessAudioCapture mutes as ChannelSend::ProcessAndEncodeAudio (channel_send.cc:909-924):
// AudioFrameOperations::Mute with the stream's previous mute state, then the level. The frame
// where mute starts fades out over its last 128 samples, muted frames after it are zeros, and
// the frame where mute ends fades in over its first 128 samples.
SM_TEST(RtpSender, ProcessAudioCaptureFadesOnMuteAsChannelSend) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kAudioLevel);
  RtpSenderConfig c;
  c.video = false;
  c.extensions = &map;
  RtpSender s(c);
  const std::vector<int16_t> loud(960, 1000);  // 10 ms of stereo at 48 kHz.
  auto frame = loud;
  s.ProcessAudioCapture(frame, 2, false);
  SM_EXPECT_TRUE(frame == loud);
  // Mute starts: kept up to sample 351, then 1000 * (1 - k/128) truncated, 0 on the last.
  frame = loud;
  s.ProcessAudioCapture(frame, 2, true);
  SM_EXPECT_EQ(frame[2 * 351], 1000);
  SM_EXPECT_EQ(frame[2 * 352], 992);
  SM_EXPECT_EQ(frame[2 * 352 + 1], 992);
  SM_EXPECT_EQ(frame[2 * 478], 7);
  SM_EXPECT_EQ(frame[2 * 479], 0);
  SM_EXPECT_EQ(frame[2 * 479 + 1], 0);
  // Muted throughout: zeros.
  frame = loud;
  s.ProcessAudioCapture(frame, 2, true);
  SM_EXPECT_TRUE(frame == std::vector<int16_t>(960, 0));
  // Mute ends: ramps up over the first 128 samples per channel.
  frame = loud;
  s.ProcessAudioCapture(frame, 2, false);
  SM_EXPECT_EQ(frame[0], 7);
  SM_EXPECT_EQ(frame[1], 7);
  SM_EXPECT_EQ(frame[2 * 127], 1000);
  SM_EXPECT_EQ(frame[2 * 479], 1000);
  // The level is analyzed after the fade, as ChannelSend: frames 1-4 above give the packet's level.
  s.SendAudio(Bytes(60, 3), 960, false, Timestamp::Millis(40));
  dsp::RmsLevel r;
  r.Analyze(std::span<const int16_t>(loud));
  auto faded = loud;
  dsp::MuteAudioFrame(faded, 2, false, true);
  r.Analyze(std::span<const int16_t>(faded));
  r.AnalyzeMuted(loud.size());
  auto rising = loud;
  dsp::MuteAudioFrame(rising, 2, true, false);
  r.Analyze(std::span<const int16_t>(rising));
  const auto out = s.TakePackets();
  SM_ASSERT_EQ(out.size(), 1u);
  const auto raw = sync::ExtensionAccessor(map).GetRaw(out[0].packet, sync::uri::kAudioLevel);
  SM_ASSERT_TRUE(raw.has_value());
  SM_EXPECT_EQ(int{(*raw)[0]}, 0x80 | r.Average());
}

SM_TEST(RtpSender, MediaHeaderSizeCountsTheExtensionsTheNextPacketCarries) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(3, sync::uri::kAbsSendTime);
  map.Register(4, sync::uri::kMid);
  RtpSenderConfig c;
  c.extensions = &map;
  c.mid = "v";
  c.mid_packets = 1;
  RtpSender s(c);
  // 12 + 4 (profile, length) + (1+2) + (1+3) + (1+1) = 12 + 4 + 9 -> padded to 12.
  SM_EXPECT_EQ(s.MediaHeaderSize(), 12u + 4u + 12u);
  auto chunks = Chunks({10});
  s.SendVideoFrame(chunks, 9000, Timestamp::Millis(10));  // The one MID packet.
  // MID is no longer repeated: 12 + 4 + (3 + 4 = 7 -> 8).
  SM_EXPECT_EQ(s.MediaHeaderSize(), 12u + 4u + 8u);
}

// RTPSender::GeneratePadding (compare/webrtc/reference/modules/rtp_rtcp/source/rtp_sender.cc:418-459) and
// RtpPacketHistory::GetPayloadPaddingPacket (rtp_packet_history.cc:261-320), the cases of
// rtp_sender_unittest.cc and rtp_packet_history_unittest.cc about payload padding (#120).
namespace {
sync::ExtensionMap TccExtensions() {
  sync::ExtensionMap m;
  m.Register(1, sync::uri::kTransportCc);
  return m;
}
RtpSenderConfig PaddingCfg(const sync::ExtensionMap* ext, bool video = true) {
  RtpSenderConfig c = VideoCfg();
  c.video = video;
  c.extensions = ext;
  return c;
}
// Sends one frame of `sizes` and lets the pacer release it, so the history holds it.
void SendAndRelease(RtpSender& s, std::initializer_list<size_t> sizes, uint32_t ts, Timestamp now) {
  auto chunks = Chunks(sizes);
  s.SendVideoFrame(chunks, ts, now);
  Release(s, now);
}
size_t PaddingTotal(const std::vector<SenderPacket>& v) {
  size_t n = 0;
  for (const auto& p : v) n += p.packet.payload.size() + p.packet.padding_size;
  return n;
}
}  // namespace

// GeneratePadding: empty padding is full 255-byte packets on the RTX SSRC (kMaxPaddingLength), so the
// total is the target rounded up, and every packet is padding at padding priority.
SM_TEST(RtpSender, GeneratePaddingEmptyPacketsAreFullOnTheRtxSsrc) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  auto out = s.GeneratePadding(600, Timestamp::Millis(5));
  SM_ASSERT_EQ(out.size(), 3u);  // 255 + 255 + 255 >= 600
  for (auto& o : out) SM_EXPECT_EQ(o.packet.timestamp, 0u);  // Set at release (PacketSequencer).
  for (auto& o : out) s.AssignSequenceNumber(o, Timestamp::Millis(5));
  uint16_t prev_seq = out[0].packet.seq;
  for (size_t i = 0; i < out.size(); ++i) {
    SM_EXPECT_EQ(out[i].packet.ssrc, 0x2222u);
    SM_EXPECT_EQ(out[i].packet.payload_type, 97);
    SM_EXPECT_EQ(out[i].packet.padding_size, 255);
    SM_EXPECT_TRUE(out[i].packet.payload.empty());
    SM_EXPECT_TRUE(out[i].kind == PacketKind::kPadding);
    SM_EXPECT_TRUE(out[i].priority == cc::PacketPriority::kPadding);
    if (i > 0) SM_EXPECT_EQ(out[i].packet.seq, static_cast<uint16_t>(prev_seq + 1));
    prev_seq = out[i].packet.seq;
  }
  SM_EXPECT_EQ(s.stats().payload_padding_packets, 0u);
}

// RTPSender::SetMaxRtpPacketSize (rtp_sender.cc:307-312) takes a whole packet of 100 to 1500 bytes.
// A max_payload_size of 0 made every empty padding packet carry 0 bytes, so GeneratePadding never
// reached its target and allocated until bad_alloc (#257). The sender clamps it to that range.
SM_TEST(RtpSender, MaxPayloadSizeIsClampedToLibwebrtcPacketSizeRange) {
  RtpSenderConfig c = VideoCfg();
  c.max_payload_size = 0;
  SM_EXPECT_EQ(RtpSender(c).config().max_payload_size, 100u - 12u);
  c.max_payload_size = 20;
  SM_EXPECT_EQ(RtpSender(c).config().max_payload_size, 100u - 12u);
  c.max_payload_size = 5000;
  SM_EXPECT_EQ(RtpSender(c).config().max_payload_size, 1500u - 12u);
  c.max_payload_size = 1200 - 12;
  SM_EXPECT_EQ(RtpSender(c).config().max_payload_size, 1200u - 12u);
}

SM_TEST(RtpSender, GeneratePaddingWithAZeroMaxPayloadSizeReachesTheTarget) {
  const auto ext = TccExtensions();
  for (bool video : {true, false}) {
    RtpSenderConfig c = PaddingCfg(&ext, video);
    c.max_payload_size = 0;
    RtpSender s(c);
    // Checked first, so that on the old code the test fails here instead of hanging below.
    SM_ASSERT_EQ(s.config().max_payload_size, 88u);
    auto out = s.GeneratePadding(600, Timestamp::Millis(5));
    // A padding packet's padding is the 100-byte limit less its header with the transport-cc
    // extension, 12 + 8 (max_packet_size_ - max_padding_fec_packet_header_, rtp_sender.cc:434-435;
    // #513). Video: full 80-byte packets, 8 x 80 >= 600. Audio: the bytes left, 50 to 80 each.
    SM_ASSERT_EQ(out.size(), 8u);
    for (const auto& p : out) {
      SM_EXPECT_GT(p.packet.padding_size, 0);
      SM_EXPECT_LE(p.packet.padding_size, 80);
    }
    SM_EXPECT_GE(PaddingTotal(out), 600u);
  }
}

// RTPSender::GeneratePadding (rtp_sender.cc:434-435, 150-165) sizes empty padding by
// max_packet_size_ - max_padding_fec_packet_header_: the header with the extensions a padding packet
// can carry (kFecOrPaddingExtensionSizes) fits in the whole-packet limit. Our padding packets were
// 8 bytes over a 100-byte limit with transport-cc (issue #513).
SM_TEST(RtpSender, EmptyPaddingWithItsHeaderExtensionsFitsThePacketLimit) {
  const auto ext = TccExtensions();
  for (size_t limit : {size_t{100}, size_t{280}, size_t{1460}}) {
    RtpSenderConfig c = PaddingCfg(&ext);
    c.max_payload_size = limit - 12;
    RtpSender s(c);
    SM_EXPECT_EQ(s.PaddingFecPacketHeaderSize(), 12u + 8u);
    auto out = s.GeneratePadding(2000, Timestamp::Millis(5));
    SM_ASSERT_GT(out.size(), 0u);
    SM_EXPECT_EQ(static_cast<size_t>(out[0].packet.padding_size), std::min<size_t>(limit - 20, 255));
  }
}

// RTPSender::ExpectedPerPacketOverhead (rtp_sender.cc:537-540, UpdateHeaderSizes :736-786, issue #577):
// the fixed header, the registered non-volatile extensions rounded up to 4, and kRtxHeaderSize with RTX.
// sm_compare's libwebrtc sender (transport-cc only) reports 22 with RTX and 20 without.
SM_TEST(RtpSender, ExpectedPerPacketOverheadIsTheHeaderWithNonVolatileExtensions) {
  const auto ext = TccExtensions();
  RtpSenderConfig c = VideoCfg();
  c.extensions = &ext;
  c.rtx_ssrc = 0x300;
  SM_EXPECT_EQ(RtpSender(c).ExpectedPerPacketOverhead(), 22u);  // 12 + (4 + 3 -> 8) + 2.
  c.rtx_ssrc.reset();
  SM_EXPECT_EQ(RtpSender(c).ExpectedPerPacketOverhead(), 20u);
  c.extensions = nullptr;
  SM_EXPECT_EQ(RtpSender(c).ExpectedPerPacketOverhead(), 12u);  // No extensions: no block.
  // abs-send-time (3) is non-volatile; the video orientation and absolute capture time are volatile.
  sync::ExtensionMap more = TccExtensions();
  more.Register(3, sync::uri::kAbsSendTime);
  more.Register(5, sync::uri::kVideoOrientation);
  more.Register(6, sync::uri::kAbsCaptureTime);
  c.extensions = &more;
  SM_EXPECT_EQ(RtpSender(c).ExpectedPerPacketOverhead(), 12u + 12u);  // 4 + 3 + 4 = 11 -> 12.
  // The audio level is non-volatile on an audio sender (kAudioExtensionSizes).
  more.Register(7, sync::uri::kAudioLevel);
  RtpSenderConfig a = c;
  a.video = false;
  SM_EXPECT_EQ(RtpSender(a).ExpectedPerPacketOverhead(), 12u + 12u + 4u);  // 4 + 3 + 4 + 2 = 13 -> 16.
  SM_EXPECT_EQ(RtpSender(c).ExpectedPerPacketOverhead(), 12u + 12u);  // Not in kVideoExtensionSizes.
}

// UpdateHeaderSizes (rtp_sender.cc:744-772): MID counts at its largest (16) while it is sent on the
// media SSRC (not yet acked) or on the RTX SSRC (RTX not yet acked), whatever the MID's length.
SM_TEST(RtpSender, ExpectedPerPacketOverheadCountsMidUntilBothSsrcsAreAcked) {
  sync::ExtensionMap ext = TccExtensions();
  ext.Register(4, sync::uri::kMid);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &ext;
  c.mid = "v";
  c.rtx_ssrc = 0x300;
  RtpSender s(c);
  SM_EXPECT_EQ(s.ExpectedPerPacketOverhead(), 12u + 24u + 2u);  // 4 + 3 + 17 = 24.
  s.OnReceivedAckOnSsrc();
  SM_EXPECT_EQ(s.ExpectedPerPacketOverhead(), 12u + 24u + 2u);  // Still sent on RTX.
  s.OnReceivedAckOnRtxSsrc();
  SM_EXPECT_EQ(s.ExpectedPerPacketOverhead(), 12u + 8u + 2u);
  c.rtx_ssrc.reset();
  RtpSender n(c);
  n.OnReceivedAckOnSsrc();
  SM_EXPECT_EQ(n.ExpectedPerPacketOverhead(), 12u + 8u);
  c.mid.clear();
  SM_EXPECT_EQ(RtpSender(c).ExpectedPerPacketOverhead(), 12u + 8u);  // No MID to send.
}

// RTPSender::RtxPacketOverhead (rtp_sender.cc:564-595): kRtxHeaderSize with RTX, 0 without.
SM_TEST(RtpSender, RtxPacketOverheadIsTheRtxHeaderWithRtx) {
  RtpSenderConfig c = VideoCfg();
  c.rtx_ssrc = 0x300;
  SM_EXPECT_EQ(RtpSender(c).RtxPacketOverhead(), 2u);
  c.rtx_ssrc.reset();
  SM_EXPECT_EQ(RtpSender(c).RtxPacketOverhead(), 0u);
}

// rtp_sender.cc:574-590 (issue #555): while the media SSRC has been acked and the RTX SSRC has not,
// RtxPacketOverhead adds MID room (2 + its length) and 3 bytes of extension rounding; libwebrtc's
// first video frames after the first receiver report are split to this smaller limit until the
// first report block for the RTX SSRC arrives.
SM_TEST(RtpSender, RtxPacketOverheadReservesExtensionRoomUntilTheRtxSsrcIsAcked) {
  RtpSenderConfig c = VideoCfg();
  c.rtx_ssrc = 0x300;
  {
    RtpSender s(c);
    SM_EXPECT_EQ(s.RtxPacketOverhead(), 2u);  // Nothing acked yet.
    s.OnReceivedAckOnSsrc();
    SM_EXPECT_EQ(s.RtxPacketOverhead(), 2u + 3u);
    s.OnReceivedAckOnRtxSsrc();
    SM_EXPECT_EQ(s.RtxPacketOverhead(), 2u);
  }
  {
    RtpSender s(c);
    s.OnReceivedAckOnRtxSsrc();  // RTX acked first: no window.
    s.OnReceivedAckOnSsrc();
    SM_EXPECT_EQ(s.RtxPacketOverhead(), 2u);
  }
  {
    c.mid = "video0";
    RtpSender s(c);
    s.OnReceivedAckOnSsrc();
    SM_EXPECT_EQ(s.RtxPacketOverhead(), 2u + (2u + 6u) + 3u);
  }
  {
    c.rtx_ssrc.reset();
    RtpSender s(c);
    s.OnReceivedAckOnSsrc();
    SM_EXPECT_EQ(s.RtxPacketOverhead(), 0u);
  }
}

// AppendPayloadPadding: the newest large packet of the history goes out again on the RTX SSRC, with the OSN
// in front of the payload, while at least kMinPayloadPaddingBytes (50) of the target are left.
SM_TEST(RtpSender, GeneratePaddingResendsTheNewestLargePacketAsPayload) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  SendAndRelease(s, {900}, 9000, Timestamp::Millis(1));
  const uint16_t osn = 65534;
  auto out = s.GeneratePadding(1000, Timestamp::Millis(2));
  // 902 of the 1000 bytes are covered and 98 are left (>= 50). The same packet again stays within
  // 3 x the target (902 <= 2000 + 98), so it goes out again.
  SM_ASSERT_GE(out.size(), 2u);
  SM_EXPECT_EQ(out[0].packet.ssrc, 0x2222u);
  SM_EXPECT_EQ(out[0].packet.padding_size, 0);
  SM_ASSERT_EQ(out[0].packet.payload.size(), 902u);
  SM_EXPECT_EQ((out[0].packet.payload[0] << 8) | out[0].packet.payload[1], osn);
  SM_EXPECT_TRUE(out[0].kind == PacketKind::kPadding);
  SM_EXPECT_EQ(out[0].packet.timestamp, 9000u);
  SM_EXPECT_EQ(s.stats().payload_padding_packets, 2u);
  // The RTX sequence numbers are used in order, at release.
  for (auto& o : out) s.AssignSequenceNumber(o, Timestamp::Millis(2));
  SM_EXPECT_EQ(out[1].packet.seq, static_cast<uint16_t>(out[0].packet.seq + 1));
}

// PacketSequencer::Sequence (packet_sequencer.cc:71-75) numbers a packet on the RTX SSRC when the
// pacer releases it (PacketRouter::SendPacket). A resend or padding packet that is built but never
// released takes no RTX sequence number, so the next one released follows the last one sent
// without a gap (issue #854: on very_low_capacity ours used one more number than libwebrtc).
SM_TEST(RtpSender, RtxSequenceNumbersAreAssignedAtReleaseOnly) {
  const auto ext = TccExtensions();
  RtpSenderConfig c = PaddingCfg(&ext);
  c.rtx.initial_seq = 893;
  RtpSender s(c);
  SendAndRelease(s, {900, 900, 900}, 9000, Timestamp::Millis(1));
  const uint16_t first = 65534;
  // Two resends built; the pacer releases only the second (the first is never sent).
  uint16_t want[] = {first, static_cast<uint16_t>(first + 1)};
  s.OnNack(want, Timestamp::Millis(10), TimeDelta::Millis(20));
  auto rtx = s.TakePackets();
  SM_ASSERT_EQ(rtx.size(), 2u);
  // Empty padding built and dropped too.
  auto dropped = s.GeneratePadding(100, Timestamp::Millis(11));
  SM_ASSERT_FALSE(dropped.empty());
  s.AssignSequenceNumber(rtx[1], Timestamp::Millis(12));
  SM_EXPECT_EQ(rtx[1].packet.seq, 893);
  auto pad = s.GeneratePadding(100, Timestamp::Millis(13));
  SM_ASSERT_FALSE(pad.empty());
  s.AssignSequenceNumber(pad[0], Timestamp::Millis(13));
  SM_EXPECT_EQ(pad[0].packet.ssrc, 0x2222u);
  SM_EXPECT_EQ(pad[0].packet.seq, 894);
  // The media sequence numbers are untouched.
  s.SendVideoFrame(Chunks({10}), 9001, Timestamp::Millis(20));
  SM_EXPECT_EQ(Release(s, Timestamp::Millis(20))[0].packet.seq, 1);
}

// kMaxPaddingSizeFactor: a packet that would take the total over 3 x the target is not used; the
// empty padding fills the target instead.
SM_TEST(RtpSender, GeneratePaddingNeverOvershootsThreeTimesTheTarget) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  SendAndRelease(s, {1000}, 9000, Timestamp::Millis(1));
  // Target 300: the overshoot allowed is 2 x 300 = 600, 1002 > 600 + 300.
  auto out = s.GeneratePadding(300, Timestamp::Millis(2));
  SM_EXPECT_EQ(s.stats().payload_padding_packets, 0u);
  SM_ASSERT_EQ(out.size(), 2u);
  SM_EXPECT_EQ(out[0].packet.padding_size, 255);
  // Target 500: 1002 <= 1000 + 500, so the payload goes once, and nothing is left.
  out = s.GeneratePadding(500, Timestamp::Millis(3));
  SM_EXPECT_EQ(s.stats().payload_padding_packets, 1u);
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.payload.size(), 1002u);
  SM_EXPECT_GE(PaddingTotal(out), 500u);
}

// kMinPayloadPaddingBytes: below 50 bytes left, no payload is resent.
SM_TEST(RtpSender, GeneratePaddingBelowTheMinimumUsesEmptyPadding) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  SendAndRelease(s, {100}, 9000, Timestamp::Millis(1));
  auto out = s.GeneratePadding(49, Timestamp::Millis(2));
  SM_EXPECT_EQ(s.stats().payload_padding_packets, 0u);
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.padding_size, 255);
  out = s.GeneratePadding(50, Timestamp::Millis(3));
  SM_EXPECT_EQ(s.stats().payload_padding_packets, 1u);
}

// RtpPacketHistory kRecentLargePacket: a small packet does not replace a large one that is recent
// (hysteresis 100 bytes), a packet within 100 bytes of it does.
SM_TEST(RtpSender, PayloadPaddingKeepsTheRecentLargePacket) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  SendAndRelease(s, {1000}, 9000, Timestamp::Millis(1));  // seq 65534
  SendAndRelease(s, {200}, 9100, Timestamp::Millis(2));   // 200 + 100 < 1000: not taken
  auto out = s.GeneratePadding(1100, Timestamp::Millis(3));
  SM_ASSERT_GE(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.payload.size(), 1002u);
  SM_EXPECT_EQ((out[0].packet.payload[0] << 8) | out[0].packet.payload[1], 65534);
  SendAndRelease(s, {950}, 9200, Timestamp::Millis(4));   // 950 + 100 > 1000: taken
  out = s.GeneratePadding(1100, Timestamp::Millis(5));
  SM_ASSERT_GE(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.payload.size(), 952u);
}

// Audio has no payload padding (no kRtxRedundantPayloads), and its empty padding may be small
// (kMinAudioPaddingLength).
SM_TEST(RtpSender, GeneratePaddingAudioIsEmptyAndMaySendSmallPackets) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext, /*video=*/false));
  auto out = s.GeneratePadding(120, Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.padding_size, 120);
  out = s.GeneratePadding(10, Timestamp::Millis(2));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.padding_size, 50);
}

// With RTX and no abs-send-time or transport-cc, a media packet must be sent first.
SM_TEST(RtpSender, GeneratePaddingWithoutBweExtensionWaitsForMedia) {
  const sync::ExtensionMap none;
  RtpSender s(PaddingCfg(&none));
  SM_EXPECT_TRUE(s.GeneratePadding(500, Timestamp::Millis(1)).empty());
  auto chunks = Chunks({100});
  s.SendVideoFrame(chunks, 9000, Timestamp::Millis(2));
  // RtpSenderEgress::MediaHasBeenSent: queued is not sent.
  SM_EXPECT_TRUE(s.GeneratePadding(500, Timestamp::Millis(2)).empty());
  Release(s, Timestamp::Millis(2));
  SM_EXPECT_FALSE(s.GeneratePadding(500, Timestamp::Millis(3)).empty());
}

// Without RTX, RTPSender::GeneratePadding pads on the media SSRC once PacketSequencer allows it
// (rtp_sender.cc:437-449, packet_sequencer.cc:139-154): not before media, and on video only after
// a frame's last packet (the marker). PopulatePaddingFields gives the padding the last media
// packet's timestamp and payload type, and the next sequence number (issue #514 row 1).
SM_TEST(RtpSender, VideoWithoutRtxPadsOnTheMediaSsrcAfterAFrameEnds) {
  const auto ext = TccExtensions();
  RtpSenderConfig c = PaddingCfg(&ext);
  c.rtx_ssrc.reset();
  c.initial_seq = 100;
  RtpSender s(c);
  SM_EXPECT_FALSE(s.CanSendPaddingOnMediaSsrc());
  SM_EXPECT_TRUE(s.GeneratePadding(500, Timestamp::Millis(1)).empty());
  s.SendVideoFrame(Chunks({300, 300}), 9000, Timestamp::Millis(2));
  auto media = s.TakePackets();
  SM_ASSERT_EQ(media.size(), 2u);
  s.AssignSequenceNumber(media[0], Timestamp::Millis(2));  // Mid-frame: no marker yet.
  SM_EXPECT_FALSE(s.CanSendPaddingOnMediaSsrc());
  SM_EXPECT_TRUE(s.GeneratePadding(500, Timestamp::Millis(2)).empty());
  s.AssignSequenceNumber(media[1], Timestamp::Millis(3));
  SM_EXPECT_TRUE(s.CanSendPaddingOnMediaSsrc());
  auto out = s.GeneratePadding(500, Timestamp::Millis(4));
  SM_ASSERT_EQ(out.size(), 2u);  // Full 255-byte packets: 2 x 255 >= 500.
  for (size_t i = 0; i < out.size(); ++i) {
    SM_EXPECT_EQ(out[i].packet.ssrc, c.ssrc);
    SM_EXPECT_EQ(out[i].packet.padding_size, 255);
    SM_EXPECT_FALSE(out[i].packet.marker);
    s.AssignSequenceNumber(out[i], Timestamp::Millis(40));
    SM_EXPECT_EQ(out[i].packet.seq, static_cast<uint16_t>(102 + i));
    SM_EXPECT_EQ(out[i].packet.timestamp, 9000u);  // Not advanced on the media SSRC.
    SM_EXPECT_EQ(out[i].packet.payload_type, c.payload_type);
  }
  // Padding is not media: the next frame continues the numbers and padding stays allowed.
  SM_EXPECT_TRUE(s.CanSendPaddingOnMediaSsrc());
}

// is_red: with ULPFEC over RED, media-SSRC padding takes the RED block's payload type
// (UpdateLastPacketState, packet_sequencer.cc:113-118), the media's, not RED's.
SM_TEST(RtpSender, MediaSsrcPaddingAfterRedTakesTheBlockPayloadType) {
  const auto ext = TccExtensions();
  RtpSenderConfig c = PaddingCfg(&ext);
  c.rtx_ssrc.reset();
  c.fec = true;
  RtpSender s(c);
  s.SendVideoFrame(Chunks({300}), 9000, Timestamp::Millis(2));
  for (auto& p : s.TakePackets()) s.AssignSequenceNumber(p, Timestamp::Millis(2));
  auto out = s.GeneratePadding(100, Timestamp::Millis(3));
  SM_ASSERT_EQ(out.size(), 1u);
  s.AssignSequenceNumber(out[0], Timestamp::Millis(3));
  SM_EXPECT_EQ(out[0].packet.payload_type, c.payload_type);
}

// Audio without RTX (issue #514 row 2): padding on the audio SSRC once a packet has gone out,
// whatever its marker (require_marker_before_media_padding is !audio), each packet the bytes left
// clamped to [50, min(max payload, 255)] (PaddingBytesInPacket, rtp_sender.cc:150-160).
SM_TEST(RtpSender, AudioWithoutRtxPadsOnItsSsrcAfterItsFirstPacket) {
  const auto ext = TccExtensions();
  RtpSenderConfig c = PaddingCfg(&ext, /*video=*/false);
  c.rtx_ssrc.reset();
  c.payload_type = 111;
  RtpSender s(c);
  SM_EXPECT_TRUE(s.GeneratePadding(120, Timestamp::Millis(1)).empty());
  const std::vector<uint8_t> frame(60, 1);
  s.SendAudio(frame, 4800, false, Timestamp::Millis(20));
  s.SendAudio(frame, 5760, false, Timestamp::Millis(40));  // No marker (not the first packet).
  for (auto& p : s.TakePackets()) s.AssignSequenceNumber(p, Timestamp::Millis(40));
  SM_EXPECT_TRUE(s.CanSendPaddingOnMediaSsrc());
  auto out = s.GeneratePadding(120, Timestamp::Millis(41));
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_EQ(out[0].packet.ssrc, c.ssrc);
  SM_EXPECT_EQ(out[0].packet.padding_size, 120);
  s.AssignSequenceNumber(out[0], Timestamp::Millis(41));
  SM_EXPECT_EQ(out[0].packet.timestamp, 5760u);
  SM_EXPECT_EQ(out[0].packet.payload_type, 111);
  out = s.GeneratePadding(400, Timestamp::Millis(42));
  // The size is taken once per call, from all 400 bytes: 255, 255.
  SM_ASSERT_EQ(out.size(), 2u);
  SM_EXPECT_EQ(out[0].packet.padding_size, 255);
  SM_EXPECT_EQ(out[1].packet.padding_size, 255);
}

// PopulatePaddingFields on the RTX SSRC (packet_sequencer.cc:120-137): empty padding before any
// media carries timestamp 0; after media, the last media timestamp plus 90 ticks per ms since it
// was released. Payload padding keeps the resent packet's timestamp.
SM_TEST(RtpSender, RtxPaddingTimestampFollowsTheLastMediaPacket) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  auto out = s.GeneratePadding(100, Timestamp::Millis(1));
  SM_ASSERT_EQ(out.size(), 1u);
  out[0].packet.timestamp = 12345;
  s.AssignSequenceNumber(out[0], Timestamp::Millis(1));
  SM_EXPECT_EQ(out[0].packet.timestamp, 0u);
  SendAndRelease(s, {100}, 9000, Timestamp::Millis(10));
  out = s.GeneratePadding(40, Timestamp::Millis(30));  // Below 50: empty padding.
  SM_ASSERT_EQ(out.size(), 1u);
  SM_ASSERT_TRUE(out[0].packet.payload.empty());
  s.AssignSequenceNumber(out[0], Timestamp::Millis(30));
  SM_EXPECT_EQ(out[0].packet.timestamp, 9000u + 20u * 90u);
  out = s.GeneratePadding(100, Timestamp::Millis(31));  // Payload padding.
  SM_ASSERT_GE(out.size(), 1u);
  SM_ASSERT_FALSE(out[0].packet.payload.empty());
  s.AssignSequenceNumber(out[0], Timestamp::Millis(35));
  SM_EXPECT_EQ(out[0].packet.timestamp, 9000u);
}

// Video orientation (CVO, #567): RTPSenderVideo::AddRtpHeaderExtensions
// (compare/webrtc/reference/modules/rtp_rtcp/source/rtp_sender_video.cc:388-408) puts the
// rotation on the last packet of a keyframe, of a frame whose rotation changed, and of every
// frame whose rotation is not 0; the byte is ConvertVideoRotationToCVOByte (rtp_cvo.h).
namespace {
std::optional<uint8_t> Cvo(const sync::ExtensionMap& map, const SenderPacket& p) {
  auto raw = sync::ExtensionAccessor(map).GetRaw(p.packet, sync::uri::kVideoOrientation);
  if (!raw || raw->size() != 1) return std::nullopt;
  return (*raw)[0];
}
}  // namespace

SM_TEST(RtpSender, VideoOrientationOnKeyframesChangesAndNonZeroRotation) {
  sync::ExtensionMap map;
  map.Register(5, sync::uri::kVideoOrientation);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  struct Step {
    bool key;
    video::Rotation rotation;
    std::optional<uint8_t> want;
  };
  const Step steps[] = {
      {true, video::Rotation::k0, 0},              // Keyframe.
      {false, video::Rotation::k0, std::nullopt},  // Unchanged and 0.
      {false, video::Rotation::k90, 1},            // Changed.
      {false, video::Rotation::k90, 1},            // Not 0.
      {false, video::Rotation::k270, 3},
      {false, video::Rotation::k180, 2},
      {false, video::Rotation::k0, 0},             // Changed back to 0.
      {false, video::Rotation::k0, std::nullopt},
      {true, video::Rotation::k0, 0},
  };
  uint32_t ts = 0;
  for (const Step& st : steps) {
    auto chunks = Chunks({100, 100, 100});
    ts += 3000;
    s.SendVideoFrame(chunks, ts, Timestamp::Millis(ts / 90), st.key, st.rotation);
    auto out = Release(s);
    SM_ASSERT_EQ(out.size(), 3u);
    SM_EXPECT_FALSE(Cvo(map, out[0]).has_value());  // Only the last packet carries it.
    SM_EXPECT_FALSE(Cvo(map, out[1]).has_value());
    SM_EXPECT_TRUE(Cvo(map, out[2]) == st.want);
  }
  SM_EXPECT_EQ(s.stats().video_orientation_packets, 7u);
}

SM_TEST(RtpSender, VideoOrientationNeedsTheExtensionMapped) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  auto chunks = Chunks({100});
  s.SendVideoFrame(chunks, 3000, Timestamp::Millis(0), true, video::Rotation::k90);
  auto out = Release(s);
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_FALSE(Cvo(map, out[0]).has_value());
  SM_EXPECT_EQ(s.LastPacketReduction(true, video::Rotation::k90), 0u);
  SM_EXPECT_EQ(s.stats().video_orientation_packets, 0u);
}

// The packetizer's last_packet_reduction_len (rtp_sender_video.cc:741-758): the last packet's
// header less a middle packet's, with the extensions rounded up to 4 bytes.
SM_TEST(RtpSender, LastPacketReductionIsTheRoomCvoTakes) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(5, sync::uri::kVideoOrientation);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  // Middle: 12 + 4 + (1+2 -> 4). Last: 12 + 4 + (1+2 + 1+1 = 5 -> 8).
  SM_EXPECT_EQ(s.MediaHeaderSize(), 20u);
  SM_EXPECT_EQ(s.LastPacketReduction(true, video::Rotation::k0), 4u);
  SM_EXPECT_EQ(s.LastPacketReduction(false, video::Rotation::k0), 0u);  // Not due.
  SM_EXPECT_EQ(s.LastPacketReduction(false, video::Rotation::k90), 4u);

  sync::ExtensionMap only_cvo;
  only_cvo.Register(5, sync::uri::kVideoOrientation);
  c.extensions = &only_cvo;
  RtpSender bare(c);
  SM_EXPECT_EQ(bare.MediaHeaderSize(), 12u);
  SM_EXPECT_EQ(bare.LastPacketReduction(true, video::Rotation::k0), 8u);  // 4 + (1+1 -> 4).
}

// Absolute capture time (#567 F8.3): RTPSenderVideo::SendVideo asks AbsoluteCaptureTimeSender
// once per frame with the capture time in whole NTP milliseconds and offset 0, and puts the
// result on the frame's first packet (rtp_sender_video.cc:690-707, 425-428); RTPSenderAudio asks
// per packet with the full-precision NTP time (rtp_sender_audio.cc:142-157, 252-256). The sender
// resends after more than 1 s, or when interpolation would be off by more than 1 ms.
namespace {
std::optional<sync::AbsoluteCaptureTime> AbsCapture(const sync::ExtensionMap& map, const SenderPacket& p) {
  auto raw = sync::ExtensionAccessor(map).GetRaw(p.packet, sync::uri::kAbsCaptureTime);
  if (!raw) return std::nullopt;
  return sync::ParseAbsoluteCaptureTime(*raw);
}
}  // namespace

SM_TEST(RtpSender, VideoAbsoluteCaptureTimeOnTheFirstPacketWhenTheReceiverCantInterpolate) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  const Timestamp start = Timestamp::Seconds(1000);
  std::vector<int> carried;
  for (int i = 0; i < 40; ++i) {
    // 30 ms frames (2700 ticks at 90 kHz), captured 10.4 ms before they are sent.
    const Timestamp now = start + TimeDelta::Millis(30 * i);
    const Timestamp capture = now - TimeDelta::Micros(10'400);
    const uint32_t ts = static_cast<uint32_t>(2700 * i);
    TimeDelta skew = i == 20 ? TimeDelta::Millis(5) : TimeDelta::Zero();  // A capture-time jump.
    auto ext = s.VideoAbsoluteCaptureTime(ts, capture + skew, now);
    auto chunks = Chunks({100, 100, 100});
    s.SendVideoFrame(chunks, ts, now, i == 0, video::Rotation::k0, ext);
    auto out = Release(s);
    SM_ASSERT_EQ(out.size(), 3u);
    SM_EXPECT_FALSE(AbsCapture(map, out[1]).has_value());
    SM_EXPECT_FALSE(AbsCapture(map, out[2]).has_value());
    const auto got = AbsCapture(map, out[0]);
    SM_EXPECT_EQ(got.has_value(), ext.has_value());
    if (!got) continue;
    carried.push_back(i);
    // Int64MsToUQ32x32(NtpTime::ToMs()): 10.4 ms rounds to whole milliseconds.
    const int64_t ms = sync::NtpToMs(timing::NtpTime::FromTimestamp(capture + skew).value);
    SM_EXPECT_EQ(got->capture_ntp, sync::Int64MsToUQ32x32(ms));
    SM_EXPECT_TRUE(got->clock_offset == std::optional<int64_t>(0));
  }
  // Frame 0; the jump at frame 20 and frame 21, which interpolates 5 ms off frame 20's; then
  // 1 s after frame 21 (1020 ms later, frame 55, is past the run, so none more).
  SM_EXPECT_TRUE(carried == (std::vector<int>{0, 20, 21}));
  SM_EXPECT_EQ(s.stats().abs_capture_time_packets, 3u);
}

SM_TEST(RtpSender, VideoAbsoluteCaptureTimeResendsAfterOneSecond) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  std::vector<int> sent;
  for (int i = 0; i < 70; ++i) {
    const Timestamp now = Timestamp::Seconds(1000) + TimeDelta::Millis(30 * i);
    if (s.VideoAbsoluteCaptureTime(static_cast<uint32_t>(2700 * i), now, now)) sent.push_back(i);
  }
  // More than kInterpolationMaxInterval (1 s) after the last one: 1020 ms, frames 0, 34, 68.
  SM_EXPECT_TRUE(sent == (std::vector<int>{0, 34, 68}));
  // Without a capture time there is nothing to send.
  SM_EXPECT_FALSE(s.VideoAbsoluteCaptureTime(0, std::nullopt, Timestamp::Seconds(2000)).has_value());
}

SM_TEST(RtpSender, AbsoluteCaptureTimeNeedsTheExtensionMapped) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  const Timestamp now = Timestamp::Seconds(1000);
  // The sender still decides (its state moves, as libwebrtc's), but nothing is written.
  auto ext = s.VideoAbsoluteCaptureTime(0, now, now);
  SM_EXPECT_TRUE(ext.has_value());
  SM_EXPECT_EQ(s.FirstPacketReduction(ext), 0u);
  auto chunks = Chunks({100});
  s.SendVideoFrame(chunks, 0, now, true, video::Rotation::k0, ext);
  auto out = Release(s);
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_FALSE(sync::ExtensionAccessor(map).GetRaw(out[0].packet, sync::uri::kAbsCaptureTime).has_value());
  SM_EXPECT_EQ(s.stats().abs_capture_time_packets, 0u);
}

// first_packet_reduction_len and single_packet_reduction_len (rtp_sender_video.cc:741-758): the
// 16-byte extension (capture time and offset), with the extensions rounded up to 4 bytes.
SM_TEST(RtpSender, FirstPacketReductionIsTheRoomAbsoluteCaptureTimeTakes) {
  sync::ExtensionMap map;
  map.Register(1, sync::uri::kTransportCc);
  map.Register(5, sync::uri::kVideoOrientation);
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpSenderConfig c = VideoCfg();
  c.extensions = &map;
  RtpSender s(c);
  const sync::AbsoluteCaptureTime ext{123, 0};
  // Middle: 12 + 4 + (1+2 -> 4) = 20. First: 12 + 4 + (1+2 + 1+16 = 20) = 36.
  // Single with CVO: 12 + 4 + (20 + 1+1 = 22 -> 24) = 40.
  SM_EXPECT_EQ(s.MediaHeaderSize(), 20u);
  SM_EXPECT_EQ(s.FirstPacketReduction(ext), 16u);
  SM_EXPECT_EQ(s.FirstPacketReduction(std::nullopt), 0u);
  SM_EXPECT_EQ(s.SinglePacketReduction(true, video::Rotation::k0, ext), 20u);
  SM_EXPECT_EQ(s.SinglePacketReduction(false, video::Rotation::k0, ext), 16u);  // CVO not due.
  SM_EXPECT_EQ(s.SinglePacketReduction(true, video::Rotation::k0, std::nullopt), s.LastPacketReduction(true, video::Rotation::k0));
}

SM_TEST(RtpSender, AudioAbsoluteCaptureTimeInFullPrecisionPerPacket) {
  sync::ExtensionMap map;
  map.Register(7, sync::uri::kAbsCaptureTime);
  RtpSenderConfig c;
  c.ssrc = 0x3333;
  c.payload_type = 111;
  c.clock_rate = 48000;
  c.video = false;
  c.extensions = &map;
  RtpSender s(c);
  const Bytes frame(60, 0x5a);
  std::vector<int> carried;
  for (int i = 0; i < 60; ++i) {
    const Timestamp now = Timestamp::Seconds(1000) + TimeDelta::Millis(20 * i);
    const Timestamp capture = now - TimeDelta::Micros(4'321);  // Not whole milliseconds.
    s.SendAudio(frame, static_cast<uint32_t>(960 * i), false, now, std::nullopt, capture);
    auto out = Release(s);
    SM_ASSERT_EQ(out.size(), 1u);
    const auto got = AbsCapture(map, out[0]);
    if (!got) continue;
    carried.push_back(i);
    SM_EXPECT_EQ(got->capture_ntp, timing::NtpTime::FromTimestamp(capture).value);
    SM_EXPECT_TRUE(got->clock_offset == std::optional<int64_t>(0));
  }
  // Every 20 ms packet interpolates exactly; it is resent past 1 s: packets 0 and 51 (1020 ms).
  SM_EXPECT_TRUE(carried == (std::vector<int>{0, 51}));
  // Without a capture time the packet carries nothing and the sender is not asked.
  s.SendAudio(frame, 960 * 60, false, Timestamp::Seconds(1010), std::nullopt);
  auto out = Release(s);
  SM_EXPECT_FALSE(AbsCapture(map, out[0]).has_value());
}

// Issue #816: the SR packet and octet counts are libwebrtc's `transmitted` counters of the media and
// RTX SSRCs (ModuleRtpRtcpImpl2::GetFeedbackState, rtp_rtcp_impl2.cc:251-254), which
// RtpSenderEgress::UpdateRtpStats adds to as each packet is sent (rtp_sender_egress.cc:332-336,
// 467-493): media, retransmissions and padding, with the payload octets (RtpPacketCounter keeps the
// padding bytes apart). A packet still in the pacer queue is not counted.
SM_TEST(RtpSender, SenderReportCountsEveryPacketSentOnTheMediaAndRtxSsrcs) {
  const auto ext = TccExtensions();
  RtpSender s(PaddingCfg(&ext));
  s.SendVideoFrame(Chunks({100, 200}), 1, Timestamp::Millis(0));
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(0)).packet_count, 0u);  // Queued, not sent.
  auto media = Release(s);
  SM_ASSERT_EQ(media.size(), 2u);
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(0)).packet_count, 2u);
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(0)).octet_count, 300u);
  // A resend: its payload is the original's plus the 2-byte original sequence number.
  s.OnNack(std::vector<uint16_t>{media[1].packet.seq}, Timestamp::Millis(50), TimeDelta::Millis(10));
  auto rtx = Release(s, Timestamp::Millis(50));
  SM_ASSERT_EQ(rtx.size(), 1u);
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(50)).packet_count, 3u);
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(50)).octet_count, 502u);
  // Empty padding: one packet each, no payload octets.
  auto padding = s.GeneratePadding(60, Timestamp::Millis(60));
  SM_ASSERT_FALSE(padding.empty());
  for (auto& p : padding) {
    SM_ASSERT_TRUE(p.packet.payload.empty());
    s.AssignSequenceNumber(p, Timestamp::Millis(60));
    s.OnPacketReleased(p, Timestamp::Millis(60));
  }
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(60)).packet_count, 3u + padding.size());
  SM_EXPECT_EQ(s.SenderState(Timestamp::Millis(60)).octet_count, 502u);
}

// Issue #816: padding on the RTX SSRC takes rtx_payload_type_map_.begin()->second
// (RTPSender::BuildPaddingPacket, rtp_sender.cc:497-502), the RTX payload type of the lowest
// associated payload type. With sm_compare's types (video 125, RED 118 -> RTX 99) that is RED's.
SM_TEST(RtpSender, RtxPaddingTakesTheRtxTypeOfTheLowestAssociatedPayloadType) {
  const auto ext = TccExtensions();
  RtpSenderConfig c = PaddingCfg(&ext);
  c.payload_type = 125;
  c.rtx_payload_type = 98;
  c.fec = true;
  c.ulpfec.red_payload_type = 118;
  c.ulpfec.red_rtx_payload_type = 99;
  RtpSender lower_red(c);
  auto out = lower_red.GeneratePadding(100, Timestamp::Millis(5));
  SM_ASSERT_FALSE(out.empty());
  SM_EXPECT_EQ(out[0].packet.ssrc, 0x2222u);
  SM_EXPECT_EQ(out[0].packet.payload_type, 99);
  // RED in the RTX map without FEC running (ConfigureRtx maps it whenever RED is configured, also
  // when ShouldDisableRedAndUlpfec turns FEC off beside NACK).
  c.fec = false;
  c.rtx.red_payload_type = 118;
  c.rtx.rtx_red_payload_type = 99;
  RtpSender red_mapped(c);
  out = red_mapped.GeneratePadding(100, Timestamp::Millis(5));
  SM_ASSERT_FALSE(out.empty());
  SM_EXPECT_EQ(out[0].packet.payload_type, 99);
  // The media type is the lower one: its RTX type.
  c.payload_type = 96;
  c.rtx_payload_type = 97;
  RtpSender lower_media(c);
  out = lower_media.GeneratePadding(100, Timestamp::Millis(5));
  SM_ASSERT_FALSE(out.empty());
  SM_EXPECT_EQ(out[0].packet.payload_type, 97);
}

// Issue #895: ModuleRtpRtcpImpl2::GetFeedbackState reads GetSendRates(now) only when an RTCP packet is
// built, and RateStatistics::Rate erases the expired buckets. SenderState(now, false) keeps the last
// read rate and erases nothing, so an audio stream that resumes within 1.5 windows keeps its window;
// a read in the gap empties the tracker, and the window restarts at the next packet.
namespace {
int64_t AudioRateAfterGap(bool read_in_gap) {
  RtpSenderConfig c;
  c.ssrc = 5;
  c.payload_type = 111;
  c.video = false;
  c.clock_rate = 48000;
  RtpSender s(c);
  const Bytes frame(88, 0x11);  // 100 bytes with the 12-byte header.
  auto send = [&](uint32_t ts, int ms) {
    s.SendAudio(frame, ts, false, Timestamp::Millis(ms));
    Release(s, Timestamp::Millis(ms));
  };
  send(960, 0);
  send(1920, 10);
  SM_EXPECT_GT(s.SenderState(Timestamp::Millis(20)).send_bitrate->bps(), 0);
  const DataRate before = *s.SenderState(Timestamp::Millis(1100), read_in_gap).send_bitrate;
  if (!read_in_gap) SM_EXPECT_EQ(before.bps(), s.SenderState(Timestamp::Millis(20), false).send_bitrate->bps());
  send(2880, 1200);
  send(3840, 1210);
  return s.SendRate(Timestamp::Millis(1210)).bps();
}
}  // namespace

SM_TEST(RtpSender, SenderStateReadsTheSendRatesOnlyWhenAsked) {
  // Kept window: the two new packets over the full second.
  SM_EXPECT_EQ(AudioRateAfterGap(false), 1600);
  // Restarted at 1200 ms: 200 bytes over 11 ms.
  SM_EXPECT_EQ(AudioRateAfterGap(true), 145455);
}

// Issue #895: a video module reads its send rates for every packet it sends (UpdateRtpStats calls
// GetSendRates(now) for the stream's SendStatisticsProxy, rtp_sender_egress.cc:495-498), so a packet
// type that paused for over a window restarts its window when it resumes, even within 1.5 windows.
SM_TEST(RtpSender, VideoModuleRestartsAPausedTypeAtItsNextPacket) {
  RtpSender s(VideoCfg());
  uint32_t ts = 1;
  for (int t = 0; t <= 1200; t += 10) {
    s.SendVideoFrame(Chunks({100}), ts += 3000, Timestamp::Millis(t));
    auto media = Release(s, Timestamp::Millis(t));
    if (t == 0 || t == 1200) {
      // Two resends: the first at 0 ms, the next 1.2 s later.
      s.OnNack(std::vector<uint16_t>{media[0].packet.seq}, Timestamp::Millis(t), TimeDelta::Millis(10));
      Release(s, Timestamp::Millis(t));
      s.OnNack(std::vector<uint16_t>{media[0].packet.seq}, Timestamp::Millis(t + 1), TimeDelta::Zero());
      Release(s, Timestamp::Millis(t + 1));
    }
  }
  // Video: 100 packets in the window (210-1200 ms) at 112 bytes. RTX restarted at 1200 ms: two
  // 114-byte packets over 2 ms. Without the per-packet read RTX would count over the whole second.
  const int64_t video = 100 * 112 * 8;
  SM_EXPECT_EQ(s.SendRate(Timestamp::Millis(1201)).bps(), video + 2 * 114 * 8000 / 2);
}
