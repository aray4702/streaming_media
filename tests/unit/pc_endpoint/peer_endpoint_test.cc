// The endpoint that sets up a call from SDP (C6 part 3, #1064). JsepTransportController cases follow libwebrtc
// M157's pc/jsep_transport_controller_unittest.cc for C6's subset (ICE role, DTLS role, BUNDLE, rtcp-mux required,
// candidates before the remote description); the PeerEndpoint cases are the issue's acceptance test: two endpoints
// exchange offer and answer as SDP text and trickle candidates as a=candidate strings over a test signaling channel,
// on the emulated network (#985) with both ends behind NATs and a STUN server; ICE connects on server-reflexive
// candidates, DTLS-SRTP completes over the one BUNDLE transport, and Opus audio and H.264 video go both ways,
// encrypted on the wire and whole at the other end. With SM_WITH_DATACHANNEL, the data channel cases (C7 part 5,
// #1091): audio, video and data channels on one BUNDLE transport, and the per-connection switch off in the offer and
// in the answer; without it, the API reports "not supported" and a data section is rejected.
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/jsep_transport_controller.h"
#include "sm/pc/peer_endpoint.h"
#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL
#include "sm/pc/sctp_data_channel.h"
#include "sm/sctp/sctp_transport.h"
#endif
#include "sm/pc/webrtc_sdp.h"
#include "sm/rtp/fake_h264.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::pc;
using p2p::IpAddress;
using p2p::SocketAddress;

namespace {

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

const IpAddress kPublicIp[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
const IpAddress kPrivateIp[2] = {Ip("192.168.1.11"), Ip("192.168.2.22")};
const IpAddress kNatIp[2] = {Ip("77.77.77.77"), Ip("88.88.88.88")};
const SocketAddress kStunAddr(Ip("99.99.99.1"), 3478);

sim::LinkConfig Link(int delay_ms) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  return c;
}

// Records every Opus frame it is given, and plays 10 ms of samples for each.
class RecordingDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t) override {
    frames.emplace_back(frame.begin(), frame.end());
    return std::vector<float>(960, 0.1f);
  }
  std::vector<std::vector<uint8_t>> frames;
};

// The audio payload of frame `i` from side `side`.
std::vector<uint8_t> AudioPayload(int side, int i) {
  std::vector<uint8_t> p(60);
  for (size_t k = 0; k < p.size(); ++k) p[k] = static_cast<uint8_t>(side * 100 + i + static_cast<int>(k));
  p[0] = 0x78;  // An Opus TOC byte (CELT FB 20 ms, one frame).
  return p;
}

struct Host {
  std::unique_ptr<sim::EmulatedSocketServer> server;
  p2p::StaticNetworkManager network_manager;
  std::unique_ptr<p2p::BasicPortAllocator> allocator;
};

struct Network {
  sim::ByteNetwork net;
  std::unique_ptr<sim::EmulatedSocketServer> stun_host;
  std::unique_ptr<sim::StunServer> stun;
  Host host[2];
  Timestamp now = Timestamp::Seconds(10);

  // Both ends behind their own NAT of `type`, or public with `nat` false.
  explicit Network(bool nat = true, sim::NatType type = sim::NatType::kPortRestricted) {
    p2p::InstallStunCryptoFromTls();
    stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunAddr.ip(), sim::ByteNetwork::kPublicRealm, Link(5), Link(5)));
    stun = std::make_unique<sim::StunServer>(stun_host->CreateUdpSocket(kStunAddr, 0, 0));
    for (int i = 0; i < 2; ++i) {
      int realm = sim::ByteNetwork::kPublicRealm;
      IpAddress ip = kPublicIp[i];
      if (nat) {
        sim::NatConfig cfg;
        cfg.type = type;
        cfg.external_ip = kNatIp[i].v4();
        realm = net.NatRealm(net.AddNat(cfg));
        ip = kPrivateIp[i];
      }
      Host& h = host[i];
      h.server = std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(ip.v4(), realm, Link(10), Link(10)));
      h.network_manager.AddInterface(ip);
      h.allocator = std::make_unique<p2p::BasicPortAllocator>(&h.network_manager, h.server.get());
      h.allocator->SetConfiguration(now, p2p::ServerAddresses{kStunAddr}, {}, 0, p2p::NO_PRUNE);
      h.allocator->set_flags(p2p::kDefaultPortAllocatorFlags);
      h.allocator->set_step_delay(p2p::kMinimumStepDelay);
    }
  }
  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Host& h : host) t = std::min(t, h.allocator->NextPooledSessionsProcessTime());
    return t;
  }
  void Deliver() {
    for (Host& h : host) {
      h.allocator->ProcessPooledSessions(now);
      h.server->Deliver(now);
    }
    stun_host->Deliver(now);
  }
};

// Two endpoints and the signaling channel: SDP and candidates as text, delivered on the next step.
struct Call {
  Network network;
  RecordingDecoder decoder[2];
  std::unique_ptr<PeerEndpoint> ep[2];
  struct Message {
    bool candidate = false;
    SdpType type = SdpType::kOffer;
    std::string mid;
    int mline_index = 0;
    std::string text;
  };
  std::deque<Message> to[2];
  std::string offer_sdp, answer_sdp;
  bool signaling_ok = true;
  int candidates_sent[2] = {0, 0};

  explicit Call(bool nat = true, std::function<void(PeerEndpointConfig&, int)> customize = nullptr) : network(nat) {
    for (int i = 0; i < 2; ++i) {
      PeerEndpointConfig c;
      c.allocator = network.host[i].allocator.get();
      c.certificate_name = i == 0 ? "endpoint-a" : "endpoint-b";
      c.audio_decoder = &decoder[i];
      c.audio_stream.nominal_rate = DataRate::KilobitsPerSec(40);
      c.session.send_side.gcc.start_rate = DataRate::KilobitsPerSec(800);
      if (customize) customize(c, i);
      ep[i] = std::make_unique<PeerEndpoint>(std::move(c), network.now);
      ep[i]->SetOnIceCandidate([this, i](std::unique_ptr<IceCandidate> cand) {
        ++candidates_sent[i];
        to[1 - i].push_back(Message{true, SdpType::kOffer, cand->sdp_mid(), cand->sdp_mline_index(),
                                    cand->ToString()});
      });
    }
  }
  // A offers, B answers.
  void Negotiate() {
    std::string error;
    auto offer = ep[0]->CreateOffer(&error);
    SM_ASSERT_TRUE(offer != nullptr);
    offer_sdp = SdpSerialize(*offer);
    SM_ASSERT_TRUE(ep[0]->SetLocalDescription(std::move(offer), network.now, &error));
    to[1].push_back(Message{false, SdpType::kOffer, "", 0, offer_sdp});
  }
  void DeliverSignaling() {
    for (int i = 0; i < 2; ++i) {
      while (!to[i].empty()) {
        Message m = std::move(to[i].front());
        to[i].pop_front();
        std::string error;
        if (m.candidate) {
          SdpParseError perr;
          auto cand = IceCandidate::Create(m.mid, m.mline_index, m.text, &perr);
          signaling_ok = signaling_ok && cand && ep[i]->AddIceCandidate(*cand, network.now, &error);
          continue;
        }
        SdpParseError perr;
        auto desc = SdpDeserialize(m.type, m.text, &perr);
        if (!desc || !ep[i]->SetRemoteDescription(std::move(desc), network.now, &error)) {
          signaling_ok = false;
          continue;
        }
        if (m.type == SdpType::kOffer) {
          auto answer = ep[i]->CreateAnswer(&error);
          if (!answer) {
            signaling_ok = false;
            continue;
          }
          answer_sdp = SdpSerialize(*answer);
          signaling_ok = signaling_ok && ep[i]->SetLocalDescription(std::move(answer), network.now, &error);
          to[1 - i].push_back(Message{false, SdpType::kAnswer, "", 0, answer_sdp});
        }
      }
    }
  }

  // Media: every 20 ms an Opus frame, every 33 ms an H.264 frame (a keyframe first and every second), each way,
  // once the endpoint is connected; receivers pull 10 ms of audio every 10 ms.
  bool media = false;
  Timestamp next_audio[2] = {Timestamp::PlusInfinity(), Timestamp::PlusInfinity()};
  Timestamp next_video[2] = {Timestamp::PlusInfinity(), Timestamp::PlusInfinity()};
  Timestamp next_pull = Timestamp::PlusInfinity();
  int audio_sent[2] = {0, 0};
  int video_sent[2] = {0, 0};
  std::vector<std::vector<uint8_t>> video_frames_sent[2];
  std::vector<std::vector<uint8_t>> video_frames_received[2];

  void StartMedia() {
    media = true;
    for (int i = 0; i < 2; ++i) next_audio[i] = next_video[i] = network.now;
    next_pull = network.now;
  }
  void MediaTick() {
    if (!media) return;
    const Timestamp now = network.now;
    for (int i = 0; i < 2; ++i) {
      rtp::Session* s = ep[i]->session();
      if (!s || !ep[i]->IsConnected()) continue;
      if (now >= next_audio[i] && ep[i]->audio_send()) {
        const std::vector<uint8_t> payload = AudioPayload(i, audio_sent[i]);
        s->SendAudio(*ep[i]->audio_send(),
                     rtp::EncodedAudioFrame{static_cast<uint32_t>(audio_sent[i] * 960), payload, audio_sent[i] == 0},
                     now);
        ++audio_sent[i];
        next_audio[i] = now + TimeDelta::Millis(20);
      }
      if (now >= next_video[i] && ep[i]->video_send()) {
        const bool key = video_sent[i] % 30 == 0;
        std::vector<uint8_t> frame = rtp::MakeFakeH264Frame(key, key ? 6000 : 1500);
        frame.back() = static_cast<uint8_t>(video_sent[i]);
        s->SendVideo(*ep[i]->video_send(),
                     rtp::EncodedVideoFrame{static_cast<uint32_t>(video_sent[i] * 3000), frame, key}, now);
        video_frames_sent[i].push_back(std::move(frame));
        ++video_sent[i];
        next_video[i] = now + TimeDelta::Micros(33333);
      }
      ep[i]->FlushOutgoing(now);
    }
    if (now >= next_pull) {
      std::vector<float> pcm(480);
      for (int i = 0; i < 2; ++i) {
        rtp::Session* s = ep[i]->session();
        if (s && ep[i]->audio_receive()) s->GetAudio(*ep[i]->audio_receive(), pcm, now);
        if (s && ep[i]->video_receive()) {
          for (auto& f : s->TakeVideoFrames(*ep[i]->video_receive())) video_frames_received[i].push_back(f.bitstream);
        }
      }
      next_pull = now + TimeDelta::Millis(10);
    }
  }

  Timestamp NextEvent() const {
    Timestamp t = network.NextEvent();
    for (int i = 0; i < 2; ++i) {
      t = std::min(t, ep[i]->NextProcessTime(network.now));
      if (!to[i].empty()) t = std::min(t, network.now);
      if (media) t = std::min({t, next_audio[i], next_video[i]});
    }
    if (media) t = std::min(t, next_pull);
    return std::max(t, network.now);
  }
  void Step(Timestamp t) {
    network.now = std::max(network.now, t);
    for (auto& e : ep) e->Process(network.now);
    network.Deliver();
    DeliverSignaling();
    MediaTick();
  }
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = network.now + limit;
    for (int guard = 0; guard < 5000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        network.now = until;
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  void Run(TimeDelta d) {
    RunUntil([] { return false; }, d);
  }
  bool Connected() const { return ep[0]->IsConnected() && ep[1]->IsConnected(); }
};

bool Contains(const std::string& sdp, const std::string& line) { return sdp.find(line + "\r\n") != std::string::npos; }

// --- JsepTransportController ----------------------------------------------------------------------------------

// Descriptions from the offer/answer factory (#1063), with each side's certificate.
struct Factories {
  struct Side {
    explicit Side(const char* name) : cert(tls::SslIdentity::Create(name)) {
      tdf.set_certificate(cert);
      vendor = std::make_unique<CodecVendor>(DefaultAudioCodecs(), DefaultAudioCodecs(), DefaultVideoCodecs(),
                                             DefaultVideoCodecs());
      f = std::make_unique<MediaSessionDescriptionFactory>(&ssrcs, &tdf, vendor.get(), &suggester);
    }
    std::shared_ptr<const tls::SslIdentity> cert;
    TransportDescriptionFactory tdf;
    SdpPayloadTypeSuggester suggester;
    UniqueRandomIdGenerator ssrcs;
    std::unique_ptr<CodecVendor> vendor;
    std::unique_ptr<MediaSessionDescriptionFactory> f;
  };
  Side a{"a"}, b{"b"};
  static MediaSessionOptions Options(bool bundle) {
    MediaSessionOptions opts;
    opts.bundle_enabled = bundle;
    for (auto [type, mid] : {std::pair{MediaType::AUDIO, "0"}, std::pair{MediaType::VIDEO, "1"}}) {
      MediaDescriptionOptions o(type, mid, RtpTransceiverDirection::kSendRecv, false);
      o.header_extensions =
          type == MediaType::AUDIO ? DefaultAudioRtpHeaderExtensions() : DefaultVideoRtpHeaderExtensions();
      opts.media_description_options.push_back(o);
    }
    return opts;
  }
};

struct ControllerFixture {
  Network network{false};
  Factories factories;
  JsepTransportController c0{network.host[0].allocator.get(), {}};
  JsepTransportController c1{network.host[1].allocator.get(), {}};
  ControllerFixture() {
    c0.SetLocalCertificate(factories.a.cert);
    c1.SetLocalCertificate(factories.b.cert);
  }
};

}  // namespace

// SetIceRole / the ICE role cases: the side applying a local offer first is controlling, the answerer controlled;
// a later remote offer doesn't change it.
SM_TEST(PcJsepTransportController, IceRoleFromTheFirstLocalDescription) {
  ControllerFixture t;
  auto offer = t.factories.a.f->CreateOfferOrError(Factories::Options(true), nullptr);
  auto answer = t.factories.b.f->CreateAnswerOrError(offer.get(), Factories::Options(true), nullptr);
  SM_ASSERT_TRUE(t.c0.SetLocalDescription(SdpType::kOffer, offer.get(), nullptr, t.network.now));
  SM_ASSERT_TRUE(t.c1.SetRemoteDescription(SdpType::kOffer, nullptr, offer.get(), t.network.now));
  SM_ASSERT_TRUE(t.c1.SetLocalDescription(SdpType::kAnswer, answer.get(), offer.get(), t.network.now));
  SM_ASSERT_TRUE(t.c0.SetRemoteDescription(SdpType::kAnswer, offer.get(), answer.get(), t.network.now));
  SM_EXPECT_TRUE(t.c0.ice_role() == p2p::ICEROLE_CONTROLLING);
  SM_EXPECT_TRUE(t.c1.ice_role() == p2p::ICEROLE_CONTROLLED);
  SM_EXPECT_TRUE(t.c0.GetJsepTransportForMid("0")->ice_transport()->GetIceRole() == p2p::ICEROLE_CONTROLLING);
  SM_EXPECT_TRUE(t.c1.GetJsepTransportForMid("0")->ice_transport()->GetIceRole() == p2p::ICEROLE_CONTROLLED);
}

// SetDtlsRole cases: an actpass offer answered with active makes the offerer the DTLS server and the answerer the
// client, on every bundled mid.
SM_TEST(PcJsepTransportController, DtlsRoleFromSetup) {
  ControllerFixture t;
  auto offer = t.factories.a.f->CreateOfferOrError(Factories::Options(true), nullptr);
  auto answer = t.factories.b.f->CreateAnswerOrError(offer.get(), Factories::Options(true), nullptr);
  SM_EXPECT_TRUE(offer->transport_infos()[0].description.connection_role == p2p::ConnectionRole::kActpass);
  SM_EXPECT_TRUE(answer->transport_infos()[0].description.connection_role == p2p::ConnectionRole::kActive);
  SM_ASSERT_TRUE(t.c0.SetLocalDescription(SdpType::kOffer, offer.get(), nullptr, t.network.now));
  SM_EXPECT_FALSE(t.c0.GetDtlsRole("0").has_value());  // Not negotiated before the answer.
  SM_ASSERT_TRUE(t.c1.SetRemoteDescription(SdpType::kOffer, nullptr, offer.get(), t.network.now));
  SM_ASSERT_TRUE(t.c1.SetLocalDescription(SdpType::kAnswer, answer.get(), offer.get(), t.network.now));
  SM_ASSERT_TRUE(t.c0.SetRemoteDescription(SdpType::kAnswer, offer.get(), answer.get(), t.network.now));
  for (const char* mid : {"0", "1"}) {
    SM_EXPECT_TRUE(t.c0.GetDtlsRole(mid) == std::optional<p2p::SslRole>(p2p::SslRole::kServer));
    SM_EXPECT_TRUE(t.c1.GetDtlsRole(mid) == std::optional<p2p::SslRole>(p2p::SslRole::kClient));
  }
}

// The BUNDLE cases with the balanced policy: an offer creates a transport per m= section (no group is established
// yet); the answer's group moves both mids onto the first one's transport and the other is destroyed. The answerer
// bundles at once.
SM_TEST(PcJsepTransportController, BundleOntoOneTransportOnAnswer) {
  ControllerFixture t;
  auto offer = t.factories.a.f->CreateOfferOrError(Factories::Options(true), nullptr);
  auto answer = t.factories.b.f->CreateAnswerOrError(offer.get(), Factories::Options(true), nullptr);
  SM_ASSERT_TRUE(t.c0.SetLocalDescription(SdpType::kOffer, offer.get(), nullptr, t.network.now));
  SM_EXPECT_EQ(t.c0.transport_count(), 2u);
  SM_EXPECT_TRUE(t.c0.GetRtpTransport("0") != t.c0.GetRtpTransport("1"));
  SM_ASSERT_TRUE(t.c1.SetRemoteDescription(SdpType::kOffer, nullptr, offer.get(), t.network.now));
  SM_ASSERT_TRUE(t.c1.SetLocalDescription(SdpType::kAnswer, answer.get(), offer.get(), t.network.now));
  SM_EXPECT_EQ(t.c1.transport_count(), 1u);
  SM_ASSERT_TRUE(t.c0.SetRemoteDescription(SdpType::kAnswer, offer.get(), answer.get(), t.network.now));
  SM_EXPECT_EQ(t.c0.transport_count(), 1u);
  SM_EXPECT_TRUE(t.c0.GetRtpTransport("0") == t.c0.GetRtpTransport("1"));
  SM_EXPECT_TRUE(t.c0.GetRtpTransport("0") != nullptr);
  SM_EXPECT_EQ(t.c0.bundle_groups().size(), 1u);
}

// max-bundle: the offer's group is used at once, so there is one transport from the start.
SM_TEST(PcJsepTransportController, MaxBundleBundlesTheOffer) {
  Network network{false};
  Factories factories;
  JsepTransportController::Config cfg;
  cfg.bundle_policy = BundlePolicy::kMaxBundle;
  JsepTransportController c(network.host[0].allocator.get(), cfg);
  c.SetLocalCertificate(factories.a.cert);
  auto offer = factories.a.f->CreateOfferOrError(Factories::Options(true), nullptr);
  SM_ASSERT_TRUE(c.SetLocalDescription(SdpType::kOffer, offer.get(), nullptr, network.now));
  SM_EXPECT_EQ(c.transport_count(), 1u);
  // An offer without a group can't be used with max-bundle.
  auto unbundled = factories.a.f->CreateOfferOrError(Factories::Options(false), nullptr);
  JsepTransportController c2(network.host[0].allocator.get(), cfg);
  c2.SetLocalCertificate(factories.a.cert);
  std::string error;
  SM_EXPECT_FALSE(c2.SetLocalDescription(SdpType::kOffer, unbundled.get(), nullptr, network.now, &error));
  SM_EXPECT_EQ(error, std::string("max-bundle is used but no bundle group found."));
}

// ValidateContent with kRtcpMuxPolicyRequire, and an answer's BUNDLE group that the offer didn't have.
SM_TEST(PcJsepTransportController, RefusesWhatLibwebrtcRefuses) {
  ControllerFixture t;
  auto offer = t.factories.a.f->CreateOfferOrError(Factories::Options(true), nullptr);
  auto no_mux = offer->Clone();
  for (ContentInfo& c : no_mux->contents()) c.media_description()->set_rtcp_mux(false);
  std::string error;
  SM_EXPECT_FALSE(t.c0.SetLocalDescription(SdpType::kOffer, no_mux.get(), nullptr, t.network.now, &error));
  SM_EXPECT_TRUE(error.find("RTCP-MUX is not enabled when it is required") != std::string::npos);

  auto unbundled_offer = t.factories.a.f->CreateOfferOrError(Factories::Options(false), nullptr);
  auto answer = t.factories.b.f->CreateAnswerOrError(offer.get(), Factories::Options(true), nullptr);
  SM_ASSERT_TRUE(t.c1.SetRemoteDescription(SdpType::kOffer, nullptr, unbundled_offer.get(), t.network.now));
  SM_EXPECT_FALSE(t.c1.SetLocalDescription(SdpType::kAnswer, answer.get(), unbundled_offer.get(), t.network.now,
                                           &error));
  SM_EXPECT_EQ(error, std::string("A BUNDLE group was added in the answer that did not exist in the offer."));
}

// AddRemoteCandidates needs the remote description, and the transport of the candidate's mid.
SM_TEST(PcJsepTransportController, CandidatesNeedTheRemoteDescription) {
  ControllerFixture t;
  auto offer = t.factories.a.f->CreateOfferOrError(Factories::Options(true), nullptr);
  SM_ASSERT_TRUE(t.c0.SetLocalDescription(SdpType::kOffer, offer.get(), nullptr, t.network.now));
  p2p::Candidate c;
  c.set_component(p2p::ICE_CANDIDATE_COMPONENT_RTP);
  c.set_address(SocketAddress(kPublicIp[1], 1234));
  std::string error;
  SM_EXPECT_FALSE(t.c0.AddRemoteCandidates("0", {c}, t.network.now, &error));
  SM_EXPECT_TRUE(error.find("remote description is not set") != std::string::npos);
  SM_EXPECT_FALSE(t.c0.AddRemoteCandidates("7", {c}, t.network.now, &error));
}

// --- PeerEndpoint ----------------------------------------------------------------------------------------------

// The issue's acceptance test: offer and answer as SDP text, trickled candidates as text, both ends behind NATs with a
// STUN server; ICE connects (server-reflexive candidates), DTLS-SRTP completes on one BUNDLE transport, and Opus and
// H.264 go both ways, encrypted, every frame whole at the other end.
SM_TEST(PcPeerEndpoint, CallOverNatsWithStunSendsOpusAndH264BothWays) {
  Call call;
  call.Negotiate();
  SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected(); }, TimeDelta::Seconds(10)));
  SM_EXPECT_TRUE(call.signaling_ok);
  SM_EXPECT_TRUE(call.ep[0]->signaling_state() == SignalingState::kStable);
  SM_EXPECT_TRUE(call.ep[1]->signaling_state() == SignalingState::kStable);
  // The SDP on the wire: BUNDLE, rtcp-mux, DTLS-SRTP, Opus and H.264 with RTX, trickle ICE.
  for (const std::string* sdp : {&call.offer_sdp, &call.answer_sdp}) {
    for (const char* line : {"a=group:BUNDLE 0 1", "a=rtcp-mux", "m=audio 9 UDP/TLS/RTP/SAVPF 111",
                             "a=rtpmap:111 opus/48000/2", "a=rtpmap:96 H264/90000", "a=fmtp:97 apt=96",
                             "a=ice-options:trickle"}) {
      SM_EXPECT_TRUE(Contains(*sdp, line));
    }
  }
  SM_EXPECT_TRUE(Contains(call.offer_sdp, "a=setup:actpass"));
  SM_EXPECT_TRUE(Contains(call.answer_sdp, "a=setup:active"));
  SM_EXPECT_TRUE(call.candidates_sent[0] > 0 && call.candidates_sent[1] > 0);
  for (int i = 0; i < 2; ++i) {
    JsepTransportController& tc = call.ep[i]->transport_controller();
    SM_EXPECT_EQ(tc.transport_count(), 1u);  // BUNDLE: one ICE + DTLS-SRTP transport.
    SM_EXPECT_TRUE(tc.GetRtpTransport("0") == tc.GetRtpTransport("1"));
    const p2p::Connection* conn = call.ep[i]->media_transport()->ice_transport()->selected_connection();
    SM_ASSERT_TRUE(conn != nullptr);
    // Behind port-restricted NATs the pair is server-reflexive at both ends (as each end sees the other).
    SM_EXPECT_TRUE(conn->remote_candidate().address().ipaddr() == kNatIp[1 - i]);
  }
  SM_EXPECT_TRUE(call.ep[0]->transport_controller().ice_role() == p2p::ICEROLE_CONTROLLING);
  SM_EXPECT_TRUE(call.ep[0]->transport_controller().GetDtlsRole("0") ==
                 std::optional<p2p::SslRole>(p2p::SslRole::kServer));
  SM_EXPECT_TRUE(call.ep[1]->transport_controller().GetDtlsRole("0") ==
                 std::optional<p2p::SslRole>(p2p::SslRole::kClient));

  call.StartMedia();
  call.Run(TimeDelta::Seconds(3));
  for (int i = 0; i < 2; ++i) {
    const int peer = 1 - i;
    // Every Opus frame the peer sent arrived whole (the last few may still be in its jitter buffer).
    SM_EXPECT_TRUE(call.audio_sent[peer] >= 140);
    const auto& got = call.decoder[i].frames;
    SM_EXPECT_TRUE(static_cast<int>(got.size()) >= call.audio_sent[peer] - 10);
    for (size_t k = 0; k < got.size(); ++k) SM_EXPECT_TRUE(got[k] == AudioPayload(peer, static_cast<int>(k)));
    // H.264 access units arrive whole and in order.
    SM_EXPECT_TRUE(call.video_sent[peer] >= 85);
    const auto& frames = call.video_frames_received[i];
    SM_EXPECT_TRUE(static_cast<int>(frames.size()) >= call.video_sent[peer] - 10);
    // A keyframe's SPS gets its VUI rewritten on the way out (SpsVuiRewriter, as libwebrtc's RtpVideoSender), so
    // keyframes are compared from their IDR slice on.
    for (size_t k = 0; k < frames.size(); ++k) {
      const std::vector<uint8_t>& sent = call.video_frames_sent[peer][k];
      if (k % 30 != 0) {
        SM_EXPECT_TRUE(frames[k] == sent);
      } else {
        SM_EXPECT_TRUE(frames[k].size() >= 5000 &&
                       std::equal(sent.end() - 5000, sent.end(), frames[k].end() - 5000));
      }
    }
    // Nothing failed to decrypt.
    SM_EXPECT_EQ(call.ep[i]->media_transport()->rtp_transport()->rtp_decryption_failures(), 0);
    SM_EXPECT_EQ(call.ep[i]->media_transport()->rtp_transport()->rtcp_decryption_failures(), 0);
    // RTCP went both ways.
    SM_EXPECT_TRUE(call.ep[i]->session()->stats().rtcp_in > 0);
  }
}

// Media is SRTP on the wire: no RTP packet a socket carries has the plain payload.
SM_TEST(PcPeerEndpoint, MediaIsEncryptedOnTheWire) {
  Call call(/*nat=*/false);
  call.Negotiate();
  SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected(); }, TimeDelta::Seconds(10)));
  call.StartMedia();
  call.Run(TimeDelta::Seconds(1));
  // DtlsSrtpTransport refuses to send without SRTP keys, and the receive side unprotects every packet: a packet sent
  // in the clear would have been counted as a decryption failure at the peer.
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_TRUE(call.ep[i]->media_transport()->rtp_transport()->IsSrtpActive());
    SM_EXPECT_EQ(call.ep[i]->media_transport()->rtp_transport()->rtp_decryption_failures(), 0);
    SM_EXPECT_TRUE(call.decoder[i].frames.size() > 30);
  }
}

// The answer follows the offer's sections: with data channels off for the connection a data section is rejected,
// audio and video accepted; signaling states and calls in the wrong state.
SM_TEST(PcPeerEndpoint, AnswerRejectsDataAndChecksSignalingState) {
  Network network{false};
  Factories factories;
  RecordingDecoder dec;
  PeerEndpointConfig cfg;
  cfg.allocator = network.host[1].allocator.get();
  cfg.audio_decoder = &dec;
  cfg.enable_data_channels = false;
  PeerEndpoint ep(std::move(cfg), network.now);
  std::string error;
  SM_EXPECT_TRUE(ep.CreateAnswer(&error) == nullptr);
  MediaSessionOptions opts = Factories::Options(true);
  for (auto& o : opts.media_description_options) {
    if (o.type == MediaType::AUDIO) o.AddAudioSender("a", {"s"});
    else o.AddVideoSender("v", {"s"}, {}, SimulcastLayerList(), 1);
  }
  opts.media_description_options.emplace_back(MediaType::DATA, "2", RtpTransceiverDirection::kSendRecv, false);
  auto offer_desc = factories.a.f->CreateOfferOrError(opts, nullptr);
  SM_ASSERT_TRUE(offer_desc != nullptr);
  auto offer = SessionDescriptionInterface::Create(SdpType::kOffer, std::move(offer_desc), "1", "2");
  // An answer can't be applied in stable.
  auto bogus = offer->Clone();
  auto as_answer = SessionDescriptionInterface::Create(SdpType::kAnswer, bogus->description()->Clone(), "1", "2");
  SM_EXPECT_FALSE(ep.SetRemoteDescription(std::move(as_answer), network.now, &error));
  SM_ASSERT_TRUE(ep.SetRemoteDescription(std::move(offer), network.now, &error));
  SM_EXPECT_TRUE(ep.signaling_state() == SignalingState::kHaveRemoteOffer);
  auto answer = ep.CreateAnswer(&error);
  SM_ASSERT_TRUE(answer != nullptr);
  const auto& contents = answer->description()->contents();
  SM_ASSERT_EQ(contents.size(), 3u);
  SM_EXPECT_FALSE(contents[0].rejected);
  SM_EXPECT_FALSE(contents[1].rejected);
  SM_EXPECT_TRUE(contents[2].rejected);
  SM_ASSERT_TRUE(ep.SetLocalDescription(std::move(answer), network.now, &error));
  SM_EXPECT_TRUE(ep.signaling_state() == SignalingState::kStable);
  // The session has both send and both receive streams, on the answer's payload types.
  SM_ASSERT_TRUE(ep.session() != nullptr);
  SM_EXPECT_TRUE(ep.audio_send().has_value() && ep.video_send().has_value());
  SM_EXPECT_TRUE(ep.audio_receive().has_value() && ep.video_receive().has_value());
  SM_EXPECT_EQ(ep.transport_controller().transport_count(), 1u);
  // A candidate for the rejected section is ignored.
  p2p::Candidate c;
  c.set_component(p2p::ICE_CANDIDATE_COMPONENT_RTP);
  c.set_address(SocketAddress(kPublicIp[0], 1234));
  SM_EXPECT_TRUE(ep.AddIceCandidate(IceCandidate("2", 2, c), network.now, &error));
  // No renegotiation yet.
  SM_EXPECT_TRUE(ep.CreateOffer(&error) == nullptr);
  // No SCTP object with the switch off.
  SM_EXPECT_FALSE(ep.data_channels_enabled());
  SM_EXPECT_TRUE(ep.sctp_transport() == nullptr);
}

// External media (#1087, the test bench's `transport ice`): the host runs its own RTP sessions. The endpoint makes
// none, announces the host's SSRCs in its descriptions (SSRC munging, with the FID group for RTX), and carries the
// host's RTP and RTCP as SRTP over the BUNDLE transport, unchanged end to end; a packet on an SSRC no section has
// is not delivered (the demuxer).
SM_TEST(PcPeerEndpoint, ExternalMediaCarriesTheHostsPackets) {
  std::vector<std::pair<std::vector<uint8_t>, bool>> got[2];
  Call call(/*nat=*/true, [&got](PeerEndpointConfig& c, int i) {
    c.external_media = true;
    c.audio_ssrc = 1 + 10 * i;
    c.video_ssrc = 2 + 10 * i;
    c.video_rtx_ssrc = 3 + 10 * i;
    c.on_media_packet = [&got, i](std::span<const uint8_t> p, bool rtcp, Timestamp) {
      got[i].emplace_back(std::vector<uint8_t>(p.begin(), p.end()), rtcp);
    };
  });
  call.Negotiate();
  SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected(); }, TimeDelta::Seconds(10)));
  SM_EXPECT_TRUE(call.signaling_ok);
  SM_EXPECT_TRUE(call.offer_sdp.find("a=ssrc:1 cname:") != std::string::npos);
  SM_EXPECT_TRUE(Contains(call.offer_sdp, "a=ssrc-group:FID 2 3"));
  SM_EXPECT_TRUE(call.answer_sdp.find("a=ssrc:11 cname:") != std::string::npos);
  SM_EXPECT_TRUE(Contains(call.answer_sdp, "a=ssrc-group:FID 12 13"));
  for (int i = 0; i < 2; ++i) SM_EXPECT_TRUE(call.ep[i]->session() == nullptr);

  auto rtp = [](uint32_t ssrc, uint16_t seq, uint8_t pt) {
    std::vector<uint8_t> p = {0x80, pt, static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq), 0, 0, 0x10, 0,
                              static_cast<uint8_t>(ssrc >> 24), static_cast<uint8_t>(ssrc >> 16),
                              static_cast<uint8_t>(ssrc >> 8), static_cast<uint8_t>(ssrc)};
    for (int k = 0; k < 100; ++k) p.push_back(static_cast<uint8_t>(seq + k));
    return p;
  };
  // A receiver report with no blocks, from SSRC 200.
  const std::vector<uint8_t> rr = {0x80, 201, 0x00, 0x01, 0x00, 0x00, 0x00, 200};
  std::vector<std::vector<uint8_t>> sent;
  for (uint16_t k = 0; k < 10; ++k) {
    for (uint32_t ssrc : {1u, 2u, 3u}) {
      sent.push_back(rtp(ssrc, static_cast<uint16_t>(100 + k), ssrc == 1 ? 111 : ssrc == 2 ? 96 : 97));
      SM_EXPECT_TRUE(call.ep[0]->SendMediaPacket(sent.back(), /*rtcp=*/false, call.network.now));
    }
  }
  SM_EXPECT_TRUE(call.ep[0]->SendMediaPacket(rtp(99, 1, 111), /*rtcp=*/false, call.network.now));  // No section's.
  SM_EXPECT_TRUE(call.ep[1]->SendMediaPacket(rr, /*rtcp=*/true, call.network.now));
  call.Run(TimeDelta::Millis(300));

  SM_ASSERT_EQ(got[1].size(), sent.size());
  for (size_t k = 0; k < sent.size(); ++k) {
    SM_EXPECT_TRUE(got[1][k].first == sent[k]);
    SM_EXPECT_TRUE(!got[1][k].second);
  }
  SM_ASSERT_EQ(got[0].size(), 1u);
  SM_EXPECT_TRUE(got[0][0].first == rr && got[0][0].second);
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_EQ(call.ep[i]->media_transport()->rtp_transport()->rtp_decryption_failures(), 0);
    SM_EXPECT_EQ(call.ep[i]->send_failures(), 0);
  }
}

#if defined(SM_WITH_DATACHANNEL) && SM_WITH_DATACHANNEL

namespace {

// Records a channel's states and messages.
struct ChannelLog : DataChannelObserver {
  std::shared_ptr<SctpDataChannel> channel;
  std::vector<DataState> states;
  std::vector<std::string> messages;
  void Attach(std::shared_ptr<SctpDataChannel> c) {
    channel = std::move(c);
    channel->RegisterObserver(this);
  }
  void OnStateChange() override { states.push_back(channel->state()); }
  void OnMessage(const DataBuffer& b) override { messages.emplace_back(b.data.begin(), b.data.end()); }
  bool Open() const { return channel && channel->state() == DataState::kOpen; }
  bool Closed() const { return channel && channel->state() == DataState::kClosed; }
};

std::vector<std::string> Messages(const char* tag, int n) {
  std::vector<std::string> m;
  for (int k = 0; k < n; ++k) m.push_back(std::string(tag) + "-" + std::to_string(k));
  return m;
}

}  // namespace

// The issue's acceptance test (#1091): two endpoints behind NATs with audio, video and data channels on one BUNDLE
// transport. A creates a reliable ordered channel before the offer (so the offer carries m=application); B creates an
// unordered one with maxRetransmits 0 after the answer; both add a negotiated channel. The remote side's in-band
// channels are announced; messages go both ways on each; then the channels close, and media keeps flowing.
SM_TEST(PcPeerEndpoint, AudioVideoAndDataChannelsOnOneBundleTransport) {
  std::deque<ChannelLog> logs;  // Outlives the call: the channels report to it until they go.
  ChannelLog* remote_of[2] = {nullptr, nullptr};
  Call call;
  for (int i = 0; i < 2; ++i) {
    call.ep[i]->SetOnDataChannel([&logs, &remote_of, i](std::shared_ptr<SctpDataChannel> c) {
      logs.emplace_back().Attach(std::move(c));
      remote_of[i] = &logs.back();
    });
  }
  RtcError err;
  ChannelLog& reliable = logs.emplace_back();
  reliable.Attach(call.ep[0]->CreateDataChannel("reliable", DataChannelInit{}, call.network.now, &err));
  SM_ASSERT_TRUE(reliable.channel != nullptr);
  DataChannelInit neg;
  neg.negotiated = true;
  neg.id = 100;
  ChannelLog* negotiated[2];
  for (int i = 0; i < 2; ++i) {
    negotiated[i] = &logs.emplace_back();
    negotiated[i]->Attach(call.ep[i]->CreateDataChannel("negotiated", neg, call.network.now, &err));
    SM_ASSERT_TRUE(negotiated[i]->channel != nullptr);
  }
  call.Negotiate();
  SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected() && reliable.Open() && remote_of[1]; },
                               TimeDelta::Seconds(10)));
  SM_EXPECT_TRUE(call.signaling_ok);
  // The data section on the wire, in the BUNDLE group (pc/media_session's defaults).
  for (const std::string* sdp : {&call.offer_sdp, &call.answer_sdp}) {
    for (const char* line : {"a=group:BUNDLE 0 1 2", "m=application 9 UDP/DTLS/SCTP webrtc-datachannel", "a=mid:2",
                             "a=sctp-port:5000", "a=max-message-size:262144"}) {
      SM_EXPECT_TRUE(Contains(*sdp, line));
    }
  }
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_EQ(call.ep[i]->transport_controller().transport_count(), 1u);
    SM_EXPECT_TRUE(call.ep[i]->sctp_mid() == std::optional<std::string>("2"));
    SM_ASSERT_TRUE(call.ep[i]->sctp_transport() != nullptr);
    SM_EXPECT_TRUE(call.ep[i]->sctp_transport()->dtls_transport() ==
                   call.ep[i]->media_transport()->rtp_dtls_transport());
  }

  // B's unordered channel, opened after the answer.
  DataChannelInit unordered_init;
  unordered_init.ordered = false;
  unordered_init.maxRetransmits = 0;
  ChannelLog& unordered = logs.emplace_back();
  unordered.Attach(call.ep[1]->CreateDataChannel("unordered", unordered_init, call.network.now, &err));
  SM_ASSERT_TRUE(unordered.channel != nullptr);
  call.StartMedia();
  SM_ASSERT_TRUE(call.RunUntil([&] { return unordered.Open() && remote_of[0] && remote_of[0]->Open(); },
                               TimeDelta::Seconds(5)));
  SM_ASSERT_TRUE(negotiated[0]->Open() && negotiated[1]->Open());
  ChannelLog& reliable_at_b = *remote_of[1];
  ChannelLog& unordered_at_a = *remote_of[0];
  SM_EXPECT_EQ(reliable_at_b.channel->label(), std::string("reliable"));
  SM_EXPECT_EQ(unordered_at_a.channel->label(), std::string("unordered"));
  SM_EXPECT_FALSE(unordered_at_a.channel->ordered());
  SM_EXPECT_TRUE(unordered_at_a.channel->maxRetransmitsOpt() == std::optional<int>(0));
  // Stream ids by DTLS role: A offered actpass and B answered active, so B is the DTLS client (even ids).
  SM_EXPECT_EQ(reliable.channel->id() % 2, 1);
  SM_EXPECT_EQ(unordered.channel->id() % 2, 0);
  SM_EXPECT_EQ(reliable.channel->id(), reliable_at_b.channel->id());

  // Messages both ways on every channel.
  struct Pair {
    ChannelLog* a;
    ChannelLog* b;
    const char* tag;
  } pairs[] = {{&reliable, &reliable_at_b, "rel"},
               {&unordered_at_a, &unordered, "unord"},
               {negotiated[0], negotiated[1], "neg"}};
  for (const Pair& p : pairs) {
    for (const std::string& m : Messages(p.tag, 20)) {
      SM_EXPECT_TRUE(p.a->channel->Send(call.network.now, DataBuffer("a:" + m)));
      SM_EXPECT_TRUE(p.b->channel->Send(call.network.now, DataBuffer("b:" + m)));
    }
  }
  call.Run(TimeDelta::Seconds(2));
  for (const Pair& p : pairs) {
    std::vector<std::string> want_at_b, want_at_a;
    for (const std::string& m : Messages(p.tag, 20)) {
      want_at_b.push_back("a:" + m);
      want_at_a.push_back("b:" + m);
    }
    std::vector<std::string> got_a = p.a->messages, got_b = p.b->messages;
    if (p.tag == std::string("unord")) {  // Unordered: every message, in any order (the link loses nothing).
      std::sort(got_a.begin(), got_a.end());
      std::sort(got_b.begin(), got_b.end());
      std::sort(want_at_a.begin(), want_at_a.end());
      std::sort(want_at_b.begin(), want_at_b.end());
    }
    SM_EXPECT_TRUE(got_b == want_at_b);
    SM_EXPECT_TRUE(got_a == want_at_a);
  }

  // Close from each side: both ends of every channel reach closed (stream reset).
  reliable.channel->Close(call.network.now);
  unordered.channel->Close(call.network.now);
  negotiated[1]->channel->Close(call.network.now);
  SM_EXPECT_TRUE(call.RunUntil(
      [&] {
        return reliable.Closed() && reliable_at_b.Closed() && unordered.Closed() && unordered_at_a.Closed() &&
               negotiated[0]->Closed() && negotiated[1]->Closed();
      },
      TimeDelta::Seconds(5)));
  // Media went on over the same transport.
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_TRUE(call.decoder[i].frames.size() > 50);
    SM_EXPECT_TRUE(call.video_frames_received[i].size() > 30);
    SM_EXPECT_EQ(call.ep[i]->media_transport()->rtp_transport()->rtp_decryption_failures(), 0);
  }
}

// The per-connection switch: off at the offerer (no m=application, createDataChannel fails, no SCTP), then off at the
// answerer (the offered data section is rejected with port 0 and the offerer's channel closes); media is unaffected.
SM_TEST(PcPeerEndpoint, DataChannelSwitchOffInTheOfferAndInTheAnswer) {
  {
    Call call(/*nat=*/false, [](PeerEndpointConfig& c, int i) { c.enable_data_channels = i != 0; });
    RtcError err;
    SM_EXPECT_TRUE(call.ep[0]->CreateDataChannel("x", DataChannelInit{}, call.network.now, &err) == nullptr);
    SM_EXPECT_TRUE(err.type() == RtcErrorType::kInvalidState);
    call.Negotiate();
    SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected(); }, TimeDelta::Seconds(10)));
    SM_EXPECT_TRUE(call.offer_sdp.find("m=application") == std::string::npos);
    SM_EXPECT_TRUE(call.answer_sdp.find("m=application") == std::string::npos);
    for (int i = 0; i < 2; ++i) {
      SM_EXPECT_TRUE(call.ep[i]->sctp_transport() == nullptr);
      SM_EXPECT_FALSE(call.ep[i]->sctp_mid().has_value());
    }
    SM_EXPECT_FALSE(call.ep[0]->data_channels_enabled());
  }
  {
    ChannelLog log;
    Call call(/*nat=*/false, [](PeerEndpointConfig& c, int i) { c.enable_data_channels = i == 0; });
    RtcError err;
    log.Attach(call.ep[0]->CreateDataChannel("x", DataChannelInit{}, call.network.now, &err));
    SM_ASSERT_TRUE(log.channel != nullptr);
    call.Negotiate();
    SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected(); }, TimeDelta::Seconds(10)));
    SM_EXPECT_TRUE(call.signaling_ok);
    SM_EXPECT_TRUE(Contains(call.offer_sdp, "m=application 9 UDP/DTLS/SCTP webrtc-datachannel"));
    SM_EXPECT_TRUE(Contains(call.answer_sdp, "m=application 0 UDP/DTLS/SCTP webrtc-datachannel"));
    SM_EXPECT_TRUE(log.Closed());
    for (int i = 0; i < 2; ++i) {
      SM_EXPECT_TRUE(call.ep[i]->sctp_transport() == nullptr);
      SM_EXPECT_FALSE(call.ep[i]->sctp_mid().has_value());
    }
    call.StartMedia();
    call.Run(TimeDelta::Seconds(1));
    for (int i = 0; i < 2; ++i) SM_EXPECT_TRUE(call.decoder[i].frames.size() > 30);
    log.channel->UnregisterObserver();
  }
}

#else  // !SM_WITH_DATACHANNEL

// Built without SM_WITH_DATACHANNEL: the data channel API reports "not supported", and a data section in a remote
// offer is rejected as with the switch off.
SM_TEST(PcPeerEndpoint, DataChannelsNotBuilt) {
  Call call(/*nat=*/false);
  RtcError err;
  SM_EXPECT_TRUE(call.ep[0]->CreateDataChannel("x", DataChannelInit{}, call.network.now, &err) == nullptr);
  SM_EXPECT_TRUE(err.type() == RtcErrorType::kUnsupportedOperation);
  SM_EXPECT_FALSE(call.ep[0]->data_channels_enabled());
  SM_EXPECT_TRUE(call.ep[0]->sctp_transport() == nullptr);
  call.Negotiate();
  SM_ASSERT_TRUE(call.RunUntil([&] { return call.Connected(); }, TimeDelta::Seconds(10)));
  SM_EXPECT_TRUE(call.offer_sdp.find("m=application") == std::string::npos);
}

#endif  // SM_WITH_DATACHANNEL

// Simulcast send (C17, #1162): with RIDs the offer has an a=rid send line per encoding and a=simulcast:send, and no
// video SSRCs (CreateStreamParamsForNewSenderWithRids). An SFU's answer (a=rid recv, a=simulcast:recv, as LiveKit
// answers; munged into this endpoint's answer here) gives one video send stream per layer with generated SSRCs and
// RTX (BaseChannel::UpdateLocalStreams_w) and the layer's RID; an answer without a=simulcast rejects simulcast and
// leaves only the first encoding active (DisableSimulcastInSender); a paused layer (~f) is inactive
// (UpdateSimulcastLayerStatusInSender).
namespace {
std::string WithSimulcastRecv(std::string answer, const std::string& simulcast_line) {
  const std::string anchor = "a=mid:1\r\n";
  const size_t at = answer.find(anchor);
  if (at == std::string::npos) return answer;
  answer.insert(at + anchor.size(), "a=rid:q recv\r\na=rid:h recv\r\na=rid:f recv\r\n" + simulcast_line + "\r\n");
  return answer;
}

struct SimulcastNegotiation {
  Network network{false};
  RecordingDecoder dec[2];
  std::unique_ptr<PeerEndpoint> a, b;
  std::string offer_sdp, answer_sdp;
  SimulcastNegotiation() {
    PeerEndpointConfig ca;
    ca.allocator = network.host[0].allocator.get();
    ca.audio_decoder = &dec[0];
    ca.video_simulcast_rids = {"q", "h", "f"};
    a = std::make_unique<PeerEndpoint>(std::move(ca), network.now);
    PeerEndpointConfig cb;
    cb.allocator = network.host[1].allocator.get();
    cb.audio_decoder = &dec[1];
    b = std::make_unique<PeerEndpoint>(std::move(cb), network.now);
    std::string error;
    auto offer = a->CreateOffer(&error);
    offer_sdp = SdpSerialize(*offer);
    a->SetLocalDescription(std::move(offer), network.now, &error);
    SdpParseError perr;
    b->SetRemoteDescription(SdpDeserialize(SdpType::kOffer, offer_sdp, &perr), network.now, &error);
    auto answer = b->CreateAnswer(&error);
    answer_sdp = SdpSerialize(*answer);
  }
  bool ApplyAnswer(const std::string& sdp) {
    std::string error;
    SdpParseError perr;
    auto desc = SdpDeserialize(SdpType::kAnswer, sdp, &perr);
    return desc && a->SetRemoteDescription(std::move(desc), network.now, &error);
  }
};
}  // namespace

SM_TEST(PcPeerEndpoint, SimulcastOfferAndSfuAnswer) {
  SimulcastNegotiation n;
  for (const char* line : {"a=rid:q send", "a=rid:h send", "a=rid:f send", "a=simulcast:send q;h;f"}) {
    SM_EXPECT_TRUE(Contains(n.offer_sdp, line));
  }
  SM_EXPECT_TRUE(n.offer_sdp.find(" urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id\r\n") != std::string::npos);
  SM_EXPECT_TRUE(n.offer_sdp.find(" urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id\r\n") != std::string::npos);
  // No a=ssrc in the video section: the layers are named by RID.
  const std::string video = n.offer_sdp.substr(n.offer_sdp.find("m=video"));
  SM_EXPECT_TRUE(video.find("a=ssrc:") == std::string::npos);
  SM_ASSERT_TRUE(n.ApplyAnswer(WithSimulcastRecv(n.answer_sdp, "a=simulcast:recv q;h;f")));
  rtp::Session* s = n.a->session();
  SM_ASSERT_TRUE(s != nullptr);
  SM_ASSERT_EQ(n.a->video_simulcast_sends().size(), 3u);
  SM_ASSERT_EQ(n.a->video_simulcast().size(), 3u);
  std::vector<uint32_t> ssrcs;
  const char* rids[] = {"q", "h", "f"};
  for (size_t i = 0; i < 3; ++i) {
    const rtp::RtpSender& sender = s->video_sender(n.a->video_simulcast_sends()[i]);
    SM_EXPECT_EQ(sender.config().rid, std::string(rids[i]));
    SM_EXPECT_TRUE(sender.config().rtx_ssrc.has_value());
    SM_EXPECT_TRUE(n.a->video_simulcast()[i].active);
    ssrcs.push_back(sender.config().ssrc);
    ssrcs.push_back(sender.config().rtx_ssrc.value_or(0));
  }
  std::sort(ssrcs.begin(), ssrcs.end());
  SM_EXPECT_TRUE(std::adjacent_find(ssrcs.begin(), ssrcs.end()) == ssrcs.end());  // Six distinct SSRCs.
  SM_EXPECT_TRUE(n.a->video_send().has_value() && n.a->video_send()->index == n.a->video_simulcast_sends()[0].index);
}

SM_TEST(PcPeerEndpoint, SimulcastRejectedOrPausedByTheAnswer) {
  {
    // A peer that does not do simulcast (this endpoint's own answer): only the first encoding stays active.
    SimulcastNegotiation n;
    SM_ASSERT_TRUE(n.ApplyAnswer(n.answer_sdp));
    SM_ASSERT_EQ(n.a->video_simulcast().size(), 3u);
    SM_EXPECT_TRUE(n.a->video_simulcast()[0].active);
    SM_EXPECT_FALSE(n.a->video_simulcast()[1].active);
    SM_EXPECT_FALSE(n.a->video_simulcast()[2].active);
  }
  {
    // The SFU pauses the top layer.
    SimulcastNegotiation n;
    SM_ASSERT_TRUE(n.ApplyAnswer(WithSimulcastRecv(n.answer_sdp, "a=simulcast:recv q;h;~f")));
    SM_ASSERT_EQ(n.a->video_simulcast().size(), 3u);
    SM_EXPECT_TRUE(n.a->video_simulcast()[0].active);
    SM_EXPECT_TRUE(n.a->video_simulcast()[1].active);
    SM_EXPECT_FALSE(n.a->video_simulcast()[2].active);
  }
}
