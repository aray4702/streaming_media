// The AI server in a LiveKit room (C20, #1172); see ai_livekit.h.
#include "ai_livekit.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "json.h"

namespace sm::apps {

namespace {
constexpr size_t k10ms = 480;    // 48 kHz.
constexpr size_t kMaxHeld = 10;  // 100 ms of a participant's audio waiting at most.
}  // namespace

AiLiveKitParticipant::AiLiveKitParticipant(const AiServerConfig& server, AiLiveKitConfig config)
    : server_(server), config_(std::move(config)) {
  metrics_.room = config_.room;
  metrics_.peer = "livekit";
  CallClientConfig c;
  c.livekit = true;
  c.room = config_.room;
  c.peer_id = config_.identity;
  c.interfaces = server_.interfaces;
  c.relay_only = server_.relay_only;
  c.audio_processing = false;  // The reply is synthetic: nothing to cancel or level.
  c.opus_min_bitrate_bps = c.opus_max_bitrate_bps = server_.opus_bitrate_bps;
  c.livekit_publish_video = false;
  c.livekit_receive_width = 0;  // The AI shows no video: the server's default layer.
  c.verbose = server_.verbose;
  driver_ = std::make_unique<CallDriver>(LiveKitJoin{config_.token_url, config_.livekit_url}, std::move(c),
                                         server_.turn_url);
  driver_->client().SetVideoEnabled(false);
  driver_->client().SetRemoteAudioSink(
      [this](const std::string& identity, std::span<const float> pcm, bool concealed, Timestamp) {
        OnRemoteAudio(identity, pcm, concealed);
      });
  conversation_ = std::make_unique<AiConversation>(server_, &metrics_, [this](const std::string& line) { Log(line); });
}

AiLiveKitParticipant::~AiLiveKitParticipant() = default;

void AiLiveKitParticipant::Log(const std::string& line) const {
  if (server_.verbose) std::fprintf(stderr, "ai_server: room %s: %s\n", config_.room.c_str(), line.c_str());
}

bool AiLiveKitParticipant::Start(Timestamp now) {
  start_ = now;
  return driver_->Start(now);
}

void AiLiveKitParticipant::OnRemoteAudio(const std::string& identity, std::span<const float> pcm, bool concealed) {
  std::deque<Heard>& q = heard_[identity];
  q.push_back(Heard{std::vector<float>(pcm.begin(), pcm.end()), concealed});
  while (q.size() > kMaxHeld) q.pop_front();
}

// The participant to listen to: the active speaker when it is someone else; otherwise the last one (while the AI
// speaks, or in a pause), unless that one has left.
void AiLiveKitParticipant::Listen() {
  const std::string active = driver_->client().active_speaker();
  const std::string& self = config_.identity;
  std::string next = listening_;
  if (!active.empty() && active != self) next = active;
  if (!next.empty()) {
    const auto group = driver_->client().participants();
    const bool present = std::any_of(group.begin(), group.end(), [&](const GroupParticipant& p) { return p.identity == next; });
    if (!present) next.clear();
  }
  if (next != listening_) {
    Log("listening to " + (next.empty() ? std::string("nobody") : next));
    listening_ = next;
    if (!next.empty() && std::find(listened_.begin(), listened_.end(), next) == listened_.end()) listened_.push_back(next);
  }
}

Timestamp AiLiveKitParticipant::Step(Timestamp now) {
  Timestamp next = driver_->Step(now);
  CallClient& client = driver_->client();
  if (client.state() == CallState::kEnded) return now + TimeDelta::Millis(10);
  if (client.state() != CallState::kConnected) return std::min(next, now + TimeDelta::Millis(10));
  if (!metrics_.connected) {
    metrics_.connected = true;
    metrics_.connect_ms = (now - start_).ms_f();
    next_pull_ = next_send_ = now;
    Log("connected in " + std::to_string(static_cast<int>(metrics_.connect_ms)) + " ms");
  }
  // Each 10 ms: the client plays out (its mix is not heard by anyone; pulling it keeps the receive clock), and the
  // detector hears the 10 ms of the participant listened to (silence when none).
  while (now >= next_pull_) {
    int16_t mix[k10ms];
    client.PullPlayout(mix);
    Listen();
    std::vector<float> pcm(k10ms, 0.f);
    timing::AudioOperation op = timing::AudioOperation::kSilence;
    if (auto it = heard_.find(listening_); it != heard_.end() && !it->second.empty()) {
      Heard h = std::move(it->second.front());
      it->second.pop_front();
      std::copy_n(h.pcm.begin(), std::min(h.pcm.size(), k10ms), pcm.begin());
      op = h.concealed ? timing::AudioOperation::kExpand : timing::AudioOperation::kNormal;
    }
    for (auto& [identity, q] : heard_) {  // The others' audio is not heard: keep it from piling up.
      if (identity != listening_) q.clear();
    }
    conversation_->OnPlayout(pcm, op, next_pull_);
    next_pull_ += TimeDelta::Millis(10);
  }
  conversation_->CollectReply(now, next_send_);
  // Each 20 ms slot: the reply's frame due by it, or silence, as the AI's microphone.
  while (now >= next_send_) {
    metrics_.send_late_max_ms = std::max(metrics_.send_late_max_ms, (now - next_send_).ms_f());
    const std::span<const float> reply = conversation_->Slot(next_send_, now);
    int16_t frame[2 * k10ms] = {};
    for (size_t i = 0; i < reply.size() && i < 2 * k10ms; ++i) {
      frame[i] = static_cast<int16_t>(std::clamp(reply[i], -1.f, 1.f) * 32767);
    }
    client.PushCapturedAudio(std::span<const int16_t>(frame, k10ms));
    client.PushCapturedAudio(std::span<const int16_t>(frame + k10ms, k10ms));
    ++metrics_.audio_frames_sent;
    next_send_ += TimeDelta::Millis(20);
  }
  return std::min({next, next_pull_, next_send_, now + TimeDelta::Millis(10)});
}

void AiLiveKitParticipant::Wait(Timestamp now, TimeDelta wait) { driver_->Wait(now, wait); }

std::string AiLiveKitParticipant::StatusJson() const {
  std::string metrics = metrics_.ToJson();  // {"type":"ai-call-metrics",...}
  std::string listened = "[";
  for (size_t i = 0; i < listened_.size(); ++i) listened += (i ? "," : "") + net::JsonEscape(listened_[i]);
  listened += "]";
  metrics.pop_back();
  return metrics + ",\"listening_to\":" + net::JsonEscape(listening_) + ",\"listened\":" + listened +
         ",\"stats\":" + driver_->client().stats().ToJson() + "}";
}

void AiLiveKitParticipant::Shutdown(Timestamp now) {
  if (finished_) return;
  finished_ = true;
  metrics_.duration_s = (now - start_).seconds();
  metrics_.audio_packets_received = static_cast<int64_t>(driver_->client().stats().audio_packets_received);
  const std::string line = StatusJson();
  driver_->Hangup(now);
  Log(line);
  if (!server_.metrics_path.empty()) {
    std::ofstream f(server_.metrics_path, std::ios::app);
    f << line << "\n";
  }
}

}  // namespace sm::apps
