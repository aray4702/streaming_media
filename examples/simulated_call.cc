// Example: a one-way audio + video call over a simulated unstable link,
// through the library's RTP session layer (rtp::Session), with metrics
// exported in Prometheus format and decisions recorded in the event log.
//
//   sender:   G.711 mu-law audio (8 kHz) -> RED; video frames -> RTP packets
//             paced at the GCC target rate, with RTX; RTCP sender reports
//   network:  2 Mbps bottleneck, 30 ms + jitter, 1% loss, drops to 700 kbps
//   receiver: transport-cc feedback, NACK, audio jitter buffer (NetEq-lite),
//             RTCP receiver reports, keyframe requests, freeze detection
//
// The application only encodes, decodes and moves datagram bytes: every packet
// crosses the simulated link as bytes, in both directions.
//
// Build: cmake -DSM_BUILD_EXAMPLES=ON ..; run: ./examples/simulated_call
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/rtp/session.h"
#include "sm/sim/network_link.h"
#include "sm/sim/signals.h"
#include "sm/video/freeze_detector.h"

using namespace sm;

namespace {
// G.711 mu-law (ITU-T G.711), the simplest real audio codec: 64 kbps.
uint8_t MuLawEncode(float x) {
  int s = static_cast<int>(std::clamp(x, -1.0f, 1.0f) * 32635.0f);
  int sign = s < 0 ? 0x80 : 0;
  if (s < 0) s = -s;
  s += 0x84;
  int exp = 7;
  for (int mask = 0x4000; exp > 0 && !(s & mask); mask >>= 1) --exp;
  int mant = (s >> (exp + 3)) & 0x0f;
  return static_cast<uint8_t>(~(sign | (exp << 4) | mant));
}
float MuLawDecode(uint8_t u) {
  u = static_cast<uint8_t>(~u);
  int t = ((u & 0x0f) << 3) + 0x84;
  t <<= (u & 0x70) >> 4;
  return static_cast<float>((u & 0x80) ? 0x84 - t : t - 0x84) / 32768.0f;
}

// Until the H.264 packetizer exists (plan PR 7), the video payload carries a
// 9-byte header: start flag, key flag and a 64-bit frame id.
constexpr size_t kVideoHeader = 9;
class VideoHeaderParser : public rtp::VideoPayloadParser {
 public:
  rtp::VideoPayloadInfo Parse(const RtpPacket& p) const override {
    rtp::VideoPayloadInfo i;
    if (p.payload.size() < kVideoHeader) return i;
    i.valid = true;
    i.frame_start = p.payload[0] & 1;
    i.keyframe = p.payload[0] & 2;
    uint64_t id = 0;
    for (size_t b = 0; b < 8; ++b) id |= static_cast<uint64_t>(p.payload[1 + b]) << (8 * b);
    i.frame_id = static_cast<int64_t>(id);
    return i;
  }
};

std::vector<std::vector<uint8_t>> MakeFrame(int64_t id, bool key, size_t bytes, size_t max_payload) {
  std::vector<std::vector<uint8_t>> chunks;
  bool first = true;
  for (size_t n : rtp::SplitEvenly(bytes, max_payload)) {
    std::vector<uint8_t> c(std::max(n, kVideoHeader), 0);
    c[0] = static_cast<uint8_t>((first ? 1 : 0) | (key ? 2 : 0));
    for (size_t b = 0; b < 8; ++b) c[1 + b] = static_cast<uint8_t>(static_cast<uint64_t>(id) >> (8 * b));
    chunks.push_back(std::move(c));
    first = false;
  }
  return chunks;
}

class MuLawDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t) override {
    std::vector<float> pcm(frame.size());
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = MuLawDecode(frame[i]);
    return pcm;
  }
};
}  // namespace

int main() {
  EventLog events(256);
  std::map<std::string, int> event_counts;
  events.SetCallback([&](const Event& e) { ++event_counts[std::string(e.module) + "/" + e.name]; });
  MetricsRegistry registry;

  // --- Sessions -------------------------------------------------------------
  auto make_config = [&](uint32_t ssrc, const char* cname) {
    rtp::SessionConfig c;
    c.local_ssrc = ssrc;
    c.cname = cname;
    c.extensions.Register(1, sync::uri::kTransportCc);
    c.extensions.Register(2, sync::uri::kAbsSendTime);
    c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(300);
    c.send_side.gcc.events = &events;
    c.events = &events;
    return c;
  };
  VideoHeaderParser parser;
  MuLawDecoder decoder;
  rtp::AudioStreamConfig audio_cfg;
  audio_cfg.ssrc = 1;
  audio_cfg.payload_type = 0;  // PCMU
  audio_cfg.red_payload_type = 63;
  audio_cfg.clock_rate = 8000;
  audio_cfg.nominal_rate = DataRate::KilobitsPerSec(150);  // G.711 with RED plus headers.
  rtp::VideoStreamConfig video_cfg;
  video_cfg.ssrc = 2;
  video_cfg.rtx_ssrc = 3;
  video_cfg.parser = &parser;
  video_cfg.max_payload_size = 1100 + 12;

  rtp::Session sender(make_config(100, "sender@example"));
  auto audio_send = sender.AddAudioSend(audio_cfg);
  auto video_send = sender.AddVideoSend(video_cfg);
  rtp::Session receiver(make_config(200, "receiver@example"));
  auto audio_recv = receiver.AddAudioReceive(audio_cfg, &decoder);
  auto video_recv = receiver.AddVideoReceive(video_cfg);
  registry.Register("sender", &sender);
  registry.Register("sender.send_side", &sender.send_side());
  registry.Register("sender.rtcp", &sender.rtcp());
  registry.Register("receiver", &receiver);
  registry.Register("receiver.rtcp", &receiver.rtcp());
  registry.Register("receiver.nack", &receiver.video_receiver(video_recv).nack());
  registry.Register("receiver.neteq", &receiver.audio_receiver(audio_recv).jitter_buffer());
  video::FreezeDetector freezes(&events);
  registry.Register("receiver.freezes", &freezes);

  // --- Network --------------------------------------------------------------
  sim::LinkConfig link_cfg;
  link_cfg.base_delay = TimeDelta::Millis(30);
  link_cfg.jitter_stddev = TimeDelta::Millis(8);
  link_cfg.loss_rate = 0.01;
  link_cfg.capacity = DataRate::KilobitsPerSec(2000);
  link_cfg.queue_limit = DataSize::Bytes(150000);
  sim::NetworkLink<std::vector<uint8_t>> forward(link_cfg, 1);
  sim::LinkConfig back_cfg;
  back_cfg.base_delay = TimeDelta::Millis(30);
  sim::NetworkLink<std::vector<uint8_t>> backward(back_cfg, 2);

  auto speech = sim::SpeechLike(8000, 31.0, 7, true);
  std::vector<float> out(80);  // 10 ms at 8 kHz.
  int64_t frame_id = 0;
  bool keyframe_needed = true;

  for (int64_t t = 0; t < 30000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t == 15000) {  // The link degrades half way through.
      link_cfg.capacity = DataRate::KilobitsPerSec(700);
      forward.SetConfig(link_cfg);
    }
    // Audio: 20 ms G.711 frames (160 bytes).
    if (t % 20 == 0) {
      size_t off = static_cast<size_t>(t) * 8;
      std::vector<uint8_t> frame(160);
      for (size_t i = 0; i < 160; ++i) frame[i] = MuLawEncode(speech[off + i]);
      sender.SendAudio(audio_send, {static_cast<uint32_t>(t * 8), frame, false}, now);
    }
    // Video: 30 fps, frame size from the session's video rate.
    if (t % 33 == 0) {
      int64_t bytes = std::max<int64_t>(300, sender.rates().video_target.bps() / 8 / 30);
      if (keyframe_needed) bytes *= 4;
      sender.SendVideoChunks(video_send, MakeFrame(frame_id++, keyframe_needed, static_cast<size_t>(bytes), 1100),
                             static_cast<uint32_t>(t * 90), now);
      keyframe_needed = false;
    }
    sender.Process(now);
    receiver.Process(now);
    for (auto& p : sender.TakeOutgoing()) forward.Send(now, std::move(p.bytes), p.wire_size);
    for (auto& p : receiver.TakeOutgoing()) backward.Send(now, std::move(p.bytes), p.wire_size);
    forward.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { receiver.OnPacket(bytes, at); });
    backward.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { sender.OnPacket(bytes, at); });
    for (const auto& f : receiver.TakeVideoFrames(video_recv)) {
      if (f.render) freezes.OnFrameRendered(now);
    }
    for (const auto& e : sender.TakeEvents()) {
      if (e.type == rtp::SessionEventType::kKeyframeRequested) keyframe_needed = true;
    }
    receiver.TakeEvents();
    if (t % 10 == 0) receiver.GetAudio(audio_recv, out, now);  // The speaker pulls 10 ms.
  }

  std::printf("# Metrics after 30 s (Prometheus text format)\n%s\n", registry.ToPrometheus().c_str());
  std::printf("# Event counts (module/name)\n");
  for (const auto& [name, n] : event_counts) std::printf("%-28s %d\n", name.c_str(), n);
  std::printf("\nfinal GCC target: %.0f kbps (link: 700 kbps since 15 s)\n", sender.rates().target.kbps());
  return 0;
}
