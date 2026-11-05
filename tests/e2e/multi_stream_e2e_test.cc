// A group call through an SFU-like forwarder (C18, #1160): five participants, each its own pc::PeerEndpoint with its
// own RTP session (Opus and H.264), connect over ICE and DTLS-SRTP to the forwarder's ingress endpoints; the forwarder
// keeps each stream's SSRC, rewrites the MID and the transport-wide sequence number for its egress as an SFU does,
// and forwards to one receiving endpoint over one BUNDLE transport. As an SFU, it terminates RTCP per leg: each
// ingress leg has a receive-only RTP session that reports to its participant (receiver reports, transport-wide
// feedback for the participant's congestion controller), and only the receiver's NACK, PLI and FIR go back. Participants join and leave by renegotiation (the
// forwarder's subsequent offers, as LiveKit's server sends them), so the receiver ends up with 5 audio and 5 video
// remote tracks at once, each decoded on its own stream, in order and whole, synced with its participant's audio,
// NACKed on its own, and reported with its MID.
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../unit/pc_endpoint/endpoint_world.h"
#include "sm/core/rtp_packet.h"
#include "sm/pc/codec.h"
#include "sm/rtp/h264.h"
#include "sm/rtp/fake_h264.h"
#include "sm/rtp/session.h"
#include "sm/sync/header_extensions.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using sm::pc::test::EndpointWorld;

namespace {

class RecordingDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t) override {
    frames.emplace_back(frame.begin(), frame.end());
    return std::vector<float>(960, 0.1f);
  }
  std::vector<std::vector<uint8_t>> frames;
};

class NullDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t) override {
    return std::vector<float>(960, 0.0f);
  }
};

struct Participant {
  int k = 0;
  PeerEndpoint* sender = nullptr;   // The participant's own endpoint.
  PeerEndpoint* ingress = nullptr;  // The forwarder's endpoint facing it.
  EndpointWorld::Link* link = nullptr;
  // The forwarder's RTP stack on this leg (receive-only): it reports to the participant.
  std::unique_ptr<rtp::Session> leg;
  NullDecoder leg_decoder;
  rtp::H264PayloadParser leg_parser;
  RecordingDecoder decoder;  // The participant's own audio decoder (it receives nothing here).
  std::pair<int, int> egress_transceivers{-1, -1};
  std::string audio_mid, video_mid;  // On the egress (and the receiver).
  uint32_t audio_ssrc = 0, video_ssrc = 0, rtx_ssrc = 0;
  bool media = false;
  bool want_keyframe = true;
  int audio_sent = 0, video_sent = 0;
  std::vector<std::vector<uint8_t>> frames_sent;
};

class Forwarder {
 public:
  PeerEndpoint* egress = nullptr;
  std::vector<Participant*> participants;
  sync::ExtensionMap ext;                 // The negotiated ids (the same table on every leg).
  std::map<uint8_t, uint8_t> rtx_pt_for;  // The egress leg's RTX payload type for a media payload type.
  int drop_video_packet_of = 0;           // Participant whose next video packet is lost on the way, once.
  uint64_t forwarded = 0, resent = 0;

  // Media from a participant's leg: its own RTX (padding, probes) stays on that leg; media goes on with the egress
  // MID and transport-wide sequence number, and is kept for retransmission. RTCP (sender reports) goes on unchanged.
  void FromParticipant(Participant& p, std::span<const uint8_t> packet, bool rtcp, Timestamp now) {
    if (p.leg) p.leg->OnPacket(packet, now);
    if (!p.media) return;
    if (rtcp) {
      egress->SendMediaPacket(packet, /*rtcp=*/true, now);
      return;
    }
    auto rtp = RtpPacket::Parse(packet);
    if (!rtp || rtp->ssrc == p.rtx_ssrc) return;
    const std::string mid = rtp->ssrc == p.audio_ssrc ? p.audio_mid : rtp->ssrc == p.video_ssrc ? p.video_mid : "";
    if (mid.empty()) return;
    Rewrite(*rtp, mid);
    auto& history = history_[rtp->ssrc];
    history[rtp->seq] = *rtp;
    if (history.size() > 600) history.erase(history.begin());
    if (drop_video_packet_of == p.k && rtp->ssrc == p.video_ssrc && !rtp->payload.empty()) {
      drop_video_packet_of = 0;  // Lost on the egress leg.
      return;
    }
    ++forwarded;
    egress->SendMediaPacket(rtp->Serialize(), /*rtcp=*/false, now);
  }

  // The receiver's RTCP: NACKs are answered from the history with RTX on the egress leg (an SFU's NACK responder);
  // PLI and FIR go to the participant whose stream they name, which then sends a keyframe.
  void FromReceiver(std::span<const uint8_t> packet, bool rtcp, Timestamp now) {
    if (!rtcp) return;
    size_t pos = 0;
    while (pos + 12 <= packet.size()) {
      const size_t len = (static_cast<size_t>(packet[pos + 2]) << 8 | packet[pos + 3]) * 4 + 4;
      if (pos + len > packet.size()) break;
      const int pt = packet[pos + 1], fmt = packet[pos] & 0x1F;
      const uint32_t media_ssrc = Be32(&packet[pos + 8]);
      if (pt == 205 && fmt == 1) {
        for (size_t f = pos + 12; f + 4 <= pos + len; f += 4) {
          const uint16_t pid = static_cast<uint16_t>(packet[f] << 8 | packet[f + 1]);
          const uint16_t blp = static_cast<uint16_t>(packet[f + 2] << 8 | packet[f + 3]);
          Resend(media_ssrc, pid, now);
          for (int b = 0; b < 16; ++b) {
            if (blp & (1 << b)) Resend(media_ssrc, static_cast<uint16_t>(pid + b + 1), now);
          }
        }
      } else if (pt == 206 && (fmt == 1 || fmt == 4)) {
        for (Participant* p : participants) {
          if (p->media && (media_ssrc == p->video_ssrc || fmt == 4)) {
            p->ingress->SendMediaPacket(packet.subspan(pos, len), /*rtcp=*/true, now);
          }
        }
      }
      pos += len;
    }
  }

  // Each leg's own RTCP to its participant (receiver reports, transport-wide feedback).
  void ProcessLegs(Timestamp now) {
    for (Participant* p : participants) {
      if (!p->leg) continue;
      p->leg->Process(now);
      for (rtp::OutgoingPacket& o : p->leg->TakeOutgoing()) {
        if (o.kind == rtp::OutgoingKind::kRtcp) p->ingress->SendMediaPacket(o.bytes, /*rtcp=*/true, now);
      }
    }
  }
  uint64_t resent_for(uint32_t ssrc) const {
    auto it = resent_by_ssrc_.find(ssrc);
    return it == resent_by_ssrc_.end() ? 0 : it->second;
  }

 private:
  static uint32_t Be32(const uint8_t* b) {
    return static_cast<uint32_t>(b[0]) << 24 | static_cast<uint32_t>(b[1]) << 16 | static_cast<uint32_t>(b[2]) << 8 |
           b[3];
  }
  // The egress leg's MID and transport-wide sequence number; every other element stays.
  void Rewrite(RtpPacket& rtp, const std::string& mid) {
    const auto mid_id = ext.IdOf(sync::uri::kMid);
    const auto tcc_id = ext.IdOf(sync::uri::kTransportCc);
    auto elements = sync::ParseHeaderExtensions(rtp.extension_profile, rtp.extension);
    if (!elements) return;
    for (sync::HeaderExtension& e : *elements) {
      if (mid_id && e.id == *mid_id) e.data.assign(mid.begin(), mid.end());
      if (tcc_id && e.id == *tcc_id) e.data = {static_cast<uint8_t>(tcc_seq_ >> 8), static_cast<uint8_t>(tcc_seq_)};
    }
    if (tcc_id) ++tcc_seq_;
    sync::WriteHeaderExtensions(*elements, rtp);
  }
  // RFC 4588: the original packet on the stream's RTX SSRC and payload type, with its sequence number first.
  void Resend(uint32_t ssrc, uint16_t seq, Timestamp now) {
    auto h = history_.find(ssrc);
    if (h == history_.end()) return;
    auto it = h->second.find(seq);
    if (it == h->second.end()) return;
    const Participant* owner = nullptr;
    for (Participant* p : participants) {
      if (p->video_ssrc == ssrc) owner = p;
    }
    auto pt = rtx_pt_for.find(it->second.payload_type);
    if (!owner || owner->rtx_ssrc == 0 || pt == rtx_pt_for.end()) return;
    RtpPacket rtx = it->second;
    rtx.ssrc = owner->rtx_ssrc;
    rtx.payload_type = pt->second;
    rtx.seq = rtx_seq_[owner->rtx_ssrc]++;
    rtx.payload.insert(rtx.payload.begin(), {static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq)});
    Rewrite(rtx, owner->video_mid);
    ++resent;
    ++resent_by_ssrc_[ssrc];
    egress->SendMediaPacket(rtx.Serialize(), /*rtcp=*/false, now);
  }
  uint16_t tcc_seq_ = 1;
  std::map<uint32_t, std::map<uint16_t, RtpPacket>> history_;
  std::map<uint32_t, uint16_t> rtx_seq_;
  std::map<uint32_t, uint64_t> resent_by_ssrc_;
};

std::optional<std::string> MidOf(const PeerEndpoint& ep, int index) {
  for (const auto& [i, s] : ep.transceivers()) {
    if (i == index) return s.mid;
  }
  return std::nullopt;
}

}  // namespace

SM_TEST(E2E_MultiStream, FiveParticipantsJoinAndLeaveThroughAForwarder) {
  EndpointWorld w;
  Forwarder fwd;
  fwd.egress = w.AddEndpoint("sfu-egress", [&](PeerEndpointConfig& c) {
    c.external_media = true;
    c.audio = false;
    c.video = false;
    c.enable_data_channels = false;
    c.on_media_packet = [&fwd](std::span<const uint8_t> p, bool rtcp, Timestamp at) { fwd.FromReceiver(p, rtcp, at); };
  });
  std::map<std::string, RecordingDecoder*> decoder_by_mid;
  PeerEndpoint* r = w.AddEndpoint("receiver", [&](PeerEndpointConfig& c) {
    c.audio = false;  // A subscriber: it only receives.
    c.video = false;
    c.enable_data_channels = false;
    c.session.av_sync = true;
    c.audio_decoder_factory = [&](const std::string& mid) -> std::unique_ptr<rtp::AudioDecoder> {
      // The endpoint owns the decoder; the test keeps a pointer while the track lives.
      auto d = std::make_unique<RecordingDecoder>();
      decoder_by_mid[mid] = d.get();
      return d;
    };
  });
  std::map<std::string, RemoteTrack> live;
  std::map<std::string, std::vector<std::vector<uint8_t>>> audio_frames, video_frames;
  std::map<std::string, rtp::ReceivedVideoFrame> last_frame;
  std::map<std::string, Timestamp> last_frame_at;
  std::vector<std::string> added, removed;
  r->SetOnTrack([&](const RemoteTrack& t) {
    added.push_back(t.mid);
    live[t.mid] = t;
  });
  r->SetOnRemoveTrack([&](const RemoteTrack& t) {
    removed.push_back(t.mid);
    if (decoder_by_mid.count(t.mid)) audio_frames[t.mid] = decoder_by_mid[t.mid]->frames;  // Before it goes.
    decoder_by_mid.erase(t.mid);
    live.erase(t.mid);
  });
  EndpointWorld::Link* sub = w.Connect(fwd.egress, r);
  std::vector<std::unique_ptr<Participant>> parts;

  // Media: each participant in the call sends an Opus frame every 20 ms (tagged with k and its index) and an H.264
  // frame every 33 ms (a keyframe first, every 30 frames and on request); the receiver pulls every track every 10 ms.
  Timestamp next = w.now;
  int64_t tick = 0;
  w.SetHook(
      [&](Timestamp now) {
        if (now < next) return;
        next = now + TimeDelta::Millis(10);
        ++tick;
        fwd.ProcessLegs(now);
        for (auto& p : parts) {
          if (!p->media || !p->sender->session()) continue;
          rtp::Session* s = p->sender->session();
          for (const auto& e : s->TakeEvents()) {
            if (e.type == rtp::SessionEventType::kKeyframeRequested) p->want_keyframe = true;
          }
          if (tick % 2 == 0) {
            std::vector<uint8_t> payload(50, static_cast<uint8_t>(p->k));
            payload[1] = static_cast<uint8_t>(p->audio_sent);
            s->SendAudio(*p->sender->audio_send(),
                         {static_cast<uint32_t>(960 * p->audio_sent), payload, p->audio_sent == 0}, now);
            ++p->audio_sent;
          }
          if (tick % 3 == 0) {
            const bool key = p->want_keyframe || p->video_sent % 30 == 0;
            p->want_keyframe = false;
            std::vector<uint8_t> frame = rtp::MakeFakeH264Frame(key, key ? 5000 : 1200);
            frame.back() = static_cast<uint8_t>(p->k * 40 + p->video_sent % 40);
            s->SendVideo(*p->sender->video_send(), {static_cast<uint32_t>(2700 * p->video_sent), frame, key}, now);
            p->frames_sent.push_back(std::move(frame));
            ++p->video_sent;
          }
          p->sender->FlushOutgoing(now);
        }
        if (!r->session()) return;
        std::vector<float> pcm(480);
        for (auto& [mid, t] : live) {
          if (t.audio) r->session()->GetAudio(*t.audio, pcm, now);
          if (t.video) {
            for (auto& f : r->session()->TakeVideoFrames(*t.video)) {
              video_frames[mid].push_back(f.bitstream);
              last_frame_at[mid] = now;
              last_frame[mid] = std::move(f);
            }
          }
        }
      },
      [&] { return next; });

  // A participant joins: its endpoint offers to its ingress endpoint; once connected the forwarder announces its two
  // tracks to the receiver in a subsequent offer, and its media starts.
  auto join = [&](std::vector<int> ks) {
    std::vector<Participant*> joining;
    for (int k : ks) {
      auto p = std::make_unique<Participant>();
      p->k = k;
      Participant* pp = p.get();
      p->sender = w.AddEndpoint("participant" + std::to_string(k), [pp](PeerEndpointConfig& c) {
        c.audio_decoder = &pp->decoder;
        c.enable_data_channels = false;
      });
      p->ingress = w.AddEndpoint("sfu-ingress" + std::to_string(k), [&fwd, pp](PeerEndpointConfig& c) {
        c.external_media = true;
        c.audio = false;
        c.video = false;
        c.enable_data_channels = false;
        c.on_media_packet = [&fwd, pp](std::span<const uint8_t> b, bool rtcp, Timestamp at) {
          fwd.FromParticipant(*pp, b, rtcp, at);
        };
      });
      p->link = w.Connect(p->sender, p->ingress);
      SM_ASSERT_TRUE(p->link->Offer(0, w.now));
      fwd.participants.push_back(pp);
      joining.push_back(pp);
      parts.push_back(std::move(p));
    }
    SM_ASSERT_TRUE(w.RunUntil(
        [&] {
          for (Participant* p : joining) {
            if (!p->sender->IsConnected() || !p->ingress->IsConnected() || p->ingress->remote_tracks().size() != 2) {
              return false;
            }
          }
          return true;
        },
        TimeDelta::Seconds(10)));
    for (Participant* p : joining) {
      SM_ASSERT_TRUE(p->link->ok);
      // The leg's receive-only session, on the leg's negotiated extensions, payload types and MIDs.
      const SessionDescription* answer = p->ingress->local_description()->description();
      rtp::SessionConfig lc;
      lc.local_ssrc = static_cast<uint32_t>(0xF000 + p->k);
      for (const RtpExtension& e : answer->contents()[0].media_description()->rtp_header_extensions()) {
        lc.extensions.Register(static_cast<uint8_t>(e.id), e.uri);
      }
      p->leg = std::make_unique<rtp::Session>(lc);
      for (const RemoteTrack& t : p->ingress->remote_tracks()) {
        const auto& codecs = answer->GetContentDescriptionByName(t.mid)->codecs();
        if (t.kind == MediaType::AUDIO) {
          p->audio_ssrc = t.ssrc;
          rtp::AudioStreamConfig a;
          a.ssrc = t.ssrc;
          a.payload_type = static_cast<uint8_t>(codecs[0].id);
          a.mid = t.mid;
          p->leg->AddAudioReceive(a, &p->leg_decoder);
        }
        if (t.kind == MediaType::VIDEO) {
          p->video_ssrc = t.ssrc;
          p->rtx_ssrc = t.rtx_ssrc.value_or(0);
          rtp::VideoStreamConfig v;
          v.ssrc = t.ssrc;
          v.rtx_ssrc = t.rtx_ssrc;
          // The answer's first H.264 (what the participant sends) and its RTX.
          const auto h264 = std::find_if(codecs.begin(), codecs.end(), [](const Codec& c) { return c.name == kH264CodecName; });
          SM_ASSERT_TRUE(h264 != codecs.end());
          v.payload_type = static_cast<uint8_t>(h264->id);
          for (const Codec& c : codecs) {
            std::string apt;
            if (c.name == kRtxCodecName && c.GetParam(kCodecParamAssociatedPayloadType, &apt) &&
                apt == std::to_string(h264->id)) {
              v.rtx_payload_type = static_cast<uint8_t>(c.id);
            }
          }
          v.parser = &p->leg_parser;
          v.mid = t.mid;
          p->leg->AddVideoReceive(v);
        }
      }
      TransceiverInit a;
      a.direction = RtpTransceiverDirection::kSendOnly;
      a.stream_ids = {"p" + std::to_string(p->k)};
      a.ssrc = p->audio_ssrc;
      TransceiverInit v = a;
      v.ssrc = p->video_ssrc;
      v.rtx_ssrc = p->rtx_ssrc;
      p->egress_transceivers = {fwd.egress->AddTransceiver(MediaType::AUDIO, a),
                                fwd.egress->AddTransceiver(MediaType::VIDEO, v)};
    }
    SM_ASSERT_TRUE(sub->Offer(0, w.now));
    SM_ASSERT_TRUE(w.RunUntil([&] { return !sub->Pending() && fwd.egress->IsConnected() && r->IsConnected(); },
                              TimeDelta::Seconds(10)));
    SM_ASSERT_TRUE(sub->ok);
    if (fwd.ext.IdOf(sync::uri::kMid) == std::nullopt) {
      const SessionDescription* offer = fwd.egress->local_description()->description();
      for (const RtpExtension& e : offer->contents()[0].media_description()->rtp_header_extensions()) {
        fwd.ext.Register(static_cast<uint8_t>(e.id), e.uri);
      }
      for (const Codec& c : offer->contents()[1].media_description()->codecs()) {
        std::string apt;
        if (c.name == kRtxCodecName && c.GetParam(kCodecParamAssociatedPayloadType, &apt)) {
          fwd.rtx_pt_for[static_cast<uint8_t>(std::stoi(apt))] = static_cast<uint8_t>(c.id);
        }
      }
    }
    for (Participant* p : joining) {
      p->audio_mid = *MidOf(*fwd.egress, p->egress_transceivers.first);
      p->video_mid = *MidOf(*fwd.egress, p->egress_transceivers.second);
      p->media = true;
    }
  };
  // A participant leaves: the forwarder makes its sections inactive (the receiver's tracks go) and stops forwarding.
  auto leave = [&](Participant& p) {
    p.media = false;
    SM_ASSERT_TRUE(fwd.egress->SetTransceiverDirection(p.egress_transceivers.first, RtpTransceiverDirection::kInactive));
    SM_ASSERT_TRUE(fwd.egress->SetTransceiverDirection(p.egress_transceivers.second, RtpTransceiverDirection::kInactive));
    SM_ASSERT_TRUE(sub->Offer(0, w.now));
  };

  join({1, 2, 3});
  SM_EXPECT_EQ(r->remote_tracks().size(), 6u);
  w.Run(TimeDelta::Seconds(4));
  fwd.drop_video_packet_of = 3;  // One of participant 3's video packets is lost: only its stream NACKs.
  join({4, 5});
  SM_EXPECT_EQ(r->remote_tracks().size(), 10u);  // 5 audio and 5 video remote tracks at once.
  w.Run(TimeDelta::Seconds(3));
  SM_ASSERT_TRUE(r->session() != nullptr);
  rtp::StatsReport all;
  SM_ASSERT_TRUE(r->session()->GetStats(w.now, &all) == ObsStatus::kOk);
  SM_EXPECT_EQ(all.inbound.size(), 10u);
  SM_EXPECT_EQ(r->session()->stats().unroutable, 0u);  // Every packet so far reached its stream.
  leave(*parts[1]);
  w.Run(TimeDelta::Seconds(5));
  SM_EXPECT_TRUE(sub->ok);

  // Signaling: 10 tracks added, participant 2's two removed; one BUNDLE transport, never restarted.
  SM_EXPECT_EQ(added.size(), 10u);
  SM_EXPECT_EQ(removed.size(), 2u);
  SM_EXPECT_TRUE(std::find(removed.begin(), removed.end(), parts[1]->audio_mid) != removed.end());
  SM_EXPECT_TRUE(std::find(removed.begin(), removed.end(), parts[1]->video_mid) != removed.end());
  SM_EXPECT_EQ(r->remote_tracks().size(), 8u);
  SM_EXPECT_EQ(r->transport_controller().transport_count(), 1u);
  SM_EXPECT_EQ(sub->offers, 3);
  SM_EXPECT_EQ(r->media_transport()->rtp_transport()->rtp_decryption_failures(), 0);
  // Only what participant 2 had in flight when it left reached no stream.
  SM_EXPECT_TRUE(r->session()->stats().unroutable <= 3);

  rtp::StatsReport report;
  SM_ASSERT_TRUE(r->session()->GetStats(w.now, &report) == ObsStatus::kOk);
  SM_EXPECT_EQ(report.inbound.size(), 8u);
  std::map<std::string, rtp::InboundRtpStats> by_mid;
  for (const auto& in : report.inbound) by_mid[in.mid] = in;
  for (auto& pp : parts) {
    Participant& p = *pp;
    if (&p == parts[1].get()) continue;
    const std::string& am = p.audio_mid;
    const std::string& vm = p.video_mid;
    // Audio: this participant's frames, in order, in its track's own decoder.
    SM_ASSERT_TRUE(decoder_by_mid.count(am) == 1);
    const auto& got = decoder_by_mid[am]->frames;
    SM_EXPECT_TRUE(static_cast<int>(got.size()) >= p.audio_sent - 15);
    for (size_t i = 0; i < got.size(); ++i) {
      SM_EXPECT_TRUE(got[i][0] == p.k && got[i][1] == static_cast<uint8_t>(i));
    }
    // Video: whole and in order (keyframe SPS units are rewritten on the way out: compare the last 1000 bytes).
    const auto& frames = video_frames[vm];
    SM_EXPECT_TRUE(static_cast<int>(frames.size()) >= p.video_sent - 15);
    for (size_t i = 0; i < frames.size() && i < p.frames_sent.size(); ++i) {
      const auto& sent = p.frames_sent[i];
      SM_EXPECT_TRUE(frames[i].size() >= 1000 && std::equal(sent.end() - 1000, sent.end(), frames[i].end() - 1000));
    }
    // Lip sync per participant: its video is paired with its own audio (sync group = msid stream "p<k>").
    const RemoteTrack& vt = live[vm];
    SM_ASSERT_TRUE(vt.video.has_value());
    SM_EXPECT_EQ(r->session()->synced_audio(*vt.video)->index, live[am].audio->index);
    // Stats per stream, with the section's MID and the participant's SSRCs; NACK only where a packet was lost.
    SM_EXPECT_EQ(by_mid[am].ssrc, p.audio_ssrc);
    SM_EXPECT_EQ(by_mid[vm].ssrc, p.video_ssrc);
    SM_EXPECT_GT(by_mid[vm].packets_received, 100u);
    SM_EXPECT_GT(by_mid[vm].frames_received, 50u);
    if (p.k == 3) {
      SM_EXPECT_GT(by_mid[vm].nack_count, 0u);
      SM_EXPECT_GT(by_mid[vm].rtx_packets_received, 0u);
      SM_EXPECT_GT(fwd.resent_for(p.video_ssrc), 0u);
    } else {
      SM_EXPECT_EQ(by_mid[vm].nack_count, 0u);
      SM_EXPECT_EQ(fwd.resent_for(p.video_ssrc), 0u);
    }
  }
  // Each participant's synchronizer has both streams' RTP-to-NTP mapping (its sender reports came through the
  // forwarder) and puts the participant's audio and video within a frame or two of each other.
  for (auto& pp : parts) {
    if (pp.get() == parts[1].get()) continue;
    const std::string& vm = pp->video_mid;
    const sync::RtpStreamsSynchronizer* sync = r->session()->av_sync(*live[vm].video);
    SM_ASSERT_TRUE(sync != nullptr && last_frame.count(vm) == 1);
    const auto offset = sync->GetStreamSyncOffsetInMs(last_frame[vm].rtp_timestamp, last_frame[vm].render_time.ms(),
                                                      last_frame_at[vm]);
    SM_ASSERT_TRUE(offset.has_value());
    SM_EXPECT_TRUE(std::abs(offset->stream_offset_ms) < 200);
  }
  // Participant 2's tracks: what arrived before it left was its own.
  SM_EXPECT_TRUE(audio_frames[parts[1]->audio_mid].size() > 100);
  for (const auto& f : audio_frames[parts[1]->audio_mid]) SM_EXPECT_EQ(f[0], 2);
}
