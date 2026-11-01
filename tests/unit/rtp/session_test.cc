#include <algorithm>
#include <cstdlib>
#include <map>
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
  explicit Pair(SessionConfig ca = Cfg(1)) : a(ca), b(Cfg(2)) {
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

// Issue #820: the sending session's SRs carry its CNAME in SDES, as RTCPSender
// with the CNAME RtpVideoSender and ChannelSend set (rtcp_sender.cc:713). The
// receiving session's RRs carry no SDES, as libwebrtc's receive-only modules
// have no CNAME (CreateRtpRtcpModule, video/rtp_video_stream_receiver2.cc:124-158).
SM_TEST(Session, SenderReportsCarryTheCnameAndReceiveOnlyReportsCarryNoSdes) {
  Pair p;
  Bytes frame(60, 7);
  size_t sr = 0, rr = 0;
  bool sr_sdes = true, rr_sdes = false;
  auto check = [&](const Bytes& d) {
    auto split = loss::SplitCompoundRtcp(d);
    if (!split || split->empty()) return;
    bool has_sdes = false;
    for (const auto& v : *split) {
      if (v.type != loss::RtcpType::kSdes) continue;
      has_sdes = true;
      auto sdes = ParseSdes(v.data);
      if (!sdes || sdes->empty() || (*sdes)[0].items.empty() || (*sdes)[0].items[0].text != "test") sr_sdes = false;
    }
    if ((*split)[0].type == loss::RtcpType::kSenderReport) {
      ++sr;
      sr_sdes = sr_sdes && has_sdes;
    } else if ((*split)[0].type == loss::RtcpType::kReceiverReport) {
      ++rr;
      rr_sdes = rr_sdes || has_sdes;
    }
  };
  for (int64_t t = 0; t < 6000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 20 == 0) p.a.SendAudio(p.a_audio, {960, frame, false}, now);
    if (t % 100 == 0) p.a.SendVideoChunks(p.a_video, test::MakeTestFrameChunks(t == 0, 3000, 1188), static_cast<uint32_t>(t * 90), now);
    p.a.Process(now);
    p.b.Process(now);
    for (auto& o : p.a.TakeOutgoing()) {
      if (o.kind == OutgoingKind::kRtcp) check(o.bytes);
      p.b.OnPacket(o.bytes, now);
    }
    for (auto& o : p.b.TakeOutgoing()) {
      if (o.kind == OutgoingKind::kRtcp) check(o.bytes);
      p.a.OnPacket(o.bytes, now);
    }
  }
  SM_EXPECT_GT(sr, 0u);
  SM_EXPECT_GT(rr, 0u);
  SM_EXPECT_TRUE(sr_sdes);
  SM_EXPECT_FALSE(rr_sdes);
}

// Issue #518: a host that runs the session at NextProcessTime runs each timer at its due time, as
// libwebrtc's delayed tasks run. With a video receiver that is no longer "at most 1 ms away": the
// receiver reports its frame release, decode time-out and NACK tick, and a packet asks for a
// Process at once.
SM_TEST(Session, NextProcessTimeWithAVideoReceiverIsItsEarliestTimer) {
  Pair p;
  p.a.SendVideo(p.a_video, {0, MakeFakeH264Frame(true, 3000), true}, Timestamp::Millis(0));
  const std::vector<OutgoingPacket> sent = Pump(p.a, 0, 100);
  p.b.Process(Timestamp::Millis(0));
  SM_EXPECT_GT(p.b.NextProcessTime(Timestamp::Millis(0)), Timestamp::Millis(1));
  // Every packet at 30.4 ms: a Process is due at once, and after it the receiver waits again.
  const Timestamp at = Timestamp::Micros(30400);
  for (const auto& o : sent) {
    if (o.kind == OutgoingKind::kVideo) p.b.OnPacket(o.bytes, at);
  }
  SM_EXPECT_EQ(p.b.NextProcessTime(at), at);
  p.b.Process(at);
  SM_EXPECT_EQ(p.b.TakeVideoFrames(p.b_video).size(), 1u);  // The keyframe, released at once.
  const Timestamp next = p.b.NextProcessTime(at);
  SM_EXPECT_GT(next, at + TimeDelta::Millis(1));
  // The receiver's own timer is among them: with no frame after the keyframe, its 3 s decode
  // time-out (kMaxWaitForFrame) runs from the release.
  SM_EXPECT_LE(next, at + TimeDelta::Seconds(3));
}

// Issue #821: `av_sync` puts the session's receive streams in one sync group, paired as
// Call::ConfigureSync (call/call.cc:1300-1341) pairs them: none without it (an empty sync group),
// the first video stream with the first audio stream once both exist, whichever came first, and a
// later stream changes nothing (RtpStreamsSynchronizer::ConfigureSync with the same audio is a no-op).
SM_TEST(Session, AvSyncPairsTheFirstAudioAndVideoReceiveStreams) {
  NullDecoder dec;
  AudioStreamConfig au;
  au.ssrc = 0x100;
  Session off(Cfg(2));
  off.AddAudioReceive(au, &dec);
  off.AddVideoReceive(Video());
  SM_EXPECT_TRUE(off.av_sync() == nullptr);

  SessionConfig c = Cfg(2);
  c.av_sync = true;
  Session s(c);
  s.AddVideoReceive(Video());
  SM_EXPECT_TRUE(s.av_sync() == nullptr);  // SetSync(nullptr): no audio in the group yet.
  s.AddAudioReceive(au, &dec);
  const sync::RtpStreamsSynchronizer* sync = s.av_sync();
  SM_ASSERT_TRUE(sync != nullptr);
  VideoStreamConfig second = Video();
  second.ssrc = 0x400;
  second.rtx_ssrc = 0x500;
  s.AddVideoReceive(second);
  AudioStreamConfig au2;
  au2.ssrc = 0x600;
  s.AddAudioReceive(au2, &dec);
  SM_EXPECT_TRUE(s.av_sync() == sync);
}

SM_TEST(Session, ZeroByteVideoFrameSendsNothingAndIsCounted) {
  Pair p;
  p.a.SendVideo(p.a_video, {0, {}}, Timestamp::Millis(0));
  SM_EXPECT_EQ(p.a.stats().empty_video_frames, 1u);
  SM_EXPECT_EQ(p.a.video_sender(p.a_video).stats().media_packets, 0u);
  p.a.SendVideoChunks(p.a_video, {}, 0, Timestamp::Millis(0));
  SM_EXPECT_EQ(p.a.stats().empty_video_frames, 2u);
}

// Issue #655: SendVideo returns the size of the access unit it sent, after the keyframe SPS VUI
// rewrite (VideoStreamEncoder::AugmentEncodedImage), which libwebrtc's RunPostEncode gives the frame
// dropper and the bitrate adjuster (video_stream_encoder.cc:2387-2396, 2638-2652).
SM_TEST(Session, SendVideoReturnsTheSizeAfterTheSpsRewrite) {
  Pair p;
  const Bytes key = MakeFakeH264Frame(true, 3000);
  const DataSize k = p.a.SendVideo(p.a_video, {0, key, true}, Timestamp::Millis(0));
  SM_EXPECT_EQ(k.bytes(), static_cast<int64_t>(ParseOutgoingBitstreamAndRewrite(key).size()));
  SM_EXPECT_GT(k.bytes(), int64_t{3000});  // The VUI the rewrite adds.
  const DataSize d = p.a.SendVideo(p.a_video, {3000, MakeFakeH264Frame(false, 1000), false}, Timestamp::Millis(33));
  SM_EXPECT_EQ(d.bytes(), int64_t{1000});  // A delta frame is sent as it is.
  SM_EXPECT_TRUE(p.a.SendVideo(p.a_video, {6000, {}}, Timestamp::Millis(66)).bytes() == 0);
  SM_EXPECT_TRUE(p.a.SendVideo(VideoSendId{7}, {6000, MakeFakeH264Frame(false, 1000)}, Timestamp::Millis(66)).bytes() == 0);
}

// Issue #655: VideoStreamConfig::h264_packetization_mode 0 reaches the packetizer: each NAL unit is
// its own packet, and a frame with a unit larger than a packet is not sent.
SM_TEST(Session, H264PacketizationModeZeroSendsSingleNalUnits) {
  Session a(Cfg(1));
  VideoStreamConfig v = Video();
  v.h264_packetization_mode = H264PacketizationMode::kSingleNalUnit;
  const VideoSendId id = a.AddVideoSend(v);
  a.SendVideo(id, {0, MakeFakeH264Frame(true, 500), true}, Timestamp::Millis(0));
  std::vector<int> types;
  for (auto& o : Pump(a, 0, 100)) {
    if (o.kind == OutgoingKind::kVideo) types.push_back(RtpPacket::Parse(o.bytes)->payload[0] & 0x1F);
  }
  SM_EXPECT_TRUE((types == std::vector<int>{kH264NalSps, kH264NalPps, kH264NalIdr}));
  a.SendVideo(id, {3000, MakeFakeH264Frame(false, 5000), false}, Timestamp::Millis(100));
  size_t video = 0;
  for (auto& o : Pump(a, 100, 100)) video += o.kind == OutgoingKind::kVideo;
  SM_EXPECT_EQ(video, 0u);
  SM_EXPECT_EQ(a.stats().empty_video_frames, 1u);
}

SM_TEST(Session, SendVideoPacketizesAnAnnexBFrameWithinTheMtu) {
  Pair p;
  const Bytes frame = MakeFakeH264Frame(true, 5000);
  p.a.SendVideo(p.a_video, {0, frame, true}, Timestamp::Millis(0));
  std::vector<RtpPacket> sent;
  for (auto& o : Pump(p.a, 0, 500)) {
    if (o.kind != OutgoingKind::kVideo) continue;
    SM_EXPECT_LE(o.bytes.size(), 1460u);  // ModuleRtpRtcpImpl2's default (#580).
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
  p.a.SendVideo(p.a_video, {3000, MakeFakeH264Frame(false, 1448 * 3)}, Timestamp::Millis(500));
  for (auto& o : Pump(p.a, 500, 500)) {
    if (o.kind == OutgoingKind::kVideo) SM_EXPECT_LE(o.bytes.size(), 1460u);
  }
}

// rtp_sender_video.cc:676-687 (issue #513): a frame that may be retransmitted leaves
// RtxPacketOverhead (2 bytes, the RTX header) of the whole-packet limit for the RTX resend, so a
// stream with NACK and RTX sends packets of at most 1458 bytes of a 1460-byte limit, as libwebrtc's
// packets in sm_compare; with NACK off libwebrtc's stream has no RTX and reserves nothing.
SM_TEST(Session, VideoPacketsLeaveRoomForTheRtxHeaderWhenTheyMayBeRetransmitted) {
  for (bool nack : {true, false}) {
    Session a(Cfg(1));
    VideoStreamConfig v = Video();
    v.nack = nack;
    v.max_payload_size = 1460 - 12;
    const VideoSendId id = a.AddVideoSend(v);
    SM_EXPECT_EQ(a.video_sender(id).RtxPacketOverhead(), 2u);
    // Frames around the limit: the largest that still goes as one packet fills the capacity.
    size_t largest = 0;
    for (int i = 0; i < 100; ++i) {
      a.SendVideo(id, {static_cast<uint32_t>(3000 * i), MakeFakeH264Frame(false, 1380 + static_cast<size_t>(i))},
                  Timestamp::Millis(100 * i));
      for (auto& o : Pump(a, 100 * i, 100)) {
        if (o.kind == OutgoingKind::kVideo) largest = std::max(largest, o.bytes.size());
      }
    }
    SM_EXPECT_EQ(largest, nack ? 1458u : 1460u);
  }
}

// CVO (#567): with the video-orientation extension mapped, the last packet of a rotated frame
// carries it and still fits the whole-packet limit, because the packetizer leaves the room as
// last_packet_reduction_len (rtp_sender_video.cc:741-758).
SM_TEST(Session, RotatedFramesCarryCvoWithinThePacketLimit) {
  SessionConfig ca = Cfg(1);
  ca.extensions.Register(5, sync::uri::kVideoOrientation);
  Session a(ca);
  VideoStreamConfig v = Video();
  v.nack = false;
  v.max_payload_size = 1460 - 12;
  const VideoSendId id = a.AddVideoSend(v);
  size_t largest = 0, cvo = 0;
  auto check = [&](std::vector<OutgoingPacket> out) {
    for (auto& o : out) {
      if (o.kind != OutgoingKind::kVideo) continue;
      largest = std::max(largest, o.bytes.size());
      const auto pkt = RtpPacket::Parse(o.bytes);
      const auto raw = sync::ExtensionAccessor(ca.extensions).GetRaw(*pkt, sync::uri::kVideoOrientation);
      if (!raw) continue;
      ++cvo;
      SM_EXPECT_TRUE(pkt->marker);
      SM_EXPECT_TRUE(*raw == Bytes{1});
    }
  };
  // Frames around the limit, as VideoPacketsLeaveRoomForTheRtxHeaderWhenTheyMayBeRetransmitted.
  for (int i = 0; i < 100; ++i) {
    EncodedVideoFrame f;
    const Bytes data = MakeFakeH264Frame(false, 1380 + static_cast<size_t>(i));
    f.rtp_timestamp = static_cast<uint32_t>(3000 * i);
    f.data = data;
    f.rotation = video::Rotation::k90;
    a.SendVideo(id, f, Timestamp::Millis(100 * i));
    check(Pump(a, 100 * i, 100));
  }
  check(Pump(a, 10000, 5000));
  SM_EXPECT_EQ(largest, 1460u);
  SM_EXPECT_EQ(cvo, 100u);
  SM_EXPECT_EQ(a.video_sender(id).stats().video_orientation_packets, 100u);
}

// Absolute capture time (#567 F8.3): with the extension mapped and a capture time on the frame,
// the frame's first packet carries it when AbsoluteCaptureTimeSender decides to send it, and the
// packetizer leaves the room as first_packet_reduction_len (rtp_sender_video.cc:690-758), so
// every packet still fits the whole-packet limit. Audio carries it per packet.
SM_TEST(Session, CapturedFramesCarryAbsoluteCaptureTimeWithinThePacketLimit) {
  SessionConfig ca = Cfg(1);
  ca.extensions.Register(5, sync::uri::kVideoOrientation);
  ca.extensions.Register(7, sync::uri::kAbsCaptureTime);
  Session a(ca);
  VideoStreamConfig v = Video();
  v.nack = false;
  v.max_payload_size = 1460 - 12;
  const VideoSendId id = a.AddVideoSend(v);
  size_t largest = 0, carried = 0;
  auto check = [&](std::vector<OutgoingPacket> out) {
    for (auto& o : out) {
      if (o.kind != OutgoingKind::kVideo) continue;
      largest = std::max(largest, o.bytes.size());
      const auto pkt = RtpPacket::Parse(o.bytes);
      const auto raw = sync::ExtensionAccessor(ca.extensions).GetRaw(*pkt, sync::uri::kAbsCaptureTime);
      if (!raw) continue;
      ++carried;
      const auto ext = sync::ParseAbsoluteCaptureTime(*raw);
      SM_ASSERT_TRUE(ext.has_value());
      SM_EXPECT_TRUE(ext->clock_offset == std::optional<int64_t>(0));
    }
  };
  // Frames around the limit, one every 2 s so each one carries it, rotated so that a single
  // packet also carries CVO.
  for (int i = 0; i < 100; ++i) {
    EncodedVideoFrame f;
    const Bytes data = MakeFakeH264Frame(false, 1380 + static_cast<size_t>(i));
    const Timestamp now = Timestamp::Seconds(2 * i);
    f.rtp_timestamp = static_cast<uint32_t>(180000 * i);
    f.data = data;
    f.rotation = video::Rotation::k90;
    f.capture_time = now - TimeDelta::Millis(15);
    a.SendVideo(id, f, now);
    check(Pump(a, 2000 * i, 2000));
  }
  SM_EXPECT_EQ(largest, 1460u);
  SM_EXPECT_EQ(carried, 100u);
  SM_EXPECT_EQ(a.video_sender(id).stats().abs_capture_time_packets, 100u);

  // Audio: the first packet, then none while the receiver can interpolate.
  Session b(ca);
  AudioStreamConfig acfg;
  acfg.ssrc = 0x100;
  const AudioSendId au = b.AddAudioSend(acfg);
  const Bytes opus(60, 1);
  for (int i = 0; i < 10; ++i) {
    const Timestamp now = Timestamp::Seconds(300) + TimeDelta::Millis(20 * i);
    EncodedAudioFrame f;
    f.rtp_timestamp = static_cast<uint32_t>(960 * i);
    f.data = opus;
    f.capture_time = now;
    b.SendAudio(au, f, now);
  }
  SM_EXPECT_EQ(b.audio_sender(au).stats().abs_capture_time_packets, 1u);
}

// #567 F6.9: the session computes the RFC 6464 level from the capture frames it is given, as
// ChannelSend (channel_send.cc:389-396, 909-924), and writes it on every audio packet.
SM_TEST(Session, AudioPacketsCarryTheLevelOfTheCaptureFrames) {
  SessionConfig ca = Cfg(1);
  ca.extensions.Register(4, sync::uri::kAudioLevel);
  Session a(ca);
  AudioStreamConfig acfg;
  acfg.ssrc = 0x100;
  const AudioSendId au = a.AddAudioSend(acfg);
  const std::vector<int16_t> pcm(480, 1000);
  const Bytes opus(60, 1);
  std::vector<int> levels;
  for (int i = 0; i < 5; ++i) {
    a.AnalyzeAudioCapture(au, pcm, /*muted=*/i >= 3);
    a.AnalyzeAudioCapture(au, pcm, /*muted=*/i >= 3);
    a.SendAudio(au, {static_cast<uint32_t>(960 * i), opus, false}, Timestamp::Millis(20 * i));
    for (auto& o : Pump(a, 20 * i, 20)) {
      if (o.kind != OutgoingKind::kAudio) continue;
      const auto raw = sync::ExtensionAccessor(ca.extensions).GetRaw(*RtpPacket::Parse(o.bytes), sync::uri::kAudioLevel);
      levels.push_back(raw ? (*raw)[0] : -1);
    }
  }
  // Packet 4: the first muted frame is analyzed as given (the caller's faded frame), the next one
  // counts as zeros, so the mean square halves (30 -> 33). Packet 5: muted throughout, 127.
  SM_EXPECT_TRUE(levels == (std::vector<int>{0x80 | 30, 0x80 | 30, 0x80 | 30, 0x80 | 33, 0x80 | 127}));
  SM_EXPECT_EQ(a.audio_sender(au).stats().audio_level_packets, 5u);
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

// Issue #860: transport-cc goes out through SendCombinedRtcpPacket, a datagram of its own, and
// ReceiveSideCongestionController's task runs before a NACK sent at the same time (it was posted
// first), so the sender's early-loss resends come before the NACK's, as libwebrtc's.
SM_TEST(Session, TransportFeedbackLeavesInItsOwnDatagramAheadOfANackDueAtTheSameTime) {
  Pair p;
  auto chunks = test::MakeTestFrameChunks(true, 3000, 1188);
  p.a.SendVideoChunks(p.a_video, chunks, 0, Timestamp::Millis(0));
  std::vector<Bytes> video;
  for (auto& o : Pump(p.a, 0, 300)) {
    if (o.kind == OutgoingKind::kVideo) video.push_back(std::move(o.bytes));
  }
  SM_ASSERT_GE(video.size(), 3u);
  for (int64_t t = 0; t < 100; ++t) {
    p.b.Process(Timestamp::Millis(t));
    p.b.TakeOutgoing();
  }
  // The first transport feedback is due at 100 ms; the packet after a lost one arrives then too.
  const Timestamp now = Timestamp::Millis(100);
  for (size_t i = 0; i < video.size(); ++i) {
    if (i != 1) p.b.OnPacket(video[i], now);
  }
  p.b.Process(now);
  std::vector<std::vector<loss::RtcpType>> datagrams;
  for (auto& o : p.b.TakeOutgoing()) {
    if (o.kind != OutgoingKind::kRtcp) continue;
    auto split = loss::SplitCompoundRtcp(o.bytes);
    SM_ASSERT_TRUE(split.has_value());
    std::vector<loss::RtcpType> types;
    for (const auto& q : *split) types.push_back(q.type);
    datagrams.push_back(types);
  }
  const auto has = [](const std::vector<loss::RtcpType>& t, loss::RtcpType x) {
    return std::find(t.begin(), t.end(), x) != t.end();
  };
  SM_ASSERT_EQ(datagrams.size(), 2u);
  SM_EXPECT_TRUE(has(datagrams[0], loss::RtcpType::kTransportCc));
  SM_EXPECT_FALSE(has(datagrams[0], loss::RtcpType::kNack));
  SM_EXPECT_TRUE(has(datagrams[1], loss::RtcpType::kNack));
  SM_EXPECT_FALSE(has(datagrams[1], loss::RtcpType::kTransportCc));
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

// Each send stream has its own RTCPReceiver::RttStats (rtcp_receiver.cc:211-229, 567-569, issue
// #568): only the block for the stream's media SSRC feeds it, so the RTX block's RTT and the other
// stream's do not reach the stream's periodic update (rtp_rtcp_impl2.cc:740-751), its packet
// history or its NACK RTT (rtp_rtcp_impl2.cc:678-692). The RTCP packet's RTT, the last block for
// the stream's media or RTX SSRC, goes to GoogCC once per stream, video first, then audio
// (rtcp_receiver.cc:1010-1012, call.cc:1357-1381).
SM_TEST(Session, RttStatsArePerSendStreamAndFromTheMediaSsrcOnly) {
  SessionConfig ca = Cfg(1);
  ca.per_stream_rtt = true;
  Pair p(ca);
  Pump(p.a, 0, 400);
  auto rr = [&](int64_t ms, int64_t video_rtt, int64_t rtx_rtt, int64_t audio_rtt) {
    const uint32_t a = timing::NtpTime::FromTimestamp(Timestamp::Millis(ms)).Compact();
    auto lsr = [&](int64_t rtt_ms) { return a - static_cast<uint32_t>((rtt_ms * 65536 + 999) / 1000); };
    timing::ReceiverReport r;
    r.ssrc = 2;
    r.blocks = {{.ssrc = 0x200, .extended_highest_seq = 10, .last_sr = lsr(video_rtt)},
                {.ssrc = 0x300, .extended_highest_seq = 10, .last_sr = lsr(rtx_rtt)},
                {.ssrc = 0x100, .extended_highest_seq = 10, .last_sr = lsr(audio_rtt)}};
    p.a.OnPacket(timing::SerializeReceiverReport(r), Timestamp::Millis(ms));
  };
  rr(400, 80, 300, 200);
  rr(420, 60, 300, 200);
  // GoogCC: the video stream's packet RTT (its RTX block, 300 ms), then the audio stream's.
  SM_EXPECT_EQ(p.a.send_side().gcc().last_rtt().ms(), 200);
  // Before the first periodic update a NACK takes the average of the stream's own samples:
  // 5 + (80 + 60) / 2.
  const loss::RtxSender& video_history = p.a.video_sender(p.a_video).rtx();
  const loss::RtxSender& audio_history = p.a.audio_sender(p.a_audio).rtx();
  RtcpConfig rc;
  rc.local_ssrc = 2;
  RtcpEngine peer(rc);
  peer.QueueNack(0x200, std::vector<uint16_t>{1});
  for (auto& r : peer.Process(Timestamp::Millis(430))) p.a.OnPacket(r.bytes, Timestamp::Millis(430));
  SM_EXPECT_EQ(video_history.rtt().ms(), 75);
  // The update at 1000 ms: each stream's last media-SSRC sample.
  Pump(p.a, 431, 600);
  SM_EXPECT_EQ(video_history.rtt().ms(), 60);
  SM_EXPECT_EQ(audio_history.rtt().ms(), 200);
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

// RtpVideoSender::OnBitrateUpdated (rtp_video_sender.cc:869-882, issue #577): with
// expected_packet_overhead the video overhead rate counts the senders' ExpectedPerPacketOverhead,
// 22 B with transport-cc and RTX (as sm_compare's libwebrtc sender), 20 B when NACK is off (no RTX
// stream in libwebrtc); off, the fixed 24 B.
SM_TEST(Session, ExpectedPacketOverheadIsTheVideoSendersHeader) {
  SessionConfig c;
  c.local_ssrc = 1;
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(1000);
  auto target = [&](bool on, bool nack) {
    SessionConfig sc = c;
    sc.send_side.expected_packet_overhead = on;
    Session s(sc);
    VideoStreamConfig v = Video();
    v.nack = nack;
    s.AddVideoSend(v);
    s.Process(Timestamp::Millis(0));
    return std::pair{s.send_side().rtp_overhead_per_packet(), s.rates().video_target.bps()};
  };
  const auto [off_bytes, off_rate] = target(false, true);
  const auto [nack_bytes, nack_rate] = target(true, true);
  const auto [plain_bytes, plain_rate] = target(true, false);
  SM_EXPECT_EQ(off_bytes, 24);
  SM_EXPECT_EQ(nack_bytes, 22);
  SM_EXPECT_EQ(plain_bytes, 20);
  // A smaller header leaves the encoder more of the same allocation.
  SM_EXPECT_GT(nack_rate, off_rate);
  SM_EXPECT_GE(plain_rate, nack_rate);
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

// AudioSendStream::GetMinMaxBitrateConstraints and ConfigureBitrateObserver (issue #569): a
// 32 kbps Opus stream in 20 ms frames registers 32000 + 48 B / 20 ms = 51200 as its min and max
// (28 B transport + the 20 B header with transport-cc), and a priority rate of the overhead
// alone, 19200 (AudioAllocationConfig::priority_bitrate 0 + overhead / longest frame).
SM_TEST(Session, AudioRegistersLibwebrtcsMinMaxAndPriorityRate) {
  SessionConfig c = Cfg(1);
  c.mid_audio.clear();
  Session s(c);
  AudioStreamConfig a;
  a.ssrc = 1;
  a.nominal_rate = DataRate::KilobitsPerSec(48);
  a.min_bitrate = a.max_bitrate = DataRate::BitsPerSec(32000);
  s.AddAudioSend(a);
  s.Process(Timestamp::Millis(0));
  const auto cfg = s.send_side().allocator().config("audio");
  SM_ASSERT_TRUE(cfg.has_value());
  SM_EXPECT_EQ(cfg->min.bps(), 51200);
  SM_EXPECT_EQ(cfg->max.bps(), 51200);
  SM_EXPECT_EQ(cfg->priority_rate.bps(), 19200);
  SM_EXPECT_TRUE(cfg->enforce_min);
}

// A frame length range (Opus with its network adaptor, 20-120 ms) spreads the overhead: the min
// adds it at the longest frame, the max at the shortest, and the priority rate at the longest.
SM_TEST(Session, AudioFrameLengthRangeSetsTheOverheadOfMinMaxAndPriority) {
  SessionConfig c = Cfg(1);
  c.mid_audio.clear();
  Session s(c);
  AudioStreamConfig a;
  a.ssrc = 1;
  a.min_bitrate = DataRate::BitsPerSec(6000);
  a.max_bitrate = DataRate::BitsPerSec(32000);
  a.min_frame_length = TimeDelta::Millis(20);
  a.max_frame_length = TimeDelta::Millis(120);
  a.priority_bitrate = DataRate::BitsPerSec(1000);
  s.AddAudioSend(a);
  s.Process(Timestamp::Millis(0));
  const auto cfg = s.send_side().allocator().config("audio");
  SM_ASSERT_TRUE(cfg.has_value());
  SM_EXPECT_EQ(cfg->min.bps(), 6000 + 48 * 8 * 1000 / 120);
  SM_EXPECT_EQ(cfg->max.bps(), 32000 + 48 * 8 * 1000 / 20);
  SM_EXPECT_EQ(cfg->priority_rate.bps(), 1000 + 48 * 8 * 1000 / 120);
}

// Without a codec range the stream keeps the fixed rate it was given.
SM_TEST(Session, AudioWithoutARangeRegistersItsFixedRate) {
  Session s(Cfg(1));
  AudioStreamConfig a;
  a.ssrc = 1;
  a.nominal_rate = DataRate::KilobitsPerSec(40);
  s.AddAudioSend(a);
  s.Process(Timestamp::Millis(0));
  const auto cfg = s.send_side().allocator().config("audio");
  SM_ASSERT_TRUE(cfg.has_value());
  SM_EXPECT_EQ(cfg->min.bps(), 40000);
  SM_EXPECT_EQ(cfg->max.bps(), 40000);
  SM_EXPECT_EQ(cfg->priority_rate.bps(), 40000);
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
    // The RTX SSRC (0x300) is reported once it has received a packet (RtxReceiveStream counts it
    // in the receive statistics, video_receive_stream2.cc:313-316; issue #555).
    SM_EXPECT_TRUE(e.ssrc == 0x100 || e.ssrc == 0x200 || e.ssrc == 0x300);
    SM_EXPECT_EQ(e.loss_fraction, 0.0);
  }
  SM_EXPECT_GE(blocks, 2u);  // One receiver report: an audio and a video block.
}

// ModuleRtpRtcpImpl2::OnReceivedRtcpReportBlocks (rtp_rtcp_impl2.cc:694-713, issue #555): a report
// block for the media SSRC acks it, one for the RTX SSRC acks that, and RtxPacketOverhead keeps
// 3 more bytes only in between.
SM_TEST(Session, ReportBlocksAckTheMediaAndRtxSsrcs) {
  Pair p;
  const RtpSender& s = p.a.video_sender(p.a_video);
  SM_EXPECT_FALSE(s.ssrc_has_acked());
  SM_EXPECT_EQ(s.RtxPacketOverhead(), 2u);
  auto events = Exchange(p, 1600);
  bool rtx_block = false;
  for (const auto& e : events) rtx_block |= e.type == SessionEventType::kReportBlock && e.ssrc == 0x300;
  SM_EXPECT_TRUE(s.ssrc_has_acked());
  SM_EXPECT_EQ(s.rtx_ssrc_has_acked(), rtx_block);
  SM_EXPECT_EQ(s.RtxPacketOverhead(), rtx_block ? 2u : 5u);
}

// Call::DeliverRtcpPacket_w and RtpTransportControllerSend::OnReport (issue #555): each send stream
// passes only the blocks for its own SSRCs (media and RTX) to the loss estimate, video first, and a
// stream's report with no packet received is dropped. A block for an SSRC nobody sends is ignored.
SM_TEST(Session, LossReportsArePerSendStreamAndNeedAReceivedPacket) {
  Pair p;
  auto rr = [&](uint32_t video_seq, uint32_t audio_seq, int32_t audio_lost, uint32_t other_seq, int64_t ms) {
    timing::ReceiverReport r;
    r.ssrc = 2;
    r.blocks = {{.ssrc = 0x200, .extended_highest_seq = video_seq},
                {.ssrc = 0x300, .extended_highest_seq = 7},
                {.ssrc = 0x100, .cumulative_lost = audio_lost, .extended_highest_seq = audio_seq},
                {.ssrc = 0x999, .cumulative_lost = static_cast<int32_t>(other_seq), .extended_highest_seq = other_seq}};
    p.a.OnPacket(timing::SerializeReceiverReport(r), Timestamp::Millis(ms));
  };
  const cc::SendSideBandwidthEstimation& bwe = p.a.send_side().gcc().send_side_bwe();
  rr(100, 50, 0, 100, 1000);
  // Video: 30 received. Audio: 10 more, all lost, so its report is dropped. 0x999: 100 lost.
  // Summed over the packet this would be 110 of 140 lost.
  rr(130, 60, 10, 200, 2000);
  SM_EXPECT_EQ(static_cast<int>(bwe.fraction_loss()), 0);
  SM_EXPECT_EQ(bwe.expected_packets_since_last_loss_update(), 0);
  // Video 10 received (< 20, accumulated), then audio 20 with 5 lost: 5 of 30, in the second report.
  rr(140, 80, 15, 200, 3000);
  SM_EXPECT_EQ(static_cast<int>(bwe.fraction_loss()), (5 << 8) / 30);
}

SM_TEST(Session, SetRttFeedsTheSessionWithoutRaisingAnEvent) {
  Pair p;
  p.b.SetRtt(TimeDelta::Millis(250));
  SM_EXPECT_EQ(p.b.rtt().ms(), 250);
  SM_EXPECT_TRUE(p.b.TakeEvents().empty());
}

// GoogCC gets an RTT only once one is known (issue #869): RtpTransportControllerSend::OnRttUpdate runs on a
// measured RTT, so until then AimdRateControl keeps its 200 ms default (aimd_rate_control.cc:34). The
// session's own 100 ms placeholder must not reach it. An RTT the host sets does, also when set before
// the session starts.
SM_TEST(Session, TheCongestionControllerGetsNoRttBeforeOneIsKnown) {
  Pair p;
  Pump(p.a, 0, 100);
  SM_EXPECT_EQ(p.a.send_side().gcc().last_rtt().ms(), 0);
  p.a.SetRtt(TimeDelta::Millis(250));
  SM_EXPECT_EQ(p.a.send_side().gcc().last_rtt().ms(), 250);

  Pair q;
  q.a.SetRtt(TimeDelta::Millis(250));
  Pump(q.a, 0, 100);
  SM_EXPECT_EQ(q.a.send_side().gcc().last_rtt().ms(), 250);
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

// The sent frame rate FecControllerDefault takes is SendStatisticsProxy's
// encoded_frame_rate_tracker_ (RateTracker, 10 buckets of 100 ms), exported as
// VideoSendStream::Stats::encode_frame_rate (issue #710): 0 for the first
// 100 ms after it starts, then frames over the time since it started, then
// over the last second, with the oldest bucket counted in part.
SM_TEST(Session, SentFrameRateIsSendStatisticsProxysRateTracker) {
  Session s(Cfg(1));
  const VideoSendId id = s.AddVideoSend(Video());
  const auto fps = [&] {
    MapMetricsWriter w;
    s.ExportMetrics(w);
    return w.Get("video_encode_frame_rate", -1);
  };
  const auto frame = [&](int64_t t) {
    auto chunks = test::MakeTestFrameChunks(t == 0, 1500, 1188);
    s.SendVideoChunks(id, chunks, static_cast<uint32_t>(t * 90), Timestamp::Millis(t));
  };
  frame(0);
  frame(10);
  s.Process(Timestamp::Millis(10));
  // Not 2 frames over 10 ms: no rate until a bucket has passed.
  SM_EXPECT_EQ(fps(), 0.0);
  frame(50);
  s.Process(Timestamp::Millis(100));
  SM_EXPECT_EQ(fps(), 30.0);  // 3 frames over 100 ms.
  // 25 fps from 100 ms to 2000 ms.
  for (int64_t t = 100; t < 2000; t += 40) frame(t);
  s.Process(Timestamp::Millis(2000));
  SM_EXPECT_EQ(fps(), 25.0);
  // 500 ms without a frame: only the last half second has frames.
  s.Process(Timestamp::Millis(2500));
  SM_EXPECT_EQ(fps(), 13.0);  // 13 frames in 1500-2000 ms.
  s.Process(Timestamp::Millis(3000));
  SM_EXPECT_EQ(fps(), 0.0);
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

// ModuleRtpRtcpImpl2 posts the report's task on the worker queue when the last report goes out
// (rtp_rtcp_impl2.cc:797-808), and TaskQueuePacedSender runs on the same queue
// (rtp_transport_controller_send.cc:100-105). When both are due at once the report's task is the
// older one and runs first, so the SR leaves ahead of the paced packets and doesn't count them
// (issue #895: issue_12_lossbwe_shallow seed 1, the SR and RTX 916 at 1063 ms).
SM_TEST(Session, ADueSenderReportLeavesAheadOfThePacketsPacedAtTheSameTime) {
  SessionConfig c = Cfg(1);
  c.rtcp.first_report_delay = TimeDelta::Millis(1000);
  Session s(c);
  AudioStreamConfig au;
  au.ssrc = 0x100;
  s.AddAudioSend(au);
  VideoSendId v = s.AddVideoSend(Video());
  for (int64_t t = 0; t < 1000; ++t) {
    s.Process(Timestamp::Millis(t));
    s.TakeOutgoing();
  }
  const Timestamp now = Timestamp::Millis(1000);
  auto chunks = test::MakeTestFrameChunks(false, 3000, 1188);
  s.SendVideoChunks(v, chunks, 0, now);
  s.Process(now);
  std::vector<OutgoingKind> kinds;
  Bytes sr;
  for (auto& o : s.TakeOutgoing()) {
    kinds.push_back(o.kind);
    if (o.kind == OutgoingKind::kRtcp && sr.empty()) sr = o.bytes;
  }
  SM_ASSERT_GE(kinds.size(), 2u);
  // The report, then what the pacer sends at that time (the first frame starts a probe: padding).
  SM_EXPECT_TRUE(kinds[0] == OutgoingKind::kRtcp);
  SM_EXPECT_EQ(std::count(kinds.begin(), kinds.end(), OutgoingKind::kRtcp), 1);
  // The SR's sender packet count (RFC 3550 6.4.1, bytes 20-23) has none of the paced packets in it.
  SM_ASSERT_GE(sr.size(), 28u);
  SM_EXPECT_EQ(sr[1], 200);
  const uint32_t packets = (uint32_t{sr[20]} << 24) | (uint32_t{sr[21]} << 16) | (uint32_t{sr[22]} << 8) | sr[23];
  SM_EXPECT_EQ(packets, 0u);
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

// #62: an audio stream receives through libwebrtc's NetEq, the only jitter buffer since
// #817 removed version 1, with NetEq::Config's defaults (neteq.h: 200 packets, no minimum delay).
SM_TEST(Session, AudioStreamDefaultsToLibwebrtcsNetEq) {
  SM_EXPECT_EQ(AudioStreamConfig{}.jitter_buffer.neteq_delay.max_packets_in_buffer, 200);
  SM_EXPECT_EQ(AudioStreamConfig{}.jitter_buffer.neteq_delay.base_min_delay_ms, 0);
  SM_EXPECT_EQ(timing::AudioJitterBufferConfig{}.neteq_delay.max_packets_in_buffer, 200);
}

// #574: ChannelReceive turns NetEq's muted state on (channel_receive.cc:107);
// NetEq::Config, like AudioJitterBufferConfig on its own, leaves it off.
SM_TEST(Session, AudioStreamTurnsOnNetEqsMutedState) {
  SM_EXPECT_TRUE(AudioStreamConfig{}.jitter_buffer.enable_muted_state);
  SM_EXPECT_FALSE(timing::AudioJitterBufferConfig{}.enable_muted_state);
}

// #568: a session aggregates the RTT per send stream (RTCPReceiver::RttStats per
// module, rtcp_receiver.cc:211-229), as libwebrtc always does.
SM_TEST(Session, RttIsAggregatedPerSendStreamByDefault) {
  SM_EXPECT_TRUE(SessionConfig{}.per_stream_rtt);
}

// #575: every receive stream keeps libwebrtc's StreamStatisticianImpl rules (max reordering
// threshold, stream restart, 255-scaled fraction lost; receive_statistics_impl.cc), as
// libwebrtc always does.
SM_TEST(Session, ReceiveStatisticsFollowLibwebrtcByDefault) {
  SM_EXPECT_TRUE(SessionConfig{}.receive_statistics_v2);
  SM_EXPECT_TRUE(RtpReceiverConfig{}.receive_statistics_v2);
}

// #24: a session's video stream runs libwebrtc's receive timing (VCMTiming and
// frame release); timing::VideoTimingConfig on its own defaults to it too (#579).
SM_TEST(Session, VideoStreamDefaultsToLibwebrtcsReceiveTiming) {
  SM_EXPECT_EQ(VideoStreamConfig{}.timing.version, 2);
  SM_EXPECT_EQ(timing::VideoTimingConfig{}.version, 2);
}

// #453: libwebrtc runs H264SpsPpsTracker (rtp_video_stream_receiver2.cc OnReceivedPayloadData) and
// PacketBuffer's missing_packets_ hold (packet_buffer.cc FindFrames) on every H.264 stream, and on
// no other codec. A session's video stream does the same. The building blocks have both on by
// default too (#580), and the session turns them off for another codec.
SM_TEST(Session, H264StreamsRunTheSpsPpsTrackerAndTheMissingPacketsHold) {
  SM_EXPECT_TRUE(VideoStreamConfig{}.sps_pps_tracker);
  SM_EXPECT_TRUE(VideoStreamConfig{}.packet_buffer.missing_packets);
  SM_EXPECT_TRUE(RtpReceiverConfig{}.sps_pps_tracker);
  SM_EXPECT_TRUE(timing::PacketBufferConfig{}.missing_packets);
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

namespace {
// The SSRCs of the report blocks in each SR and RR of one compound RTCP datagram.
std::vector<uint32_t> ReportBlockSsrcs(const Bytes& p) {
  std::vector<uint32_t> out;
  for (size_t off = 0; off + 4 <= p.size();) {
    const size_t len = 4 * ((static_cast<size_t>(p[off + 2]) << 8 | p[off + 3]) + 1);
    if (off + len > p.size()) break;
    const int type = p[off + 1];
    if (type == 200 || type == 201) {
      const size_t first = off + (type == 200 ? 28 : 8);
      for (size_t b = 0; b < static_cast<size_t>(p[off] & 0x1f) && first + 24 * (b + 1) <= off + len; ++b) {
        const size_t at = first + 24 * b;
        out.push_back(static_cast<uint32_t>(p[at]) << 24 | static_cast<uint32_t>(p[at + 1]) << 16 |
                      static_cast<uint32_t>(p[at + 2]) << 8 | p[at + 3]);
      }
    }
    off += len;
  }
  return out;
}

struct ReceiverRtcp {
  std::vector<int64_t> audio_ms;  // When each compound with the audio block left the receiver.
  size_t mixed = 0;               // Compounds with the audio block and a video or RTX block.
  size_t audio_only_sizes_ok = 0;  // Audio compounds that are one RR with one block (32 bytes, no SDES).
};

// A 30 s call of audio every 20 ms and video every 33 ms from `a` to `b`; what b's RTCP carried.
ReceiverRtcp RunCall(bool audio_receive_rtcp) {
  NullDecoder dec;
  SessionConfig cb = Cfg(2);
  cb.audio_receive_rtcp = audio_receive_rtcp;
  Session a(Cfg(1)), b(cb);
  AudioStreamConfig au;
  au.ssrc = 0x100;
  const AudioSendId a_audio = a.AddAudioSend(au);
  const VideoSendId a_video = a.AddVideoSend(Video());
  b.AddAudioReceive(au, &dec);
  b.AddVideoReceive(Video());
  ReceiverRtcp r;
  const Bytes frame(60, 7);
  for (int64_t t = 0; t < 30000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 20 == 0) a.SendAudio(a_audio, {static_cast<uint32_t>(t * 48), frame, t == 0}, now);
    if (t % 33 == 0) a.SendVideoChunks(a_video, test::MakeTestFrameChunks(t == 0, 1500, 1188), static_cast<uint32_t>(t * 90), now);
    a.Process(now);
    for (auto& o : a.TakeOutgoing()) b.OnPacket(o.bytes, now);
    b.Process(now);
    for (auto& o : b.TakeOutgoing()) {
      if (o.kind == OutgoingKind::kRtcp) {
        const std::vector<uint32_t> ssrcs = ReportBlockSsrcs(o.bytes);
        if (std::find(ssrcs.begin(), ssrcs.end(), 0x100u) != ssrcs.end()) {
          r.audio_ms.push_back(t);
          r.mixed += ssrcs.size() > 1;
          r.audio_only_sizes_ok += ssrcs.size() == 1 && o.bytes.size() == 32;
        }
      }
      a.OnPacket(o.bytes, now);
    }
    a.TakeEvents();
    b.TakeEvents();
  }
  return r;
}
}  // namespace

// Issue #484: libwebrtc's audio ChannelReceive has its own ReceiveStatistics and a receiver-only RtpRtcp module
// (audio/channel_receive.cc:115-139, 608), so the audio block goes out alone in an RR every 5 s
// (rtcp_receiver.cc:85 kDefaultAudioReportInterval), the first at half of it (rtcp_sender.cc:171), without SDES,
// and the video stream's RRs, NACKs and PLIs carry only the video and RTX blocks.
SM_TEST(Session, AudioReceiveStreamReportsInItsOwnRtcpEveryFiveSeconds) {
  const ReceiverRtcp on = RunCall(true);
  SM_EXPECT_EQ(on.mixed, 0u);
  SM_EXPECT_EQ(on.audio_only_sizes_ok, on.audio_ms.size());
  SM_EXPECT_GE(on.audio_ms.size(), 4u);  // 30 s at 5 s, each interval drawn from [2.5, 7.5] s.
  SM_EXPECT_LE(on.audio_ms.size(), 8u);
  SM_EXPECT_EQ(on.audio_ms.front(), 2500);  // Half the nominal interval, no draw.
  for (size_t i = 1; i < on.audio_ms.size(); ++i) {
    SM_EXPECT_GE(on.audio_ms[i] - on.audio_ms[i - 1], 2500);
    SM_EXPECT_LE(on.audio_ms[i] - on.audio_ms[i - 1], 7500);
  }
  // Off: the audio block rides in every compound with the video blocks (the 1 s video RRs at least).
  const ReceiverRtcp off = RunCall(false);
  SM_EXPECT_GE(off.audio_ms.size(), 25u);
  SM_EXPECT_EQ(off.mixed, off.audio_ms.size());
}

SM_TEST(Session, AudioReceiveRtcpEngineExistsOnlyWithAnAudioReceiveStream) {
  SessionConfig c = Cfg(2);
  c.audio_receive_rtcp = true;
  Session s(c);
  SM_EXPECT_TRUE(s.audio_receive_rtcp() == nullptr);
  s.AddVideoReceive(Video());
  SM_EXPECT_TRUE(s.audio_receive_rtcp() == nullptr);
  NullDecoder dec;
  AudioStreamConfig au;
  au.ssrc = 0x100;
  s.AddAudioReceive(au, &dec);
  SM_EXPECT_TRUE(s.audio_receive_rtcp() != nullptr);
  SessionConfig co = Cfg(2);
  co.audio_receive_rtcp = false;
  Session off(co);
  off.AddAudioReceive(au, &dec);
  SM_EXPECT_TRUE(off.audio_receive_rtcp() == nullptr);
}

// Issue #484: per-stream audio RTCP is the default, as libwebrtc's ChannelReceive.
SM_TEST(Session, AudioReceiveRtcpIsTheDefault) {
  SM_EXPECT_TRUE(SessionConfig{}.audio_receive_rtcp);
  Session s(Cfg(2));
  NullDecoder dec;
  AudioStreamConfig au;
  au.ssrc = 0x100;
  s.AddAudioReceive(au, &dec);
  SM_EXPECT_TRUE(s.audio_receive_rtcp() != nullptr);
}

// #567 F8.3 part 6, receive half: back to back, with RRTR so that the receive-only side has an RTT
// (ModuleRtpRtcpImpl2::LastRtt), the SRs train the video receiver's RemoteNtpTimeEstimator after each
// RTCP packet (RtpVideoStreamReceiver2::DeliverRtcp). Frames then carry their capture time on the
// local NTP clock and the capture clock offset against the local clock, 0 here (one clock, no delay).
SM_TEST(Session, ReceivedFramesCarryTheCaptureTimeOnTheLocalClock) {
  SessionConfig ca = Cfg(1), cb = Cfg(2);
  for (SessionConfig* c : {&ca, &cb}) {
    c->extensions.Register(7, sync::uri::kAbsCaptureTime);
    c->rtcp.xr_rrtr = true;
  }
  Session a(ca), b(cb);
  const VideoSendId vs = a.AddVideoSend(Video());
  const VideoRecvId vr = b.AddVideoReceive(Video());
  std::vector<std::pair<Timestamp, ReceivedVideoFrame>> frames;  // Capture time, frame.
  std::map<uint32_t, Timestamp> captured;
  for (int64_t t = 0; t < 12000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 33 == 0) {
      EncodedVideoFrame f;
      f.rtp_timestamp = static_cast<uint32_t>(t * 90);
      const Bytes data = MakeFakeH264Frame(t == 0, 1500);
      f.data = data;
      f.keyframe = t == 0;
      f.capture_time = now;
      captured[f.rtp_timestamp + a.video_sender(vs).StartTimestamp()] = now;  // The timestamp on the wire.
      a.SendVideo(vs, f, now);
    }
    a.Process(now);
    for (auto& o : a.TakeOutgoing()) b.OnPacket(o.bytes, now);
    b.Process(now);
    for (auto& o : b.TakeOutgoing()) a.OnPacket(o.bytes, now);
    for (auto& f : b.TakeVideoFrames(vr)) frames.emplace_back(captured[f.rtp_timestamp], std::move(f));
  }
  SM_EXPECT_GE(b.video_receiver(vr).stats().remote_ntp_updates, 3u);
  SM_ASSERT_GT(frames.size(), 300u);
  SM_EXPECT_TRUE(frames.front().second.absolute_capture_time.has_value());
  SM_EXPECT_EQ(frames.front().second.ntp_time_ms, -1);  // Before any SR.
  const auto& [capture, last] = frames.back();
  SM_ASSERT_TRUE(last.absolute_capture_time.has_value());
  SM_EXPECT_EQ(sync::UQ32x32ToInt64Ms(last.absolute_capture_time->capture_ntp),
               sync::NtpToMs(timing::NtpTime::FromTimestamp(capture).value));
  SM_ASSERT_TRUE(last.local_capture_clock_offset.has_value());
  SM_EXPECT_LE(std::abs(last.local_capture_clock_offset->ms()), 1);
  SM_EXPECT_NEAR(static_cast<double>(last.ntp_time_ms),
                 static_cast<double>(sync::NtpToMs(timing::NtpTime::FromTimestamp(capture).value)), 1.0);
}

// Each receive stream's RemoteNtpTimeEstimator waits for its own module's LastRtt
// (rtp_rtcp_impl2.cc:482-491, 740-751): a receive-only video stream has the XR DLRR RTT from the 1 s
// periodic update, while the audio one, without AudioReceiveStream's enable_non_sender_rtt (off by
// default), has none, and so trains no estimate, as in libwebrtc's one-way call. SetRtt does not change it.
SM_TEST(Session, ReceiveStreamsTrainTheirNtpEstimateFromTheirOwnModulesRtt) {
  SessionConfig ca = Cfg(1), cb = Cfg(2);
  for (SessionConfig* c : {&ca, &cb}) c->rtcp.xr_rrtr = true;
  Session a(ca), b(cb);
  AudioStreamConfig au;
  au.ssrc = 0x100;
  a.AddAudioSend(au);
  a.AddVideoSend(Video());
  NullDecoder dec;
  const AudioRecvId ar = b.AddAudioReceive(au, &dec);
  const VideoRecvId vr = b.AddVideoReceive(Video());
  for (int64_t t = 0; t < 8000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t == 4000) b.SetRtt(TimeDelta::Millis(120));
    a.Process(now);
    for (auto& o : a.TakeOutgoing()) b.OnPacket(o.bytes, now);
    b.Process(now);
    for (auto& o : b.TakeOutgoing()) a.OnPacket(o.bytes, now);
  }
  SM_EXPECT_TRUE(b.video_receiver(vr).LastRtt().has_value());
  SM_EXPECT_GE(b.video_receiver(vr).stats().remote_ntp_updates, 1u);
  SM_EXPECT_FALSE(b.audio_receiver(ar).LastRtt().has_value());
  SM_EXPECT_EQ(b.audio_receiver(ar).stats().remote_ntp_updates, 0u);
}

// Issue #514: each stream starts at libwebrtc's random sequence number and RTP timestamp. Both come
// from the clock time the modules are built at, which is the session's `seed` (RtcpRandomSeed in the
// test bench): RTPSender's timestamp_offset_ is Random(t).Rand<uint32_t>() (rtp_sender.cc:247-250),
// and the sequencer draws the media then the RTX start number, Rand(1, 32767) each. The values are
// libwebrtc's own on the compare traces of seed 1 (clock 100000 s) and seed 3 (115838 s).
SM_TEST(Session, StreamsStartAtLibwebrtcsRandomSequenceNumberAndTimestamp) {
  struct Case {
    uint64_t seed;
    uint32_t timestamp_offset;
    uint16_t seq, rtx_seq;
  };
  for (const Case& k : {Case{100000ull * 1000000, 873682028u, 6666, 887},
                        Case{115838ull * 1000000, 2301725640u, 17561, 0}}) {
    SessionConfig c = Cfg(1);
    c.seed = k.seed;
    Pair p(c);
    SM_EXPECT_EQ(p.a.audio_sender(p.a_audio).StartTimestamp(), k.timestamp_offset);
    SM_EXPECT_EQ(p.a.video_sender(p.a_video).StartTimestamp(), k.timestamp_offset);
    if (k.rtx_seq) SM_EXPECT_EQ(p.a.video_sender(p.a_video).config().rtx.initial_seq, k.rtx_seq);
    Bytes frame(60, 7);
    p.a.SendAudio(p.a_audio, {960, frame, false}, Timestamp::Millis(0));
    p.a.SendVideoChunks(p.a_video, test::MakeTestFrameChunks(true, 3000, 1188), 90, Timestamp::Millis(0));
    std::optional<uint16_t> audio_seq, video_seq;
    std::optional<uint32_t> audio_ts, video_ts;
    std::optional<bool> audio_marker;
    for (auto& o : Pump(p.a, 0, 200)) {
      const uint16_t seq = static_cast<uint16_t>((o.bytes[2] << 8) | o.bytes[3]);
      const uint32_t ts = (uint32_t{o.bytes[4]} << 24) | (uint32_t{o.bytes[5]} << 16) | (uint32_t{o.bytes[6]} << 8) | o.bytes[7];
      if (o.kind == OutgoingKind::kAudio && !audio_seq) {
        audio_seq = seq;
        audio_ts = ts;
        audio_marker = (o.bytes[1] & 0x80) != 0;
      }
      if (o.kind == OutgoingKind::kVideo && !video_seq) {
        video_seq = seq;
        video_ts = ts;
      }
    }
    SM_ASSERT_TRUE(audio_seq.has_value() && video_seq.has_value());
    SM_EXPECT_EQ(*audio_seq, k.seq);
    SM_EXPECT_EQ(*video_seq, k.seq);
    // ChannelSend and RtpVideoSender add the start timestamp to the frame's own.
    SM_EXPECT_EQ(*audio_ts, k.timestamp_offset + 960u);
    SM_EXPECT_EQ(*video_ts, k.timestamp_offset + 90u);
    SM_EXPECT_TRUE(*audio_marker);  // RTPSenderAudio::MarkerBit: the stream's first packet.
  }
}

// Issue #777: RtpVideoStreamReceiver2::OnRtpPacket updates the receive statistics after
// ReceivePacket (rtp_video_stream_receiver2.cc:983-990), so the NACK that a packet's gap triggers
// goes out with report blocks that don't count that packet yet; the next report counts it.
// libwebrtc's sender then sees the gap's loss in one report (fraction 54 at 610 ms on
// issue_464_start_rate_above_int_bps, every seed).
SM_TEST(Session, TheNackForAGapCarriesReportBlocksWithoutThePacketThatRevealedIt) {
  Pair p;
  struct Report {
    bool nack = false;
    timing::ReportBlock video;
  };
  std::vector<Report> reports;
  auto read = [&](const Bytes& d) {
    auto split = loss::SplitCompoundRtcp(d);
    if (!split || split->empty() || (*split)[0].type != loss::RtcpType::kReceiverReport) return;
    auto rr = timing::ParseReceiverReport((*split)[0].data);
    if (!rr) return;
    Report r;
    for (const auto& b : rr->blocks) {
      if (b.ssrc == 0x200) r.video = b;
    }
    if (r.video.ssrc == 0) return;
    for (const auto& v : *split) r.nack = r.nack || v.type == loss::RtcpType::kNack;
    reports.push_back(r);
  };
  auto deliver = [&](const Bytes& bytes, Timestamp now) {
    // Each arriving packet is handled, and its RTCP sent, before the next one.
    p.b.OnPacket(bytes, now);
    p.b.Process(now);
    for (auto& r : p.b.TakeOutgoing()) {
      if (r.kind == OutgoingKind::kRtcp) read(r.bytes);
    }
  };
  // Video sequence numbers per frame, in sending order (the RTP timestamps start at random).
  std::vector<std::pair<uint32_t, std::vector<uint16_t>>> frames;
  for (int64_t t = 0; t < 2000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 100 == 0) p.a.SendVideoChunks(p.a_video, test::MakeTestFrameChunks(t == 0, 3000, 1188), static_cast<uint32_t>(t * 90), now);
    p.a.Process(now);
    for (auto& o : p.a.TakeOutgoing()) {
      if (o.kind == OutgoingKind::kVideo) {
        const auto rtp = RtpPacket::Parse(o.bytes);
        if (frames.empty() || frames.back().first != rtp->timestamp) frames.push_back({rtp->timestamp, {}});
        frames.back().second.push_back(rtp->seq);
        if (frames.size() == 2) continue;  // The second frame is lost on the way.
      }
      deliver(o.bytes, now);
    }
    p.b.Process(now);
    for (auto& r : p.b.TakeOutgoing()) {
      if (r.kind == OutgoingKind::kRtcp) read(r.bytes);
    }
  }
  SM_ASSERT_GE(frames.size(), 3u);
  const uint16_t keyframe_last = frames[0].second.back();
  const auto lost = static_cast<int32_t>(frames[1].second.size());
  const uint16_t revealing = frames[2].second.front();
  const auto first_nack = std::find_if(reports.begin(), reports.end(), [](const Report& r) { return r.nack; });
  SM_ASSERT_TRUE(first_nack != reports.end());
  // The third frame's first packet revealed the gap: the NACK's block ends at the keyframe, no loss yet.
  SM_EXPECT_EQ(first_nack->video.extended_highest_seq & 0xffff, keyframe_last);
  SM_EXPECT_EQ(first_nack->video.cumulative_lost, 0);
  // The next report counts it, with the lost frame.
  SM_ASSERT_TRUE(first_nack + 1 != reports.end());
  SM_EXPECT_GE((first_nack + 1)->video.extended_highest_seq & 0xffff, revealing);
  SM_EXPECT_EQ((first_nack + 1)->video.cumulative_lost, lost);
}

// Issue #893: Call::DeliverRtcpPacket_w (call.cc:1357-1381) hands every RTCP packet to the audio send
// stream too, and its RTCPReceiver keeps the receiver's RRTR whatever its sender
// (RTCPReceiver::HandleXrReceiveReferenceTime, rtcp_receiver.cc:687-707), so the audio channel's next
// SR carries a DLRR for it (RTCPSender::BuildExtendedReports via GetFeedbackState's last_xr_rtis).
SM_TEST(Session, TheAudioSendStreamsReportEchoesTheReceiversRrtr) {
  SessionConfig cb = Cfg(2);
  cb.rtcp.xr_rrtr = true;
  Session a(Cfg(1)), b(cb);
  AudioStreamConfig au;
  au.ssrc = 0x100;
  const AudioSendId as = a.AddAudioSend(au);
  a.AddVideoSend(Video());
  NullDecoder dec;
  b.AddAudioReceive(au, &dec);
  b.AddVideoReceive(Video());
  const Bytes frame(60, 7);
  int audio_reports = 0, audio_reports_with_dlrr = 0;
  for (int64_t t = 0; t < 16000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t % 20 == 0) a.SendAudio(as, {960, frame, false}, now);
    a.Process(now);
    for (auto& o : a.TakeOutgoing()) {
      if (o.kind == OutgoingKind::kRtcp && o.bytes.size() >= 8 && o.bytes[1] == 200) {
        const uint32_t ssrc = (uint32_t{o.bytes[4]} << 24) | (uint32_t{o.bytes[5]} << 16) |
                              (uint32_t{o.bytes[6]} << 8) | o.bytes[7];
        if (ssrc == au.ssrc) {
          ++audio_reports;
          bool dlrr = false;
          for (size_t i = 0; i + 4 <= o.bytes.size();) {
            if (o.bytes[i + 1] == 207 && i + 8 < o.bytes.size() && o.bytes[i + 8] == 5) dlrr = true;  // XR, block type 5.
            i += 4 * (static_cast<size_t>((o.bytes[i + 2] << 8) | o.bytes[i + 3]) + 1);
          }
          if (dlrr) ++audio_reports_with_dlrr;
        }
      }
      b.OnPacket(o.bytes, now);
    }
    b.Process(now);
    for (auto& o : b.TakeOutgoing()) a.OnPacket(o.bytes, now);
  }
  SM_EXPECT_GE(audio_reports, 2);
  SM_EXPECT_GE(audio_reports_with_dlrr, 1);
}

// Issue #764: CancelAudio drops the audio stream's packets still in the pacer,
// as ChannelSend::StopSend's PacingController::RemovePacketsForSsrc (channel_send.cc:633).
// They are never sent, the video stream's packets stay, and the next audio
// packet takes the next sequence number (sequence numbers are assigned at send).
SM_TEST(Session, CancelAudioDropsTheQueuedAudioOnlyAndLeavesNoSequenceGap) {
  Pair p;
  const Bytes opus(60, 1);
  p.a.Process(Timestamp::Millis(0));
  p.a.SendAudio(p.a_audio, {0, opus, true}, Timestamp::Millis(0));
  std::vector<uint16_t> seqs;
  const auto audio_seqs = [&](const std::vector<OutgoingPacket>& out) {
    int n = 0;
    for (const auto& o : out) {
      if (o.kind != OutgoingKind::kAudio) continue;
      seqs.push_back(RtpPacket::Parse(o.bytes)->seq);
      ++n;
    }
    return n;
  };
  SM_EXPECT_EQ(audio_seqs(Pump(p.a, 0, 20)), 1);
  // Audio waits in the pacer only while it is paused (or with WebRTC-Pacer-BlockAudio); otherwise
  // each enqueue sends it at once, as in libwebrtc. A reply sent ahead while it is paused: 10
  // frames queued, with a video frame behind them.
  p.a.send_side().pacer().Pause();
  for (int i = 1; i <= 10; ++i) p.a.SendAudio(p.a_audio, {static_cast<uint32_t>(960 * i), opus, false}, Timestamp::Millis(20));
  const auto chunks = test::MakeTestFrameChunks(true, 300, 1188);
  p.a.SendVideoChunks(p.a_video, chunks, 9000, Timestamp::Millis(20), true);
  const size_t removed = p.a.CancelAudio(p.a_audio, Timestamp::Millis(20));
  SM_EXPECT_EQ(removed, 10u);
  SM_EXPECT_EQ(p.a.stats().audio_cancels, 1u);
  SM_EXPECT_EQ(p.a.stats().audio_cancelled_packets, 10u);
  p.a.send_side().pacer().Resume();
  int video = 0;
  const auto out = Pump(p.a, 20, 100);
  for (const auto& o : out) video += o.kind == OutgoingKind::kVideo;
  SM_EXPECT_EQ(audio_seqs(out), 0);
  SM_EXPECT_EQ(video, 1);
  // The next reply starts a talk spurt right after the last packet sent.
  p.a.SendAudio(p.a_audio, {960 * 100, opus, true}, Timestamp::Millis(120));
  SM_EXPECT_EQ(audio_seqs(Pump(p.a, 120, 20)), 1);
  SM_ASSERT_EQ(seqs.size(), 2u);
  SM_EXPECT_EQ(static_cast<uint16_t>(seqs[1] - seqs[0]), 1);
  // A cancel with nothing queued, and unknown ids, change nothing.
  SM_EXPECT_EQ(p.a.CancelAudio(p.a_audio, Timestamp::Millis(140)), 0u);
  SM_EXPECT_EQ(p.a.CancelAudio(AudioSendId{}, Timestamp::Millis(140)), 0u);
  SM_EXPECT_EQ(p.a.CancelAudio(AudioSendId{5}, Timestamp::Millis(140)), 0u);
  SM_EXPECT_EQ(p.a.stats().audio_cancels, 2u);
  MapMetricsWriter w;
  p.a.ExportMetrics(w);
  SM_EXPECT_NEAR(w.values().at("audio_cancelled_packets"), 10.0, 1e-9);
}

// Issue #764: FlushAudio is NetEq::FlushBuffers on the stream's jitter buffer:
// the queued audio is dropped and playout waits for the next packet.
SM_TEST(Session, FlushAudioFlushesTheStreamsJitterBuffer) {
  Pair p;
  const Bytes opus(60, 1);
  std::vector<float> pcm(480);
  p.a.Process(Timestamp::Millis(0));
  p.b.Process(Timestamp::Millis(0));
  // 200 ms of audio delivered at once after 1 s of real-time playout.
  int64_t t = 0;
  for (int i = 0; i < 60; ++i) {
    p.a.SendAudio(p.a_audio, {static_cast<uint32_t>(960 * i), opus, i == 0}, Timestamp::Millis(t));
    const int64_t until = i < 50 ? t + 20 : t;
    for (int64_t u = t; u <= until; ++u) {
      p.a.Process(Timestamp::Millis(u));
      for (auto& o : p.a.TakeOutgoing()) p.b.OnPacket(o.bytes, Timestamp::Millis(u));
      p.b.Process(Timestamp::Millis(u));
      if (u % 10 == 0 && i < 50) p.b.GetAudio(p.b_audio, pcm, Timestamp::Millis(u));
    }
    if (i < 50) t += 20;
  }
  const auto& jb = p.b.audio_receiver(p.b_audio).jitter_buffer();
  SM_EXPECT_GT(jb.CurrentLevelMs(), 100.0);
  p.b.FlushAudio(p.b_audio, Timestamp::Millis(t));
  SM_EXPECT_EQ(jb.stats().flush_buffers_calls, 1u);
  SM_EXPECT_LT(jb.CurrentLevelMs(), 1.0);
  SM_EXPECT_FALSE(jb.PlayoutTimestamp().has_value());
  SM_EXPECT_EQ(p.b.stats().audio_flushes, 1u);
  SM_EXPECT_TRUE(p.b.GetAudio(p.b_audio, pcm, Timestamp::Millis(t + 10)) == timing::AudioOperation::kExpand);
  // Unknown ids are ignored.
  p.b.FlushAudio(AudioRecvId{}, Timestamp::Millis(t));
  p.b.FlushAudio(AudioRecvId{3}, Timestamp::Millis(t));
  SM_EXPECT_EQ(p.b.stats().audio_flushes, 1u);
  MapMetricsWriter w;
  p.b.ExportMetrics(w);
  SM_EXPECT_NEAR(w.values().at("audio_flushes"), 1.0, 1e-9);
}

// #762: with `send_ahead`, audio handed over before its due time goes out up to `send_ahead` early, in order, within
// the budget the target leaves; without it (or without a due time) every frame goes out when it is handed over.
SM_TEST(Session, AudioAheadOfRealTimeIsSentWithinItsLead) {
  auto run = [](TimeDelta send_ahead, bool with_due) {
    Session s(Cfg(1));
    AudioStreamConfig acfg;
    acfg.ssrc = 0x100;
    acfg.nominal_rate = DataRate::KilobitsPerSec(40);
    acfg.send_ahead = send_ahead;
    const AudioSendId au = s.AddAudioSend(acfg);
    const Bytes opus(80, 1);
    const Timestamp t0 = Timestamp::Seconds(100);
    // A reply of 100 frames (2 s), all generated at once at t0, each due at real-time pace.
    for (int k = 0; k < 100; ++k) {
      EncodedAudioFrame f;
      f.rtp_timestamp = static_cast<uint32_t>(960 * k);
      f.data = opus;
      if (with_due) f.due_time = t0 + TimeDelta::Millis(20 * k);
      s.SendAudio(au, f, t0);
    }
    // Packets out by time, over 2 s in 1 ms steps.
    std::vector<int> sent_by_ms;
    int sent = 0;
    for (int ms = 0; ms <= 2000; ++ms) {
      const Timestamp now = t0 + TimeDelta::Millis(ms);
      s.Process(now);
      for (const auto& p : s.TakeOutgoing()) sent += p.kind == OutgoingKind::kAudio ? 1 : 0;
      sent_by_ms.push_back(sent);
    }
    return std::make_pair(sent_by_ms, std::map<std::string, double>(CollectMetrics(s)));
  };
  // No lead: handed over means sent, due time or not.
  SM_EXPECT_EQ(run(TimeDelta::Zero(), true).first[1], 100);
  SM_EXPECT_EQ(run(TimeDelta::Millis(500), false).first[1], 100);
  // 500 ms lead: at most the frames due by t + 500 ms are out at t, and they go out within the budget, ahead of the
  // real-time 1 per 20 ms.
  const auto [ahead, metrics] = run(TimeDelta::Millis(500), true);
  for (int ms = 0; ms <= 2000; ms += 10) {
    SM_EXPECT_TRUE(ahead[static_cast<size_t>(ms)] <= std::min(100, (ms + 500) / 20 + 1));
    SM_EXPECT_TRUE(ahead[static_cast<size_t>(ms)] >= std::min(100, ms / 20 + 1));  // Never behind real time.
  }
  SM_EXPECT_TRUE(ahead[400] > 400 / 20 + 5);  // A cushion builds.
  SM_EXPECT_EQ(ahead[2000], 100);
  SM_EXPECT_TRUE(metrics.at("audio_ahead_frames") > 0);
  SM_EXPECT_TRUE(metrics.at("audio_ahead_max_lead_ms") <= 500);
  // Without send-ahead the export has no new names.
  SM_EXPECT_TRUE(run(TimeDelta::Zero(), true).second.count("audio_ahead_frames") == 0);
}
