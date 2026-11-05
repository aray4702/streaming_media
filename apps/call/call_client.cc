// The calling app's call controller (C14, #1116); see call_client.h.
#include "call_client.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <utility>

#include "ai_server.h"
#include "json.h"
#include "livekit_proto.h"
#include "opus_decoder.h"
#include "opus_encoder.h"
#include "sm/audio/aec3.h"
#include "sm/audio/agc1.h"
#include "sm/audio/hpf.h"
#include "sm/audio/ns.h"
#include "sm/audio/splitting_filter.h"
#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/connection_stats.h"
#include "sm/pc/peer_endpoint.h"
#include "sm/pc/webrtc_sdp.h"
#include "sm/rtp/session.h"
#if defined(SM_WITH_VIDEOTOOLBOX) && SM_WITH_VIDEOTOOLBOX
#include "sm/video/videotoolbox_h264.h"
#define SM_CALL_VIDEO 1
#else
#define SM_CALL_VIDEO 0
#endif

namespace sm::apps {

namespace {
constexpr int kRate = 48000;
constexpr size_t k10ms = kRate / 100;
constexpr size_t k20ms = kRate / 50;
constexpr size_t kMaxCapture = kRate;               // 1 s of microphone waiting at most.
constexpr size_t kPlayoutTarget = 3 * k10ms;        // Played-out audio kept ready for the device.
constexpr size_t kBands = 3;                        // 48 kHz: three 16 kHz bands (SplittingFilter).
constexpr size_t kBandSamples = audio::SplittingFilter::kSamplesPerBand;
// RTCIceCandidateInit as JSON, the form LiveKit's TrickleRequest carries (candidateInit).
std::string CandidateInitJson(const pc::IceCandidate& cand) {
  return "{\"candidate\":" + net::JsonEscape(cand.ToString()) + ",\"sdpMid\":" + net::JsonEscape(cand.sdp_mid()) +
         ",\"sdpMLineIndex\":" + std::to_string(cand.sdp_mline_index()) + "}";
}
}  // namespace

const char* CallStateName(CallState s) {
  switch (s) {
    case CallState::kIdle: return "idle";
    case CallState::kWaitingForPeer: return "waiting for peer";
    case CallState::kConnecting: return "connecting";
    case CallState::kConnected: return "connected";
    case CallState::kEnded: return "ended";
  }
  return "?";
}

std::optional<int> OpusRateFromAllocation(const rtp::Session& session, int max_bitrate_bps) {
  const auto registered = session.send_side().allocator().config("audio");
  const DataRate allocation = session.rates().audio_rate;
  if (!registered || allocation <= DataRate::Zero()) return std::nullopt;
  // AudioSendStream::OnBitrateUpdated: between the registered constraints.
  const DataRate target = std::clamp(allocation, registered->min, std::max(registered->min, registered->max));
  // OnReceivedUplinkAllocation: less the overhead at the packet's frame length (the max's overhead at 20 ms).
  const int64_t overhead_bps = std::max<int64_t>(0, registered->max.bps() - max_bitrate_bps);
  return static_cast<int>(std::clamp<int64_t>(target.bps() - overhead_bps, 6000, 510000));
}

void ApplyIosAudioOptions(CallClientConfig* config) {
  config->echo_cancellation = false;  // "Always disable AEC on iOS. Use built-in instead."
  config->agc = false;                // "Always disable AGC on iOS. Use built-in instead."
}

// --- The capture chain, in APM's order (AudioProcessingImpl::ProcessCaptureStreamLocked) ----------------------------

class CallClient::AudioProcessor {
 public:
  AudioProcessor(bool echo_cancellation, bool agc)
      : hpf_(kRate, 1),
        capture_split_(kRate),
        render_split_(kRate),
        ns_(audio::ns::NoiseSuppressorConfig{kRate, audio::ns::SuppressionLevel::k18dB, 1}) {
    if (echo_cancellation) aec_ = std::make_unique<audio::aec3::EchoCanceller3>(audio::aec3::Aec3Config{}, kRate, 1, 1);
    if (agc) {
      // APM's GainController1 defaults in kFixedDigital (api/audio/audio_processing.h:257-271).
      audio::agc1::GainControlConfig g;
      g.sample_rate = kRate;
      g.target_level_dbfs = 3;
      g.compression_gain_db = 9;
      g.enable_limiter = true;
      agc_ = std::make_unique<audio::agc1::GainControl>(g);
    }
  }
  double agc_gain_db() const { return agc_ ? agc_->stats().gain_db : 0; }
  std::string chain() const { return std::string("hpf,ns") + (aec_ ? ",aec3" : "") + (agc_ ? ",agc" : ""); }

  // 10 ms played out, in [-1, 1]: AEC3's render signal (AnalyzeReverseStream, split into bands).
  void Render(std::span<const float> pcm) {
    if (!aec_ && !agc_) return;  // Nothing in the chain reads the render signal.
    std::array<float, k10ms> x{}, bands{};
    for (size_t i = 0; i < k10ms && i < pcm.size(); ++i) x[i] = pcm[i] * 32768.f;
    render_split_.Analysis(x, bands);
    if (aec_) {
      ToFrame(bands, &render_frame_);
      aec_->AnalyzeRender(render_frame_);
    }
    if (agc_) {
      // GainControlImpl::PackRenderAudioBuffer: the lowest band in S16, queued for ProcessRenderAudio.
      std::array<int16_t, kBandSamples> band0{};
      for (size_t i = 0; i < kBandSamples; ++i) {
        band0[i] = static_cast<int16_t>(std::clamp(std::lround(bands[i]), -32768L, 32767L));
      }
      agc_->ProcessRenderAudio(band0);
    }
  }

  // 10 ms of microphone, processed in place: HPF, NS analysis, AEC3 (when on), NS, AGC (when on).
  void Capture(std::span<int16_t> pcm, int delay_ms) {
    std::array<float, k10ms> x{}, bands{};
    for (size_t i = 0; i < k10ms; ++i) x[i] = pcm[i];
    hpf_.Process(x);
    const std::span<const float> channels[1] = {x};
    if (aec_) aec_->AnalyzeCapture(channels);
    capture_split_.Analysis(x, bands);
    // The noise suppressor's API takes [-1, 1] floats; AEC3 the S16 range.
    std::array<float, k10ms> unit{};
    for (size_t i = 0; i < k10ms; ++i) unit[i] = bands[i] / 32768.f;
    ns_.Analyze(unit);
    if (aec_) {
      aec_->SetAudioBufferDelay(delay_ms);
      ToFrame(bands, &capture_frame_);
      aec_->ProcessCapture(&capture_frame_, false);
      for (size_t b = 0; b < kBands; ++b) {
        for (size_t i = 0; i < kBandSamples; ++i) unit[b * kBandSamples + i] = capture_frame_[b][0][i] / 32768.f;
      }
    }
    ns_.Process(unit);
    for (size_t i = 0; i < k10ms; ++i) bands[i] = unit[i] * 32768.f;
    if (agc_) {
      // GainControlImpl::ProcessCaptureAudio on the split bands (kFixedDigital: AnalyzeCaptureAudio does nothing).
      std::span<float> split[kBands];
      for (size_t b = 0; b < kBands; ++b) split[b] = std::span<float>(bands.data() + b * kBandSamples, kBandSamples);
      agc_->ProcessBands(split);
    }
    capture_split_.Synthesis(bands, x);
    for (size_t i = 0; i < k10ms; ++i) pcm[i] = static_cast<int16_t>(std::clamp(std::lround(x[i]), -32768L, 32767L));
  }

 private:
  static void ToFrame(std::span<const float> bands, audio::aec3::Frame* f) {
    f->assign(kBands, std::vector<std::vector<float>>(1, std::vector<float>(kBandSamples)));
    for (size_t b = 0; b < kBands; ++b) std::copy_n(bands.begin() + b * kBandSamples, kBandSamples, (*f)[b][0].begin());
  }
  audio::hpf::HighPassFilter hpf_;
  audio::SplittingFilter capture_split_, render_split_;
  audio::ns::NoiseSuppressor ns_;
  std::unique_ptr<audio::aec3::EchoCanceller3> aec_;
  audio::aec3::Frame capture_frame_, render_frame_;
  std::unique_ptr<audio::agc1::GainControl> agc_;
};

// --- Video: the camera and remote frames, and VideoToolbox H.264 -----------------------------------------------------

class CallClient::Video {
 public:
  std::mutex mu;
  std::optional<video::I420Frame> camera;  // The latest camera frame not yet encoded.
  video::Rotation camera_rotation = video::Rotation::k0;
  video::Rotation send_rotation = video::Rotation::k0, recv_rotation = video::Rotation::k0;
  std::optional<video::I420Frame> remote;  // The latest rendered remote frame not yet taken.
  uint64_t sent = 0, received = 0, decoded = 0, rendered = 0;
  int send_w = 0, send_h = 0, recv_w = 0, recv_h = 0;
  bool keyframe_request = true;
  Timestamp next_frame = Timestamp::MinusInfinity();
#if SM_CALL_VIDEO
  video::VideoToolboxH264Encoder encoder;
  bool encoder_ready = false;
  DataRate rate = DataRate::Zero();
  Timestamp rate_time = Timestamp::MinusInfinity();
#endif

  void Send(pc::PeerEndpoint& ep, int fps, Timestamp now) {
    rtp::Session* s = ep.session();
    if (!ep.video_send() || now < next_frame) return;
    std::optional<video::I420Frame> f;
    video::Rotation rotation;
    {
      std::lock_guard<std::mutex> lock(mu);
      f.swap(camera);
      rotation = camera_rotation;
    }
    if (!f || f->width <= 0 || f->height <= 0) return;
    next_frame = now + TimeDelta::Micros(1'000'000 / std::max(fps, 1)) - TimeDelta::Millis(3);
#if SM_CALL_VIDEO
    if (!encoder_ready || f->width != send_w || f->height != send_h) {
      encoder.Release();
      encoder_ready = encoder.InitEncode(video::EncoderCodecSettings{f->width, f->height}) == video::EncoderStatus::kOk;
      send_w = f->width;
      send_h = f->height;
      s->SetVideoEncodingData(*ep.video_send(), send_w, send_h);
      keyframe_request = true;
      rate = DataRate::Zero();
      if (!encoder_ready) return;
    }
    // The congestion controller's video target, as VideoStreamEncoder::OnBitrateUpdated hands it on.
    DataRate target = s->rates().video_target;
    if (target <= DataRate::Zero()) target = DataRate::KilobitsPerSec(300);
    if (rate.IsZero() || now - rate_time >= TimeDelta::Seconds(1) ||
        std::abs(target.bps() - rate.bps()) > rate.bps() / 20) {
      encoder.SetRates(video::EncoderRates{target, static_cast<double>(fps)});
      rate = target;
      rate_time = now;
    }
    const uint32_t ts = static_cast<uint32_t>(now.us() * 9 / 100);  // 90 kHz.
    std::optional<video::H264EncodedFrame> out;
    if (encoder.Encode(*f, ts, keyframe_request, now, out) != video::EncoderStatus::kOk || !out) return;
    keyframe_request = false;
    rtp::EncodedVideoFrame frame;
    frame.rtp_timestamp = ts;
    frame.data = out->annexb;
    frame.keyframe = out->keyframe;
    frame.rotation = rotation;  // RtpSenderVideo puts it in CVO.
    s->SendVideo(*ep.video_send(), frame, now);
    ++sent;
    send_rotation = rotation;
#else
    (void)s;
#endif
  }

  // One remote video track (by MID): its decoder (call thread only) and, under `mu`, its participant, counters and
  // latest frame. A 1:1 call has one; a group one per participant with video (C20).
  struct RemoteTrack {
    std::string identity;
    std::optional<video::I420Frame> frame;
    uint64_t received = 0, decoded = 0, rendered = 0;
    int w = 0, h = 0;
    uint64_t decoded_last_second = 0;
    double fps = 0;
#if SM_CALL_VIDEO
    video::VideoToolboxH264Decoder decoder;
    bool decoder_ready = false;
#endif
  };
  std::map<std::string, std::unique_ptr<RemoteTrack>> tracks;  // By MID; the map changes on the call thread, under mu.
  std::string first_mid;  // The first remote video track's: TakeRemoteVideo's.

  // Every remote video track of `ep`: decoded, turned upright and kept as its latest frame. `identity` names a track's
  // participant (empty in a 1:1 call).
  void Receive(pc::PeerEndpoint& ep, Timestamp now, const std::function<std::string(const pc::RemoteTrack&)>& identity) {
    rtp::Session* s = ep.session();
    std::vector<std::string> live;
    for (const pc::RemoteTrack& t : ep.remote_tracks()) {
      if (!t.video) continue;
      live.push_back(t.mid);
      RemoteTrack* track;
      {
        std::lock_guard<std::mutex> lock(mu);
        auto& slot = tracks[t.mid];
        if (!slot) slot = std::make_unique<RemoteTrack>();
        track = slot.get();
        if (identity) track->identity = identity(t);
        if (first_mid.empty()) first_mid = t.mid;
      }
      for (rtp::ReceivedVideoFrame& f : s->TakeVideoFrames(*t.video)) {
        {
          std::lock_guard<std::mutex> lock(mu);
          ++received;
          ++track->received;
        }
#if SM_CALL_VIDEO
        if (f.bitstream.empty()) continue;
        if (!track->decoder_ready) {
          video::DecoderSettings settings;
          settings.codec_type = video::DecoderCodecType::kH264;
          track->decoder_ready = track->decoder.Configure(settings);
        }
        video::I420Frame out;
        if (track->decoder.Decode(f.bitstream, now, out) != video::DecoderStatus::kOk) {
          s->OnDecodeFailure(*t.video, now);  // A keyframe request.
          continue;
        }
        std::lock_guard<std::mutex> lock(mu);
        ++decoded;
        ++track->decoded;
        if (!f.render) continue;
        ++rendered;
        ++track->rendered;
        // The renderer shows the frame turned by the rotation it arrived with (RTCMTLRenderer's rotation).
        if (f.rotation != video::Rotation::k0) out = video::ApplyOrientation(out, video::VideoOrientation{f.rotation});
        track->w = out.width;
        track->h = out.height;
        if (t.mid == first_mid) {
          recv_rotation = f.rotation;
          recv_w = out.width;
          recv_h = out.height;
          remote = out;
        }
        track->frame = std::move(out);
#else
        (void)f;
        (void)now;
#endif
      }
    }
    std::lock_guard<std::mutex> lock(mu);
    for (auto it = tracks.begin(); it != tracks.end();) {
      if (std::find(live.begin(), live.end(), it->first) == live.end()) it = tracks.erase(it);
      else ++it;
    }
    if (!tracks.count(first_mid)) first_mid = tracks.empty() ? std::string() : tracks.begin()->first;
  }

  // Once a second: each track's decoded frame rate.
  void UpdateRates() {
    std::lock_guard<std::mutex> lock(mu);
    for (auto& [mid, t] : tracks) {
      t->fps = static_cast<double>(t->decoded - t->decoded_last_second);
      t->decoded_last_second = t->decoded;
    }
  }
};

// --- LiveKit's session (C19) -----------------------------------------------------------------------------------------

struct CallClient::LiveKit {
  struct Meter {  // The local meter on a participant's played-out audio.
    double level = 0;
    Timestamp last_loud = Timestamp::MinusInfinity();
  };
  bool joined = false;
  std::string room, sid, identity;
  std::string audio_cid, video_cid;
  std::map<std::string, std::string> published;  // cid -> track sid
  bool offered = false;
  uint32_t offer_id = 0;
  TimeDelta ping_interval = TimeDelta::Seconds(10);
  Timestamp next_ping = Timestamp::PlusInfinity();
  double rtt_ms = -1;
  std::map<std::string, livekit::ParticipantInfo> others;  // identity -> info
  std::vector<std::string> pending_sub_candidates, pending_pub_candidates;
  std::string subscriber = "none";  // The subscriber connection's state, for the stats.
  bool subscriber_connected = false;
  // C20: the participants in the order they joined, who joined and left after us, the speakers (participant sid ->
  // its latest active SpeakerInfo; SpeakersChanged sends changes only), the local meters, the identities that have
  // been active speakers, the mute states announced, and the video tracks whose size was asked for.
  std::vector<std::string> order, joined_after, left;
  bool got_speakers = false;
  std::map<std::string, livekit::SpeakerInfo> speakers;
  std::map<std::string, Meter> meters;
  std::vector<std::string> speakers_seen;
  std::optional<bool> audio_mute_sent, video_mute_sent;
  std::vector<std::string> settings_sent;
  std::map<std::string, GroupParticipant> stats;  // identity -> the last second's receive stats
  Timestamp next_refresh = Timestamp::MinusInfinity();
  // The playout mix (#1175): one mixer source per remote audio track, by mid.
  GroupPlayoutMixer mixing;
};

// --- CallStats -------------------------------------------------------------------------------------------------------

std::string CallStats::Text() const {
  char buf[1024];
  std::snprintf(buf, sizeof buf,
                "state: %s%s%s\n"
                "connect: %.0f ms  duration: %.0f s\n"
                "path: %s -> %s\n"
                "rtt: %.0f ms  target: %.0f kbps\n"
                "send: %.0f kbps  recv: %.0f kbps\n"
                "audio: sent %llu  recv %llu  lost %lld  jitter %.0f ms\n"
                "audio: concealed %.1f%%  buffer %.0f ms  mic %.2f\n"
                "audio: opus %.1f kbps  allocation %.1f kbps  agc %.1f dB  chain %s\n"
                "video: sent %llu (%dx%d)  decoded %llu  shown %llu (%dx%d)  rotation %d/%d\n"
                "dtls: %s  srtp: %s",
                CallStateName(state), ended_reason.empty() ? "" : " - ", ended_reason.c_str(), connect_ms, duration_s,
                local_candidate.empty() ? "-" : local_candidate.c_str(),
                remote_candidate.empty() ? "-" : remote_candidate.c_str(), rtt_ms, target_kbps, send_kbps, recv_kbps,
                static_cast<unsigned long long>(audio_packets_sent), static_cast<unsigned long long>(audio_packets_received),
                static_cast<long long>(audio_packets_lost), audio_jitter_ms, audio_concealed_pct, jitter_buffer_ms,
                capture_level, opus_kbps, audio_allocation_kbps, agc_gain_db, capture_chain.empty() ? "-" : capture_chain.c_str(),
                static_cast<unsigned long long>(video_frames_sent), video_send_width, video_send_height,
                static_cast<unsigned long long>(video_frames_decoded),
                static_cast<unsigned long long>(video_frames_rendered), video_recv_width, video_recv_height,
                video_send_rotation, video_recv_rotation, dtls_cipher.empty() ? "-" : dtls_cipher.c_str(), srtp_cipher.empty() ? "-" : srtp_cipher.c_str());
  std::string out = buf;
  if (livekit_participants >= 0) {
    out += "\nlivekit: " + livekit_room + "  others " + std::to_string(livekit_participants) + "  published " +
           std::to_string(livekit_published) + "  subscriber " + livekit_subscriber;
    out += "\nspeaker: " + (livekit_active_speaker.empty() ? std::string("-") : livekit_active_speaker);
    for (const GroupParticipant& p : livekit_group) {
      char line[256];
      std::snprintf(line, sizeof line, "\n%s%s: audio %llu pkts lost %lld jitter %.0f ms%s  video %dx%d %.0f fps%s",
                    p.speaking ? "* " : "", p.identity.c_str(), static_cast<unsigned long long>(p.audio_packets_received),
                    static_cast<long long>(p.audio_packets_lost), p.audio_jitter_ms, p.audio_muted ? " (muted)" : "",
                    p.video_width, p.video_height, p.video_fps, p.video_muted ? " (off)" : "");
      out += line;
    }
  }
  return out;
}

std::string GroupParticipant::ToJson() const {
  char b[640];
  std::snprintf(b, sizeof b,
                ",\"has_audio\":%s,\"has_video\":%s,\"audio_muted\":%s,\"video_muted\":%s,\"audio_level\":%.3f,"
                "\"speaking\":%s,\"audio_packets_received\":%llu,\"audio_packets_lost\":%lld,\"audio_jitter_ms\":%.1f,"
                "\"audio_concealed_pct\":%.2f,\"audio_played_s\":%.2f,\"video_frames_received\":%llu,"
                "\"video_frames_decoded\":%llu,\"video_width\":%d,\"video_height\":%d,\"video_fps\":%.1f}",
                has_audio ? "true" : "false", has_video ? "true" : "false", audio_muted ? "true" : "false",
                video_muted ? "true" : "false", audio_level, speaking ? "true" : "false",
                static_cast<unsigned long long>(audio_packets_received), static_cast<long long>(audio_packets_lost),
                audio_jitter_ms, audio_concealed_pct, audio_played_s,
                static_cast<unsigned long long>(video_frames_received),
                static_cast<unsigned long long>(video_frames_decoded), video_width, video_height, video_fps);
  return "{\"identity\":" + net::JsonEscape(identity) + ",\"sid\":" + net::JsonEscape(sid) + b;
}

namespace {
std::string JsonStrings(const std::vector<std::string>& v) {
  std::string out = "[";
  for (size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + net::JsonEscape(v[i]);
  return out + "]";
}
}  // namespace

std::string CallStats::ToJson() const {
  char buf[1024];
  std::snprintf(buf, sizeof buf,
                "{\"state\":\"%s\",\"connect_ms\":%.1f,\"duration_s\":%.2f,\"rtt_ms\":%.1f,\"target_kbps\":%.1f,"
                "\"send_kbps\":%.1f,\"recv_kbps\":%.1f,\"audio_packets_sent\":%llu,\"audio_packets_received\":%llu,"
                "\"audio_packets_lost\":%lld,\"audio_jitter_ms\":%.1f,\"audio_concealed_pct\":%.2f,"
                "\"jitter_buffer_ms\":%.1f,\"audio_played_s\":%.2f,\"video_frames_sent\":%llu,"
                "\"video_frames_received\":%llu,\"video_frames_decoded\":%llu,\"video_frames_rendered\":%llu,"
                "\"capture_level\":%.3f,\"agc_gain_db\":%.2f,\"opus_kbps\":%.1f,\"audio_allocation_kbps\":%.1f,"
                "\"video_send_rotation\":%d,\"video_recv_rotation\":%d,",
                CallStateName(state), connect_ms, duration_s, rtt_ms, target_kbps, send_kbps, recv_kbps,
                static_cast<unsigned long long>(audio_packets_sent), static_cast<unsigned long long>(audio_packets_received),
                static_cast<long long>(audio_packets_lost), audio_jitter_ms, audio_concealed_pct, jitter_buffer_ms,
                audio_played_s, static_cast<unsigned long long>(video_frames_sent),
                static_cast<unsigned long long>(video_frames_received),
                static_cast<unsigned long long>(video_frames_decoded),
                static_cast<unsigned long long>(video_frames_rendered), capture_level, agc_gain_db, opus_kbps,
                audio_allocation_kbps, video_send_rotation, video_recv_rotation);
  return buf + std::string("\"relay_only\":") + (relay_only ? "true" : "false") +
         ",\"local_type\":" + net::JsonEscape(local_type) + ",\"remote_type\":" + net::JsonEscape(remote_type) +
         ",\"protocol\":" + net::JsonEscape(protocol) + ",\"relay_protocol\":" + net::JsonEscape(relay_protocol) +
         ",\"local_candidate\":" + net::JsonEscape(local_candidate) +
         ",\"remote_candidate\":" + net::JsonEscape(remote_candidate) + ",\"dtls_cipher\":" + net::JsonEscape(dtls_cipher) +
         ",\"srtp_cipher\":" + net::JsonEscape(srtp_cipher) + ",\"capture_chain\":" + net::JsonEscape(capture_chain) +
         ",\"ended_reason\":" + net::JsonEscape(ended_reason) + LiveKitJson() + "}";
}

std::string CallStats::LiveKitJson() const {
  if (livekit_participants < 0) return "";
  char b[160];
  std::snprintf(b, sizeof b, ",\"livekit_participants\":%d,\"livekit_published\":%d,\"livekit_signal_rtt_ms\":%.0f",
                livekit_participants, livekit_published, livekit_signal_rtt_ms);
  std::string group = "[";
  for (size_t i = 0; i < livekit_group.size(); ++i) group += (i ? "," : "") + livekit_group[i].ToJson();
  group += "]";
  char level[48];
  std::snprintf(level, sizeof level, ",\"livekit_local_level\":%.3f", livekit_local_level);
  return b + std::string(",\"livekit_room\":") + net::JsonEscape(livekit_room) +
         ",\"livekit_subscriber\":" + net::JsonEscape(livekit_subscriber) +
         ",\"livekit_identity\":" + net::JsonEscape(livekit_identity) +
         ",\"livekit_active_speaker\":" + net::JsonEscape(livekit_active_speaker) + level +
         ",\"livekit_speakers_seen\":" + JsonStrings(livekit_speakers_seen) +
         ",\"livekit_joined\":" + JsonStrings(livekit_joined) + ",\"livekit_left\":" + JsonStrings(livekit_left) +
         ",\"livekit_group\":" + group;
}

// --- CallClient ------------------------------------------------------------------------------------------------------

CallClient::CallClient(CallClientConfig config, std::function<void(const std::string&)> send_signal)
    : config_(std::move(config)), send_signal_(std::move(send_signal)), video_(std::make_unique<Video>()) {
  static const bool crypto_installed = [] {
    p2p::InstallStunCryptoFromTls();
    return true;
  }();
  (void)crypto_installed;
  if (config_.peer_id.empty()) {
    std::random_device rd;
    config_.peer_id = "ios-" + std::to_string(rd() % 1000000);
  }
  decoder_ = std::make_unique<testbench::OpusAudioDecoder>(kRate);
  // AudioEncoderOpusConfig::bitrate_bps: 32 kbps until the first allocation, within the configured range.
  encoder_ = std::make_unique<testbench::OpusSendEncoder>(
      std::clamp(32000, config_.opus_min_bitrate_bps, std::max(config_.opus_min_bitrate_bps, config_.opus_max_bitrate_bps)),
      true);
  if (config_.audio_processing) apm_ = std::make_unique<AudioProcessor>(config_.echo_cancellation, config_.agc);
  if (config_.livekit) {
    lk_ = std::make_unique<LiveKit>();
    lk_->audio_cid = "sm-audio-" + config_.peer_id;  // The a=msid track ids of the publisher's offer.
    if (config_.livekit_publish_video) lk_->video_cid = "sm-video-" + config_.peer_id;
  }
}

CallClient::~CallClient() {
  sub_.reset();  // The endpoints before the allocators and the sockets they use.
  ep_.reset();
  sub_allocator_.reset();
  allocator_.reset();
}

pc::PeerEndpoint* CallClient::receiver() const { return lk_ ? sub_.get() : ep_.get(); }

void CallClient::Log(const std::string& line) const {
  if (config_.verbose) std::fprintf(stderr, "[call %s] %s\n", config_.room.c_str(), line.c_str());
}

void CallClient::SendSignal(const std::string& json) {
  if (send_signal_) send_signal_(json);
}

std::string CallClient::To() const { return "\"to\":" + net::JsonEscape(remote_); }

void CallClient::SetTurnResponse(const std::string& json) {
  stun_.clear();
  turn_.clear();
  ParseTurnResponse(json, &stun_, &turn_);
  Log("/turn: " + std::to_string(stun_.size()) + " STUN, " + std::to_string(turn_.size()) + " TURN servers");
}

void CallClient::Start(Timestamp now) {
  start_ = now;
  state_ = CallState::kWaitingForPeer;
  if (lk_) {  // The WebSocket's URL carries the join (the token names the room and identity).
    Log("joining the LiveKit room as " + config_.peer_id);
    return;
  }
  SendSignal("{\"type\":\"join\",\"room\":" + net::JsonEscape(config_.room) + ",\"peer\":" +
             net::JsonEscape(config_.peer_id) + "}");
  Log("joining as " + config_.peer_id);
}

void CallClient::End(const std::string& why, Timestamp now) {
  if (state_ == CallState::kEnded) return;
  if (ep_ && ep_->session()) UpdateStats(now);
  state_ = CallState::kEnded;
  ended_reason_ = why;
  {
    std::lock_guard<std::mutex> lock(stats_mu_);
    stats_.ended_reason = why;
    if (start_.IsFinite()) stats_.duration_s = (now - start_).seconds();
  }
  Log("ended (" + why + ")");
  sub_.reset();
  ep_.reset();
  sub_allocator_.reset();
  allocator_.reset();
}

void CallClient::Hangup(Timestamp now) {
  if (state_ == CallState::kEnded) return;
  if (lk_) {
    livekit::LeaveRequest leave;
    leave.reason = livekit::kClientInitiated;
    SendSignal(livekit::EncodeLeave(leave));
  } else if (!remote_.empty()) {
    SendSignal("{\"type\":\"bye\"," + To() + "}");
  }
  End("hang-up", now);
}

void CallClient::OnSignalingClosed(Timestamp now) {
  if (state_ != CallState::kConnected) End("signaling closed", now);
}

void CallClient::MakeEndpoint(Timestamp now) {
  if (ep_ || state_ == CallState::kEnded) return;
  network_ = std::make_unique<p2p::StaticNetworkManager>();
  for (const auto& ip : config_.interfaces.empty() ? LocalIpv4Interfaces() : config_.interfaces) network_->AddInterface(ip);
  allocator_ = std::make_unique<p2p::BasicPortAllocator>(network_.get(), &sockets_);
  allocator_->SetConfiguration(now, stun_, turn_, 0, p2p::NO_PRUNE);
  allocator_->set_flags(p2p::PORTALLOCATOR_DISABLE_TCP);
  if (config_.relay_only) allocator_->SetCandidateFilter(p2p::CF_RELAY);
  pc::PeerEndpointConfig c;
  c.allocator = allocator_.get();
  c.certificate_name = "sm-call";
  c.transport.async_dns_resolver_factory = &resolver_;
  c.audio = true;
  c.video = true;  // Received from the web app even with our camera off; the AI server offers audio only.
  c.enable_data_channels = false;
  c.audio_decoder = decoder_.get();
  if (lk_) {
    // LiveKit's publisher connection: it sends only (the subscriber connection receives), and its tracks are the
    // ones AddTrackRequest announced, matched by cid (the a=msid track id).
    c.video = config_.livekit_publish_video;
    c.audio_decoder = nullptr;
    c.audio_track_id = lk_->audio_cid;
    if (!lk_->video_cid.empty()) c.video_track_id = lk_->video_cid;
    c.stream_id = "sm-" + config_.peer_id;
  }
  // The allocator registration: the Opus range plus the overhead at 20 ms frames (issue #569's path).
  c.audio_stream.min_bitrate = DataRate::BitsPerSec(config_.opus_min_bitrate_bps);
  c.audio_stream.max_bitrate = DataRate::BitsPerSec(std::max(config_.opus_min_bitrate_bps, config_.opus_max_bitrate_bps));
  ep_ = std::make_unique<pc::PeerEndpoint>(std::move(c), now);
  ep_->SetOnIceCandidate([this](std::unique_ptr<pc::IceCandidate> cand) {
    if (lk_) {
      livekit::TrickleRequest t;
      t.candidate_init = CandidateInitJson(*cand);
      t.target = livekit::SignalTarget::kPublisher;
      SendSignal(livekit::EncodeTrickle(t));
      return;
    }
    SendSignal("{\"type\":\"candidate\"," + To() + ",\"candidate\":" + net::JsonEscape(cand->ToString()) +
               ",\"sdpMid\":" + net::JsonEscape(cand->sdp_mid()) +
               ",\"sdpMLineIndex\":" + std::to_string(cand->sdp_mline_index()) + "}");
  });
  state_ = CallState::kConnecting;
}

void CallClient::Offer(Timestamp now) {
  std::string error;
  auto offer = ep_->CreateOffer(&error);
  if (!offer) return End("CreateOffer failed: " + error, now);
  if (lk_) {
    livekit::SessionDescription sd;
    sd.type = "offer";
    sd.sdp = pc::SdpSerialize(*offer);
    sd.id = ++lk_->offer_id;
    SendSignal(livekit::EncodeOffer(sd));
    lk_->offered = true;
  } else {
    SendSignal("{\"type\":\"offer\"," + To() + ",\"sdp\":" + net::JsonEscape(pc::SdpSerialize(*offer)) + "}");
  }
  if (!ep_->SetLocalDescription(std::move(offer), now, &error)) End("SetLocalDescription failed: " + error, now);
}

// --- LiveKit (C19) ---------------------------------------------------------------------------------------------------

void CallClient::MakeSubscriber(Timestamp now) {
  if (sub_ || state_ == CallState::kEnded || !network_) return;
  sub_allocator_ = std::make_unique<p2p::BasicPortAllocator>(network_.get(), &sockets_);
  sub_allocator_->SetConfiguration(now, stun_, turn_, 0, p2p::NO_PRUNE);
  sub_allocator_->set_flags(p2p::PORTALLOCATOR_DISABLE_TCP);
  if (config_.relay_only) sub_allocator_->SetCandidateFilter(p2p::CF_RELAY);
  pc::PeerEndpointConfig c;
  c.allocator = sub_allocator_.get();
  c.certificate_name = "sm-call";
  c.transport.async_dns_resolver_factory = &resolver_;
  // Receive only: no AddTrack transceivers; every section the server offers gets a recvonly transceiver of its own
  // (C18's AssociateTransceiver), with an Opus decoder per audio track.
  c.audio = false;
  c.video = false;
  c.receive_remote_tracks = true;
  // The server's offer carries LiveKit's data channels (_reliable, _lossy). Built without SM_WITH_DATACHANNEL (the
  // iOS presets) the section is rejected, which LiveKit accepts: its signaling carries what the client needs.
  c.enable_data_channels = true;
  c.audio_decoder_factory = [](const std::string&) -> std::unique_ptr<rtp::AudioDecoder> {
    return std::make_unique<testbench::OpusAudioDecoder>(kRate);
  };
  c.stream_id = "sm-" + config_.peer_id + "-sub";
  sub_ = std::make_unique<pc::PeerEndpoint>(std::move(c), now);
  sub_->SetOnTrack([this](const pc::RemoteTrack& t) { OnRemoteTrack(t, true); });
  sub_->SetOnRemoveTrack([this](const pc::RemoteTrack& t) { OnRemoteTrack(t, false); });
  sub_->SetOnIceCandidate([this](std::unique_ptr<pc::IceCandidate> cand) {
    livekit::TrickleRequest t;
    t.candidate_init = CandidateInitJson(*cand);
    t.target = livekit::SignalTarget::kSubscriber;
    SendSignal(livekit::EncodeTrickle(t));
  });
}

void CallClient::LiveKitPublish(Timestamp now) {
  // Our tracks, as the SDKs' LocalParticipant.publishTrack announces them before the publisher's offer.
  livekit::AddTrackRequest audio;
  audio.cid = lk_->audio_cid;
  audio.name = "microphone";
  audio.type = livekit::TrackType::kAudio;
  audio.source = livekit::TrackSource::kMicrophone;
  audio.disable_red = true;  // Plain Opus: the library's sender doesn't send RED audio.
  SendSignal(livekit::EncodeAddTrack(audio));
  if (!lk_->video_cid.empty()) {
    livekit::AddTrackRequest video;
    video.cid = lk_->video_cid;
    video.name = "camera";
    video.type = livekit::TrackType::kVideo;
    video.source = livekit::TrackSource::kCamera;
    video.width = static_cast<uint32_t>(config_.livekit_video_width);
    video.height = static_cast<uint32_t>(config_.livekit_video_height);
    // One layer until C17 (simulcast send).
    video.layers.push_back({livekit::VideoQuality::kHigh, video.width, video.height, 0, 0});
    SendSignal(livekit::EncodeAddTrack(video));
  }
  (void)now;
}

void CallClient::LiveKitTrickle(const std::string& candidate_init, bool subscriber, Timestamp now) {
  pc::PeerEndpoint* ep = subscriber ? sub_.get() : ep_.get();
  if (!ep || !ep->remote_description()) {
    (subscriber ? lk_->pending_sub_candidates : lk_->pending_pub_candidates).push_back(candidate_init);
    return;
  }
  auto m = net::JsonParseFlat(candidate_init);
  if (m["candidate"].empty()) return;  // End of candidates.
  const std::string& idx = m["sdpMLineIndex"];
  const int index = idx.empty() || idx == "null" ? 0 : std::atoi(idx.c_str());
  const std::string mid = m["sdpMid"] == "null" ? "" : m["sdpMid"];
  pc::SdpParseError perr;
  std::string error;
  if (auto cand = pc::IceCandidate::Create(mid, index, m["candidate"], &perr)) ep->AddIceCandidate(*cand, now, &error);
}

void CallClient::LiveKitSubscriberOffer(const std::string& sdp, uint32_t id, Timestamp now) {
  MakeSubscriber(now);
  if (!sub_) return;
  pc::SdpParseError perr;
  std::string error;
  auto desc = pc::SdpDeserialize(pc::SdpType::kOffer, sdp, &perr);
  if (!desc || !sub_->SetRemoteDescription(std::move(desc), now, &error)) {
    // The publisher path goes on; this end keeps receiving what the earlier offers brought.
    lk_->subscriber = "offer " + std::to_string(id) + " not applied: " + (desc ? error : perr.description);
    Log("subscriber: " + lk_->subscriber);
    return;
  }
  auto answer = sub_->CreateAnswer(&error);
  if (!answer) {
    lk_->subscriber = "CreateAnswer failed: " + error;
    Log("subscriber: " + lk_->subscriber);
    return;
  }
  livekit::SessionDescription sd;
  sd.type = "answer";
  sd.sdp = pc::SdpSerialize(*answer);
  sd.id = id;
  SendSignal(livekit::EncodeAnswer(sd));
  if (!sub_->SetLocalDescription(std::move(answer), now, &error)) {
    lk_->subscriber = "SetLocalDescription failed: " + error;
    Log("subscriber: " + lk_->subscriber);
    return;
  }
  lk_->subscriber = "answered offer " + std::to_string(id);
  Log("subscriber: " + lk_->subscriber);
  for (const std::string& c : std::exchange(lk_->pending_sub_candidates, {})) LiveKitTrickle(c, true, now);
}

void CallClient::OnLiveKit(const std::string& bytes, Timestamp now) {
  using Kind = livekit::SignalResponse::Kind;
  const livekit::SignalResponse r = livekit::DecodeSignalResponse(bytes);
  switch (r.kind) {
    case Kind::kJoin: {
      if (lk_->joined) break;
      lk_->joined = true;
      lk_->room = r.join.room_name;
      lk_->sid = r.join.participant.sid;
      lk_->identity = r.join.participant.identity;
      for (const auto& p : r.join.other_participants) {
        lk_->others[p.identity] = p;
        lk_->order.push_back(p.identity);
      }
      // The server's ICE servers (on the host: coturn, with a REST credential) next to /turn's.
      for (const auto& s : r.join.ice_servers) {
        std::string urls;
        for (const auto& u : s.urls) urls += (urls.empty() ? "" : ",") + net::JsonEscape(u);
        ParseTurnResponse("{\"urls\":[" + urls + "],\"username\":" + net::JsonEscape(s.username) +
                              ",\"credential\":" + net::JsonEscape(s.credential) + "}",
                          &stun_, &turn_);
      }
      if (r.join.ping_interval_s > 0) lk_->ping_interval = TimeDelta::Seconds(r.join.ping_interval_s);
      lk_->next_ping = now + lk_->ping_interval;
      Log("joined LiveKit room " + lk_->room + " as " + lk_->identity + " (" + lk_->sid + "), server " +
          r.join.server_version + ", " + std::to_string(r.join.other_participants.size()) + " others, " +
          std::to_string(r.join.ice_servers.size()) + " ICE servers, subscriber primary " +
          (r.join.subscriber_primary ? "yes" : "no"));
      MakeEndpoint(now);
      LiveKitPublish(now);
      break;
    }
    case Kind::kTrackPublished: {
      lk_->published[r.track_published.cid] = r.track_published.track.sid;
      Log("published " + r.track_published.cid + " as " + r.track_published.track.sid);
      const bool all = lk_->published.count(lk_->audio_cid) &&
                       (lk_->video_cid.empty() || lk_->published.count(lk_->video_cid));
      if (all && !lk_->offered && ep_) Offer(now);
      break;
    }
    case Kind::kAnswer: {
      if (!ep_) break;
      pc::SdpParseError perr;
      std::string error;
      auto desc = pc::SdpDeserialize(pc::SdpType::kAnswer, r.description.sdp, &perr);
      if (!desc || !ep_->SetRemoteDescription(std::move(desc), now, &error)) {
        return End("bad publisher answer: " + (desc ? error : perr.description), now);
      }
      for (const std::string& c : std::exchange(lk_->pending_pub_candidates, {})) LiveKitTrickle(c, false, now);
      break;
    }
    case Kind::kOffer:
      LiveKitSubscriberOffer(r.description.sdp, r.description.id, now);
      break;
    case Kind::kTrickle:
      LiveKitTrickle(r.trickle.candidate_init, r.trickle.target == livekit::SignalTarget::kSubscriber, now);
      break;
    case Kind::kUpdate:
      for (const auto& p : r.participants) {
        if (p.identity == lk_->identity) continue;
        if (p.state == livekit::ParticipantInfo::State::kDisconnected) {
          if (lk_->others.erase(p.identity)) {
            Log("participant left: " + p.identity);
            lk_->left.push_back(p.identity);
            lk_->order.erase(std::remove(lk_->order.begin(), lk_->order.end(), p.identity), lk_->order.end());
            lk_->meters.erase(p.identity);
            lk_->speakers.erase(p.sid);
          }
        } else {
          if (!lk_->others.count(p.identity)) {
            Log("participant joined: " + p.identity);
            lk_->joined_after.push_back(p.identity);
            lk_->order.push_back(p.identity);
          }
          lk_->others[p.identity] = p;
        }
      }
      RefreshGroup(now);
      break;
    case Kind::kSpeakersChanged:
      LiveKitSpeakers(r.speakers);
      RefreshGroup(now);
      break;
    case Kind::kMute: {  // The server (an admin) mutes or unmutes one of our tracks.
      const auto audio = lk_->published.find(lk_->audio_cid), video = lk_->published.find(lk_->video_cid);
      if (audio != lk_->published.end() && audio->second == r.text) muted_ = r.muted;
      if (video != lk_->published.end() && video->second == r.text) video_enabled_ = !r.muted;
      break;
    }
    case Kind::kLeave:
      End("LiveKit server leave (reason " + std::to_string(r.leave.reason) + ")", now);
      break;
    case Kind::kPongResp:
      // Our ping's timestamp (this clock, in ms) comes back: the signaling round trip.
      if (now.us() / 1000 >= r.pong.last_ping_timestamp) {
        lk_->rtt_ms = static_cast<double>(now.us() / 1000 - r.pong.last_ping_timestamp);
      }
      break;
    case Kind::kInvalid:
      Log("undecodable LiveKit message (" + std::to_string(bytes.size()) + " bytes)");
      break;
    default:
      break;  // Speakers, connection quality, stream state, refreshed token: nothing to do yet (C20).
  }
}

// --- A group: participants, tracks, speakers (C20) --------------------------------------------------------------------

std::string CallClient::TrackIdentity(const std::string& track_id, const std::vector<std::string>& stream_ids) const {
  if (!lk_) return "";
  // LiveKit's msid: the stream id is "<participant sid>|<track sid>" and the track id the track sid.
  std::string participant_sid;
  if (!stream_ids.empty()) participant_sid = stream_ids.front().substr(0, stream_ids.front().find('|'));
  for (const auto& [identity, p] : lk_->others) {
    if (!participant_sid.empty() && p.sid == participant_sid) return identity;
    for (const livekit::TrackInfo& t : p.tracks) {
      if (t.sid == track_id) return identity;
    }
  }
  return "";
}

void CallClient::OnRemoteTrack(const pc::RemoteTrack& t, bool added) {
  const std::string who = TrackIdentity(t.track_id, t.stream_ids);
  Log(std::string(added ? "receiving " : "stopped receiving ") + (t.kind == pc::MediaType::VIDEO ? "video" : "audio") +
      " " + t.track_id + " (mid " + t.mid + ") of " + (who.empty() ? "?" : who));
  // The size we show it at: the server forwards the simulcast layer that fits (UpdateTrackSettings).
  if (added && t.kind == pc::MediaType::VIDEO && config_.livekit_receive_width > 0 &&
      std::find(lk_->settings_sent.begin(), lk_->settings_sent.end(), t.track_id) == lk_->settings_sent.end()) {
    lk_->settings_sent.push_back(t.track_id);
    SendSignal(livekit::EncodeUpdateTrackSettings({t.track_id}, false,
                                                  static_cast<uint32_t>(config_.livekit_receive_width),
                                                  static_cast<uint32_t>(config_.livekit_receive_height)));
  }
}

void CallClient::LiveKitSpeakers(const std::vector<livekit::SpeakerInfo>& speakers) {
  lk_->got_speakers = true;
  for (const livekit::SpeakerInfo& s : speakers) {
    if (s.active) lk_->speakers[s.sid] = s;
    else lk_->speakers.erase(s.sid);
  }
}

void CallClient::LiveKitAnnounceMute() {
  // MuteTrackRequest for our microphone (muted) and camera (video off), so the others show it (TrackInfo.muted).
  auto announce = [this](const std::string& cid, bool muted, std::optional<bool>* sent) {
    const auto it = lk_->published.find(cid);
    if (cid.empty() || it == lk_->published.end() || (*sent && **sent == muted)) return;
    SendSignal(livekit::EncodeMute(it->second, muted));
    *sent = muted;
  };
  announce(lk_->audio_cid, muted_, &lk_->audio_mute_sent);
  announce(lk_->video_cid, !video_enabled_, &lk_->video_mute_sent);
}

void CallClient::RefreshGroup(Timestamp now) {
  if (!lk_) return;
  const pc::PeerEndpoint* rx = sub_.get();
  const std::vector<pc::RemoteTrack> tracks = rx ? rx->remote_tracks() : std::vector<pc::RemoteTrack>{};
  constexpr TimeDelta kHangover = TimeDelta::Millis(500);  // The local meter's: speaking until 500 ms quiet.
  std::vector<GroupParticipant> group;
  std::string active;
  double loudest = -1;
  auto consider = [&](const std::string& identity, double level, bool speaking) {
    if (!speaking) return;
    if (level > loudest) {
      loudest = level;
      active = identity;
    }
    if (std::find(lk_->speakers_seen.begin(), lk_->speakers_seen.end(), identity) == lk_->speakers_seen.end()) {
      lk_->speakers_seen.push_back(identity);
    }
  };
  for (const std::string& identity : lk_->order) {
    const auto it = lk_->others.find(identity);
    if (it == lk_->others.end()) continue;
    const livekit::ParticipantInfo& info = it->second;
    GroupParticipant p;
    if (const auto st = lk_->stats.find(identity); st != lk_->stats.end()) p = st->second;
    p.identity = identity;
    p.name = info.name;
    p.sid = info.sid;
    p.audio_muted = p.video_muted = true;
    for (const livekit::TrackInfo& t : info.tracks) {
      if (t.type == livekit::TrackType::kAudio) p.audio_muted = p.audio_muted && t.muted;
      if (t.type == livekit::TrackType::kVideo) p.video_muted = p.video_muted && t.muted;
    }
    p.has_audio = p.has_video = false;
    for (const pc::RemoteTrack& t : tracks) {
      if (TrackIdentity(t.track_id, t.stream_ids) != identity) continue;
      (t.kind == pc::MediaType::VIDEO ? p.has_video : p.has_audio) = true;
    }
    const LiveKit::Meter meter = lk_->meters.count(identity) ? lk_->meters[identity] : LiveKit::Meter{};
    if (lk_->got_speakers) {
      const auto s = lk_->speakers.find(info.sid);
      p.speaking = s != lk_->speakers.end();
      p.audio_level = p.speaking ? s->second.level : 0;
    } else {
      p.speaking = now - meter.last_loud < kHangover;
      p.audio_level = meter.level;
    }
    consider(identity, p.audio_level, p.speaking);
    group.push_back(std::move(p));
  }
  // This participant: LiveKit's word on our sid, else our own processed microphone.
  if (lk_->got_speakers) {
    const auto s = lk_->speakers.find(lk_->sid);
    if (s != lk_->speakers.end()) consider(lk_->identity, s->second.level, true);
  } else {
    if (!muted_ && capture_level_ > 0.01) lk_->meters[lk_->identity].last_loud = now;
    consider(lk_->identity, capture_level_, now - lk_->meters[lk_->identity].last_loud < kHangover);
  }
  std::lock_guard<std::mutex> lock(group_mu_);
  group_ = std::move(group);
  active_speaker_ = active;
}

void CallClient::MixGroupAudio(std::span<float> pcm, Timestamp now) {
  std::fill(pcm.begin(), pcm.end(), 0.f);
  if (!sub_ || !sub_->session()) return;
  // Every remote audio track is a mixer source, as each AudioReceiveStream is one of libwebrtc's AudioMixerImpl
  // (#1175): sources come and go with the subscriber's tracks. A source's 10 ms is ChannelReceive::
  // GetAudioFrameWithInfo's: the track's jitter buffer, which also feeds the meters and the remote-audio sink.
  rtp::Session* session = sub_->session();
  for (const pc::RemoteTrack& t : sub_->remote_tracks()) {
    if (!t.audio) continue;
    lk_->mixing.Track(t.mid, t.ssrc,
                      [this, session, id = *t.audio, identity = TrackIdentity(t.track_id, t.stream_ids),
                       now](std::span<float> x) {
                        const timing::AudioOperation op = session->GetAudio(id, x, now);
                        double energy = 0;
                        for (float v : x) energy += static_cast<double>(v) * v;
                        const double rms = std::sqrt(energy / static_cast<double>(x.size()));
                        LiveKit::Meter& m = lk_->meters[identity];
                        m.level = std::max(rms, m.level * 0.9);  // Fast attack, about 100 ms decay.
                        if (rms > 0.01) m.last_loud = now;       // -40 dBFS, as sm_call_smoke's "audio playing".
                        if (remote_audio_sink_ && !identity.empty()) {
                          remote_audio_sink_(identity, x, op == timing::AudioOperation::kExpand, now);
                        }
                      });
  }
  lk_->mixing.Mix(pcm);
}

// --- GroupPlayoutMixer -----------------------------------------------------------------------------------------------

void GroupPlayoutMixer::Track(const std::string& key, uint32_t ssrc, GetAudio get_audio) {
  std::unique_ptr<Source>& src = sources_[key];
  if (!src) {
    src = std::make_unique<Source>();
    mixer_.AddSource(src.get());
  }
  src->ssrc = ssrc;
  src->get_audio = std::move(get_audio);
  src->named = true;
}

void GroupPlayoutMixer::Mix(std::span<float> pcm) {
  for (auto it = sources_.begin(); it != sources_.end();) {
    if (it->second->named) {
      it->second->named = false;
      ++it;
      continue;
    }
    mixer_.RemoveSource(it->second.get());
    it = sources_.erase(it);
  }
  // Mono at the rate the sources prefer: the sum in FloatS16 and AGC2's limiter (FrameCombiner), back to [-1, 1].
  mixer_.Mix(1, &frame_);
  const std::span<const int16_t> d = frame_.data();
  std::fill(pcm.begin(), pcm.end(), 0.f);
  for (size_t i = 0; i < pcm.size() && i < d.size(); ++i) pcm[i] = static_cast<float>(d[i]) / 32768.f;
}

audio::AudioMixerSource::AudioFrameInfo GroupPlayoutMixer::Source::GetAudioFrameWithInfo(int sample_rate_hz,
                                                                                         audio::AudioFrame* frame) {
  // 10 ms at kSampleRateHz, the only rate PreferredSampleRate asks for.
  std::array<float, k10ms> x{};
  if (get_audio) get_audio(x);
  std::array<int16_t, k10ms> s16{};
  for (size_t i = 0; i < k10ms; ++i) s16[i] = audio::FloatS16ToS16(x[i] * 32768.f);  // FloatToS16 (audio_util.h).
  frame->UpdateFrame(0, s16.data(), k10ms, sample_rate_hz, 1);
  return AudioFrameInfo::kNormal;
}

std::vector<GroupParticipant> CallClient::participants() const {
  std::lock_guard<std::mutex> lock(group_mu_);
  return group_;
}

std::string CallClient::active_speaker() const {
  std::lock_guard<std::mutex> lock(group_mu_);
  return active_speaker_;
}

bool CallClient::TakeParticipantVideo(const std::string& identity, video::I420Frame* out) {
  std::lock_guard<std::mutex> lock(video_->mu);
  for (auto& [mid, t] : video_->tracks) {
    if (t->identity != identity || !t->frame) continue;
    *out = std::move(*t->frame);
    t->frame.reset();
    return true;
  }
  return false;
}

void CallClient::OnSignal(const std::string& text, Timestamp now) {
  if (state_ == CallState::kEnded) return;
  if (lk_) return OnLiveKit(text, now);
  auto m = net::JsonParseFlat(text);
  const std::string& type = m["type"];
  if (type == "joined") {
    const auto peers = net::JsonStringArray(m["peers"]);
    if (!peers.empty() && remote_.empty()) {
      remote_ = peers.front();  // We joined later: we offer.
      MakeEndpoint(now);
      Offer(now);
    }
  } else if (type == "peer-joined") {
    if (remote_.empty()) {
      remote_ = m["peer"];  // They joined later: they offer.
      MakeEndpoint(now);
    }
  } else if (type == "offer" || type == "answer") {
    if (remote_.empty()) remote_ = m["from"];
    if (!m["from"].empty() && m["from"] != remote_) return;  // A third peer: one call at a time.
    MakeEndpoint(now);
    if (!ep_) return;
    pc::SdpParseError perr;
    std::string error;
    auto desc = pc::SdpDeserialize(type == "offer" ? pc::SdpType::kOffer : pc::SdpType::kAnswer, m["sdp"], &perr);
    if (!desc || !ep_->SetRemoteDescription(std::move(desc), now, &error)) return End("bad remote description", now);
    if (type == "offer") {
      auto answer = ep_->CreateAnswer(&error);
      if (!answer) return End("CreateAnswer failed: " + error, now);
      SendSignal("{\"type\":\"answer\"," + To() + ",\"sdp\":" + net::JsonEscape(pc::SdpSerialize(*answer)) + "}");
      if (!ep_->SetLocalDescription(std::move(answer), now, &error)) return End("SetLocalDescription failed", now);
    }
    for (auto& c : pending_candidates_) AddCandidate(c, now);
    pending_candidates_.clear();
  } else if (type == "candidate") {
    if (!ep_ || !ep_->remote_description()) pending_candidates_.push_back(m);
    else AddCandidate(m, now);
  } else if (type == "peer-left" || type == "bye") {
    const std::string who = type == "bye" ? m["from"] : m["peer"];
    if (who.empty() || who == remote_) End("peer left", now);
  } else if (type == "error") {
    End("signaling error: " + m["reason"], now);
  }
}

void CallClient::AddCandidate(std::map<std::string, std::string>& m, Timestamp now) {
  if (m["candidate"].empty() || !ep_) return;  // End of candidates.
  const std::string& idx = m["sdpMLineIndex"];
  const int index = idx.empty() || idx == "null" ? 0 : std::atoi(idx.c_str());
  const std::string mid = m["sdpMid"] == "null" ? "" : m["sdpMid"];
  pc::SdpParseError perr;
  std::string error;
  if (auto cand = pc::IceCandidate::Create(mid, index, m["candidate"], &perr)) ep_->AddIceCandidate(*cand, now, &error);
}

Timestamp CallClient::Step(Timestamp now) {
  if (state_ == CallState::kEnded || !ep_) return now + TimeDelta::Millis(10);
  ep_->Process(now);
  if (sub_) {
    sub_->Process(now);
    if (!lk_->subscriber_connected && sub_->IsConnected()) {
      lk_->subscriber_connected = true;
      Log("subscriber connected in " + std::to_string(static_cast<int>((now - start_).ms_f())) + " ms");
    }
  }
  if (lk_ && now >= lk_->next_ping) {  // LiveKit's keepalive: ping_req every ping_interval.
    SendSignal(livekit::EncodePing(now.us() / 1000, lk_->rtt_ms < 0 ? 0 : static_cast<int64_t>(lk_->rtt_ms)));
    lk_->next_ping = now + lk_->ping_interval;
  }
  if (lk_ && lk_->joined) {
    LiveKitAnnounceMute();
    if (now >= lk_->next_refresh) {  // The grid's speaker state, ten times a second.
      RefreshGroup(now);
      lk_->next_refresh = now + TimeDelta::Millis(100);
    }
  }
  if (state_ != CallState::kConnected && ep_->IsConnected() && ep_->session()) {
    state_ = CallState::kConnected;
    connect_ms_ = (now - start_).ms_f();
    next_stats_ = now;
    std::lock_guard<std::mutex> lock(audio_mu_);
    capture_ring_.clear();
    Log("connected in " + std::to_string(static_cast<int>(connect_ms_)) + " ms");
  }
  if (state_ == CallState::kConnected) {
    rtp::Session* s = ep_->session();
    for (const rtp::SessionEvent& e : s->TakeEvents()) {
      if (e.type == rtp::SessionEventType::kKeyframeRequested) video_->keyframe_request = true;
    }
    Audio(now);
    if (video_enabled_) video_->Send(*ep_, config_.video_fps, now);
    if (pc::PeerEndpoint* rx = receiver(); rx && rx->session()) {
      if (lk_) {
        video_->Receive(*rx, now, [this](const pc::RemoteTrack& t) { return TrackIdentity(t.track_id, t.stream_ids); });
      } else {
        video_->Receive(*rx, now, nullptr);
      }
    }
    ep_->FlushOutgoing(now);
    if (sub_ && sub_->session()) sub_->FlushOutgoing(now);
    if (now >= next_stats_) {
      UpdateStats(now);
      next_stats_ = now + TimeDelta::Seconds(1);
    }
  }
  Timestamp next = std::min(ep_->NextProcessTime(now), now + TimeDelta::Millis(5));
  if (sub_) next = std::min(next, sub_->NextProcessTime(now));
  return next;
}

void CallClient::Wait(Timestamp now, TimeDelta wait) {
  sockets_.Poll(now, std::clamp(wait, TimeDelta::Zero(), TimeDelta::Millis(5)));
  resolver_.Poll(now);
}

void CallClient::Audio(Timestamp now) {
  rtp::Session* s = ep_->session();
  // Playout: keep kPlayoutTarget ready for the device; each 10 ms is also AEC3's render signal.
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(audio_mu_);
      if (playout_ring_.size() >= kPlayoutTarget) break;
    }
    std::array<float, k10ms> pcm{};
    if (lk_) {
      MixGroupAudio(pcm, now);  // Every participant's audio (C20).
    } else if (pc::PeerEndpoint* rx = receiver(); rx && rx->session() && rx->audio_receive()) {
      rx->session()->GetAudio(*rx->audio_receive(), pcm, now);
    }
    if (apm_) apm_->Render(pcm);
    std::lock_guard<std::mutex> lock(audio_mu_);
    for (float v : pcm) playout_ring_.push_back(static_cast<int16_t>(std::clamp(v, -1.f, 1.f) * 32767));
  }
  // Capture: process each 10 ms, encode each 20 ms.
  for (;;) {
    std::array<int16_t, k10ms> frame{};
    int delay_ms = 0;
    {
      std::lock_guard<std::mutex> lock(audio_mu_);
      if (capture_ring_.size() < k10ms) break;
      std::copy_n(capture_ring_.begin(), k10ms, frame.begin());
      capture_ring_.erase(capture_ring_.begin(), capture_ring_.begin() + k10ms);
      delay_ms = device_delay_ms_;
    }
    if (apm_) apm_->Capture(frame, delay_ms);
    if (ep_->audio_send()) s->ProcessAudioCapture(*ep_->audio_send(), frame, 1, muted_);
    double sum = 0;
    for (int16_t v : frame) sum += static_cast<double>(v) * v;
    capture_level_ = std::sqrt(sum / k10ms) / 32768.0;
    send_buf_.insert(send_buf_.end(), frame.begin(), frame.end());
    if (send_buf_.size() < k20ms) continue;
    // The latest allocation reaches the encoder before its next frame (ChannelSend::OnBitrateAllocation).
    if (auto bps = OpusRateFromAllocation(*s, std::max(config_.opus_min_bitrate_bps, config_.opus_max_bitrate_bps))) {
      encoder_->SetTargetBitrate(*bps);
    }
    audio_allocation_kbps_ = s->rates().audio_rate.kbps();
    const std::vector<uint8_t> payload = encoder_->Encode(std::span<const int16_t>(send_buf_.data(), k20ms));
    send_buf_.erase(send_buf_.begin(), send_buf_.begin() + k20ms);
    if (!payload.empty() && ep_->audio_send()) {
      s->SendAudio(*ep_->audio_send(), rtp::EncodedAudioFrame{rtp_ts_, payload, audio_frames_sent_ == 0}, now);
      ++audio_frames_sent_;
    }
    rtp_ts_ += static_cast<uint32_t>(k20ms);
  }
}

void CallClient::PushCapturedAudio(std::span<const int16_t> samples, int total_delay_ms) {
  std::lock_guard<std::mutex> lock(audio_mu_);
  capture_ring_.insert(capture_ring_.end(), samples.begin(), samples.end());
  if (capture_ring_.size() > kMaxCapture) capture_ring_.erase(capture_ring_.begin(), capture_ring_.end() - kMaxCapture);
  device_delay_ms_ = total_delay_ms;
}

size_t CallClient::PullPlayout(std::span<int16_t> out) {
  std::lock_guard<std::mutex> lock(audio_mu_);
  const size_t n = std::min(out.size(), playout_ring_.size());
  std::copy_n(playout_ring_.begin(), n, out.begin());
  playout_ring_.erase(playout_ring_.begin(), playout_ring_.begin() + static_cast<long>(n));
  std::fill(out.begin() + static_cast<long>(n), out.end(), int16_t{0});
  return n;
}

void CallClient::PushCameraFrame(video::I420Frame frame, video::Rotation rotation) {
  std::lock_guard<std::mutex> lock(video_->mu);
  video_->camera = std::move(frame);
  video_->camera_rotation = rotation;
}

bool CallClient::TakeRemoteVideo(video::I420Frame* out) {
  std::lock_guard<std::mutex> lock(video_->mu);
  if (!video_->remote) return false;
  *out = std::move(*video_->remote);
  video_->remote.reset();
  return true;
}

CallStats CallClient::stats() const {
  std::lock_guard<std::mutex> lock(stats_mu_);
  CallStats s = stats_;
  s.state = state_;
  s.relay_only = config_.relay_only;
  if (s.ended_reason.empty()) s.ended_reason = state_ == CallState::kEnded ? ended_reason_ : "";
  return s;
}

void CallClient::UpdateStats(Timestamp now) {
  rtp::Session* s = ep_ ? ep_->session() : nullptr;
  if (!s) return;
  rtp::StatsReport r;
  s->GetStats(now, &r);
  if (pc::JsepTransport* t = ep_->media_transport(); t && t->rtp_dtls_transport()) {
    pc::GetConnectionStats(now, *t->rtp_dtls_transport(), &r);
  }
  CallStats c;
  c.connect_ms = connect_ms_;
  c.duration_s = (now - start_).seconds();
  c.rtt_ms = r.transport.round_trip_time.IsZero() ? -1 : r.transport.round_trip_time.ms_f();
  c.target_kbps = r.transport.target_bitrate.kbps();
  for (const auto& o : r.outbound) {
    if (!o.video) c.audio_packets_sent += o.packets_sent;
  }
  // Received audio: the subscriber connection's with LiveKit.
  rtp::StatsReport rx_report;
  const rtp::StatsReport* rr = &r;
  if (lk_) {
    if (sub_ && sub_->session()) sub_->session()->GetStats(now, &rx_report);
    rr = &rx_report;
  }
  // In a group, the call's audio figures are the most-played stream's (packets: all streams'); each participant's are
  // in livekit_group.
  std::map<std::string, std::string> mid_identity;
  if (lk_ && sub_) {
    for (const pc::RemoteTrack& t : sub_->remote_tracks()) mid_identity[t.mid] = TrackIdentity(t.track_id, t.stream_ids);
    lk_->stats.clear();
  }
  for (const auto& in : rr->inbound) {
    if (in.video) continue;
    c.audio_packets_received += in.packets_received;
    c.audio_packets_lost += in.packets_lost;
    const double played =
        static_cast<double>(in.total_samples_received - std::min(in.concealed_samples, in.total_samples_received)) / kRate;
    const double concealed = in.total_samples_received > 0 ? 100.0 * in.concealed_samples / in.total_samples_received : 0;
    if (played >= c.audio_played_s) {
      c.audio_played_s = played;
      c.audio_jitter_ms = in.jitter_ms;
      c.audio_concealed_pct = concealed;
      if (in.jitter_buffer_emitted_count > 0) c.jitter_buffer_ms = in.jitter_buffer_delay_ms / in.jitter_buffer_emitted_count;
    }
    if (lk_) {
      const auto who = mid_identity.find(in.mid);
      if (who == mid_identity.end() || who->second.empty()) continue;
      GroupParticipant& p = lk_->stats[who->second];
      p.audio_packets_received += in.packets_received;
      p.audio_packets_lost += in.packets_lost;
      p.audio_jitter_ms = in.jitter_ms;
      p.audio_concealed_pct = concealed;
      p.audio_played_s += played;
    }
  }
  if (lk_) {
    video_->UpdateRates();
    std::lock_guard<std::mutex> lock(video_->mu);
    for (const auto& [mid, t] : video_->tracks) {
      if (t->identity.empty()) continue;
      GroupParticipant& p = lk_->stats[t->identity];
      p.video_frames_received += t->received;
      p.video_frames_decoded += t->decoded;
      p.video_width = t->w;
      p.video_height = t->h;
      p.video_fps += t->fps;
    }
  }
  if (!r.ice_transports.empty()) {
    const auto& t = r.ice_transports.front();
    c.dtls_cipher = t.dtls_cipher.value_or("");
    c.srtp_cipher = t.srtp_cipher.value_or("");
    auto describe = [&](const std::string& id) {
      for (const auto& cand : r.candidates) {
        if (cand.id != id) continue;
        std::string d = cand.candidate_type + " " + cand.protocol;
        if (!cand.relay_protocol.empty()) d += " via " + cand.relay_protocol;
        return d + " " + cand.address + ":" + std::to_string(cand.port);
      }
      return std::string();
    };
    for (const auto& p : r.candidate_pairs) {
      if (p.id != t.selected_candidate_pair_id) continue;
      c.local_candidate = describe(p.local_candidate_id);
      c.remote_candidate = describe(p.remote_candidate_id);
      for (const auto& cand : r.candidates) {
        if (cand.id == p.local_candidate_id) {
          c.local_type = cand.candidate_type;
          c.protocol = cand.protocol;
          c.relay_protocol = cand.relay_protocol;
        } else if (cand.id == p.remote_candidate_id) {
          c.remote_type = cand.candidate_type;
        }
      }
      if (p.current_round_trip_time && c.rtt_ms < 0) c.rtt_ms = *p.current_round_trip_time * 1000;
      c.send_kbps = (p.bytes_sent - std::min(p.bytes_sent, last_pair_bytes_sent_)) * 8 / 1000.0;
      c.recv_kbps = (p.bytes_received - std::min(p.bytes_received, last_pair_bytes_received_)) * 8 / 1000.0;
      last_pair_bytes_sent_ = p.bytes_sent;
      last_pair_bytes_received_ = p.bytes_received;
    }
  }
  {
    std::lock_guard<std::mutex> lock(video_->mu);
    c.video_frames_sent = video_->sent;
    c.video_frames_received = video_->received;
    c.video_frames_decoded = video_->decoded;
    c.video_frames_rendered = video_->rendered;
    c.video_send_width = video_->send_w;
    c.video_send_height = video_->send_h;
    c.video_recv_width = video_->recv_w;
    c.video_recv_height = video_->recv_h;
    c.video_send_rotation = static_cast<int>(video_->send_rotation) * 90;
    c.video_recv_rotation = static_cast<int>(video_->recv_rotation) * 90;
  }
  c.capture_level = capture_level_;
  c.agc_gain_db = apm_ ? apm_->agc_gain_db() : 0;
  c.capture_chain = apm_ ? apm_->chain() : "off";
  c.opus_kbps = encoder_->target_bitrate_bps() / 1000.0;
  c.audio_allocation_kbps = audio_allocation_kbps_;
  if (lk_) {
    c.livekit_room = lk_->room;
    c.livekit_participants = static_cast<int>(lk_->others.size());
    c.livekit_published = static_cast<int>(lk_->published.size());
    c.livekit_subscriber = lk_->subscriber + (lk_->subscriber_connected ? ", connected" : "");
    c.livekit_signal_rtt_ms = lk_->rtt_ms;
    RefreshGroup(now);
    c.livekit_group = participants();
    c.livekit_active_speaker = active_speaker();
    c.livekit_identity = lk_->identity;
    c.livekit_speakers_seen = lk_->speakers_seen;
    c.livekit_joined = lk_->joined_after;
    c.livekit_left = lk_->left;
    if (lk_->got_speakers) {
      const auto me = lk_->speakers.find(lk_->sid);
      c.livekit_local_level = me == lk_->speakers.end() ? 0 : me->second.level;
    } else {
      c.livekit_local_level = capture_level_;
    }
  }
  std::lock_guard<std::mutex> lock(stats_mu_);
  stats_ = std::move(c);
}

}  // namespace sm::apps
