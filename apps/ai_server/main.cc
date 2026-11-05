// sm_ai_server (C12 part 1, #1103): answers the signaling service's `ai-` rooms; see ai_server.h.
//
//   sm_ai_server [--signaling=ws://HOST:PORT/ws] [--turn=http://HOST:PORT/turn|none] [--ip=A.B.C.D]... [--relay]
//                [--metrics=FILE] [--reply-seconds=S] [--first-chunk-ms=MS] [--max-seconds=S] [--quiet]
//   sm_ai_server --livekit=ws[s]://HOST:PORT|auto --token=http[s]://HOST:PORT/lk-token --room=R [--identity=ID]
//                [same options]
//
// Runs until SIGINT or SIGTERM (or --max-seconds), then logs the open calls' metrics. With --livekit (C20, #1172) it
// joins the LiveKit room R as a participant instead (ai_livekit.h), answers the active speaker's turns, prints its
// status as a JSON line once a second, and leaves when stopped or when the room ends.
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ai_livekit.h"
#include "ai_server.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }

sm::Timestamp Now() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return sm::Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

}  // namespace

int main(int argc, char** argv) {
  sm::apps::AiServerConfig config;
  config.verbose = true;
  double reply_seconds = 3, max_seconds = 0;
  sm::apps::AiLiveKitConfig livekit;
  bool use_livekit = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&](const char* prefix) -> const char* {
      const size_t n = std::char_traits<char>::length(prefix);
      return a.compare(0, n, prefix) == 0 ? argv[i] + n : nullptr;
    };
    const char* v = nullptr;
    if ((v = value("--signaling="))) {
      config.signaling_url = v;
    } else if ((v = value("--turn="))) {
      config.turn_url = v;
    } else if ((v = value("--metrics="))) {
      config.metrics_path = v;
    } else if ((v = value("--reply-seconds="))) {
      reply_seconds = std::atof(v);
    } else if ((v = value("--first-chunk-ms="))) {
      config.ai_source.first_chunk = sm::TimeDelta::Millis(std::atoi(v));
    } else if ((v = value("--max-seconds="))) {
      max_seconds = std::atof(v);
    } else if ((v = value("--ip="))) {
      const auto ip = sm::p2p::IpAddress::FromString(v);
      if (!ip) {
        std::fprintf(stderr, "bad --ip=%s\n", v);
        return 2;
      }
      config.interfaces.push_back(*ip);
    } else if ((v = value("--livekit="))) {
      use_livekit = true;
      livekit.livekit_url = std::string(v) == "auto" ? "" : v;
    } else if ((v = value("--token="))) {
      livekit.token_url = v;
    } else if ((v = value("--room="))) {
      livekit.room = v;
    } else if ((v = value("--identity="))) {
      livekit.identity = v;
    } else if (a == "--relay") {
      config.relay_only = true;
    } else if (a == "--quiet") {
      config.verbose = false;
    } else {
      std::fprintf(stderr,
                   "usage: sm_ai_server [--signaling=ws[s]://HOST:PORT/ws] [--turn=URL|none] [--ip=A.B.C.D]... [--relay] "
                   "[--metrics=FILE] [--reply-seconds=S] [--first-chunk-ms=MS] [--max-seconds=S] [--quiet]\n"
                   "       sm_ai_server --livekit=ws[s]://HOST:PORT|auto --token=http[s]://HOST:PORT/lk-token --room=R "
                   "[--identity=ID] [same options]\n");
      return a == "--help" ? 0 : 2;
    }
  }
  const sm::TimeDelta reply = sm::TimeDelta::Micros(static_cast<int64_t>(reply_seconds * 1e6));
  config.reply_factory = [reply] { return std::make_unique<sm::apps::ScriptedReplySource>(std::vector<sm::TimeDelta>{reply}); };

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  std::signal(SIGPIPE, SIG_IGN);
  if (use_livekit) {
    if (livekit.token_url.empty() || livekit.room.empty()) {
      std::fprintf(stderr, "--livekit needs --token=URL and --room=R\n");
      return 2;
    }
    sm::apps::AiLiveKitParticipant ai(config, livekit);
    const sm::Timestamp start = Now();
    if (!ai.Start(start)) {
      std::fprintf(stderr, "cannot join LiveKit room %s: %s\n", livekit.room.c_str(), ai.error().c_str());
      return 1;
    }
    sm::Timestamp next_status = start;
    while (!g_stop && !ai.ended() && (max_seconds <= 0 || (Now() - start).seconds() < max_seconds)) {
      const sm::Timestamp now = Now();
      const sm::Timestamp next = ai.Step(now);
      if (now >= next_status) {
        std::printf("%s\n", ai.StatusJson().c_str());
        std::fflush(stdout);
        next_status = now + sm::TimeDelta::Seconds(1);
      }
      ai.Wait(now, next - now);
    }
    ai.Shutdown(Now());
    std::printf("%s\n", ai.StatusJson().c_str());
    return 0;
  }
  sm::apps::AiServer server(config);
  const sm::Timestamp start = Now();
  if (!server.Start(start)) {
    std::fprintf(stderr, "bad --signaling=%s\n", config.signaling_url.c_str());
    return 2;
  }
  while (!g_stop && (max_seconds <= 0 || (Now() - start).seconds() < max_seconds)) {
    const sm::Timestamp now = Now();
    const sm::Timestamp next = server.Step(now);
    // Wait stamps arrivals from the clock it is given (#1168): read it after Step's work.
    const sm::Timestamp before_wait = Now();
    server.Wait(before_wait, next - before_wait);
  }
  server.Shutdown(Now());
  return 0;
}
