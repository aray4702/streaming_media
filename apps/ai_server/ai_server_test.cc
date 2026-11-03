// The AI server (C12 part 1, #1103). AiServerNet: the WebSocket, JSON and /turn helpers. AiServer: the issue's
// acceptance test. A native caller (pc::PeerEndpoint over OS sockets, Opus through libopus) calls the server through
// a signaling stub of #1102's protocol (WebSocket /ws, JSON, rooms, `ai-` rooms routed to the registered AI, GET
// /turn), and through the real one (apps/signaling/server.py, #1110; skipped without python3), on loopback in real
// time: it connects, the user speaks and stops, a scripted reply comes back after the
// turn, and when the user talks over the reply (barge-in) the reply stops long before its scripted end.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ai_server.h"
#include "json.h"
#include "opus_decoder.h"
#include "opus_encoder.h"
#include "os_sockets.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"
#include "websocket.h"

using namespace sm;

namespace {

Timestamp Now() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

// --- A signaling stub of #1102's protocol, on its own thread -------------------------------------------------------

class SignalingStub {
 public:
  SignalingStub() {
    std::signal(SIGPIPE, SIG_IGN);
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listen_fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    listen(listen_fd_, 16);
    socklen_t len = sizeof sa;
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&sa), &len);
    port_ = ntohs(sa.sin_port);
    thread_ = std::thread([this] { Run(); });
  }
  ~SignalingStub() {
    stop_ = true;
    thread_.join();
    for (auto& c : conns_) close(c.fd);
    close(listen_fd_);
  }
  uint16_t port() const { return port_; }
  std::atomic<int> ai_calls{0};
  std::atomic<int> turn_requests{0};
  std::atomic<int> relayed{0};

 private:
  struct Conn {
    int fd = -1;
    std::string in;
    bool ws = false, ai = false, dead = false, close_after_write = false;
    std::string room, peer;
  };

  void Write(Conn& c, const std::string& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
      const ssize_t n = send(c.fd, bytes.data() + off, bytes.size() - off, 0);
      if (n > 0) {
        off += static_cast<size_t>(n);
      } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        pollfd p{c.fd, POLLOUT, 0};
        poll(&p, 1, 100);
      } else {
        c.dead = true;
        return;
      }
    }
  }
  void SendWs(Conn& c, const std::string& json) {
    const auto f = net::WsEncodeFrame(net::kWsText, json, false);
    Write(c, std::string(f.begin(), f.end()));
  }

  void OnMessage(size_t from, const std::string& text) {
    auto m = net::JsonParseFlat(text);
    const std::string type = m["type"];
    Conn& c = conns_[from];
    if (type == "register-ai") {
      c.ai = true;
    } else if (type == "join") {
      std::vector<size_t> members;
      for (size_t i = 0; i < conns_.size(); ++i) {
        if (i != from && !conns_[i].dead && conns_[i].ws && conns_[i].room == m["room"]) members.push_back(i);
      }
      if (members.size() >= 4) {
        SendWs(c, "{\"type\":\"error\",\"reason\":\"room full\"}");
        return;
      }
      c.room = m["room"];
      c.peer = m["peer"];
      std::string peers = "[";
      for (size_t k = 0; k < members.size(); ++k) peers += (k ? "," : "") + net::JsonEscape(conns_[members[k]].peer);
      SendWs(c, "{\"type\":\"joined\",\"peers\":" + peers + "]}");
      for (size_t i : members) SendWs(conns_[i], "{\"type\":\"peer-joined\",\"peer\":" + net::JsonEscape(c.peer) + "}");
      if (members.empty() && c.room.rfind("ai-", 0) == 0) {
        for (auto& a : conns_) {
          if (a.ai && !a.dead) {
            SendWs(a, "{\"type\":\"ai-call\",\"room\":" + net::JsonEscape(c.room) + "}");
            ++ai_calls;
            break;
          }
        }
      }
    } else if (type == "offer" || type == "answer" || type == "candidate" || type == "bye") {
      for (auto& to : conns_) {
        if (!to.dead && to.ws && to.room == c.room && to.peer == m["to"]) {
          SendWs(to, "{\"from\":" + net::JsonEscape(c.peer) + "," + text.substr(text.find('{') + 1));
          ++relayed;
        }
      }
    }
  }

  void OnData(size_t i) {
    Conn& c = conns_[i];
    if (!c.ws) {
      const size_t end = c.in.find("\r\n\r\n");
      if (end == std::string::npos) return;
      const std::string req = c.in.substr(0, end + 4);
      c.in.erase(0, end + 4);
      if (req.rfind("GET /turn", 0) == 0) {
        ++turn_requests;
        const std::string body = "{\"urls\":[],\"ttl\":86400}";  // No secret configured: no TURN, and no STUN here.
        Write(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nConnection: close\r\n\r\n" + body);
        c.dead = true;
        return;
      }
      const std::string resp = net::WsServerHandshakeResponse(req);
      if (resp.empty() || req.rfind("GET /ws", 0) != 0) {
        Write(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        c.dead = true;
        return;
      }
      Write(c, resp);
      c.ws = true;
    }
    while (!conns_[i].dead) {
      net::WsFrame f;
      const long used = net::WsDecodeFrame(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(conns_[i].in.data()),
                                                                    conns_[i].in.size()),
                                           &f);
      if (used <= 0) {
        if (used < 0) conns_[i].dead = true;
        return;
      }
      conns_[i].in.erase(0, static_cast<size_t>(used));
      if (f.opcode == net::kWsClose) conns_[i].dead = true;
      else if (f.opcode == net::kWsText) OnMessage(i, f.payload);
    }
  }

  void Drop(size_t i) {
    Conn gone = conns_[i];
    close(gone.fd);
    conns_.erase(conns_.begin() + static_cast<long>(i));
    if (gone.room.empty()) return;
    for (auto& c : conns_) {
      if (!c.dead && c.ws && c.room == gone.room) SendWs(c, "{\"type\":\"peer-left\",\"peer\":" + net::JsonEscape(gone.peer) + "}");
    }
  }

  void Run() {
    while (!stop_) {
      std::vector<pollfd> fds{{listen_fd_, POLLIN, 0}};
      for (const auto& c : conns_) fds.push_back({c.fd, POLLIN, 0});
      if (poll(fds.data(), fds.size(), 5) <= 0) continue;
      if (fds[0].revents & POLLIN) {
        const int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd >= 0) {
          Conn c;
          c.fd = fd;
          conns_.push_back(c);
        }
      }
      for (size_t k = 1; k < fds.size(); ++k) {
        if (!(fds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        char buf[16384];
        const ssize_t n = recv(fds[k].fd, buf, sizeof buf, 0);
        for (size_t i = 0; i < conns_.size(); ++i) {
          if (conns_[i].fd != fds[k].fd) continue;
          if (n <= 0) conns_[i].dead = true;
          else {
            conns_[i].in.append(buf, static_cast<size_t>(n));
            OnData(i);
          }
        }
      }
      for (size_t i = 0; i < conns_.size();) {
        if (conns_[i].dead) Drop(i);
        else ++i;
      }
    }
  }

  int listen_fd_ = -1;
  uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::vector<Conn> conns_;
  std::thread thread_;
};

// --- The caller: a native endpoint with a scripted user ------------------------------------------------------------

constexpr int kRate = 48000;

// The test runs in real time; a sanitizer build (Debug, ASan) runs libopus and the endpoints several times slower, so
// the caller's playout falls behind and holds more of the reply when the server cuts it. The server-side metrics keep
// their bounds; the caller's get this slack there.
#if defined(__SANITIZE_ADDRESS__)
constexpr double kSlowBuild = 2;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr double kSlowBuild = 2;
#else
constexpr double kSlowBuild = 1;
#endif
#else
constexpr double kSlowBuild = 1;
#endif

double Rms(std::span<const float> x) {
  double s = 0;
  for (float v : x) s += static_cast<double>(v) * v;
  return x.empty() ? 0 : std::sqrt(s / static_cast<double>(x.size()));
}

struct Caller {
  interop::OsSocketServer sockets;
  p2p::StaticNetworkManager network;
  std::unique_ptr<p2p::BasicPortAllocator> allocator;
  testbench::OpusAudioDecoder decoder{kRate};
  testbench::OpusSendEncoder encoder{32000, true};
  std::unique_ptr<pc::PeerEndpoint> ep;
  net::WebSocketClient ws;
  std::string remote;
  bool signaling_ok = true;

  // The user's speech: sample `n` of the call (from the connection), zero outside the scheduled utterances.
  struct Utterance {
    Timestamp start = Timestamp::PlusInfinity();
    std::vector<float> pcm;
  };
  std::vector<Utterance> speech;
  Timestamp connected_at = Timestamp::PlusInfinity();
  Timestamp next_send = Timestamp::PlusInfinity(), next_pull = Timestamp::PlusInfinity();
  uint32_t rtp_ts = 0;
  int frames_sent = 0;

  // What the caller hears: 10 ms frames' RMS, with their play times.
  std::vector<std::pair<Timestamp, double>> heard;

  Caller(uint16_t port, const std::string& room, Timestamp now) {
    network.AddInterface(*p2p::IpAddress::FromString("127.0.0.1"));
    allocator = std::make_unique<p2p::BasicPortAllocator>(&network, &sockets);
    allocator->SetConfiguration(now, p2p::ServerAddresses{}, {}, 0, p2p::NO_PRUNE);
    allocator->set_flags(p2p::PORTALLOCATOR_DISABLE_TCP);
    pc::PeerEndpointConfig c;
    c.allocator = allocator.get();
    c.certificate_name = "caller";
    c.video = false;
    c.enable_data_channels = false;
    c.audio_decoder = &decoder;
    c.audio_stream.nominal_rate = DataRate::KilobitsPerSec(40);
    ep = std::make_unique<pc::PeerEndpoint>(std::move(c), now);
    ep->SetOnIceCandidate([this](std::unique_ptr<pc::IceCandidate> cand) {
      ws.Send("{\"type\":\"candidate\",\"to\":" + net::JsonEscape(remote) + ",\"candidate\":" +
              net::JsonEscape(cand->ToString()) + ",\"sdpMid\":" + net::JsonEscape(cand->sdp_mid()) +
              ",\"sdpMLineIndex\":" + std::to_string(cand->sdp_mline_index()) + "}");
    });
    ws.Connect(net::Url{"127.0.0.1", port, "/ws"});
    ws.Send("{\"type\":\"join\",\"room\":" + net::JsonEscape(room) + ",\"peer\":\"caller\"}");
  }

  void Say(Timestamp at, double seconds, uint64_t seed) {
    speech.push_back(Utterance{at, sim::SpeechLike(kRate, seconds, seed, false)});
  }

  void Signaling(Timestamp now) {
    ws.Poll();
    for (const std::string& text : ws.TakeMessages()) {
      auto m = net::JsonParseFlat(text);
      std::string error;
      if (m["type"] == "offer") {
        remote = m["from"];
        pc::SdpParseError perr;
        auto desc = pc::SdpDeserialize(pc::SdpType::kOffer, m["sdp"], &perr);
        if (!desc || !ep->SetRemoteDescription(std::move(desc), now, &error)) {
          signaling_ok = false;
          continue;
        }
        auto answer = ep->CreateAnswer(&error);
        if (!answer) {
          signaling_ok = false;
          continue;
        }
        ws.Send("{\"type\":\"answer\",\"to\":" + net::JsonEscape(remote) + ",\"sdp\":" + net::JsonEscape(pc::SdpSerialize(*answer)) + "}");
        signaling_ok = ep->SetLocalDescription(std::move(answer), now, &error) && signaling_ok;
      } else if (m["type"] == "candidate" && ep->remote_description()) {
        pc::SdpParseError perr;
        auto cand = pc::IceCandidate::Create(m["sdpMid"], std::atoi(m["sdpMLineIndex"].c_str()), m["candidate"], &perr);
        if (cand) ep->AddIceCandidate(*cand, now, &error);
      }
    }
  }

  void Step(Timestamp now) {
    Signaling(now);
    ep->Process(now);
    if (connected_at.IsPlusInfinity() && ep->IsConnected() && ep->session()) {
      connected_at = next_send = next_pull = now;
    }
    if (connected_at.IsPlusInfinity()) return;
    rtp::Session* s = ep->session();
    while (now >= next_send) {
      int16_t pcm[960] = {};
      for (const auto& u : speech) {
        for (int k = 0; k < 960; ++k) {
          const int64_t n = (next_send - u.start).us() * kRate / 1'000'000 + k;
          if (n >= 0 && n < static_cast<int64_t>(u.pcm.size())) pcm[k] = static_cast<int16_t>(u.pcm[static_cast<size_t>(n)] * 32767);
        }
      }
      const auto payload = encoder.Encode(pcm);
      if (!payload.empty() && ep->audio_send()) {
        s->SendAudio(*ep->audio_send(), rtp::EncodedAudioFrame{rtp_ts, payload, frames_sent == 0}, now);
        ++frames_sent;
      }
      rtp_ts += 960;
      next_send += TimeDelta::Millis(20);
    }
    while (now >= next_pull) {
      std::vector<float> out(480);
      if (ep->audio_receive()) s->GetAudio(*ep->audio_receive(), out, now);
      heard.emplace_back(next_pull, Rms(out));
      next_pull += TimeDelta::Millis(10);
    }
    ep->FlushOutgoing(now);
  }

  // The first time at or after `from` that `n` frames in a row are loud (> on) or quiet (< off).
  std::optional<Timestamp> FirstRun(Timestamp from, bool loud, int n) const {
    int run = 0;
    for (size_t i = 0; i < heard.size(); ++i) {
      if (heard[i].first < from) continue;
      const bool hit = loud ? heard[i].second > 0.02 : heard[i].second < 0.003;
      run = hit ? run + 1 : 0;
      if (run == n) return heard[i + 1 - static_cast<size_t>(n)].first;
    }
    return std::nullopt;
  }
};

}  // namespace

// --- AiServerNet ----------------------------------------------------------------------------------------------------

SM_TEST(AiServerNet, WebSocketAcceptKeyMatchesRfc6455) {
  // RFC 6455 section 1.3's example.
  SM_EXPECT_EQ(net::WebSocketAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="), std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
  const uint8_t f[] = {'f', 'o', 'o', 'b', 'a'};
  SM_EXPECT_EQ(net::Base64Encode(std::span<const uint8_t>(f, 5)), std::string("Zm9vYmE="));
  SM_EXPECT_EQ(net::Base64Encode(std::span<const uint8_t>(f, 4)), std::string("Zm9vYg=="));
}

SM_TEST(AiServerNet, FramesRoundTripMaskedAndLong) {
  for (size_t len : {0u, 5u, 125u, 126u, 70000u}) {
    const std::string payload(len, 'x');
    for (bool mask : {false, true}) {
      const auto bytes = net::WsEncodeFrame(net::kWsText, payload, mask);
      net::WsFrame f;
      SM_EXPECT_EQ(net::WsDecodeFrame(std::span<const uint8_t>(bytes.data(), bytes.size() - 1), &f), 0L);
      SM_EXPECT_EQ(net::WsDecodeFrame(bytes, &f), static_cast<long>(bytes.size()));
      SM_EXPECT_EQ(f.payload, payload);
      SM_EXPECT_EQ(static_cast<int>(f.opcode), static_cast<int>(net::kWsText));
    }
  }
}

SM_TEST(AiServerNet, JsonAndTurnResponse) {
  auto m = net::JsonParseFlat("{\"type\":\"joined\",\"peers\":[\"a\",\"b\\\"c\"],\"n\":3,\"sdp\":\"v=0\\r\\n\"}");
  SM_EXPECT_EQ(m["type"], std::string("joined"));
  SM_EXPECT_EQ(m["n"], std::string("3"));
  SM_EXPECT_EQ(m["sdp"], std::string("v=0\r\n"));
  const auto peers = net::JsonStringArray(m["peers"]);
  SM_ASSERT_EQ(peers.size(), size_t{2});
  SM_EXPECT_EQ(peers[1], std::string("b\"c"));
  p2p::ServerAddresses stun;
  std::vector<p2p::RelayServerConfig> turn;
  apps::ParseTurnResponse(
      "{\"urls\":[\"stun:10.0.0.1:3478\",\"turn:10.0.0.1:3478?transport=udp\",\"turn:10.0.0.1?transport=tcp\","
      "\"turns:127.0.0.1:5349?transport=tcp\"],\"username\":\"123:ai\",\"credential\":\"c2VjcmV0\",\"ttl\":86400}",
      &stun, &turn);
  SM_EXPECT_EQ(stun.size(), size_t{1});
  SM_ASSERT_EQ(turn.size(), size_t{3});
  SM_EXPECT_EQ(static_cast<int>(turn[0].ports[0].proto), static_cast<int>(p2p::PROTO_UDP));
  SM_EXPECT_EQ(static_cast<int>(turn[1].ports[0].proto), static_cast<int>(p2p::PROTO_TCP));
  SM_EXPECT_EQ(static_cast<int>(turn[2].ports[0].proto), static_cast<int>(p2p::PROTO_TLS));
  SM_EXPECT_EQ(turn[0].credentials.username, std::string("123:ai"));
}

// --- AiServer -------------------------------------------------------------------------------------------------------

namespace {

// The service as a separate process: apps/signaling/server.py (#1110) on a free loopback port, without TURN or STUN.
struct PythonService {
  pid_t pid = -1;
  uint16_t port = 0;
  // False if python3 or the script is missing, or it doesn't come up within 10 s.
  bool Start() {
    const char* python = SM_PYTHON;
    const char* script = SM_SIGNALING_SERVER_PY;
    if (python[0] == '\0' || access(script, R_OK) != 0) return false;
    int fds[2];
    if (pipe(fds) != 0) return false;
    pid = fork();
    if (pid == 0) {
      dup2(fds[1], 1);
      close(fds[0]);
      setenv("PORT", "0", 1);
      setenv("HOST", "127.0.0.1", 1);
      setenv("STUN_URLS", "", 1);
      unsetenv("TURN_URLS");
      unsetenv("TURN_SECRET");
      execlp(python, python, script, static_cast<char*>(nullptr));
      _exit(127);
    }
    close(fds[1]);
    std::string line;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (line.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline) {
      pollfd p{fds[0], POLLIN, 0};
      if (poll(&p, 1, 100) <= 0) continue;
      char buf[256];
      const ssize_t n = read(fds[0], buf, sizeof buf);
      if (n <= 0) break;
      line.append(buf, static_cast<size_t>(n));
    }
    close(fds[0]);
    const size_t at = line.find("port ");
    if (at != std::string::npos) port = static_cast<uint16_t>(std::atoi(line.c_str() + at + 5));
    return port != 0;
  }
  ~PythonService() {
    if (pid > 0) {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
    }
  }
};

// The acceptance call through the signaling service on `port`: the caller joins `room`.
void CallTheAiServer(uint16_t port, const std::string& room) {
  apps::AiServerConfig cfg;
  cfg.signaling_url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
  cfg.interfaces = {*p2p::IpAddress::FromString("127.0.0.1")};
  cfg.verbose = true;
  cfg.ai_source.first_chunk = TimeDelta::Millis(200);  // The model's thinking time before its first audio.
  // A long first reply (the user cuts it), then a short one for the turn the barge-in starts.
  cfg.reply_factory = [] {
    return std::make_unique<apps::ScriptedReplySource>(std::vector<TimeDelta>{TimeDelta::Seconds(5), TimeDelta::Seconds(1)});
  };
  apps::AiServer server(cfg);
  Timestamp now = Now();
  SM_ASSERT_TRUE(server.Start(now));
  // Wait for the registration before the call, as a deployed server is up before callers.
  for (Timestamp until = now + TimeDelta::Seconds(5); now < until && !server.registered();) {
    server.Wait(now, server.Step(now) - now);
    now = Now();
  }
  SM_ASSERT_TRUE(server.registered());
  // The service reads the registration before the caller's join (another connection).
  for (Timestamp until = now + TimeDelta::Millis(300); now < until;) {
    server.Wait(now, server.Step(now) - now);
    now = Now();
  }

  Caller caller(port, room, now);
  const Timestamp start = now;
  bool said = false, barged = false;
  std::optional<Timestamp> reply_start, barge_at, reply_stop;
  while (now - start < TimeDelta::Seconds(25)) {
    server.Step(now);
    caller.Step(now);
    if (!said && !caller.connected_at.IsPlusInfinity()) {
      said = true;
      caller.Say(caller.connected_at + TimeDelta::Millis(500), 1.2, 1);  // The user's turn.
    }
    if (said && !reply_start) reply_start = caller.FirstRun(caller.connected_at + TimeDelta::Millis(1800), true, 3);
    if (reply_start && !barged && now >= *reply_start + TimeDelta::Seconds(1)) {
      barged = true;
      barge_at = now;
      caller.Say(now, 1.0, 7);  // The user talks over the reply.
    }
    if (barge_at && !reply_stop) {
      // Quiet for 300 ms once the user's own speech has started (the caller hears only the AI).
      reply_stop = caller.FirstRun(*barge_at, false, 30);
    }
    if (reply_stop && now - *barge_at > TimeDelta::Seconds(4)) break;
    caller.sockets.Poll(now, TimeDelta::Millis(1));
    server.Wait(now, TimeDelta::Millis(1));
    now = Now();
  }
  caller.ws.Send("{\"type\":\"bye\",\"to\":" + net::JsonEscape(caller.remote) + "}");
  caller.ws.Poll();
  for (Timestamp until = now + TimeDelta::Seconds(3); now < until && server.finished_calls().empty();) {
    caller.ws.Poll();
    server.Wait(now, server.Step(now) - now);
    now = Now();
  }
  server.Shutdown(now);

  SM_EXPECT_TRUE(caller.signaling_ok);
  SM_ASSERT_TRUE(!caller.connected_at.IsPlusInfinity());
  std::printf("connected after %.0f ms\n", (caller.connected_at - start).ms_f());
  SM_ASSERT_TRUE(reply_start.has_value());
  // The user stopped at +1.7 s; the reply arrives after the end-of-turn silence, the thinking time and the path.
  const double reply_after_ms = (*reply_start - (caller.connected_at + TimeDelta::Millis(1700))).ms_f();
  std::printf("reply heard %.0f ms after the user stopped\n", reply_after_ms);
  SM_EXPECT_TRUE(reply_after_ms > 600 && reply_after_ms < 2500);
  SM_ASSERT_TRUE(barge_at.has_value() && reply_stop.has_value());
  const double stop_ms = (*reply_stop - *barge_at).ms_f();
  const double heard_ms = (*reply_stop - *reply_start).ms_f();
  std::printf("barge-in: reply stopped %.0f ms after the user started; reply heard for %.0f of 5000 ms\n", stop_ms, heard_ms);
  SM_EXPECT_TRUE(stop_ms < 1500 * kSlowBuild);
  SM_EXPECT_TRUE(heard_ms < 3500 + 1000 * (kSlowBuild - 1));  // Of a 5 s reply.

  SM_ASSERT_EQ(server.finished_calls().size(), size_t{1});
  const apps::AiCallMetrics& mt = server.finished_calls()[0];
  std::printf("%s\n", mt.ToJson().c_str());
  SM_EXPECT_EQ(mt.room, room);
  SM_EXPECT_EQ(mt.peer, std::string("caller"));
  SM_EXPECT_TRUE(mt.connected);
  SM_EXPECT_EQ(mt.barge_ins, 1);
  SM_EXPECT_TRUE(mt.user_turns >= 1);
  SM_EXPECT_TRUE(mt.replies >= 1);
  SM_ASSERT_TRUE(!mt.response_delay_ms.empty());
  // The server's view: the end of the user's speech as played out here to the first reply frame sent. It holds the
  // end-of-turn silence (600 ms plus the VAD's hangover) and the 200 ms thinking time.
  SM_EXPECT_TRUE(mt.response_delay_ms[0] >= 750 && mt.response_delay_ms[0] < 1500);
  SM_ASSERT_EQ(mt.barge_in_stop_ms.size(), size_t{1});
  SM_EXPECT_TRUE(mt.barge_in_stop_ms[0] >= 150 && mt.barge_in_stop_ms[0] < 800);
  SM_EXPECT_TRUE(mt.user_speech_ms > 1500);
  SM_EXPECT_TRUE(mt.user_speech_lost_ms <= 0.02 * mt.user_speech_ms);  // Loopback: nothing lost.
  SM_EXPECT_TRUE(mt.reply_frames_sent > 0);
}

}  // namespace

SM_TEST(AiServer, AnswersAnAiRoomRepliesAfterTheTurnAndStopsOnBargeIn) {
  SignalingStub stub;
  CallTheAiServer(stub.port(), "ai-test-room");
  SM_EXPECT_EQ(stub.ai_calls.load(), 1);
  SM_EXPECT_TRUE(stub.turn_requests.load() >= 1);  // STUN/TURN asked of the service.
  SM_EXPECT_TRUE(stub.relayed.load() > 0);
}

// The same call through the real service, apps/signaling/server.py (#1110).
SM_TEST(AiServer, ThroughTheSignalingService) {
  PythonService service;
  if (!service.Start()) {
    std::printf("SKIPPED: apps/signaling/server.py or python3 not available\n");
    return;
  }
  CallTheAiServer(service.port, "ai-service-room");
}
