// The calling app's call controller (C14, #1116); see call_client.h.
#include "call_client.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>

#include "ai_server.h"
#include "json.h"
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
  video::VideoToolboxH264Decoder decoder;
  bool encoder_ready = false, decoder_ready = false;
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

  void Receive(pc::PeerEndpoint& ep, Timestamp now) {
    rtp::Session* s = ep.session();
    if (!ep.video_receive()) return;
    for (rtp::ReceivedVideoFrame& f : s->TakeVideoFrames(*ep.video_receive())) {
      ++received;
#if SM_CALL_VIDEO
      if (f.bitstream.empty()) continue;
      if (!decoder_ready) {
        video::DecoderSettings settings;
        settings.codec_type = video::DecoderCodecType::kH264;
        decoder_ready = decoder.Configure(settings);
      }
      video::I420Frame out;
      if (decoder.Decode(f.bitstream, now, out) != video::DecoderStatus::kOk) {
        s->OnDecodeFailure(*ep.video_receive(), now);  // A keyframe request.
        continue;
      }
      ++decoded;
      if (!f.render) continue;
      ++rendered;
      // The renderer shows the frame turned by the rotation it arrived with (RTCMTLRenderer's rotation).
      if (f.rotation != video::Rotation::k0) out = video::ApplyOrientation(out, video::VideoOrientation{f.rotation});
      std::lock_guard<std::mutex> lock(mu);
      recv_rotation = f.rotation;
      recv_w = out.width;
      recv_h = out.height;
      remote = std::move(out);
#else
      (void)f;
      (void)now;
#endif
    }
  }
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
  return buf;
}

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
         ",\"ended_reason\":" + net::JsonEscape(ended_reason) + "}";
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
}

CallClient::~CallClient() {
  ep_.reset();  // Before the allocator and the sockets it uses.
  allocator_.reset();
}

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
  ep_.reset();
  allocator_.reset();
}

void CallClient::Hangup(Timestamp now) {
  if (state_ == CallState::kEnded) return;
  if (!remote_.empty()) SendSignal("{\"type\":\"bye\"," + To() + "}");
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
  c.audio = true;
  c.video = true;  // Received from the web app even with our camera off; the AI server offers audio only.
  c.enable_data_channels = false;
  c.audio_decoder = decoder_.get();
  // The allocator registration: the Opus range plus the overhead at 20 ms frames (issue #569's path).
  c.audio_stream.min_bitrate = DataRate::BitsPerSec(config_.opus_min_bitrate_bps);
  c.audio_stream.max_bitrate = DataRate::BitsPerSec(std::max(config_.opus_min_bitrate_bps, config_.opus_max_bitrate_bps));
  ep_ = std::make_unique<pc::PeerEndpoint>(std::move(c), now);
  ep_->SetOnIceCandidate([this](std::unique_ptr<pc::IceCandidate> cand) {
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
  SendSignal("{\"type\":\"offer\"," + To() + ",\"sdp\":" + net::JsonEscape(pc::SdpSerialize(*offer)) + "}");
  if (!ep_->SetLocalDescription(std::move(offer), now, &error)) End("SetLocalDescription failed: " + error, now);
}

void CallClient::OnSignal(const std::string& text, Timestamp now) {
  if (state_ == CallState::kEnded) return;
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
    video_->Receive(*ep_, now);
    ep_->FlushOutgoing(now);
    if (now >= next_stats_) {
      UpdateStats(now);
      next_stats_ = now + TimeDelta::Seconds(1);
    }
  }
  return std::min(ep_->NextProcessTime(now), now + TimeDelta::Millis(5));
}

void CallClient::Wait(Timestamp now, TimeDelta wait) {
  sockets_.Poll(now, std::clamp(wait, TimeDelta::Zero(), TimeDelta::Millis(5)));
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
    if (ep_->audio_receive()) s->GetAudio(*ep_->audio_receive(), pcm, now);
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
  for (const auto& in : r.inbound) {
    if (in.video) continue;
    c.audio_packets_received += in.packets_received;
    c.audio_packets_lost += in.packets_lost;
    c.audio_jitter_ms = in.jitter_ms;
    c.audio_played_s = static_cast<double>(in.total_samples_received - std::min(in.concealed_samples, in.total_samples_received)) / kRate;
    if (in.total_samples_received > 0) c.audio_concealed_pct = 100.0 * in.concealed_samples / in.total_samples_received;
    if (in.jitter_buffer_emitted_count > 0) c.jitter_buffer_ms = in.jitter_buffer_delay_ms / in.jitter_buffer_emitted_count;
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
  std::lock_guard<std::mutex> lock(stats_mu_);
  stats_ = std::move(c);
}

}  // namespace sm::apps
