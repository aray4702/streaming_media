// Transceivers, renegotiation and multi-stream receive on one connection (C18, #1160), as libwebrtc M157's Unified
// Plan in pc/sdp_offer_answer.cc: an SFU-like peer offers one sendonly section per remote track, our endpoint answers
// recvonly with a transceiver per section (AssociateTransceiver) and receives each track on its own stream over one
// BUNDLE transport; subsequent offers add sections (participants join), make them inactive (leave), stop them, and
// recycle a stopped section for a new track; both sides can re-offer and the DTLS role stays. Each remote track is
// demultiplexed by MID and SSRC to its own receive stream with its own decoder, NACK, sync group and stats.
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "endpoint_world.h"
#include "sm/pc/codec.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/session.h"
#include "sm/rtp/simulcast_send_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using sm::pc::test::EndpointWorld;
using sm::pc::test::MediaLines;
using sm::pc::test::SdpHas;

namespace {

// Records every Opus frame it decodes.
class RecordingDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t) override {
    frames.emplace_back(frame.begin(), frame.end());
    return std::vector<float>(960, 0.1f);
  }
  std::vector<std::vector<uint8_t>> frames;
};

// The decoders the receiving endpoint's factory made, by MID (each track's own).
struct Decoders {
  std::map<std::string, RecordingDecoder*> by_mid;
  std::function<std::unique_ptr<rtp::AudioDecoder>(const std::string&)> Factory() {
    return [this](const std::string& mid) {
      auto d = std::make_unique<RecordingDecoder>();
      by_mid[mid] = d.get();
      return d;
    };
  }
};

// An SFU-side endpoint: the host's packets (external media), no transceivers of its own, no data channels.
void SfuConfig(PeerEndpointConfig& c, std::function<void(std::span<const uint8_t>, bool, Timestamp)> on_packet) {
  c.external_media = true;
  c.audio = false;
  c.video = false;
  c.enable_data_channels = false;
  c.on_media_packet = std::move(on_packet);
}

// Participant k's tracks on the SFU: sendonly audio on 0x100 * k and video on 0x100 * k + 1 (RTX + 2), msid stream
// "p<k>". Returns the transceiver indices.
std::pair<int, int> AddParticipant(PeerEndpoint* sfu, int k) {
  TransceiverInit a;
  a.direction = RtpTransceiverDirection::kSendOnly;
  a.stream_ids = {"p" + std::to_string(k)};
  a.track_id = "audio" + std::to_string(k);
  a.ssrc = static_cast<uint32_t>(0x100 * k);
  TransceiverInit v = a;
  v.track_id = "video" + std::to_string(k);
  v.ssrc = static_cast<uint32_t>(0x100 * k + 1);
  v.rtx_ssrc = static_cast<uint32_t>(0x100 * k + 2);
  return {sfu->AddTransceiver(MediaType::AUDIO, a), sfu->AddTransceiver(MediaType::VIDEO, v)};
}

std::string IceUfrag(const std::string& sdp) {
  const size_t p = sdp.find("a=ice-ufrag:");
  return p == std::string::npos ? "" : sdp.substr(p, sdp.find("\r\n", p) - p);
}

int Count(const std::string& s, const std::string& what) {
  int n = 0;
  for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) ++n;
  return n;
}

std::optional<TransceiverState> StateOfMid(const PeerEndpoint& ep, const std::string& mid) {
  for (const auto& [i, s] : ep.transceivers()) {
    if (s.mid == mid) return s;
  }
  return std::nullopt;
}

}  // namespace

// An SFU's offers drive the receiver: each sendonly section becomes a recvonly transceiver with a remote track and a
// receive stream; later offers add sections, take tracks away (inactive), stop a section and recycle it, all on the
// one BUNDLE transport without an ICE restart.
SM_TEST(PcRenegotiation, SfuOffersAddAndRemoveRemoteTracks) {
  EndpointWorld w;
  PeerEndpoint* sfu = w.AddEndpoint("sfu", [](PeerEndpointConfig& c) { SfuConfig(c, nullptr); });
  Decoders decoders;
  PeerEndpoint* r = w.AddEndpoint("receiver", [&](PeerEndpointConfig& c) {
    c.session.av_sync = true;
    c.audio_decoder_factory = decoders.Factory();
    c.enable_data_channels = false;
  });
  std::vector<std::string> added, removed;
  std::map<std::string, RemoteTrack> tracks;
  r->SetOnTrack([&](const RemoteTrack& t) {
    added.push_back(t.mid);
    tracks[t.mid] = t;
  });
  r->SetOnRemoveTrack([&](const RemoteTrack& t) { removed.push_back(t.mid); });
  EndpointWorld::Link* link = w.Connect(sfu, r);

  // Participant 1: one audio and one video section.
  const auto p1 = AddParticipant(sfu, 1);
  SM_ASSERT_TRUE(p1.first >= 0 && p1.second >= 0);
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  SM_ASSERT_TRUE(w.RunUntil([&] { return sfu->IsConnected() && r->IsConnected(); }, TimeDelta::Seconds(10)));
  SM_ASSERT_TRUE(link->ok);
  const std::string ufrag = IceUfrag(link->last_answer);
  SM_EXPECT_TRUE(added == std::vector<std::string>({"0", "1"}));
  SM_EXPECT_EQ(Count(link->last_offer, "a=sendonly\r\n"), 2);
  SM_EXPECT_EQ(Count(link->last_answer, "a=recvonly\r\n"), 2);
  SM_ASSERT_EQ(r->remote_tracks().size(), 2u);
  SM_EXPECT_TRUE(tracks["0"].kind == MediaType::AUDIO && tracks["0"].ssrc == 0x100u);
  SM_EXPECT_TRUE(tracks["0"].stream_ids == std::vector<std::string>({"p1"}) && tracks["0"].track_id == "audio1");
  SM_EXPECT_TRUE(tracks["1"].kind == MediaType::VIDEO && tracks["1"].ssrc == 0x101u);
  SM_EXPECT_TRUE(tracks["1"].rtx_ssrc == std::optional<uint32_t>(0x102u));
  SM_ASSERT_TRUE(r->session() != nullptr);
  SM_ASSERT_TRUE(tracks["0"].audio.has_value() && tracks["1"].video.has_value());
  SM_EXPECT_TRUE(r->session()->HasAudioReceive(*tracks["0"].audio));
  SM_EXPECT_TRUE(r->session()->HasVideoReceive(*tracks["1"].video));
  SM_EXPECT_TRUE(decoders.by_mid.count("0") == 1);
  // Participant 1's video and audio are one sync group (msid stream "p1").
  SM_EXPECT_TRUE(r->session()->synced_audio(*tracks["1"].video).has_value());
  // The receiver's own AddTrack transceivers stay unassociated: the SFU's sections are sendonly
  // (FindAvailableTransceiverToReceive takes only sendrecv or recvonly ones).
  int unassociated = 0;
  for (const auto& [i, s] : r->transceivers()) unassociated += s.mid ? 0 : 1;
  SM_EXPECT_EQ(unassociated, 2);
  SM_EXPECT_TRUE(StateOfMid(*r, "0")->direction == RtpTransceiverDirection::kRecvOnly);
  SM_EXPECT_TRUE(StateOfMid(*r, "0")->current_direction == RtpTransceiverDirection::kRecvOnly);

  // Participants 2 and 3 join: a subsequent offer with four more sections.
  const auto p2 = AddParticipant(sfu, 2);
  const auto p3 = AddParticipant(sfu, 3);
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(500));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(added == std::vector<std::string>({"0", "1", "2", "3", "4", "5"}));
  SM_EXPECT_TRUE(SdpHas(link->last_offer, "a=group:BUNDLE 0 1 2 3 4 5"));
  SM_EXPECT_TRUE(SdpHas(link->last_answer, "a=group:BUNDLE 0 1 2 3 4 5"));
  SM_EXPECT_EQ(MediaLines(link->last_answer).size(), 6u);
  SM_EXPECT_TRUE(IceUfrag(link->last_answer) == ufrag);  // No ICE restart.
  SM_EXPECT_EQ(r->transport_controller().transport_count(), 1u);
  SM_EXPECT_TRUE(r->IsConnected() && sfu->IsConnected());
  SM_EXPECT_EQ(r->remote_tracks().size(), 6u);
  SM_EXPECT_TRUE(tracks["4"].stream_ids == std::vector<std::string>({"p3"}));
  SM_EXPECT_TRUE(r->session()->synced_audio(*tracks["5"].video)->index == tracks["4"].audio->index);

  // Participant 2 leaves: the SFU makes its sections inactive (the msid stays, JSEP); the tracks go.
  SM_ASSERT_TRUE(sfu->SetTransceiverDirection(p2.first, RtpTransceiverDirection::kInactive));
  SM_ASSERT_TRUE(sfu->SetTransceiverDirection(p2.second, RtpTransceiverDirection::kInactive));
  const rtp::AudioRecvId p2_audio = *tracks["2"].audio;
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(300));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(removed == std::vector<std::string>({"2", "3"}));
  SM_EXPECT_FALSE(r->session()->HasAudioReceive(p2_audio));
  SM_EXPECT_EQ(r->remote_tracks().size(), 4u);
  SM_EXPECT_EQ(Count(link->last_answer, "a=inactive\r\n"), 2);
  SM_EXPECT_TRUE(StateOfMid(*r, "2")->current_direction == RtpTransceiverDirection::kInactive);
  SM_EXPECT_TRUE(decoders.by_mid.count("0") == 1);

  // Participant 2 comes back on the same sections: the tracks are added again.
  SM_ASSERT_TRUE(sfu->SetTransceiverDirection(p2.first, RtpTransceiverDirection::kSendOnly));
  SM_ASSERT_TRUE(sfu->SetTransceiverDirection(p2.second, RtpTransceiverDirection::kSendOnly));
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(300));
  SM_EXPECT_EQ(r->remote_tracks().size(), 6u);
  SM_EXPECT_TRUE(r->session()->HasAudioReceive(*tracks["2"].audio));
  SM_EXPECT_TRUE(tracks["2"].audio->index != p2_audio.index);  // A new receive stream.

  // Participant 3's audio section is stopped: rejected (port 0) in the offer and the answer, the track goes, and the
  // receiver's transceiver is removed (RemoveStoppedTransceivers).
  SM_ASSERT_TRUE(sfu->StopTransceiver(p3.first));
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(300));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(MediaLines(link->last_offer)[4].rfind("m=audio 0 ", 0) == 0);
  SM_EXPECT_TRUE(MediaLines(link->last_answer)[4].rfind("m=audio 0 ", 0) == 0);
  SM_EXPECT_TRUE(std::count(removed.begin(), removed.end(), "4") == 1);
  SM_EXPECT_FALSE(StateOfMid(*r, "4").has_value());
  SM_EXPECT_FALSE(StateOfMid(*sfu, "4").has_value());
  SM_EXPECT_EQ(r->remote_tracks().size(), 5u);

  // Participant 4's audio recycles the stopped section: same m-line, a new MID (JSEP 5.2.2).
  TransceiverInit a4;
  a4.direction = RtpTransceiverDirection::kSendOnly;
  a4.stream_ids = {"p4"};
  a4.ssrc = 0x400;
  SM_ASSERT_TRUE(sfu->AddTransceiver(MediaType::AUDIO, a4) >= 0);
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(300));
  SM_ASSERT_TRUE(link->ok);
  const auto lines = MediaLines(link->last_offer);
  SM_EXPECT_EQ(lines.size(), 6u);
  SM_EXPECT_TRUE(lines[4].rfind("m=audio 9 ", 0) == 0);
  SM_EXPECT_TRUE(added.back() == "6");
  SM_EXPECT_TRUE(tracks["6"].ssrc == 0x400u && tracks["6"].stream_ids == std::vector<std::string>({"p4"}));
  SM_EXPECT_EQ(r->remote_tracks().size(), 6u);
  SM_EXPECT_TRUE(IceUfrag(link->last_answer) == ufrag);
  SM_EXPECT_TRUE(r->IsConnected());
}

// Each section's packets reach their own receive stream: two participants' RTP sessions behind the SFU send Opus and
// H.264 on their own SSRCs and MIDs; the receiver decodes each participant's audio with that track's decoder and
// assembles each one's video on its own stream, syncs each participant on its own, NACKs only the stream that lost a
// packet, and reports per-stream stats with their MIDs.
SM_TEST(PcRenegotiation, EachSectionIsDemuxedToItsOwnStreamWithItsOwnFeedbackAndStats) {
  EndpointWorld w;
  struct Participant {
    std::unique_ptr<rtp::Session> session;
    rtp::AudioSendId audio;
    rtp::VideoSendId video;
    int audio_sent = 0, video_sent = 0;
    std::vector<std::vector<uint8_t>> frames_sent;
  };
  std::vector<Participant> parts(2);
  PeerEndpoint* sfu = w.AddEndpoint("sfu", [&](PeerEndpointConfig& c) {
    SfuConfig(c, [&](std::span<const uint8_t> p, bool rtcp, Timestamp at) {
      // The receiver's RTCP goes to both participants, as an SFU forwarding feedback by SSRC would.
      if (!rtcp) return;
      for (Participant& pt : parts) {
        if (pt.session) pt.session->OnPacket(p, at);
      }
    });
  });
  Decoders decoders;
  PeerEndpoint* r = w.AddEndpoint("receiver", [&](PeerEndpointConfig& c) {
    c.session.av_sync = true;
    c.audio_decoder_factory = decoders.Factory();
    c.enable_data_channels = false;
    c.audio = false;
    c.video = false;
  });
  std::map<std::string, RemoteTrack> tracks;
  r->SetOnTrack([&](const RemoteTrack& t) { tracks[t.mid] = t; });
  EndpointWorld::Link* link = w.Connect(sfu, r);
  AddParticipant(sfu, 1);
  AddParticipant(sfu, 2);
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  SM_ASSERT_TRUE(w.RunUntil([&] { return sfu->IsConnected() && r->IsConnected(); }, TimeDelta::Seconds(10)));
  SM_ASSERT_TRUE(link->ok);
  SM_ASSERT_EQ(tracks.size(), 4u);

  // Each participant's session sends as the SFU's section k: its MIDs, the negotiated extensions and payload types.
  const SessionDescription* offer = sfu->local_description()->description();
  sync::ExtensionMap ext;
  for (const RtpExtension& e : offer->contents()[0].media_description()->rtp_header_extensions()) {
    // Without transport-wide sequence numbers: the two sessions' own counters would interleave on one transport (an
    // SFU numbers its egress itself).
    if (e.uri != RtpExtension::kTransportSequenceNumberUri) ext.Register(static_cast<uint8_t>(e.id), e.uri);
  }
  const auto& video_codecs = offer->contents()[1].media_description()->codecs();
  uint8_t h264_pt = 0, rtx_pt = 0;
  for (const Codec& c : video_codecs) {
    if (c.name == kH264CodecName && h264_pt == 0) h264_pt = static_cast<uint8_t>(c.id);
  }
  for (const Codec& c : video_codecs) {
    std::string apt;
    if (c.name == kRtxCodecName && c.GetParam(kCodecParamAssociatedPayloadType, &apt) && apt == std::to_string(h264_pt))
      rtx_pt = static_cast<uint8_t>(c.id);
  }
  SM_ASSERT_TRUE(h264_pt != 0 && rtx_pt != 0);
  for (int k = 0; k < 2; ++k) {
    rtp::SessionConfig sc;
    sc.local_ssrc = static_cast<uint32_t>(0x100 * (k + 1));
    sc.cname = "p" + std::to_string(k + 1);
    sc.extensions = ext;
    sc.mid_audio = std::to_string(2 * k);
    sc.mid_video = std::to_string(2 * k + 1);
    sc.send_side.gcc.start_rate = DataRate::KilobitsPerSec(800);
    parts[k].session = std::make_unique<rtp::Session>(sc);
    rtp::AudioStreamConfig a;
    a.ssrc = static_cast<uint32_t>(0x100 * (k + 1));
    a.payload_type = 111;
    a.nominal_rate = DataRate::KilobitsPerSec(40);
    parts[k].audio = parts[k].session->AddAudioSend(a);
    rtp::VideoStreamConfig v;
    v.ssrc = static_cast<uint32_t>(0x100 * (k + 1) + 1);
    v.rtx_ssrc = static_cast<uint32_t>(0x100 * (k + 1) + 2);
    v.payload_type = h264_pt;
    v.rtx_payload_type = rtx_pt;
    parts[k].video = parts[k].session->AddVideoSend(v);
  }
  // Media: every 20 ms an Opus frame tagged with the participant, every 33 ms an H.264 frame; participant 2's 40th
  // video packet is lost on the way to the receiver. The receiver pulls each track every 10 ms.
  Timestamp next = w.now;
  int video_packets_2 = 0;
  std::map<std::string, std::vector<std::vector<uint8_t>>> frames_received;
  const Timestamp media_start = w.now;
  w.SetHook(
      [&](Timestamp now) {
        if (now < next) return;
        for (int k = 0; k < 2; ++k) {
          Participant& p = parts[k];
          const int64_t ms = (now - media_start).ms();
          if (ms >= 20 * p.audio_sent) {
            std::vector<uint8_t> payload(50, static_cast<uint8_t>(k + 1));
            payload[1] = static_cast<uint8_t>(p.audio_sent);
            p.session->SendAudio(p.audio, {static_cast<uint32_t>(960 * p.audio_sent), payload, p.audio_sent == 0},
                                 now);
            ++p.audio_sent;
          }
          if (ms * 3 >= 100 * p.video_sent) {
            const bool key = p.video_sent % 30 == 0;
            std::vector<uint8_t> frame = rtp::MakeFakeH264Frame(key, key ? 5000 : 1200);
            frame.back() = static_cast<uint8_t>(16 * (k + 1) + p.video_sent % 16);
            p.session->SendVideo(p.video, {static_cast<uint32_t>(3000 * p.video_sent), frame, key}, now);
            p.frames_sent.push_back(std::move(frame));
            ++p.video_sent;
          }
          p.session->Process(now);
          for (rtp::OutgoingPacket& o : p.session->TakeOutgoing()) {
            if (k == 1 && o.kind == rtp::OutgoingKind::kVideo && ++video_packets_2 == 40) continue;
            sfu->SendMediaPacket(o.bytes, o.kind == rtp::OutgoingKind::kRtcp, now);
          }
        }
        std::vector<float> pcm(480);
        for (auto& [mid, t] : tracks) {
          if (t.audio) r->session()->GetAudio(*t.audio, pcm, now);
          if (t.video) {
            for (auto& f : r->session()->TakeVideoFrames(*t.video)) frames_received[mid].push_back(f.bitstream);
          }
        }
        next = now + TimeDelta::Millis(1);
      },
      [&] { return next; });
  w.Run(TimeDelta::Seconds(4));
  SM_EXPECT_TRUE(link->ok);
  for (int k = 0; k < 2; ++k) {
    const std::string amid = std::to_string(2 * k), vmid = std::to_string(2 * k + 1);
    // The track's decoder got that participant's frames, in order.
    SM_ASSERT_TRUE(decoders.by_mid.count(amid) == 1);
    const auto& got = decoders.by_mid[amid]->frames;
    SM_EXPECT_TRUE(static_cast<int>(got.size()) >= parts[k].audio_sent - 10);
    for (size_t i = 0; i < got.size(); ++i) {
      SM_EXPECT_TRUE(got[i][0] == k + 1 && got[i][1] == static_cast<uint8_t>(i));
    }
    // The video arrives whole and in order (keyframe SPS units are rewritten on the way out: compare the tail).
    const auto& frames = frames_received[vmid];
    SM_EXPECT_TRUE(static_cast<int>(frames.size()) >= parts[k].video_sent - 10);
    for (size_t i = 0; i < frames.size() && i < parts[k].frames_sent.size(); ++i) {
      const auto& sent = parts[k].frames_sent[i];
      SM_EXPECT_TRUE(frames[i].size() >= 1000 && std::equal(sent.end() - 1000, sent.end(), frames[i].end() - 1000));
    }
    // Each participant's video is synced with its own audio.
    SM_EXPECT_EQ(r->session()->synced_audio(*tracks[vmid].video)->index, tracks[amid].audio->index);
  }
  // Per-stream stats, with each section's MID; only participant 2's video was NACKed, and only its sender got the
  // NACK.
  rtp::StatsReport report;
  SM_ASSERT_TRUE(r->session()->GetStats(w.now, &report) == ObsStatus::kOk);
  SM_ASSERT_EQ(report.inbound.size(), 4u);
  std::map<std::string, rtp::InboundRtpStats> by_mid;
  for (const auto& in : report.inbound) by_mid[in.mid] = in;
  SM_EXPECT_EQ(by_mid["1"].ssrc, 0x101u);
  SM_EXPECT_EQ(by_mid["3"].ssrc, 0x201u);
  for (const char* mid : {"0", "1", "2", "3"}) SM_EXPECT_GT(by_mid[mid].packets_received, 50u);
  SM_EXPECT_EQ(by_mid["1"].nack_count, 0u);
  SM_EXPECT_GT(by_mid["3"].nack_count, 0u);
  SM_EXPECT_GT(by_mid["3"].rtx_packets_received, 0u);
  SM_EXPECT_EQ(parts[0].session->video_sender(parts[0].video).stats().rtx_packets, 0u);
  SM_EXPECT_GT(parts[1].session->video_sender(parts[1].video).stats().rtx_packets, 0u);
  SM_EXPECT_EQ(r->session()->stats().unroutable, 0u);
}

// Either side may re-offer: the answerer of the first exchange offers next, the first offerer answers keeping its DTLS
// role (passive: it is the DTLS server), and media keeps flowing both ways; then the first offerer re-offers.
SM_TEST(PcRenegotiation, EitherSideReoffersAndTheDtlsRoleStays) {
  EndpointWorld w;
  RecordingDecoder dec[2];
  PeerEndpoint* ep[2];
  for (int i = 0; i < 2; ++i) {
    ep[i] = w.AddEndpoint(i == 0 ? "a" : "b", [&dec, i](PeerEndpointConfig& c) {
      c.audio_decoder = &dec[i];
      c.enable_data_channels = false;
    });
  }
  EndpointWorld::Link* link = w.Connect(ep[0], ep[1]);
  int sent[2] = {0, 0};
  Timestamp next = Timestamp::PlusInfinity();
  w.SetHook(
      [&](Timestamp now) {
        if (now < next) return;
        for (int i = 0; i < 2; ++i) {
          if (!ep[i]->session() || !ep[i]->audio_send() || !ep[i]->IsConnected()) continue;
          std::vector<uint8_t> payload(40, static_cast<uint8_t>(i));
          ep[i]->session()->SendAudio(*ep[i]->audio_send(),
                                      {static_cast<uint32_t>(960 * sent[i]), payload, sent[i] == 0}, now);
          ++sent[i];
          ep[i]->FlushOutgoing(now);
          if (ep[i]->audio_receive()) {
            std::vector<float> pcm(480);
            ep[i]->session()->GetAudio(*ep[i]->audio_receive(), pcm, now);
          }
        }
        next = now + TimeDelta::Millis(20);
      },
      [&] { return next; });
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  SM_ASSERT_TRUE(w.RunUntil([&] { return ep[0]->IsConnected() && ep[1]->IsConnected(); }, TimeDelta::Seconds(10)));
  next = w.now;
  w.Run(TimeDelta::Seconds(1));
  const std::string ufrag_a = IceUfrag(link->last_offer);
  SM_EXPECT_TRUE(SdpHas(link->last_answer, "a=setup:active"));
  size_t before[2] = {dec[0].frames.size(), dec[1].frames.size()};
  SM_EXPECT_TRUE(before[0] > 20 && before[1] > 20);

  // B re-offers (actpass); A answers passive, as it is the DTLS server already.
  SM_ASSERT_TRUE(link->Offer(1, w.now));
  w.Run(TimeDelta::Seconds(1));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(SdpHas(link->last_offer, "a=setup:actpass"));
  SM_EXPECT_TRUE(SdpHas(link->last_answer, "a=setup:passive"));
  SM_EXPECT_TRUE(IceUfrag(link->last_answer) == ufrag_a);
  SM_EXPECT_TRUE(ep[0]->signaling_state() == SignalingState::kStable);
  SM_EXPECT_TRUE(ep[0]->IsConnected() && ep[1]->IsConnected());
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_TRUE(dec[i].frames.size() > before[i] + 30);
    before[i] = dec[i].frames.size();
  }
  // A re-offers; B answers active, as before.
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Seconds(1));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(SdpHas(link->last_answer, "a=setup:active"));
  for (int i = 0; i < 2; ++i) SM_EXPECT_TRUE(dec[i].frames.size() > before[i] + 30);
  SM_EXPECT_EQ(link->offers, 3);
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_EQ(ep[i]->media_transport()->rtp_transport()->rtp_decryption_failures(), 0);
    SM_EXPECT_EQ(ep[i]->remote_tracks().size(), 2u);
    SM_EXPECT_TRUE(StateOfMid(*ep[i], "0")->current_direction == RtpTransceiverDirection::kSendRecv);
  }
}

// AddTransceiver, SetTransceiverDirection and StopTransceiver as libwebrtc's: audio or video only; a recvonly
// transceiver is offered as its own section; without external media only the config's transceivers send; a stopped
// transceiver that was never negotiated is dropped at the next offer/answer.
SM_TEST(PcRenegotiation, TransceiverApi) {
  EndpointWorld w;
  RecordingDecoder dec;
  PeerEndpoint* a = w.AddEndpoint("a", [&](PeerEndpointConfig& c) {
    c.audio_decoder = &dec;
    c.enable_data_channels = false;
  });
  PeerEndpoint* b = w.AddEndpoint("b", [](PeerEndpointConfig& c) { c.enable_data_channels = false; });
  std::string error;
  SM_EXPECT_EQ(a->AddTransceiver(MediaType::DATA, {}, &error), -1);
  TransceiverInit send;
  send.direction = RtpTransceiverDirection::kSendOnly;
  SM_EXPECT_EQ(a->AddTransceiver(MediaType::VIDEO, send, &error), -1);
  SM_EXPECT_FALSE(error.empty());
  TransceiverInit recv;
  recv.direction = RtpTransceiverDirection::kRecvOnly;
  const int extra = a->AddTransceiver(MediaType::VIDEO, recv, &error);
  const int dropped = a->AddTransceiver(MediaType::AUDIO, recv, &error);
  SM_ASSERT_TRUE(extra == 2 && dropped == 3);
  SM_EXPECT_FALSE(a->SetTransceiverDirection(extra, RtpTransceiverDirection::kSendRecv));  // Own session: no send.
  SM_EXPECT_TRUE(a->StopTransceiver(dropped));
  SM_EXPECT_EQ(a->transceivers().size(), 4u);
  EndpointWorld::Link* link = w.Connect(a, b);
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(200));
  SM_ASSERT_TRUE(link->ok);
  // Audio, video, and the recvonly video; the stopped one never made it into an offer and is gone.
  const auto lines = MediaLines(link->last_offer);
  SM_ASSERT_EQ(lines.size(), 3u);
  SM_EXPECT_TRUE(lines[2].rfind("m=video 9 ", 0) == 0);
  SM_EXPECT_EQ(a->transceivers().size(), 3u);
  // B has no transceiver free for a recvonly video section, so it makes one that has nothing to send: inactive.
  SM_EXPECT_TRUE(SdpHas(link->last_answer, "a=inactive"));
  SM_EXPECT_FALSE(a->SetTransceiverDirection(dropped, RtpTransceiverDirection::kInactive));
  SM_EXPECT_FALSE(a->StopTransceiver(99));
}

// receive_remote_tracks off: sections no transceiver takes are rejected, as the endpoint did before C18.
SM_TEST(PcRenegotiation, ReceiveRemoteTracksOffRejectsSectionsWithoutATransceiver) {
  EndpointWorld w;
  PeerEndpoint* sfu = w.AddEndpoint("sfu", [](PeerEndpointConfig& c) { SfuConfig(c, nullptr); });
  RecordingDecoder dec;
  PeerEndpoint* r = w.AddEndpoint("receiver", [&](PeerEndpointConfig& c) {
    c.audio_decoder = &dec;
    c.video = false;
    c.receive_remote_tracks = false;
    c.enable_data_channels = false;
  });
  int tracks = 0;
  r->SetOnTrack([&](const RemoteTrack&) { ++tracks; });
  EndpointWorld::Link* link = w.Connect(sfu, r);
  TransceiverInit sr;
  sr.direction = RtpTransceiverDirection::kSendRecv;
  sr.ssrc = 0x100;
  sfu->AddTransceiver(MediaType::AUDIO, sr);
  AddParticipant(sfu, 2);
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  w.Run(TimeDelta::Millis(200));
  SM_ASSERT_TRUE(link->ok);
  const auto lines = MediaLines(link->last_answer);
  SM_ASSERT_EQ(lines.size(), 3u);
  SM_EXPECT_TRUE(lines[0].rfind("m=audio 9 ", 0) == 0);  // The receiver's own (AddTrack) audio.
  SM_EXPECT_TRUE(lines[1].rfind("m=audio 0 ", 0) == 0);
  SM_EXPECT_TRUE(lines[2].rfind("m=video 0 ", 0) == 0);
  SM_EXPECT_EQ(tracks, 1);
}

// Simulcast send (C17) through an SFU's re-offers (C18), as a LiveKit publisher: the client offers three layers by RID
// (no SSRCs), the SFU takes them (a=simulcast:recv, as LiveKit's SFU answers; this library's answerer doesn't receive
// simulcast, so the test adds it to the SFU's descriptions), and the client sends each layer on its own SSRC. Another
// participant joins: the SFU re-offers with that participant's sendonly sections. The client's answer signals the
// same RIDs and simulcast layers, again without SSRCs; its three send streams keep their RIDs and SSRCs and keep
// reaching the SFU, and the new participant's video is received on its own stream.
namespace {
std::string WithSimulcastRecv(std::string sdp) {
  const std::string anchor = "a=mid:1\r\n";
  const size_t at = sdp.find(anchor);
  if (at == std::string::npos) return sdp;
  sdp.insert(at + anchor.size(), "a=rid:q recv\r\na=rid:h recv\r\na=rid:f recv\r\na=simulcast:recv q;h;f\r\n");
  return sdp;
}

// The m= section of `sdp` with a=mid:<mid>.
std::string SectionOfMid(const std::string& sdp, const std::string& mid) {
  const size_t at = sdp.find("a=mid:" + mid + "\r\n");
  if (at == std::string::npos) return "";
  const size_t begin = sdp.rfind("m=", at);
  const size_t end = sdp.find("\r\nm=", at);
  return sdp.substr(begin, end == std::string::npos ? std::string::npos : end + 2 - begin);
}

uint32_t SsrcOf(std::span<const uint8_t> rtp) {
  return rtp.size() < 12 ? 0 : (uint32_t{rtp[8]} << 24) | (uint32_t{rtp[9]} << 16) | (uint32_t{rtp[10]} << 8) | rtp[11];
}
}  // namespace

SM_TEST(PcRenegotiation, SfuReofferKeepsTheSimulcastLayersAndAddsARemoteTrack) {
  EndpointWorld w;
  std::map<uint32_t, int> at_sfu;  // The client's RTP packets the SFU got, by SSRC.
  PeerEndpoint* sfu = w.AddEndpoint("sfu", [&](PeerEndpointConfig& c) {
    SfuConfig(c, [&](std::span<const uint8_t> p, bool rtcp, Timestamp) {
      if (!rtcp) ++at_sfu[SsrcOf(p)];
    });
  });
  Decoders decoders;
  PeerEndpoint* client = w.AddEndpoint("client", [&](PeerEndpointConfig& c) {
    c.audio_decoder_factory = decoders.Factory();
    c.enable_data_channels = false;
    c.video_simulcast_rids = {"q", "h", "f"};
  });
  std::map<std::string, RemoteTrack> tracks;
  client->SetOnTrack([&](const RemoteTrack& t) { tracks[t.mid] = t; });
  EndpointWorld::Link* link = w.Connect(client, sfu);
  link->munge = [](int to, SdpType, std::string sdp) { return to == 0 ? WithSimulcastRecv(std::move(sdp)) : sdp; };

  SM_ASSERT_TRUE(link->Offer(0, w.now));
  SM_ASSERT_TRUE(w.RunUntil([&] { return client->IsConnected() && sfu->IsConnected(); }, TimeDelta::Seconds(10)));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(SdpHas(link->last_offer, "a=simulcast:send q;h;f"));
  rtp::Session* s = client->session();
  SM_ASSERT_TRUE(s != nullptr);
  SM_ASSERT_EQ(client->video_simulcast_sends().size(), 3u);
  struct Layer {
    int index;
    std::string rid;
    uint32_t ssrc;
    std::optional<uint32_t> rtx_ssrc;
  };
  std::vector<Layer> layers;
  for (const rtp::VideoSendId id : client->video_simulcast_sends()) {
    const auto& c = s->video_sender(id).config();
    layers.push_back({id.index, c.rid, c.ssrc, c.rtx_ssrc});
  }
  for (const SimulcastEncoding& e : client->video_simulcast()) SM_EXPECT_TRUE(e.active);

  // Participant 2 joins: the SFU re-offers; the client answers.
  AddParticipant(sfu, 2);
  SM_ASSERT_TRUE(link->Offer(1, w.now));
  w.Run(TimeDelta::Millis(500));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(client->signaling_state() == SignalingState::kStable);
  SM_EXPECT_EQ(MediaLines(link->last_answer).size(), 4u);
  const std::string video = SectionOfMid(link->last_answer, "1");
  for (const char* line : {"a=rid:q send", "a=rid:h send", "a=rid:f send", "a=simulcast:send q;h;f"}) {
    SM_EXPECT_TRUE(SdpHas(video, line));
  }
  SM_EXPECT_TRUE(video.find("a=ssrc:") == std::string::npos);
  SM_ASSERT_TRUE(client->session() == s);
  SM_ASSERT_EQ(client->video_simulcast_sends().size(), 3u);
  for (size_t i = 0; i < 3; ++i) {
    const rtp::VideoSendId id = client->video_simulcast_sends()[i];
    const auto& c = s->video_sender(id).config();
    SM_EXPECT_EQ(id.index, layers[i].index);
    SM_EXPECT_EQ(c.rid, layers[i].rid);
    SM_EXPECT_EQ(c.ssrc, layers[i].ssrc);
    SM_EXPECT_TRUE(c.rtx_ssrc == layers[i].rtx_ssrc);
    SM_EXPECT_TRUE(client->video_simulcast()[i].active);
  }
  SM_ASSERT_TRUE(tracks.count("3") == 1 && tracks["3"].video.has_value());
  SM_EXPECT_EQ(tracks["3"].ssrc, 0x201u);
  SM_EXPECT_TRUE(s->HasVideoReceive(*tracks["3"].video));

  // Media: participant 2's session sends H.264 as the SFU's section 3; the client sends a frame on every layer.
  const MediaContentDescription* p2_video = sfu->local_description()->description()->GetContentDescriptionByName("3");
  SM_ASSERT_TRUE(p2_video != nullptr);
  uint8_t h264_pt = 0, rtx_pt = 0;
  for (const Codec& c : p2_video->codecs()) {
    if (c.name == kH264CodecName && h264_pt == 0) h264_pt = static_cast<uint8_t>(c.id);
  }
  for (const Codec& c : p2_video->codecs()) {
    std::string apt;
    if (c.name == kRtxCodecName && c.GetParam(kCodecParamAssociatedPayloadType, &apt) && apt == std::to_string(h264_pt))
      rtx_pt = static_cast<uint8_t>(c.id);
  }
  SM_ASSERT_TRUE(h264_pt != 0 && rtx_pt != 0);
  rtp::SessionConfig pc;
  pc.local_ssrc = 0x201;
  pc.cname = "p2";
  for (const RtpExtension& e : p2_video->rtp_header_extensions()) {
    if (e.uri != RtpExtension::kTransportSequenceNumberUri) pc.extensions.Register(static_cast<uint8_t>(e.id), e.uri);
  }
  pc.mid_video = "3";
  rtp::Session p2(pc);
  rtp::VideoStreamConfig pv;
  pv.ssrc = 0x201;
  pv.rtx_ssrc = 0x202;
  pv.payload_type = h264_pt;
  pv.rtx_payload_type = rtx_pt;
  const rtp::VideoSendId p2_send = p2.AddVideoSend(pv);
  at_sfu.clear();
  int frames = 0, received = 0;
  Timestamp next = w.now;
  w.SetHook(
      [&](Timestamp now) {
        if (now < next) return;
        const bool key = frames % 30 == 0;
        const std::vector<uint8_t> frame = rtp::MakeFakeH264Frame(key, key ? 4000 : 1000);
        p2.SendVideo(p2_send, {static_cast<uint32_t>(3000 * frames), frame, key}, now);
        p2.Process(now);
        for (rtp::OutgoingPacket& o : p2.TakeOutgoing()) {
          sfu->SendMediaPacket(o.bytes, o.kind == rtp::OutgoingKind::kRtcp, now);
        }
        for (const rtp::VideoSendId id : client->video_simulcast_sends()) {
          s->SendVideo(id, {static_cast<uint32_t>(3000 * frames), frame, key}, now);
        }
        client->FlushOutgoing(now);
        received += static_cast<int>(s->TakeVideoFrames(*tracks["3"].video).size());
        ++frames;
        next = now + TimeDelta::Millis(33);
      },
      [&] { return next; });
  w.Run(TimeDelta::Seconds(2));
  SM_EXPECT_TRUE(link->ok);
  SM_EXPECT_GT(received, frames - 10);
  for (const Layer& l : layers) SM_EXPECT_GT(at_sfu[l.ssrc], 20);
}

// Issue #1173: the SFU pauses a simulcast layer in a re-offer (a=simulcast:recv q;h;~f, as LiveKit does for a layer
// no subscriber watches) and resumes it in the next one. The client's rtp::SimulcastSendController takes the state
// the endpoint puts on the session: the paused layer is not encoded, nothing more reaches the SFU on its SSRC, and
// when it is resumed its first frame is a keyframe (libwebrtc M157: UpdateSimulcastLayerStatusInSender ->
// WebRtcVideoSendStream::SetRtpParameters -> VideoStreamEncoder::ReconfigureEncoder).
SM_TEST(PcRenegotiation, SfuReofferPausesAndResumesASimulcastLayer) {
  EndpointWorld w;
  std::map<uint32_t, int> at_sfu;  // The client's RTP packets the SFU got, by SSRC.
  PeerEndpoint* sfu = w.AddEndpoint("sfu", [&](PeerEndpointConfig& c) {
    SfuConfig(c, [&](std::span<const uint8_t> p, bool rtcp, Timestamp) {
      if (!rtcp) ++at_sfu[SsrcOf(p)];
    });
  });
  PeerEndpoint* client = w.AddEndpoint("client", [&](PeerEndpointConfig& c) {
    c.enable_data_channels = false;
    c.video_simulcast_rids = {"q", "h", "f"};
    // A start rate that covers every layer (the SFU here sends no transport feedback, so the estimate stays).
    c.session.send_side.gcc.start_rate = DataRate::KilobitsPerSec(3000);
    c.session.send_side.gcc.max_rate = DataRate::KilobitsPerSec(5000);
  });
  std::string recv_layers = "q;h;f";
  EndpointWorld::Link* link = w.Connect(client, sfu);
  link->munge = [&](int to, SdpType, std::string sdp) {
    if (to != 0) return sdp;
    const std::string anchor = "a=mid:1\r\n";
    const size_t at = sdp.find(anchor);
    if (at == std::string::npos) return sdp;
    sdp.insert(at + anchor.size(),
               "a=rid:q recv\r\na=rid:h recv\r\na=rid:f recv\r\na=simulcast:recv " + recv_layers + "\r\n");
    return sdp;
  };
  SM_ASSERT_TRUE(link->Offer(0, w.now));
  SM_ASSERT_TRUE(w.RunUntil([&] { return client->IsConnected() && sfu->IsConnected(); }, TimeDelta::Seconds(10)));
  SM_ASSERT_TRUE(link->ok);
  rtp::Session* s = client->session();
  SM_ASSERT_TRUE(s != nullptr);
  const std::vector<rtp::VideoSendId> ids = client->video_simulcast_sends();
  SM_ASSERT_EQ(ids.size(), 3u);
  const uint32_t q_ssrc = s->video_sender(ids[0]).config().ssrc;
  const uint32_t f_ssrc = s->video_sender(ids[2]).config().ssrc;

  // The app's encoder loop: one controller over the endpoint's layers, each encoded layer sent on its stream.
  rtp::SimulcastSendController sc({}, *s, ids);
  int frames = 0, f_encoded = 0;
  bool f_was_encoding = false, paused_once = false;
  std::vector<bool> f_restart_keyframes;  // The top layer's first frame each time it starts encoding again.
  Timestamp next = w.now;
  w.SetHook(
      [&](Timestamp now) {
        if (now < next) return;
        const rtp::SimulcastEncodeDecision d = sc.OnCapturedFrame({1280, 720}, now);
        for (size_t i = 0; i < d.layers.size(); ++i) {
          if (!d.layers[i].encode) continue;
          const bool key = d.layers[i].keyframe;
          const size_t bytes = static_cast<size_t>(std::max<int64_t>(300, d.layers[i].target.bps() / 8 / 30));
          const std::vector<uint8_t> frame = rtp::MakeFakeH264Frame(key, bytes);
          const DataSize sent = s->SendVideo(ids[i], {static_cast<uint32_t>(3000 * frames), frame, key}, now);
          sc.OnEncoded(i, {sent, key}, now);
        }
        const bool f_encoding = d.layers.size() == 3 && d.layers[2].encode;
        if (f_encoding) ++f_encoded;
        if (f_encoding && !f_was_encoding && paused_once) f_restart_keyframes.push_back(d.layers[2].keyframe);
        f_was_encoding = f_encoding;
        client->FlushOutgoing(now);
        ++frames;
        next = now + TimeDelta::Millis(33);
      },
      [&] { return next; });
  w.Run(TimeDelta::Seconds(2));
  SM_EXPECT_TRUE(sc.layer_active(2));
  SM_EXPECT_GT(at_sfu[f_ssrc], 20);

  // The SFU re-offers with the top layer paused.
  recv_layers = "q;h;~f";
  SM_ASSERT_TRUE(link->Offer(1, w.now));
  w.Run(TimeDelta::Millis(500));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_FALSE(client->video_simulcast()[2].active);
  SM_EXPECT_FALSE(s->video_send_active(ids[2]));
  SM_EXPECT_FALSE(sc.layer_active(2));
  SM_EXPECT_TRUE(sc.layer_paused(2));
  paused_once = true;
  const int f_at_pause = at_sfu[f_ssrc];
  const int f_encoded_at_pause = f_encoded;
  const int q_at_pause = at_sfu[q_ssrc];
  w.Run(TimeDelta::Seconds(2));
  SM_EXPECT_EQ(f_encoded, f_encoded_at_pause);
  SM_EXPECT_EQ(at_sfu[f_ssrc], f_at_pause);  // Nothing more on the paused layer's SSRC.
  SM_EXPECT_GT(at_sfu[q_ssrc], q_at_pause + 20);  // The others keep going.

  // The next re-offer resumes it: it is encoded again, starting with a keyframe, and reaches the SFU.
  recv_layers = "q;h;f";
  SM_ASSERT_TRUE(link->Offer(1, w.now));
  w.Run(TimeDelta::Seconds(2));
  SM_ASSERT_TRUE(link->ok);
  SM_EXPECT_TRUE(client->video_simulcast()[2].active);
  SM_EXPECT_TRUE(sc.layer_active(2));
  SM_ASSERT_EQ(f_restart_keyframes.size(), 1u);
  SM_EXPECT_TRUE(f_restart_keyframes[0]);
  SM_EXPECT_GT(f_encoded, f_encoded_at_pause + 10);
  SM_EXPECT_GT(at_sfu[f_ssrc], f_at_pause + 10);
}
