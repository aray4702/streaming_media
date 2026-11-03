// The calling app's call controller (C14, #1116), through the real signaling service (apps/signaling/server.py) on
// loopback in real time (skipped without python3):
// - CallClient.CallsTheAiServer: the app calls an `ai-` room; the AI server (apps/ai_server, in this process) joins,
//   offers, and replies after the user's turn; the app plays the reply. The app's live stats are filled.
// - CallClient.TwoAppsByRoomCode: two apps join one room code, as the app and the web app do (the later one offers);
//   audio both ways, and on Apple platforms video both ways through VideoToolbox. Mute silences the sender; hang-up
//   ends the other side's call with "peer left".
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <string>
#include <vector>

#include "ai_server.h"
#include "call_client.h"
#include "call_driver.h"
#include "sm/sim/signals.h"
#include "sm/video/frame.h"
#include "sm_test.h"

using namespace sm;

namespace {

// ASan builds run several times slower: two apps with the capture chain (AEC3, NS) and VideoToolbox on one thread
// fall behind real time there, so TwoAppsByRoomCode runs them without the chain (CallsTheAiServer keeps it).
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SM_SLOW_BUILD 1
#endif
#endif
#if !defined(SM_SLOW_BUILD) && defined(__SANITIZE_ADDRESS__)
#define SM_SLOW_BUILD 1
#endif
#ifndef SM_SLOW_BUILD
#define SM_SLOW_BUILD 0
#endif

struct PythonService {
  pid_t pid = -1;
  uint16_t port = 0;
  bool Start() {
    const char* python = SM_PYTHON;
    const char* script = SM_SIGNALING_SERVER_PY;
    if (python[0] == '\0' || access(script, R_OK) != 0) return false;
    std::signal(SIGPIPE, SIG_IGN);
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
    const Timestamp deadline = apps::WallNow() + TimeDelta::Seconds(10);
    while (line.find('\n') == std::string::npos && apps::WallNow() < deadline) {
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
  std::string ws() const { return "ws://127.0.0.1:" + std::to_string(port) + "/ws"; }
  ~PythonService() {
    if (pid > 0) {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
    }
  }
};

apps::CallClientConfig AppConfig(const std::string& room, const std::string& peer) {
  apps::CallClientConfig c;
  c.room = room;
  c.peer_id = peer;
  c.interfaces = {*p2p::IpAddress::FromString("127.0.0.1")};
  c.verbose = true;
  c.video_fps = 15;
  return c;
}

// The device side of one app in the test: the microphone plays `speech` from `speak_at` (then silence), the speaker
// is drained every 10 ms, and the camera gives a test pattern at 15 fps.
struct FakeDevices {
  std::vector<float> speech;
  Timestamp speak_at = Timestamp::PlusInfinity();
  Timestamp next_audio = Timestamp::MinusInfinity(), next_video = Timestamp::MinusInfinity();
  size_t pos = 0;
  int frame = 0;
  double heard_energy = 0;
  size_t heard_samples = 0;
  void Step(apps::CallClient& c, Timestamp now, bool camera) {
    if (next_audio.IsMinusInfinity()) next_audio = next_video = now;
    while (now >= next_audio) {
      int16_t mic[480] = {};
      if (next_audio >= speak_at) {
        for (int i = 0; i < 480 && pos < speech.size(); ++i) mic[i] = static_cast<int16_t>(speech[pos++] * 32767);
      }
      c.PushCapturedAudio(mic, 20);
      int16_t out[480];
      c.PullPlayout(out);
      for (int16_t v : out) heard_energy += static_cast<double>(v) * v;
      heard_samples += 480;
      next_audio += TimeDelta::Millis(10);
    }
    if (camera && now >= next_video) {
      c.PushCameraFrame(video::MakeTestFrame(320, 240, frame++));
      next_video += TimeDelta::Micros(66667);
    }
  }
};

}  // namespace

SM_TEST(CallClient, CallsTheAiServer) {
  PythonService service;
  if (!service.Start()) {
    std::printf("SKIPPED: apps/signaling/server.py or python3 not available\n");
    return;
  }
  apps::AiServerConfig cfg;
  cfg.signaling_url = service.ws();
  cfg.interfaces = {*p2p::IpAddress::FromString("127.0.0.1")};
  cfg.verbose = true;
  cfg.ai_source.first_chunk = TimeDelta::Millis(200);
  apps::AiServer server(cfg);
  Timestamp now = apps::WallNow();
  SM_ASSERT_TRUE(server.Start(now));
  for (Timestamp until = now + TimeDelta::Seconds(5); now < until && !server.registered();) {
    server.Wait(now, server.Step(now) - now);
    now = apps::WallNow();
  }
  SM_ASSERT_TRUE(server.registered());

  apps::CallDriver app(service.ws(), AppConfig("ai-app-test", "ios-test"));
  FakeDevices dev;
  dev.speech = sim::SpeechLike(48000, 1.2, 1, false);
  SM_ASSERT_TRUE(app.Start(now));
  const Timestamp start = now;
  bool spoke = false;
  while (now - start < TimeDelta::Seconds(12)) {
    server.Step(now);
    app.Step(now);
    if (app.client().state() == apps::CallState::kConnected) {
      if (!spoke) {
        spoke = true;
        dev.speak_at = now + TimeDelta::Millis(500);
      }
      dev.Step(app.client(), now, false);
      if (app.client().stats().audio_played_s > 2.0 && now > dev.speak_at + TimeDelta::Seconds(5)) break;
    }
    app.Wait(now, TimeDelta::Millis(1));
    server.Wait(now, TimeDelta::Millis(1));
    now = apps::WallNow();
  }
  const apps::CallStats st = app.client().stats();
  std::printf("app stats: %s\n", st.ToJson().c_str());
  std::printf("%s\n", st.Text().c_str());
  app.Hangup(now);
  for (Timestamp until = now + TimeDelta::Seconds(3); now < until && server.finished_calls().empty();) {
    server.Wait(now, server.Step(now) - now);
    now = apps::WallNow();
  }
  server.Shutdown(now);

  SM_EXPECT_EQ(std::string(apps::CallStateName(st.state)), std::string("connected"));
  SM_EXPECT_TRUE(st.connect_ms > 0 && st.connect_ms < 5000);
  SM_EXPECT_EQ(app.client().remote_peer().rfind("ai-server-", 0), size_t{0});
  SM_EXPECT_TRUE(st.audio_packets_sent > 100);
  SM_EXPECT_TRUE(st.audio_packets_received > 100);
  SM_EXPECT_TRUE(st.audio_played_s > 2.0);  // The AI's reply (3 s by default) and its silence, played out.
  SM_EXPECT_TRUE(dev.heard_energy > 0);
  SM_EXPECT_TRUE(st.rtt_ms >= 0 && st.rtt_ms < 200);
  SM_EXPECT_TRUE(st.local_candidate.rfind("host udp", 0) == 0);
  SM_EXPECT_TRUE(!st.srtp_cipher.empty());
  SM_EXPECT_EQ(std::string(apps::CallStateName(app.client().state())), std::string("ended"));
  SM_ASSERT_EQ(server.finished_calls().size(), size_t{1});
  const apps::AiCallMetrics& mt = server.finished_calls()[0];
  std::printf("%s\n", mt.ToJson().c_str());
  SM_EXPECT_TRUE(mt.connected);
  SM_EXPECT_EQ(mt.peer, std::string("ios-test"));
  SM_EXPECT_TRUE(mt.user_turns >= 1);  // The AI heard the app's speech through the capture chain.
  SM_EXPECT_TRUE(mt.replies >= 1);
}

SM_TEST(CallClient, TwoAppsByRoomCode) {
  PythonService service;
  if (!service.Start()) {
    std::printf("SKIPPED: apps/signaling/server.py or python3 not available\n");
    return;
  }
  Timestamp now = apps::WallNow();
  apps::CallClientConfig ca = AppConfig("room-4821", "app-a"), cb = AppConfig("room-4821", "app-b");
  ca.audio_processing = cb.audio_processing = !SM_SLOW_BUILD;
  apps::CallDriver a(service.ws(), ca);
  apps::CallDriver b(service.ws(), cb);
  FakeDevices da, db;
  da.speech = sim::SpeechLike(48000, 20, 2, false);
  db.speech = sim::SpeechLike(48000, 20, 3, false);
  da.speak_at = db.speak_at = now;
  SM_ASSERT_TRUE(a.Start(now));
  // B joins once A is in the room, so B offers.
  for (Timestamp until = now + TimeDelta::Millis(300); now < until;) {
    a.Step(now);
    a.Wait(now, TimeDelta::Millis(5));
    now = apps::WallNow();
  }
  SM_ASSERT_TRUE(b.Start(now));
  const Timestamp start = now;
  Timestamp muted_at = Timestamp::PlusInfinity();
  while (now - start < TimeDelta::Seconds(8)) {
    a.Step(now);
    b.Step(now);
    if (a.client().state() == apps::CallState::kConnected) da.Step(a.client(), now, true);
    if (b.client().state() == apps::CallState::kConnected) db.Step(b.client(), now, true);
    if (muted_at.IsPlusInfinity() && now - start > TimeDelta::Seconds(5)) {
      a.client().SetMuted(true);
      muted_at = now;
    }
    a.Wait(now, TimeDelta::Millis(1));
    b.Wait(now, TimeDelta::Millis(1));
    now = apps::WallNow();
  }
  const apps::CallStats sa = a.client().stats(), sb = b.client().stats();
  std::printf("A: %s\nB: %s\n", sa.ToJson().c_str(), sb.ToJson().c_str());
  std::printf("%s\n", sa.Text().c_str());
  a.Hangup(now);
  for (Timestamp until = now + TimeDelta::Seconds(3); now < until && b.client().state() != apps::CallState::kEnded;) {
    b.Step(now);
    b.Wait(now, TimeDelta::Millis(5));
    now = apps::WallNow();
  }

  SM_EXPECT_EQ(a.client().remote_peer(), std::string("app-b"));
  SM_EXPECT_EQ(b.client().remote_peer(), std::string("app-a"));
  SM_EXPECT_TRUE(sa.state == apps::CallState::kConnected && sb.state == apps::CallState::kConnected);
  SM_EXPECT_TRUE(sa.audio_played_s > 3.0);
  SM_EXPECT_TRUE(sb.audio_played_s > 3.0);
  SM_EXPECT_TRUE(sa.capture_level < 0.001);  // Muted: the session's fade left silence.
  SM_EXPECT_TRUE(sb.capture_level > 0.001);
  SM_EXPECT_TRUE(sa.target_kbps > 0);
#if defined(SM_WITH_VIDEOTOOLBOX) && SM_WITH_VIDEOTOOLBOX
  SM_EXPECT_TRUE(sa.video_frames_sent > 30);
  SM_EXPECT_TRUE(sb.video_frames_sent > 30);
  SM_EXPECT_TRUE(sa.video_frames_decoded > 30);
  SM_EXPECT_TRUE(sb.video_frames_decoded > 30);
  SM_EXPECT_EQ(sa.video_recv_width, 320);
  video::I420Frame remote;
  SM_EXPECT_TRUE(a.client().TakeRemoteVideo(&remote));
  SM_EXPECT_EQ(remote.height, 240);
#endif
  SM_EXPECT_EQ(std::string(apps::CallStateName(b.client().state())), std::string("ended"));
  SM_EXPECT_EQ(b.client().stats().ended_reason, std::string("peer left"));
}
