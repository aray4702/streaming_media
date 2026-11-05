// The AI server (C12 part 1, #1103); see ai_server.h.
#include "ai_server.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <chrono>
#include <fstream>
#include <future>
#include <optional>

#include "json.h"
#include "opus_decoder.h"
#include "opus_encoder.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/sim/signals.h"

namespace sm::apps {

namespace {

constexpr int kRate = 48000;
constexpr size_t k10ms = kRate / 100;
constexpr size_t k20ms = kRate / 50;
constexpr size_t kMaxUserAudio = static_cast<size_t>(kRate) * 30;  // What a turn hands the reply source, at most.

std::string JsonNumbers(const std::vector<double>& v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%.1f", i ? "," : "", v[i]);
    s += buf;
  }
  return s + "]";
}

double Mean(const std::vector<double>& v) {
  if (v.empty()) return -1;
  double s = 0;
  for (double x : v) s += x;
  return s / static_cast<double>(v.size());
}

}  // namespace

std::vector<p2p::IpAddress> LocalIpv4Interfaces() {
  std::vector<p2p::IpAddress> out;
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return out;
  for (ifaddrs* a = list; a != nullptr; a = a->ifa_next) {
    if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET) continue;
    if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
    const auto* sa = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
    out.emplace_back(ntohl(sa->sin_addr.s_addr));
  }
  freeifaddrs(list);
  return out;
}

std::vector<float> ScriptedReplySource::OnUserTurn(std::span<const float>, Timestamp) {
  if (lengths_.empty()) return {};
  const TimeDelta len = lengths_[std::min(turn_, lengths_.size() - 1)];
  ++turn_;
  return sim::SpeechLike(kRate, len.seconds(), 1000 + turn_, false);
}

std::string AiCallMetrics::ToJson() const {
  char buf[512];
  std::snprintf(buf, sizeof buf,
                ",\"connected\":%s,\"connect_ms\":%.1f,\"duration_s\":%.2f,\"user_turns\":%d,\"replies\":%d,"
                "\"barge_ins\":%d,\"conv.response_delay_ms_mean\":%.1f,\"conv.barge_in_stop_ms_mean\":%.1f,"
                "\"conv.user_speech_ms\":%.0f,\"conv.user_speech_lost_ms\":%.0f,\"conv.user_speech_lost_pct\":%.2f,"
                "\"audio_frames_sent\":%lld,\"reply_frames_sent\":%lld,\"audio_packets_received\":%lld,"
                "\"send_late_max_ms\":%.1f,\"rtp_ahead_max_ms\":%.1f",
                connected ? "true" : "false", connect_ms, duration_s, user_turns, replies, barge_ins,
                Mean(response_delay_ms), Mean(barge_in_stop_ms), user_speech_ms, user_speech_lost_ms,
                user_speech_ms > 0 ? 100.0 * user_speech_lost_ms / user_speech_ms : 0.0,
                static_cast<long long>(audio_frames_sent), static_cast<long long>(reply_frames_sent),
                static_cast<long long>(audio_packets_received), send_late_max_ms, rtp_ahead_max_ms);
  return "{\"type\":\"ai-call-metrics\",\"room\":" + net::JsonEscape(room) + ",\"peer\":" + net::JsonEscape(peer) +
         buf + ",\"conv.response_delay_ms\":" + JsonNumbers(response_delay_ms) +
         ",\"conv.barge_in_stop_ms\":" + JsonNumbers(barge_in_stop_ms) +
         ",\"reply_generation_ms\":" + JsonNumbers(reply_generation_ms) + "}";
}

void ParseTurnResponse(const std::string& json, p2p::ServerAddresses* stun, std::vector<p2p::RelayServerConfig>* turn) {
  auto m = net::JsonParseFlat(json);
  for (const std::string& url : net::JsonStringArray(m["urls"])) {
    // stun:host[:port], turn:host[:port][?transport=udp|tcp], turns:host[:port][?transport=tcp].
    const size_t colon = url.find(':');
    if (colon == std::string::npos) continue;
    const std::string scheme = url.substr(0, colon);
    std::string rest = url.substr(colon + 1);
    std::string transport = "udp";
    if (const size_t q = rest.find('?'); q != std::string::npos) {
      const std::string query = rest.substr(q + 1);
      rest = rest.substr(0, q);
      if (query.find("transport=tcp") != std::string::npos) transport = "tcp";
    }
    uint16_t port = scheme == "turns" ? 5349 : 3478;
    std::string host = rest;
    if (const size_t pc = rest.rfind(':'); pc != std::string::npos && rest.find(']') == std::string::npos) {
      port = static_cast<uint16_t>(std::atoi(rest.substr(pc + 1).c_str()));
      host = rest.substr(0, pc);
    }
    const auto ip = net::ResolveIpv4(host);
    if (!ip || port == 0) continue;
    p2p::SocketAddress addr(*ip, port);
    if (scheme == "stun") {
      stun->insert(addr);
    } else if (scheme == "turn" || scheme == "turns") {
      if (scheme == "turns") addr.SetHostname(host);
      const p2p::ProtocolType proto = scheme == "turns" ? p2p::PROTO_TLS : transport == "tcp" ? p2p::PROTO_TCP : p2p::PROTO_UDP;
      turn->emplace_back(addr, m["username"], m["credential"], proto);
    }
  }
}

// One call: its signaling WebSocket, endpoint and audio.
// --- AiConversation ---------------------------------------------------------------------------------------------------

AiConversation::AiConversation(const AiServerConfig& config, AiCallMetrics* metrics,
                               std::function<void(const std::string&)> log)
    : metrics_(metrics),
      log_(std::move(log)),
      detector_(config.turn_detector),
      source_(config.ai_source),
      reply_(config.reply_factory ? config.reply_factory() : std::make_unique<ScriptedReplySource>()) {
  detector_config_ok_ = config.turn_detector.sample_rate_hz == kRate;
}

AiConversation::~AiConversation() {
  if (generating_.valid()) generating_.wait();  // The worker uses reply_.
}

// The reply source runs on a worker thread (#1126): a model's time no longer holds up the 20 ms slots.
void AiConversation::StartGeneration(std::vector<float> user_audio, Timestamp at, Timestamp last_speech) {
  gen_at_ = at;
  gen_last_speech_ = last_speech;
  generating_ = std::async(std::launch::async, [src = reply_.get(), audio = std::move(user_audio), at] {
    return src->OnUserTurn(audio, at);
  });
}

// A reply that is ready starts at the first slot not yet sent.
void AiConversation::CollectReply(Timestamp now, Timestamp next_slot) {
  if (!generating_.valid() || generating_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  std::vector<float> reply = generating_.get();
  metrics_->reply_generation_ms.push_back((now - gen_at_).ms_f());
  if (barge_in_deferred_) {
    barge_in_deferred_ = false;
    reply_->OnBargeIn(barge_in_at_);
  }
  if (next_turn_) {  // The user ended another turn meanwhile: its reply replaces this one.
    PendingTurn t = std::move(*next_turn_);
    next_turn_.reset();
    StartGeneration(std::move(t.audio), t.at, t.last_speech);
    return;
  }
  if (reply.empty()) return;
  reply_audio_ = std::move(reply);
  held_.clear();
  source_.StartTurn(next_slot, TimeDelta::Micros(static_cast<int64_t>(reply_audio_.size()) * 1'000'000 / kRate));
  ++metrics_->replies;
  turn_end_speech_ = gen_last_speech_;
  awaiting_first_frame_ = true;
  log_("end of turn, replying");
}

std::span<const float> AiConversation::Slot(Timestamp slot, Timestamp now) {
  for (const auto& f : source_.Poll(slot)) held_.push_back(f);
  if (held_.empty()) return {};
  const testbench::AiFrame f = held_.front();
  held_.pop_front();
  const size_t begin = static_cast<size_t>(f.media_offset.us() * kRate / 1'000'000);
  std::span<const float> pcm;
  if (begin < reply_audio_.size()) {
    pcm = std::span<const float>(reply_audio_).subspan(begin, std::min(k20ms, reply_audio_.size() - begin));
  }
  if (awaiting_first_frame_) {
    awaiting_first_frame_ = false;
    metrics_->response_delay_ms.push_back((now - turn_end_speech_).ms_f());
  }
  ++metrics_->reply_frames_sent;
  return pcm;
}

void AiConversation::OnPlayout(std::span<const float> pcm, timing::AudioOperation op, Timestamp at) {
  if (op != timing::AudioOperation::kSilence) ++metrics_->audio_packets_received;
  if (!detector_config_ok_) return;
  const bool speaking = source_.active();
  const auto ev = detector_.Process(pcm, at, speaking);
  if (detector_.in_utterance()) {
    metrics_->user_speech_ms += 10;
    if (op == timing::AudioOperation::kExpand) metrics_->user_speech_lost_ms += 10;
    if (user_audio_.size() + pcm.size() <= kMaxUserAudio) user_audio_.insert(user_audio_.end(), pcm.begin(), pcm.end());
  }
  if (!ev) return;
  switch (ev->type) {
    case testbench::TurnEventType::kSpeechStart:
      user_audio_.clear();
      break;
    case testbench::TurnEventType::kBargeIn:
      if (source_.active()) {
        source_.Cancel(at);
        held_.clear();
        awaiting_first_frame_ = false;
        ++metrics_->barge_ins;
        metrics_->barge_in_stop_ms.push_back((ev->at - ev->speech_start).ms_f());
        if (generating_.valid()) {  // Never call the source while OnUserTurn runs.
          barge_in_deferred_ = true;
          barge_in_at_ = at;
        } else {
          reply_->OnBargeIn(at);
        }
        log_("barge-in, reply cut");
      }
      break;
    case testbench::TurnEventType::kEndOfTurn: {
      ++metrics_->user_turns;
      if (generating_.valid()) {
        next_turn_ = PendingTurn{std::move(user_audio_), at, ev->last_speech};
      } else {
        StartGeneration(std::move(user_audio_), at, ev->last_speech);
      }
      user_audio_.clear();
      break;
    }
  }
}


class AiCall {
 public:
  AiCall(AiServer& server, std::string room, int id, Timestamp now)
      : server_(server),
        self_("ai-server-" + std::to_string(id)),
        decoder_(kRate),
        encoder_(server.config().opus_bitrate_bps, true),
        conversation_(server.config(), &metrics_, [this](const std::string& line) { server_.Log("call " + this->room() + ": " + line); }),
        start_(now) {
    metrics_.room = std::move(room);
    ws_.Connect(server.signaling_);
    ws_.Send("{\"type\":\"join\",\"room\":" + net::JsonEscape(metrics_.room) + ",\"peer\":" + net::JsonEscape(self_) + "}");
  }
  ~AiCall() {
    if (!ws_.closed()) {
      if (!metrics_.peer.empty()) Send("{\"type\":\"bye\",\"to\":" + net::JsonEscape(metrics_.peer) + "}");
      ws_.Poll();
      ws_.Close();
    }
  }

  bool done() const { return done_; }
  AiCallMetrics& metrics() { return metrics_; }
  const std::string& room() const { return metrics_.room; }

  Timestamp Step(Timestamp now) {
    ws_.Poll();
    for (const std::string& msg : ws_.TakeMessages()) OnSignal(msg, now);
    if (ws_.closed()) End("signaling closed");
    if (now - start_ > server_.config().max_call) End("max call length");
    if (done_ || !ep_) return now + TimeDelta::Millis(10);
    ep_->Process(now);
    if (!metrics_.connected && ep_->IsConnected() && ep_->session()) {
      metrics_.connected = true;
      metrics_.connect_ms = (now - start_).ms_f();
      next_pull_ = next_send_ = now;
      server_.Log("call " + room() + ": connected in " + std::to_string(static_cast<int>(metrics_.connect_ms)) + " ms");
    }
    if (metrics_.connected) Media(now);
    Timestamp next = ep_->NextProcessTime(now);
    if (metrics_.connected) next = std::min({next, next_pull_, next_send_});
    return std::min(next, now + TimeDelta::Millis(10));
  }

  void End(const char* why) {
    if (done_) return;
    done_ = true;
    server_.Log("call " + room() + ": ended (" + why + ")");
  }
  void Finish(Timestamp now) {
    metrics_.duration_s = (now - start_).seconds();
  }

 private:
  void Send(const std::string& json) { ws_.Send(json); }
  std::string To() const { return "\"to\":" + net::JsonEscape(metrics_.peer); }

  void MakeEndpoint(Timestamp now) {
    if (ep_) return;
    const AiServerConfig& cfg = server_.config();
    for (const auto& ip : cfg.interfaces.empty() ? LocalIpv4Interfaces() : cfg.interfaces) network_.AddInterface(ip);
    p2p::ServerAddresses stun;
    std::vector<p2p::RelayServerConfig> turn;
    if (cfg.turn_url != "none") {
      std::optional<net::Url> url = cfg.turn_url.empty()
                                        ? std::optional<net::Url>(net::Url{server_.signaling_.host, server_.signaling_.port, "/turn", server_.signaling_.tls})
                                        : net::ParseUrl(cfg.turn_url);
      if (url) {
        if (auto body = net::HttpGet(*url, 2000)) ParseTurnResponse(*body, &stun, &turn);
        else server_.Log("call " + room() + ": GET /turn failed; host candidates only");
      }
    }
    allocator_ = std::make_unique<p2p::BasicPortAllocator>(&network_, &server_.sockets_);
    allocator_->SetConfiguration(now, stun, turn, 0, p2p::NO_PRUNE);
    allocator_->set_flags(p2p::PORTALLOCATOR_DISABLE_TCP);
    if (cfg.relay_only) allocator_->SetCandidateFilter(p2p::CF_RELAY);
    pc::PeerEndpointConfig c;
    c.allocator = allocator_.get();
    c.certificate_name = "sm-ai-server";
    c.transport.async_dns_resolver_factory = &server_.resolver_;
    c.audio = true;
    c.video = false;
    // Only the AI's own audio transceiver: a caller's video section is rejected, not received (C18's default).
    c.receive_remote_tracks = false;
    c.enable_data_channels = false;
    c.audio_decoder = &decoder_;
    c.audio_stream.nominal_rate = DataRate::BitsPerSec(cfg.opus_bitrate_bps + 8000);
    c.session.send_side.gcc.start_rate = DataRate::KilobitsPerSec(300);
    ep_ = std::make_unique<pc::PeerEndpoint>(std::move(c), now);
    ep_->SetOnIceCandidate([this](std::unique_ptr<pc::IceCandidate> cand) {
      const std::string m = "{\"type\":\"candidate\"," + To() + ",\"candidate\":" + net::JsonEscape(cand->ToString()) +
                            ",\"sdpMid\":" + net::JsonEscape(cand->sdp_mid()) +
                            ",\"sdpMLineIndex\":" + std::to_string(cand->sdp_mline_index()) + "}";
      Send(m);
    });
  }

  void SendDescription(const pc::SessionDescriptionInterface& desc, const char* type) {
    Send(std::string("{\"type\":\"") + type + "\"," + To() + ",\"sdp\":" + net::JsonEscape(pc::SdpSerialize(desc)) + "}");
  }

  void Offer(Timestamp now) {
    std::string error;
    auto offer = ep_->CreateOffer(&error);
    if (!offer) return End("CreateOffer failed");
    SendDescription(*offer, "offer");
    if (!ep_->SetLocalDescription(std::move(offer), now, &error)) End("SetLocalDescription failed");
  }

  void OnSignal(const std::string& text, Timestamp now) {
    auto m = net::JsonParseFlat(text);
    const std::string& type = m["type"];
    if (type == "joined") {
      const auto peers = net::JsonStringArray(m["peers"]);
      if (!peers.empty() && metrics_.peer.empty()) {
        metrics_.peer = peers.front();  // We joined later: we offer.
        MakeEndpoint(now);
        Offer(now);
      }
    } else if (type == "peer-joined") {
      if (metrics_.peer.empty()) {
        metrics_.peer = m["peer"];  // They joined later: they offer.
        MakeEndpoint(now);
      }
    } else if (type == "offer" || type == "answer") {
      if (metrics_.peer.empty()) metrics_.peer = m["from"];
      MakeEndpoint(now);
      pc::SdpParseError perr;
      std::string error;
      auto desc = pc::SdpDeserialize(type == "offer" ? pc::SdpType::kOffer : pc::SdpType::kAnswer, m["sdp"], &perr);
      if (!desc || !ep_->SetRemoteDescription(std::move(desc), now, &error)) return End("bad remote description");
      if (type == "offer") {
        auto answer = ep_->CreateAnswer(&error);
        if (!answer) return End("CreateAnswer failed");
        SendDescription(*answer, "answer");
        if (!ep_->SetLocalDescription(std::move(answer), now, &error)) return End("SetLocalDescription failed");
      }
      for (auto& c : pending_candidates_) AddCandidate(c, now);
      pending_candidates_.clear();
    } else if (type == "candidate") {
      if (!ep_ || !ep_->remote_description()) pending_candidates_.push_back(m);
      else AddCandidate(m, now);
    } else if (type == "peer-left" || type == "bye") {
      const std::string who = type == "bye" ? m["from"] : m["peer"];
      if (who.empty() || who == metrics_.peer) End("peer left");
    }
  }

  void AddCandidate(std::map<std::string, std::string>& m, Timestamp now) {
    if (m["candidate"].empty()) return;  // End of candidates.
    const std::string& idx = m["sdpMLineIndex"];
    const int index = idx.empty() || idx == "null" ? 0 : std::atoi(idx.c_str());
    const std::string mid = m["sdpMid"] == "null" ? "" : m["sdpMid"];
    pc::SdpParseError perr;
    std::string error;
    if (auto cand = pc::IceCandidate::Create(mid, index, m["candidate"], &perr)) ep_->AddIceCandidate(*cand, now, &error);
  }

  void Media(Timestamp now) {
    rtp::Session* s = ep_->session();
    // Play out 10 ms; the turn detector hears it.
    while (now >= next_pull_) {
      std::vector<float> pcm(k10ms);
      timing::AudioOperation op = timing::AudioOperation::kSilence;
      if (ep_->audio_receive()) op = s->GetAudio(*ep_->audio_receive(), pcm, now);
      conversation_.OnPlayout(pcm, op, next_pull_);
      next_pull_ += TimeDelta::Millis(10);
    }
    conversation_.CollectReply(now, next_send_);
    // Send 20 ms per slot: the reply's frame due by the slot (not by `now`), or silence. Exactly one frame per slot
    // keeps the RTP timestamps on the send clock (#1126): polling by `now` after a late wake-up took the next slot's
    // frame too, then sent silence in that slot, cutting a hole in the reply and moving the timestamps 20 ms ahead
    // for the rest of the call. A loop that wakes late catches up slot by slot, as libwebrtc's audio thread does.
    while (now >= next_send_) {
      metrics_.send_late_max_ms = std::max(metrics_.send_late_max_ms, (now - next_send_).ms_f());
      SendFrame(conversation_.Slot(next_send_, now), now);
      ++slots_;
      metrics_.rtp_ahead_max_ms = std::max(metrics_.rtp_ahead_max_ms, 20.0 * static_cast<double>(frames_out_ - slots_));
      next_send_ += TimeDelta::Millis(20);
    }
    ep_->FlushOutgoing(now);
  }


  void SendFrame(std::span<const float> pcm, Timestamp now) {
    int16_t in[k20ms] = {};
    for (size_t i = 0; i < pcm.size() && i < k20ms; ++i) in[i] = static_cast<int16_t>(std::clamp(pcm[i], -1.f, 1.f) * 32767);
    const std::vector<uint8_t> payload = encoder_.Encode(in);
    if (!payload.empty() && ep_->audio_send()) {
      ep_->session()->SendAudio(*ep_->audio_send(), rtp::EncodedAudioFrame{rtp_ts_, payload, metrics_.audio_frames_sent == 0},
                                now);
      ++metrics_.audio_frames_sent;
    }
    rtp_ts_ += static_cast<uint32_t>(k20ms);
    ++frames_out_;
  }

  AiServer& server_;
  std::string self_;
  net::WebSocketClient ws_;
  p2p::StaticNetworkManager network_;
  std::unique_ptr<p2p::BasicPortAllocator> allocator_;
  testbench::OpusAudioDecoder decoder_;
  testbench::OpusSendEncoder encoder_;
  std::unique_ptr<pc::PeerEndpoint> ep_;
  AiCallMetrics metrics_;
  AiConversation conversation_;
  int64_t slots_ = 0;
  int64_t frames_out_ = 0;
  std::vector<std::map<std::string, std::string>> pending_candidates_;
  Timestamp start_;
  Timestamp next_pull_ = Timestamp::PlusInfinity();
  Timestamp next_send_ = Timestamp::PlusInfinity();
  uint32_t rtp_ts_ = 0;
  bool done_ = false;
};

AiServer::AiServer(AiServerConfig config) : config_(std::move(config)) {}

AiServer::~AiServer() = default;

void AiServer::Log(const std::string& line) const {
  if (config_.verbose) std::fprintf(stderr, "ai_server: %s\n", line.c_str());
}

bool AiServer::Start(Timestamp now) {
  auto url = net::ParseUrl(config_.signaling_url);
  if (!url) return false;
  signaling_ = *url;
  p2p::InstallStunCryptoFromTls();
  next_reconnect_ = now;
  Step(now);
  return true;
}

void AiServer::HandleControl(const std::string& message, Timestamp now) {
  auto m = net::JsonParseFlat(message);
  if (m["type"] == "ai-call") {
    const std::string& room = m["room"];
    if (room.empty()) return;
    for (const auto& c : calls_) {
      if (c->room() == room) return;
    }
    Log("ai-call for room " + room);
    calls_.push_back(std::make_unique<AiCall>(*this, room, next_call_id_++, now));
  }
}

Timestamp AiServer::Step(Timestamp now) {
  if (now >= next_reconnect_) {
    next_reconnect_ = Timestamp::PlusInfinity();
    control_ = std::make_unique<net::WebSocketClient>();
    registered_ = false;
    if (control_->Connect(signaling_)) {
      control_->Send("{\"type\":\"register-ai\"}");
    }
  }
  if (control_) {
    control_->Poll();
    // The service doesn't acknowledge the registration (#1110): an open socket that sent it is registered.
    if (control_->open()) registered_ = true;
    for (const std::string& msg : control_->TakeMessages()) HandleControl(msg, now);
    if (control_->closed() && next_reconnect_.IsPlusInfinity()) {
      registered_ = false;
      next_reconnect_ = now + TimeDelta::Seconds(2);
      Log("signaling connection lost; retrying in 2 s");
    }
  }
  Timestamp next = std::min(next_reconnect_, now + TimeDelta::Millis(10));
  for (auto& c : calls_) next = std::min(next, c->Step(now));
  for (size_t i = 0; i < calls_.size();) {
    if (calls_[i]->done()) {
      Finish(*calls_[i], now);
      calls_.erase(calls_.begin() + static_cast<long>(i));
    } else {
      ++i;
    }
  }
  return next;
}

void AiServer::Finish(AiCall& call, Timestamp now) {
  call.Finish(now);
  const std::string line = call.metrics().ToJson();
  Log(line);
  if (!config_.metrics_path.empty()) {
    std::ofstream f(config_.metrics_path, std::ios::app);
    f << line << "\n";
  }
  finished_.push_back(call.metrics());
}

void AiServer::Wait(Timestamp now, TimeDelta wait) {
  sockets_.Poll(now, std::clamp(wait, TimeDelta::Zero(), TimeDelta::Millis(5)));
  resolver_.Poll(now);
}

void AiServer::Shutdown(Timestamp now) {
  for (auto& c : calls_) {
    c->End("shutdown");
    Finish(*c, now);
  }
  calls_.clear();
  if (control_) control_->Close();
}

}  // namespace sm::apps
