// Test harness for several pc::PeerEndpoints on one emulated network (C18, #1160): hosts on public addresses (host
// candidates only), signaling links that carry offers, answers and trickled candidates as SDP text (so every
// description goes through the SDP writer and parser), and a time loop that runs the endpoints, the network, the
// links and a per-step hook (the test's media) at their next due time. Nothing reads a clock.
#pragma once

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
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/sim/emulated_sockets.h"

namespace sm::pc::test {

class EndpointWorld {
 public:
  struct Host {
    std::unique_ptr<sim::EmulatedSocketServer> server;
    p2p::StaticNetworkManager network_manager;
    std::unique_ptr<p2p::BasicPortAllocator> allocator;
  };

  // A signaling channel between two endpoints. Messages are delivered on the next step, in order.
  struct Link {
    struct Message {
      bool candidate = false;
      SdpType type = SdpType::kOffer;
      std::string mid;
      int mline_index = 0;
      std::string text;
    };
    PeerEndpoint* ep[2] = {nullptr, nullptr};
    std::deque<Message> to[2];
    bool ok = true;
    std::string error;
    std::string last_offer, last_answer;  // SDP text.
    int offers = 0, answers = 0;

    // ep[from] makes an offer and applies it; the other end answers when the offer arrives.
    bool Offer(int from, Timestamp now) {
      std::string e;
      auto offer = ep[from]->CreateOffer(&e);
      if (!offer) return Failed("CreateOffer: " + e);
      last_offer = SdpSerialize(*offer);
      if (!ep[from]->SetLocalDescription(std::move(offer), now, &e)) return Failed("SetLocalDescription(offer): " + e);
      to[1 - from].push_back(Message{false, SdpType::kOffer, "", 0, last_offer});
      ++offers;
      return true;
    }
    bool Pending() const { return !to[0].empty() || !to[1].empty(); }
    void Pump(Timestamp now) {
      for (int i = 0; i < 2; ++i) {
        while (!to[i].empty()) {
          Message m = std::move(to[i].front());
          to[i].pop_front();
          std::string e;
          SdpParseError perr;
          if (m.candidate) {
            auto cand = IceCandidate::Create(m.mid, m.mline_index, m.text, &perr);
            if (!cand || !ep[i]->AddIceCandidate(*cand, now, &e)) Failed("AddIceCandidate: " + e);
            continue;
          }
          auto desc = SdpDeserialize(m.type, m.text, &perr);
          if (!desc) {
            Failed("SdpDeserialize: " + perr.description + " at " + perr.line);
            continue;
          }
          if (!ep[i]->SetRemoteDescription(std::move(desc), now, &e)) {
            Failed(std::string("SetRemoteDescription(") + SdpTypeToString(m.type) + "): " + e);
            continue;
          }
          if (m.type != SdpType::kOffer) continue;
          auto answer = ep[i]->CreateAnswer(&e);
          if (!answer) {
            Failed("CreateAnswer: " + e);
            continue;
          }
          last_answer = SdpSerialize(*answer);
          if (!ep[i]->SetLocalDescription(std::move(answer), now, &e)) Failed("SetLocalDescription(answer): " + e);
          to[1 - i].push_back(Message{false, SdpType::kAnswer, "", 0, last_answer});
          ++answers;
        }
      }
    }
    bool Failed(std::string e) {
      ok = false;
      if (error.empty()) error = std::move(e);
      return false;
    }
  };

  EndpointWorld() { p2p::InstallStunCryptoFromTls(); }

  // A host on its own public address (10.0.0.N), whose allocator gathers host candidates.
  p2p::PortAllocator* AddHost() {
    const int n = static_cast<int>(hosts_.size()) + 1;
    const p2p::IpAddress ip = *p2p::IpAddress::FromString("10.0." + std::to_string(n / 250) + "." +
                                                          std::to_string(n % 250 + 1));
    auto h = std::make_unique<Host>();
    sim::LinkConfig link;
    link.base_delay = TimeDelta::Millis(5);
    h->server = std::make_unique<sim::EmulatedSocketServer>(
        net_, net_.AddHost(ip.v4(), sim::ByteNetwork::kPublicRealm, link, link));
    h->network_manager.AddInterface(ip);
    h->allocator = std::make_unique<p2p::BasicPortAllocator>(&h->network_manager, h->server.get());
    h->allocator->SetConfiguration(now, p2p::ServerAddresses{}, {}, 0, p2p::NO_PRUNE);
    h->allocator->set_flags(p2p::kDefaultPortAllocatorFlags | p2p::PORTALLOCATOR_DISABLE_TCP);
    h->allocator->set_step_delay(p2p::kMinimumStepDelay);
    hosts_.push_back(std::move(h));
    return hosts_.back()->allocator.get();
  }

  // An endpoint on a new host; `customize` edits its config (the allocator is set).
  PeerEndpoint* AddEndpoint(const std::string& name, const std::function<void(PeerEndpointConfig&)>& customize) {
    PeerEndpointConfig c;
    c.allocator = AddHost();
    c.certificate_name = name;
    c.audio_stream.nominal_rate = DataRate::KilobitsPerSec(40);
    c.session.send_side.gcc.start_rate = DataRate::KilobitsPerSec(800);
    if (customize) customize(c);
    endpoints_.push_back(std::make_unique<PeerEndpoint>(std::move(c), now));
    return endpoints_.back().get();
  }

  // A signaling link between `a` and `b`; each endpoint's gathered candidates go over its (one) link.
  Link* Connect(PeerEndpoint* a, PeerEndpoint* b) {
    links_.push_back(std::make_unique<Link>());
    Link* l = links_.back().get();
    l->ep[0] = a;
    l->ep[1] = b;
    for (int i = 0; i < 2; ++i) {
      l->ep[i]->SetOnIceCandidate([l, i](std::unique_ptr<IceCandidate> c) {
        l->to[1 - i].push_back(
            Link::Message{true, SdpType::kOffer, c->sdp_mid(), c->sdp_mline_index(), c->ToString()});
      });
    }
    return l;
  }

  // The test's media: run at every step; `next` says when it next wants to run (+infinity: never).
  void SetHook(std::function<void(Timestamp now)> hook, std::function<Timestamp()> next) {
    hook_ = std::move(hook);
    hook_next_ = std::move(next);
  }

  Timestamp NextEvent() const {
    Timestamp t = net_.NextEvent();
    for (const auto& h : hosts_) t = std::min(t, h->allocator->NextPooledSessionsProcessTime());
    for (const auto& e : endpoints_) t = std::min(t, e->NextProcessTime(now));
    for (const auto& l : links_) {
      if (l->Pending()) t = std::min(t, now);
    }
    if (hook_next_) t = std::min(t, hook_next_());
    return std::max(t, now);
  }

  void Step(Timestamp t) {
    now = std::max(now, t);
    for (auto& e : endpoints_) e->Process(now);
    for (auto& h : hosts_) {
      h->allocator->ProcessPooledSessions(now);
      h->server->Deliver(now);
    }
    for (auto& l : links_) l->Pump(now);
    if (hook_) hook_(now);
  }

  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 20000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        now = until;
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  void Run(TimeDelta d) {
    RunUntil([] { return false; }, d);
  }

  Timestamp now = Timestamp::Seconds(10);

 private:
  sim::ByteNetwork net_;
  std::vector<std::unique_ptr<Host>> hosts_;
  std::vector<std::unique_ptr<PeerEndpoint>> endpoints_;
  std::vector<std::unique_ptr<Link>> links_;
  std::function<void(Timestamp)> hook_;
  std::function<Timestamp()> hook_next_;
};

// The m= sections of an SDP text: "m=" lines in order.
inline std::vector<std::string> MediaLines(const std::string& sdp) {
  std::vector<std::string> out;
  size_t pos = 0;
  while ((pos = sdp.find("m=", pos)) != std::string::npos) {
    if (pos == 0 || sdp[pos - 1] == '\n') out.push_back(sdp.substr(pos, sdp.find("\r\n", pos) - pos));
    pos += 2;
  }
  return out;
}

inline bool SdpHas(const std::string& sdp, const std::string& line) {
  return sdp.find(line + "\r\n") != std::string::npos;
}

}  // namespace sm::pc::test
