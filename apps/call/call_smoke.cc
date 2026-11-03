// The calling app's controller from the command line (#1116): joins a room through a ws:// signaling service, speaks
// for a moment (speech-like audio) and reports the call's live stats as JSON lines. It is the simulator smoke run:
// built for the iOS simulator as a plain executable and started with `xcrun simctl spawn`.
//
//   sm_call_smoke --signaling=ws://HOST:PORT/ws [--room=ai-smoke] [--seconds=10] [--turn=URL|none] [--ip=A.B.C.D]
//
// Exit status 0 when the call connected and received audio was played out.
#include <cstdio>
#include <cstring>
#include <string>

#include "call_driver.h"
#include "sm/sim/signals.h"

using namespace sm;

int main(int argc, char** argv) {
  std::string signaling, room = "ai-smoke", turn;
  double seconds = 10;
  apps::CallClientConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto val = [&](const char* k) { return a.rfind(k, 0) == 0 ? a.substr(std::strlen(k)) : std::string(); };
    if (!val("--signaling=").empty()) signaling = val("--signaling=");
    else if (!val("--room=").empty()) room = val("--room=");
    else if (!val("--seconds=").empty()) seconds = std::atof(val("--seconds=").c_str());
    else if (!val("--turn=").empty()) turn = val("--turn=");
    else if (!val("--ip=").empty()) cfg.interfaces.push_back(*p2p::IpAddress::FromString(val("--ip=")));
    else {
      std::fprintf(stderr, "usage: sm_call_smoke --signaling=ws://HOST:PORT/ws [--room=R] [--seconds=S] [--turn=URL|none] [--ip=A.B.C.D]\n");
      return 2;
    }
  }
  if (signaling.empty()) {
    std::fprintf(stderr, "usage: sm_call_smoke --signaling=ws://HOST:PORT/ws ...\n");
    return 2;
  }
  cfg.room = room;
  cfg.verbose = true;
  apps::CallDriver app(signaling, cfg, turn);
  Timestamp now = apps::WallNow();
  if (!app.Start(now)) {
    std::fprintf(stderr, "cannot connect to %s\n", signaling.c_str());
    return 1;
  }
  const std::vector<float> speech = sim::SpeechLike(48000, 1.2, 1, false);
  const Timestamp start = now;
  Timestamp next_audio = Timestamp::PlusInfinity(), speak_at = Timestamp::PlusInfinity(), next_report = now;
  size_t pos = 0;
  while (now - start < TimeDelta::SecondsF(seconds) && app.client().state() != apps::CallState::kEnded) {
    app.Step(now);
    if (app.client().state() == apps::CallState::kConnected && next_audio.IsPlusInfinity()) {
      next_audio = now;
      speak_at = now + TimeDelta::Millis(500);
    }
    while (now >= next_audio) {
      int16_t mic[480] = {}, out[480];
      if (next_audio >= speak_at) {
        for (int i = 0; i < 480 && pos < speech.size(); ++i) mic[i] = static_cast<int16_t>(speech[pos++] * 32767);
      }
      app.client().PushCapturedAudio(mic, 20);
      app.client().PullPlayout(out);
      next_audio += TimeDelta::Millis(10);
    }
    if (now >= next_report) {
      std::printf("%s\n", app.client().stats().ToJson().c_str());
      std::fflush(stdout);
      next_report = now + TimeDelta::Seconds(1);
    }
    app.Wait(now, TimeDelta::Millis(2));
    now = apps::WallNow();
  }
  const apps::CallStats st = app.client().stats();
  app.Hangup(now);
  std::printf("%s\n", st.ToJson().c_str());
  const bool ok = st.state == apps::CallState::kConnected && st.audio_played_s > 1.0;
  std::printf("smoke: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
