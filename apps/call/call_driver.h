// A CallClient (call_client.h) on the plain ws:// signaling client of apps/net, driven in real time on the caller's
// thread: for the e2e test and the simulator smoke run (#1116). The iOS app uses the platform's wss:// instead.
#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "call_client.h"
#include "json.h"
#include "websocket.h"

namespace sm::apps {

inline Timestamp WallNow() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

class CallDriver {
 public:
  // `signaling` is ws://host:port/ws; /turn is fetched from the same host unless `turn_url` is "none".
  CallDriver(const std::string& signaling, CallClientConfig config, const std::string& turn_url = "") {
    url_ = net::ParseUrl(signaling).value_or(net::Url{});
    client_ = std::make_unique<CallClient>(std::move(config), [this](const std::string& m) { ws_.Send(m); });
    if (turn_url != "none") {
      net::Url t = turn_url.empty() ? net::Url{url_.host, url_.port, "/turn"} : net::ParseUrl(turn_url).value_or(net::Url{});
      if (auto body = net::HttpGet(t, 2000)) client_->SetTurnResponse(*body);
    }
  }
  bool Start(Timestamp now) {
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
  net::WebSocketClient ws_;
  std::unique_ptr<CallClient> client_;
};

}  // namespace sm::apps
