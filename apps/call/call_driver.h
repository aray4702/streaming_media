// A CallClient (call_client.h) on the plain ws:// signaling client of apps/net, driven in real time on the caller's
// thread: for the e2e test and the simulator smoke run (#1116). The iOS app uses the platform's wss:// instead.
// With a LiveKitJoin (C19, #1164) it joins a LiveKit room: a token from the signaling service's /lk-token, the
// WebSocket to LiveKit's /rtc, and binary (protobuf) frames.
#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "call_client.h"
#include "json.h"
#include "livekit_proto.h"
#include "websocket.h"

namespace sm::apps {

inline Timestamp WallNow() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

// Where a LiveKit call joins: `token_url` is the signaling service's http[s]://HOST[:PORT]/lk-token; `url` is the
// LiveKit server (ws[s]://HOST[:PORT][/prefix]); empty: the one /lk-token returns, else the token service's origin
// (the deployment's nginx routes /rtc to LiveKit there, docs/deploy/livekit.md).
struct LiveKitJoin {
  std::string token_url;
  std::string url;
};

class CallDriver {
 public:
  // `signaling` is ws://host:port/ws; /turn is fetched from the same host unless `turn_url` is "none".
  CallDriver(const std::string& signaling, CallClientConfig config, const std::string& turn_url = "") {
    url_ = net::ParseUrl(signaling).value_or(net::Url{});
    client_ = std::make_unique<CallClient>(std::move(config), [this](const std::string& m) { ws_.Send(m); });
    if (turn_url != "none") {
      net::Url t = turn_url.empty() ? net::Url{url_.host, url_.port, "/turn", url_.tls} : net::ParseUrl(turn_url).value_or(net::Url{});
      if (auto body = net::HttpGet(t, 2000)) client_->SetTurnResponse(*body);
    }
  }
  // A LiveKit call (above); it sets config.livekit. /turn comes from the token service's host unless `turn_url`.
  CallDriver(const LiveKitJoin& join, CallClientConfig config, const std::string& turn_url = "") {
    config.livekit = true;
    if (config.peer_id.empty()) config.peer_id = "native-" + std::to_string(WallNow().us() % 1000000);
    binary_ = true;
    const net::Url token_service = net::ParseUrl(join.token_url).value_or(net::Url{});
    std::string lk_url = join.url;
    net::Url q = token_service;
    q.path += "?room=" + config.room + "&identity=" + config.peer_id;  // The service accepts [A-Za-z0-9_.-] only.
    // Publish only (until C18): a grant without canSubscribe, so LiveKit makes the publisher connection the primary
    // one and opens no subscriber connection (whose data-only first offer the endpoint can't take yet).
    if (!config.livekit_subscribe) q.path += "&subscribe=0";
    if (auto body = net::HttpGet(q, 3000)) {
      auto m = net::JsonParseFlat(*body);
      token_ = m["token"];
      if (lk_url.empty()) lk_url = m["url"];
      if (token_.empty()) error_ = "/lk-token: " + *body;
    } else {
      error_ = "no token from " + join.token_url;
    }
    if (lk_url.empty()) {
      lk_url = std::string(token_service.tls ? "wss://" : "ws://") + token_service.host + ":" +
               std::to_string(token_service.port);
    }
    url_ = net::ParseUrl(lk_url).value_or(net::Url{});
    url_.path = livekit::JoinPath(url_.path, token_, config.livekit_subscribe);
    client_ = std::make_unique<CallClient>(std::move(config), [this](const std::string& m) { ws_.SendBinary(m); });
    if (turn_url != "none") {
      net::Url t = turn_url.empty() ? net::Url{token_service.host, token_service.port, "/turn", token_service.tls}
                                    : net::ParseUrl(turn_url).value_or(net::Url{});
      if (auto body = net::HttpGet(t, 2000)) client_->SetTurnResponse(*body);
    }
  }
  // Why a LiveKit driver has no token; empty otherwise.
  const std::string& error() const { return error_; }
  bool Start(Timestamp now) {
    if (binary_ && token_.empty()) return false;
    if (!ws_.Connect(url_)) return false;
    client_->Start(now);
    return true;
  }
  // Signaling and the call; returns when it next needs to run.
  Timestamp Step(Timestamp now) {
    ws_.Poll();
    for (const std::string& m : ws_.TakeMessages()) client_->OnSignal(m, now);
    if (ws_.closed()) client_->OnSignalingClosed(now);
    return client_->Step(now);
  }
  void Wait(Timestamp now, TimeDelta wait) { client_->Wait(now, wait); }
  void Hangup(Timestamp now) {
    client_->Hangup(now);
    ws_.Poll();
  }
  CallClient& client() { return *client_; }

 private:
  net::Url url_;
  bool binary_ = false;
  std::string token_, error_;
  net::WebSocketClient ws_;
  std::unique_ptr<CallClient> client_;
};

}  // namespace sm::apps
