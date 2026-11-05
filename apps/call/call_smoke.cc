// The calling app's controller from the command line (#1116): joins a room through the signaling service, holds a
// scripted conversation (speech-like audio) and reports the call's live stats as JSON lines. It is the simulator
// smoke run (built for the iOS simulator as a plain executable and started with `xcrun simctl spawn`) and the native
// client of the real-network calls (#1121, tools/real_calls.py).
//
//   sm_call_smoke --signaling=ws[s]://HOST:PORT/ws [--room=ai-smoke] [--seconds=10] [--turn=URL|none] [--ip=A.B.C.D]
//                 [--relay] [--turns=N] [--barge] [--no-talk] [--no-apm] [--ios-audio|--aec3] [--video] [--label=TEXT]
//
// - `--relay`: TURN candidates only (iceTransportPolicy "relay").
// - The conversation, measured at this end as the test bench's two-way call does (testbench/call.cc, #753, #754):
//   the user speaks a 1.5 s turn, then waits for the reply. `response_ms` is the end of the user's speech (the last
//   sample captured) to the first AI audio played out here; `response_transport_ms` takes off the AI side's own time,
//   the 600 ms of silence its endpointer waits for (TurnDetectorConfig::end_of_turn_silence), as the test bench's
//   conv.response_delay does. With `--barge`, every other turn talks over the reply 1 s after it starts, and
//   `barge_in_stop_ms` is the barge-in's first sample captured to the AI's audio stopping here (the first silent
//   frame of a 200 ms silence); one that stops after 1.5 s counts in `barge_ins_not_stopped` instead (the reply ran
//   out by itself). Up to `--turns` turns (default: as many as fit) within `--seconds`; `--no-talk`: none (a silent
//   microphone, for a call with a person).
// - `--no-apm`: the raw microphone, without the capture chain (CallClientConfig::audio_processing).
// - `--ios-audio`: the SmCall iOS app's chain (#1124, ApplyIosAudioOptions): high-pass filter and NS, the software
//   AEC3 and AGC off, as libwebrtc's iOS client leaves them to VoiceProcessingIO. This program has no audio device, so
//   nothing takes their place. It is the default when built for iOS (the simulator run). `--aec3` is the library's
//   chain (high-pass filter, NS, AEC3, AGC), the default elsewhere.
// - `--video`: sends a synthetic 640x360 camera at 30 fps (H.264 when built with VideoToolbox). Received video gives
//   the render rate and freezes (WebRTC's definition: a frame interval above max(3 x mean, mean + 150 ms) of the
//   preceding frames, as RTCInboundRtpStreamStats.freezeCount).
//
// - LiveKit (C19, #1164): `--livekit=ws[s]://HOST:PORT` joins the LiveKit room `--room` instead (call_client.h), with
//   a token from `--token=http[s]://HOST:PORT/lk-token` (the signaling service; default: --signaling's host).
//   `--livekit=auto` takes the URL /lk-token returns. `--peer=ID` is the identity (default native-<n>). It publishes
//   the microphone (and with --video the camera) and receives every participant (C20, #1172); its stats lines carry
//   each one's stats (livekit_group), the active speaker, and who joined and left. `--publish-only` doesn't subscribe.
//   Its result needs the publisher connected and audio played out (published audio only with --publish-only).
//
//   sm_call_smoke --livekit=ws://127.0.0.1:7880 --token=http://127.0.0.1:8080/lk-token --room=team --video
//
// Prints the stats once a second, then one result line {"type":"call-result", ...} and `smoke: ok|FAILED`.
// Exit status 0 when the call connected and received audio was played out.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "call_driver.h"
#include "sm/sim/signals.h"

using namespace sm;

namespace {

constexpr double kEndpointerMs = 600;          // The AI server's TurnDetectorConfig::end_of_turn_silence.
constexpr double kActiveRms = 0.01;            // -40 dBFS: the AI's audio is playing in this 10 ms frame.
constexpr int kStopFrames = 20;                // 200 ms of silence: the AI's audio has stopped.
constexpr double kReplyTimeoutS = 6;           // No reply by then: the turn is missed.
constexpr double kBargeStopMaxMs = 1500;       // The AI's audio stopping later than this did not stop for the barge-in.

double Median(std::vector<double> v) {
  if (v.empty()) return -1;
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

std::string Numbers(const std::vector<double>& v) {
  std::string s = "[";
  char b[32];
  for (size_t i = 0; i < v.size(); ++i) {
    std::snprintf(b, sizeof b, "%s%.0f", i ? "," : "", v[i]);
    s += b;
  }
  return s + "]";
}

// The scripted user, one 10 ms frame at a time.
class Conversation {
 public:
  Conversation(int max_turns, bool barge) : max_turns_(max_turns), barge_(barge) {}

  // Fills `mic` for the frame starting at `t` and reads the frame played out at `t`.
  void Frame(Timestamp t, int16_t* mic, const int16_t* played) {
    if (next_turn_.IsMinusInfinity()) next_turn_ = t + TimeDelta::Seconds(1);  // The first turn, 1 s in.
    double e = 0;
    for (int i = 0; i < 480; ++i) e += double(played[i]) * played[i];
    const bool active = std::sqrt(e / 480) / 32768.0 > kActiveRms;
    if (active) {
      quiet_frames_ = 0;
      if (!playing_) {
        playing_ = true;
        OnReplyStart(t);
      }
    } else if (playing_ && ++quiet_frames_ == 1) {
      first_quiet_ = t;
    } else if (playing_ && quiet_frames_ >= kStopFrames) {
      playing_ = false;
      OnReplyStop(first_quiet_);
    }
    if (phase_ == Phase::kPause && t >= next_turn_ && max_turns_ >= 0 && (max_turns_ == 0 || turns_ < max_turns_)) {
      StartSpeech();
    }
    if (phase_ == Phase::kWaitReply && t - speech_end_ > TimeDelta::SecondsF(kReplyTimeoutS)) {
      ++missed_;
      Pause(t, 500);
    }
    if (phase_ == Phase::kBargeWait && t >= barge_at_) {
      barge_start_ = t;
      barge_pending_ = true;
      StartSpeech();
    }
    std::memset(mic, 0, 480 * sizeof(int16_t));
    if (phase_ == Phase::kSpeak) {
      for (int i = 0; i < 480 && pos_ < speech_.size(); ++i) mic[i] = static_cast<int16_t>(speech_[pos_++] * 32767);
      if (pos_ >= speech_.size()) {
        speech_end_ = t + TimeDelta::Millis(10);
        phase_ = Phase::kWaitReply;
      }
    }
  }

  std::string Json() const {
    char b[256];
    std::snprintf(b, sizeof b,
                  "\"turns\":%d,\"replies\":%zu,\"missed_replies\":%d,\"response_ms_p50\":%.0f,"
                  "\"response_transport_ms_p50\":%.0f,\"barge_ins\":%d,\"barge_ins_not_stopped\":%d,\"barge_in_stop_ms_p50\":%.0f,",
                  turns_, response_ms_.size(), missed_, Median(response_ms_),
                  response_ms_.empty() ? -1.0 : Median(response_ms_) - kEndpointerMs, barge_attempts_,
                  barge_missed_, Median(barge_stop_ms_));
    return b + std::string("\"response_ms\":") + Numbers(response_ms_) + ",\"barge_in_stop_ms\":" +
           Numbers(barge_stop_ms_) + ",";
  }

 private:
  enum class Phase { kPause, kSpeak, kWaitReply, kListen, kBargeWait };

  void StartSpeech() {
    speech_ = sim::SpeechLike(48000, 1.5, 100 + turns_, false);
    pos_ = 0;
    ++turns_;
    phase_ = Phase::kSpeak;
  }
  void Pause(Timestamp t, int ms) {
    phase_ = Phase::kPause;
    next_turn_ = t + TimeDelta::Millis(ms);
  }
  void OnReplyStart(Timestamp t) {
    if (phase_ != Phase::kWaitReply) return;
    response_ms_.push_back((t - speech_end_).ms_f());
    // Every other turn talks over the reply, 1 s into it.
    if (barge_ && turns_ % 2 == 1) {
      barge_at_ = t + TimeDelta::Seconds(1);
      phase_ = Phase::kBargeWait;
      ++barge_attempts_;
    } else {
      phase_ = Phase::kListen;
    }
  }
  void OnReplyStop(Timestamp t) {
    if (barge_pending_ && t >= barge_start_) {
      // A reply the barge-in did not cut ends by itself about 2 s later (the AI server's 3 s reply): not a stop.
      const double ms = (t - barge_start_).ms_f();
      if (ms < kBargeStopMaxMs) barge_stop_ms_.push_back(ms);
      else ++barge_missed_;
      barge_pending_ = false;
    }
    if (phase_ == Phase::kListen) Pause(t, 700);
  }

  const int max_turns_;
  const bool barge_;
  Phase phase_ = Phase::kPause;
  Timestamp next_turn_ = Timestamp::MinusInfinity();
  std::vector<float> speech_;
  size_t pos_ = 0;
  int turns_ = 0, missed_ = 0, barge_attempts_ = 0, barge_missed_ = 0;
  Timestamp speech_end_ = Timestamp::MinusInfinity();
  Timestamp barge_at_ = Timestamp::PlusInfinity(), barge_start_ = Timestamp::MinusInfinity();
  bool barge_pending_ = false;
  bool playing_ = false;
  int quiet_frames_ = 0;
  Timestamp first_quiet_ = Timestamp::MinusInfinity();
  std::vector<double> response_ms_, barge_stop_ms_;
};

// Render rate and freezes of the received video: each new rendered frame (TakeRemoteVideo, polled every loop).
struct VideoWatch {
  std::deque<double> intervals;  // The last 30 frame intervals, ms.
  Timestamp last = Timestamp::MinusInfinity();
  Timestamp first = Timestamp::MinusInfinity();
  uint64_t frames = 0;
  int freezes = 0;
  double freeze_ms = 0;
  void Update(Timestamp now) {
    ++frames;
    if (last.IsFinite()) {
      const double d = (now - last).ms_f();
      if (intervals.size() >= 5) {
        double mean = 0;
        for (double x : intervals) mean += x;
        mean /= intervals.size();
        if (d > std::max(3 * mean, mean + 150)) {
          ++freezes;
          freeze_ms += d;
        }
      }
      intervals.push_back(d);
      if (intervals.size() > 30) intervals.pop_front();
    } else {
      first = now;
    }
    last = now;
  }
  double Fps() const { return frames > 1 ? (frames - 1) / (last - first).seconds() : 0; }
};

void SyntheticFrame(video::I420Frame& f, int n) {
  for (int y = 0; y < f.height; ++y)
    for (int x = 0; x < f.width; ++x) f.Y(x, y) = static_cast<uint8_t>((x + y + 4 * n) & 0xff);
  std::fill(f.u.begin(), f.u.end(), static_cast<uint8_t>(128 + 40 * std::sin(n * 0.05)));
  std::fill(f.v.begin(), f.v.end(), 128);
}

}  // namespace

int main(int argc, char** argv) {
  std::string signaling, room = "ai-smoke", turn, label;
  double seconds = 10;
  int max_turns = 0;
  bool barge = false, video = false;
  apps::CallClientConfig cfg;
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  apps::ApplyIosAudioOptions(&cfg);  // As the SmCall app on iOS (#1124).
#endif
  const char* usage =
      "usage: sm_call_smoke --signaling=ws[s]://HOST:PORT/ws [--room=R] [--seconds=S] [--turn=URL|none] "
      "[--ip=A.B.C.D] [--relay] [--turns=N] [--barge] [--no-talk] [--no-apm] [--ios-audio|--aec3] [--video] "
      "[--label=TEXT]\n"
      "       sm_call_smoke --livekit=ws[s]://HOST:PORT|auto [--token=http[s]://HOST:PORT/lk-token] [--peer=ID] "
      "[--publish-only] [same options]\n";
  std::string livekit, token_url, peer;
  bool subscribe = true;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto val = [&](const char* k) { return a.rfind(k, 0) == 0 ? a.substr(std::strlen(k)) : std::string(); };
    if (!val("--signaling=").empty()) signaling = val("--signaling=");
    else if (!val("--livekit=").empty()) livekit = val("--livekit=");
    else if (!val("--token=").empty()) token_url = val("--token=");
    else if (!val("--peer=").empty()) peer = val("--peer=");
    else if (a == "--subscribe") subscribe = true;  // The default since C20.
    else if (a == "--publish-only") subscribe = false;
    else if (!val("--room=").empty()) room = val("--room=");
    else if (!val("--seconds=").empty()) seconds = std::atof(val("--seconds=").c_str());
    else if (!val("--turn=").empty()) turn = val("--turn=");
    else if (!val("--ip=").empty()) cfg.interfaces.push_back(*p2p::IpAddress::FromString(val("--ip=")));
    else if (!val("--turns=").empty()) max_turns = std::atoi(val("--turns=").c_str());
    else if (!val("--label=").empty()) label = val("--label=");
    else if (a == "--relay") cfg.relay_only = true;
    else if (a == "--barge") barge = true;
    else if (a == "--video") video = true;
    else if (a == "--no-talk") max_turns = -1;
    else if (a == "--no-apm") cfg.audio_processing = false;
    else if (a == "--ios-audio") apps::ApplyIosAudioOptions(&cfg);
    else if (a == "--aec3") cfg.echo_cancellation = cfg.agc = true;
    else {
      std::fprintf(stderr, "%s", usage);
      return 2;
    }
  }
  if (signaling.empty() && (livekit.empty() || token_url.empty())) {
    std::fprintf(stderr, "%s", usage);
    return 2;
  }
  cfg.room = room;
  cfg.peer_id = peer;
  cfg.verbose = true;
  std::unique_ptr<apps::CallDriver> driver;
  if (!livekit.empty()) {
    if (token_url.empty()) {  // The signaling service's /lk-token, next to its /ws.
      const net::Url s = net::ParseUrl(signaling).value_or(net::Url{});
      token_url = std::string(s.tls ? "https://" : "http://") + s.host + ":" + std::to_string(s.port) + "/lk-token";
    }
    cfg.livekit_subscribe = subscribe;
    cfg.livekit_publish_video = video;
    driver = std::make_unique<apps::CallDriver>(apps::LiveKitJoin{token_url, livekit == "auto" ? "" : livekit}, cfg, turn);
  } else {
    driver = std::make_unique<apps::CallDriver>(signaling, cfg, turn);
  }
  apps::CallDriver& app = *driver;
  app.client().SetVideoEnabled(video);
  Timestamp now = apps::WallNow();
  if (!app.Start(now)) {
    std::fprintf(stderr, "cannot connect to %s%s%s\n", livekit.empty() ? signaling.c_str() : livekit.c_str(),
                 app.error().empty() ? "" : ": ", app.error().c_str());
    return 1;
  }
  Conversation talk(max_turns, barge);
  VideoWatch watch;
  const Timestamp start = now;
  Timestamp next_audio = Timestamp::PlusInfinity(), next_frame = Timestamp::PlusInfinity(), next_report = now;
  video::I420Frame camera(640, 360), remote;
  int frame_no = 0;
  while (now - start < TimeDelta::SecondsF(seconds) && app.client().state() != apps::CallState::kEnded) {
    app.Step(now);
    if (app.client().state() == apps::CallState::kConnected && next_audio.IsPlusInfinity()) {
      next_audio = now;
      next_frame = now;
    }
    while (now >= next_audio) {
      int16_t mic[480], out[480] = {};
      // The frame played at `next_audio` is the one pulled now; the microphone frame follows it (AEC3's order).
      app.client().PullPlayout(out);
      talk.Frame(next_audio, mic, out);
      app.client().PushCapturedAudio(mic, 20);
      next_audio += TimeDelta::Millis(10);
    }
    if (video && now >= next_frame) {
      SyntheticFrame(camera, frame_no++);
      app.client().PushCameraFrame(camera);
      next_frame += TimeDelta::Micros(1'000'000 / 30);
    }
    if (now >= next_report) {
      std::printf("%s\n", app.client().stats().ToJson().c_str());
      std::fflush(stdout);
      next_report = now + TimeDelta::Seconds(1);
    }
    if (app.client().TakeRemoteVideo(&remote)) watch.Update(now);
    app.Wait(now, TimeDelta::Millis(2));
    now = apps::WallNow();
  }
  const apps::CallStats st = app.client().stats();
  app.Hangup(now);
  std::printf("%s\n", st.ToJson().c_str());
  char v[192];
  std::snprintf(v, sizeof v, "\"video_render_fps\":%.1f,\"video_freezes\":%d,\"video_freeze_ms\":%.0f,", watch.Fps(),
                watch.freezes, watch.freeze_ms);
  const std::string stats = st.ToJson();
  std::printf("{\"type\":\"call-result\",\"client\":\"sm_call_smoke\",\"label\":%s,\"room\":%s,%s%s\"stats\":%s}\n",
              net::JsonEscape(label).c_str(), net::JsonEscape(room).c_str(), talk.Json().c_str(), v, stats.c_str());
  // A LiveKit call publishes: connected (the publisher connection) with audio sent; received audio only when it
  // subscribed (every participant's, mixed, C20).
  const bool ok = st.state == apps::CallState::kConnected &&
                  (livekit.empty() || subscribe ? st.audio_played_s > 1.0 : st.audio_packets_sent > 50);
  std::printf("smoke: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
