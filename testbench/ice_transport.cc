// `transport ice` (issue #1087); see ice_transport.h.
#include "ice_transport.h"

#if SM_TESTBENCH_ICE

#include <algorithm>
#include <deque>
#include <utility>

#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm/sim/turn_server.h"
#include "sm/tls/ssl_identity.h"

namespace sm::testbench {

struct IceLink::Datagram {
  sim::Datagram<sim::Wire> d;
};

// ice_turn_tls.cc.
void InstallIceSslAdapter();
sim::TurnServer::SslWrapper MakeTurnTlsWrapper();

namespace {

using sim::SocketAddress;
constexpr uint32_t Ip(int a, int b, int c, int d) { return SocketAddress::Ip(a, b, c, d); }

// Per side, per interface: the host's address (public, or private behind its NAT) and the NAT's external one.
constexpr uint32_t kPublicIp[2][2] = {{Ip(11, 11, 11, 11), Ip(11, 11, 12, 11)}, {Ip(22, 22, 22, 22), Ip(22, 22, 23, 22)}};
constexpr uint32_t kPrivateIp[2][2] = {{Ip(192, 168, 1, 11), Ip(10, 0, 1, 11)}, {Ip(192, 168, 2, 22), Ip(10, 0, 2, 22)}};
constexpr uint32_t kNatIp[2][2] = {{Ip(77, 77, 77, 77), Ip(77, 77, 78, 77)}, {Ip(88, 88, 88, 88), Ip(88, 88, 89, 88)}};
const SocketAddress kStunAddr{Ip(99, 99, 99, 1), 3478};
const SocketAddress kTurnAddr{Ip(99, 99, 99, 3), 3478};
const SocketAddress kTurnTlsAddr{Ip(99, 99, 99, 3), 5349};
constexpr uint32_t kTurnExtIp = Ip(99, 99, 99, 5);
const char kTurnUser[] = "testbench";
const char kTurnRealm[] = "testbench.example.org";

// The emulated network's own links take no time: the scenario's links carry the delay.
sim::LinkConfig Ideal() {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Zero();
  return c;
}

int64_t WireSize(const sim::Wire& w) {
  return static_cast<int64_t>(w.data.size()) + (w.proto == sim::Wire::Proto::kTcp
                                                    ? sim::EmulatedSocketServer::kTcpHeaderBytes
                                                    : sim::EmulatedSocketServer::kUdpIpv4HeaderBytes);
}

class Auth final : public sim::TurnAuthInterface {
 public:
  bool GetKey(std::string_view username, std::string_view realm, std::string* key) override {
    return p2p::ComputeStunCredentialHash(std::string(username), std::string(realm), std::string(username), key);
  }
};

// A host with several interfaces: each socket binds on the server of its address's interface.
class MultiHomeFactory final : public p2p::PacketSocketFactory {
 public:
  explicit MultiHomeFactory(std::vector<sim::EmulatedSocketServer*> servers) : servers_(std::move(servers)) {}
  std::unique_ptr<p2p::AsyncPacketSocket> CreateUdpSocket(const p2p::SocketAddress& address, uint16_t min_port,
                                                          uint16_t max_port) override {
    return For(address).CreateUdpSocket(address, min_port, max_port);
  }
  std::unique_ptr<p2p::AsyncListenSocket> CreateServerTcpSocket(const p2p::SocketAddress& address, uint16_t min_port,
                                                                uint16_t max_port, int opts) override {
    return For(address).CreateServerTcpSocket(address, min_port, max_port, opts);
  }
  std::unique_ptr<p2p::AsyncPacketSocket> CreateClientTcpSocket(Timestamp now, const p2p::SocketAddress& local,
                                                                const p2p::SocketAddress& remote,
                                                                const p2p::PacketSocketTcpOptions& options) override {
    return For(local).CreateClientTcpSocket(now, local, remote, options);
  }

 private:
  sim::EmulatedSocketServer& For(const p2p::SocketAddress& a) {
    for (sim::EmulatedSocketServer* s : servers_) {
      if (s->ip() == a.ipaddr()) return *s;
    }
    return *servers_.front();
  }
  std::vector<sim::EmulatedSocketServer*> servers_;
};


}  // namespace

struct IceLink::Impl {
  explicit Impl(const IceOptions& o) : opt(o), net(o.seed) {}

  IceOptions opt;
  sim::ByteNetwork net;
  struct Side {
    std::vector<std::unique_ptr<sim::EmulatedSocketServer>> ifaces;
    std::unique_ptr<MultiHomeFactory> factory;
    p2p::StaticNetworkManager network_manager;
    std::unique_ptr<p2p::BasicPortAllocator> allocator;
  } side[2];
  std::unique_ptr<sim::EmulatedSocketServer> stun_host, turn_host, turn_ext_host;
  std::unique_ptr<sim::StunServer> stun;
  Auth auth;
  std::unique_ptr<sim::TurnServer> turn;
  std::unique_ptr<pc::PeerEndpoint> ep[2];

  struct Message {
    bool candidate = false;
    pc::SdpType type = pc::SdpType::kOffer;
    std::string mid;
    int mline_index = 0;
    std::string text;
  };
  std::deque<Message> to[2];
  bool signaling_ok = true;

  Emit emit;
  OnMedia on_media;
  Timestamp now = Timestamp::Seconds(10);  // The endpoints' clock.
  Timestamp offer_time = Timestamp::Seconds(10);
  TimeDelta offset = TimeDelta::Zero();  // Endpoint clock = call time + offset, in the media phase.
  int64_t wire = 0, media = 0, setup_wire = 0;

  bool IsEndpointIp(uint32_t ip) const {
    for (const Side& s : side) {
      for (const auto& i : s.ifaces) {
        if (i->ip().v4() == ip) return true;
      }
    }
    return false;
  }

  // Hands every datagram that has moved by `now` on the (timeless) emulated network to its host; the endpoints'
  // hosts' arrivals go to the scenario's links through the tap.
  void Pump(Timestamp t) {
    for (int guard = 0; guard < 64; ++guard) {
      for (Side& s : side) {
        for (auto& i : s.ifaces) i->Deliver(t);
      }
      for (auto* h : {stun_host.get(), turn_host.get(), turn_ext_host.get()}) {
        if (h) h->Deliver(t);
      }
      if (turn) turn->Process(t);
      if (net.NextEvent() > t) return;
    }
  }

  void DeliverSignaling() {
    for (int i = 0; i < 2; ++i) {
      while (!to[i].empty()) {
        Message m = std::move(to[i].front());
        to[i].pop_front();
        std::string error;
        pc::SdpParseError perr;
        if (m.candidate) {
          auto cand = pc::IceCandidate::Create(m.mid, m.mline_index, m.text, &perr);
          signaling_ok = signaling_ok && cand && ep[i]->AddIceCandidate(*cand, now, &error);
          continue;
        }
        auto desc = pc::SdpDeserialize(m.type, m.text, &perr);
        if (!desc || !ep[i]->SetRemoteDescription(std::move(desc), now, &error)) {
          signaling_ok = false;
          continue;
        }
        if (m.type == pc::SdpType::kOffer) {
          auto answer = ep[i]->CreateAnswer(&error);
          if (!answer) {
            signaling_ok = false;
            continue;
          }
          std::string text = pc::SdpSerialize(*answer);
          signaling_ok = signaling_ok && ep[i]->SetLocalDescription(std::move(answer), now, &error);
          to[1 - i].push_back(Message{false, pc::SdpType::kAnswer, "", 0, std::move(text)});
        }
      }
    }
  }

  bool IceWritable(int i) const {
    const pc::JsepTransport* t = ep[i]->media_transport();
    return t && t->ice_transport()->writable();
  }
};

bool IceLink::Available() { return true; }

IceLink::IceLink(const IceOptions& options) : impl_(std::make_unique<Impl>(options)) {
  static const bool crypto_installed = [] {
    p2p::InstallStunCryptoFromTls();
    InstallIceSslAdapter();
    return true;
  }();
  (void)crypto_installed;
  Impl& m = *impl_;
  sim::ByteNetwork& net = m.net;
  const int ifaces = options.two_interfaces ? 2 : 1;
  for (int s = 0; s < 2; ++s) {
    Impl::Side& side = m.side[s];
    std::vector<sim::EmulatedSocketServer*> servers;
    for (int k = 0; k < ifaces; ++k) {
      int realm = sim::ByteNetwork::kPublicRealm;
      uint32_t ip = kPublicIp[s][k];
      if (options.nat[s] >= 0) {
        sim::NatConfig cfg;
        cfg.type = static_cast<sim::NatType>(options.nat[s]);
        cfg.external_ip = kNatIp[s][k];
        realm = net.NatRealm(net.AddNat(cfg));
        ip = kPrivateIp[s][k];
      }
      side.ifaces.push_back(std::make_unique<sim::EmulatedSocketServer>(net, net.AddHost(ip, realm, Ideal(), Ideal())));
      sim::EmulatedSocketServer* server = side.ifaces.back().get();
      servers.push_back(server);
      // What reaches this host crosses the scenario's link toward this side first.
      server->SetInboundTap([this, s](Timestamp, sim::Datagram<sim::Wire>& d) {
        Impl& im = *impl_;
        if (!im.emit) return false;
        const DataSize size = DataSize::Bytes(WireSize(d.payload));
        im.emit(s, std::make_shared<Datagram>(Datagram{std::move(d)}), size);
        return true;
      });
      side.network_manager.AddInterface(p2p::IpAddress(ip), k == 0 ? "eth0" : "wlan0");
    }
    side.factory = std::make_unique<MultiHomeFactory>(servers);
    side.allocator = std::make_unique<p2p::BasicPortAllocator>(&side.network_manager, side.factory.get());
  }

  p2p::ServerAddresses stun_servers;
  if (options.stun) {
    m.stun_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kStunAddr.ip, sim::ByteNetwork::kPublicRealm, Ideal(), Ideal()));
    m.stun = std::make_unique<sim::StunServer>(m.stun_host->CreateUdpSocket(sim::ToP2p(kStunAddr), 0, 0));
    stun_servers.insert(sim::ToP2p(kStunAddr));
  }
  std::vector<p2p::RelayServerConfig> turn_servers;
  if (options.turn > 0) {
    m.turn_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kTurnAddr.ip, sim::ByteNetwork::kPublicRealm, Ideal(), Ideal()));
    m.turn_ext_host = std::make_unique<sim::EmulatedSocketServer>(
        net, net.AddHost(kTurnExtIp, sim::ByteNetwork::kPublicRealm, Ideal(), Ideal()));
    m.turn = std::make_unique<sim::TurnServer>();
    m.turn->set_realm(kTurnRealm);
    m.turn->set_auth_hook(&m.auth);
    const p2p::ProtocolType proto = options.turn == 1 ? p2p::PROTO_UDP : options.turn == 2 ? p2p::PROTO_TCP : p2p::PROTO_TLS;
    const SocketAddress addr = proto == p2p::PROTO_TLS ? kTurnTlsAddr : kTurnAddr;
    if (proto == p2p::PROTO_UDP) {
      m.turn->AddInternalSocket(m.turn_host->CreateUdpSocket(sim::ToP2p(addr), 0, 0), proto);
    } else {
      std::unique_ptr<p2p::Socket> socket = m.turn_host->CreateStreamSocket(p2p::IpFamily::kV4);
      socket->Bind(sim::ToP2p(addr));
      socket->Listen(5);
      sim::TurnServer::SslWrapper ssl;
      if (proto == p2p::PROTO_TLS) ssl = MakeTurnTlsWrapper();
      m.turn->AddInternalServerSocket(std::move(socket), proto, std::move(ssl));
    }
    m.turn->SetExternalSocketFactory(m.turn_ext_host.get(), p2p::SocketAddress(kTurnExtIp, 0));
    p2p::RelayServerConfig relay(sim::ToP2p(addr), kTurnUser, kTurnUser, proto);
    relay.tls_cert_policy = p2p::TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK;
    turn_servers.push_back(relay);
  }

  net.SetSendObserver([this](const sim::Datagram<sim::Wire>& d) {
    if (impl_->IsEndpointIp(d.src.ip)) impl_->wire += WireSize(d.payload);
  });

  for (int s = 0; s < 2; ++s) {
    Impl::Side& side = m.side[s];
    side.allocator->SetConfiguration(m.now, stun_servers, turn_servers, 0, p2p::NO_PRUNE);
    side.allocator->set_flags(p2p::kDefaultPortAllocatorFlags);
    // PeerConnection::InitializePortAllocator_n: kMinimumStepDelay.
    side.allocator->set_step_delay(p2p::kMinimumStepDelay);
    // RTCConfiguration::type kRelay when the scenario forces the relay.
    if (options.turn > 0) side.allocator->set_candidate_filter(p2p::CF_RELAY);

    pc::PeerEndpointConfig c;
    c.allocator = side.allocator.get();
    c.certificate_name = s == 0 ? "testbench-a" : "testbench-b";
    c.audio = options.audio;
    c.video = options.video;
    c.external_media = true;
    c.on_media_packet = [this, s](std::span<const uint8_t> packet, bool rtcp, Timestamp at) {
      Impl& im = *impl_;
      if (!im.on_media) return;
      std::vector<uint8_t> bytes(packet.begin(), packet.end());
      im.on_media(s, std::move(bytes), rtcp, at - im.offset);
    };
    if (options.ssrcs[s].audio) c.audio_ssrc = options.ssrcs[s].audio;
    if (options.ssrcs[s].video) c.video_ssrc = options.ssrcs[s].video;
    c.video_rtx_ssrc = options.ssrcs[s].rtx;
    m.ep[s] = std::make_unique<pc::PeerEndpoint>(std::move(c), m.now);
    m.ep[s]->SetOnIceCandidate([this, s](std::unique_ptr<pc::IceCandidate> cand) {
      impl_->to[1 - s].push_back(
          Impl::Message{true, pc::SdpType::kOffer, cand->sdp_mid(), cand->sdp_mline_index(), cand->ToString()});
    });
  }
}

sync::ExtensionMap IceLink::NegotiatedExtensions() const {
  sync::ExtensionMap map;
  const pc::SessionDescriptionInterface* l = impl_->ep[0]->local_description();
  const pc::SessionDescriptionInterface* r = impl_->ep[0]->remote_description();
  if (!l || !r) return map;
  const pc::SessionDescriptionInterface* answer = l->GetType() == pc::SdpType::kAnswer ? l : r;
  for (const pc::ContentInfo& content : answer->description()->contents()) {
    if (content.rejected || !content.media_description()) continue;
    for (const pc::RtpExtension& e : content.media_description()->rtp_header_extensions()) {
      if (e.id >= 1 && e.id <= 255) map.Register(static_cast<uint8_t>(e.id), e.uri);
    }
  }
  return map;
}

IceLink::~IceLink() {
  // The endpoints close their ports (a TURN allocation sends its last refresh) before the network goes.
  Impl& m = *impl_;
  m.emit = nullptr;
  m.on_media = nullptr;
  m.net.SetSendObserver(nullptr);
  for (auto& e : m.ep) e.reset();
}

TimeDelta IceLink::setup_duration() const { return impl_->offset - (impl_->offer_time - Timestamp::Millis(0)); }

bool IceLink::Connect(const sim::LinkConfig toward[2], TimeDelta limit) {
  Impl& m = *impl_;
  sim::NetworkLink<DatagramPtr> link[2] = {sim::NetworkLink<DatagramPtr>(toward[0], m.opt.seed * 7919 + 1),
                                           sim::NetworkLink<DatagramPtr>(toward[1], m.opt.seed * 7919 + 2)};
  m.emit = [&](int to, DatagramPtr d, DataSize size) { link[to].Send(m.now, std::move(d), size); };

  std::string error;
  m.offer_time = m.now;
  auto offer = m.ep[0]->CreateOffer(&error);
  if (!offer) {
    stats_.error = "CreateOffer: " + error;
    return false;
  }
  std::string text = pc::SdpSerialize(*offer);
  if (!m.ep[0]->SetLocalDescription(std::move(offer), m.now, &error)) {
    stats_.error = "SetLocalDescription: " + error;
    return false;
  }
  m.to[1].push_back(Impl::Message{false, pc::SdpType::kOffer, "", 0, std::move(text)});

  const Timestamp until = m.now + limit;
  for (int guard = 0; guard < 2000000; ++guard) {
    m.DeliverSignaling();
    for (auto& e : m.ep) e->ProcessTransport(m.now);
    m.Pump(m.now);
    for (int i = 0; i < 2; ++i) {
      for (DatagramPtr& d : link[i].Receive(m.now)) Arrive(i, d, m.now - m.offset);
    }
    m.Pump(m.now);
    if (stats_.ice_connected_ms < 0 && m.IceWritable(0) && m.IceWritable(1)) {
      stats_.ice_connected_ms = (m.now - m.offer_time).us() / 1000.0;
    }
    if (m.ep[0]->IsConnected() && m.ep[1]->IsConnected()) {
      stats_.dtls_connected_ms = (m.now - m.offer_time).us() / 1000.0;
      stats_.connected = true;
      break;
    }
    if (!m.signaling_ok) {
      stats_.error = "signaling failed";
      break;
    }
    Timestamp t = std::min({m.net.NextEvent(), link[0].NextArrival(), link[1].NextArrival(),
                            m.ep[0]->NextProcessTime(m.now), m.ep[1]->NextProcessTime(m.now)});
    if (m.turn) t = std::min(t, m.turn->NextProcessTime());
    if (!m.to[0].empty() || !m.to[1].empty()) t = m.now;
    // Whole milliseconds at least, as the call's loop runs.
    t = std::max(t, m.now + TimeDelta::Millis(1));
    if (t > until) {
      stats_.error = "not connected after " + std::to_string(limit.ms()) + " ms";
      break;
    }
    m.now = t;
  }
  m.emit = nullptr;
  m.setup_wire = m.wire;
  // The media phase starts on the next millisecond of the endpoints' clock.
  m.now = m.now + TimeDelta::Millis(1);
  m.offset = m.now - Timestamp::Millis(0);
  UpdateSelectedPair();
  return stats_.connected;
}

void IceLink::SetEmit(Emit emit) { impl_->emit = std::move(emit); }
void IceLink::SetOnMedia(OnMedia on_media) { impl_->on_media = std::move(on_media); }

bool IceLink::Send(int side, std::span<const uint8_t> packet, bool rtcp, Timestamp t) {
  Impl& m = *impl_;
  m.now = std::max(m.now, t + m.offset);
  m.media += static_cast<int64_t>(packet.size()) + sim::EmulatedSocketServer::kUdpIpv4HeaderBytes;
  const bool sent = m.ep[side]->SendMediaPacket(packet, rtcp, m.now);
  m.Pump(m.now);
  return sent;
}

void IceLink::Process(Timestamp t) {
  Impl& m = *impl_;
  m.now = std::max(m.now, t + m.offset);
  for (auto& e : m.ep) e->ProcessTransport(m.now);
  m.Pump(m.now);
}

void IceLink::Arrive(int to_side, const DatagramPtr& d, Timestamp at) {
  Impl& m = *impl_;
  // A firewall rule made after the datagram left (a network change) drops it on arrival, as FirewallSocket's
  // RecvFrom does.
  if (m.net.Blocked(d->d.src.ip) || m.net.Blocked(d->d.dst.ip)) return;
  m.now = std::max(m.now, at + m.offset);
  for (auto& i : m.side[to_side].ifaces) {
    if (i->ip().v4() == d->d.dst.ip) {
      sim::Datagram<sim::Wire> copy = d->d;
      i->Inject(m.now, copy);
      break;
    }
  }
  m.Pump(m.now);
}

bool IceLink::IsMedia(const Datagram& d) {
  const sim::Bytes& b = d.d.payload.data;
  if (d.d.payload.proto != sim::Wire::Proto::kUdp || b.empty()) return false;
  // RFC 7983: RTP/RTCP 128-191; TURN ChannelData 64-79 carries the peer's datagram after 4 bytes.
  if (b[0] >= 64 && b[0] <= 79) return b.size() > 4 && b[4] >= 128 && b[4] <= 191;
  return b[0] >= 128 && b[0] <= 191;
}

void IceLink::FailInterface(int side, Timestamp t) {
  Impl& m = *impl_;
  m.now = std::max(m.now, t + m.offset);
  m.net.BlockAddress(m.side[side].ifaces.front()->ip().v4());
}

void IceLink::UpdateSelectedPair() {
  const pc::JsepTransport* t = impl_->ep[0]->media_transport();
  const p2p::Connection* c = t ? t->ice_transport()->selected_connection() : nullptr;
  if (!c) return;
  stats_.local_type = std::string(c->local_candidate().type_name());
  stats_.remote_type = std::string(c->remote_candidate().type_name());
  stats_.relay_protocol = c->local_candidate().is_relay() ? c->local_candidate().relay_protocol() : std::string();
}

bool IceLink::connected() const { return impl_->ep[0]->IsConnected() && impl_->ep[1]->IsConnected(); }
int64_t IceLink::wire_bytes() const { return impl_->wire; }
int64_t IceLink::media_bytes() const { return impl_->media; }
int64_t IceLink::setup_wire_bytes() const { return impl_->setup_wire; }

}  // namespace sm::testbench

#else  // !SM_TESTBENCH_ICE

namespace sm::testbench {

struct IceLink::Datagram {};
struct IceLink::Impl {};
bool IceLink::Available() { return false; }
IceLink::IceLink(const IceOptions&) {}
IceLink::~IceLink() = default;
bool IceLink::Connect(const sim::LinkConfig[2], TimeDelta) {
  stats_.error = "transport ice needs a build with TLS (SM_WITH_TLS)";
  return false;
}
TimeDelta IceLink::setup_duration() const { return TimeDelta::Zero(); }
void IceLink::SetEmit(Emit) {}
void IceLink::SetOnMedia(OnMedia) {}
bool IceLink::Send(int, std::span<const uint8_t>, bool, Timestamp) { return false; }
void IceLink::Process(Timestamp) {}
void IceLink::Arrive(int, const DatagramPtr&, Timestamp) {}
bool IceLink::IsMedia(const Datagram&) { return false; }
void IceLink::FailInterface(int, Timestamp) {}
void IceLink::UpdateSelectedPair() {}
bool IceLink::connected() const { return false; }
int64_t IceLink::wire_bytes() const { return 0; }
int64_t IceLink::media_bytes() const { return 0; }
int64_t IceLink::setup_wire_bytes() const { return 0; }

}  // namespace sm::testbench

#endif  // SM_TESTBENCH_ICE
