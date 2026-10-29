#include "sm/video/codec_fallback.h"

#include <cstdio>
#include <numeric>

namespace sm::video {

// Line references are to api/video_codecs/video_encoder_software_fallback_wrapper.cc.

bool ForcedFallbackParams::SupportsResolutionBasedSwitch(const EncoderCodecSettings& codec) const {
  if (codec.width * codec.height > max_pixels) return false;  // :55-68
  if (vp8_specific_resolution_switch && (!codec.vp8 || codec.simulcast_streams > 1)) return false;
  return true;
}

std::optional<ForcedFallbackParams> ParseForcedFallbackParams(const CodecFallbackConfig& cfg,
                                                              const FallbackEncoderInfo& main_info) {
  // :85-127. WebRTC-Video-EncoderFallbackSettings wins over the VP8 trial.
  if (cfg.resolution_threshold_px) {
    ForcedFallbackParams p;
    p.max_pixels = *cfg.resolution_threshold_px;
    return p;
  }
  if (cfg.vp8_forced_fallback.rfind("Enabled", 0) != 0) return std::nullopt;
  int max_pixels_lower_bound = main_info.min_pixels_per_frame - 1;
  ForcedFallbackParams p;
  int min_bps = 0;
  if (std::sscanf(cfg.vp8_forced_fallback.c_str(), "Enabled-%d,%d,%d", &p.min_pixels, &p.max_pixels, &min_bps) != 3)
    return std::nullopt;
  if (p.min_pixels <= 0 || p.max_pixels < max_pixels_lower_bound || p.max_pixels < p.min_pixels || min_bps <= 0)
    return std::nullopt;
  p.vp8_specific_resolution_switch = true;
  return p;
}

CodecFallbackController::CodecFallbackController(CodecFallbackConfig cfg, FallbackEncoder& sw, FallbackEncoder& hw)
    : cfg_(std::move(cfg)), fallback_(sw), main_(hw), forced_(ParseForcedFallbackParams(cfg_, hw.GetEncoderInfo())) {}

FallbackEncoder* CodecFallbackController::current() {
  switch (state_) {
    case EncoderState::kUninitialized:
      return nullptr;
    case EncoderState::kMainEncoderUsed:
      return &main_;
    case EncoderState::kFallbackDueToFailure:
    case EncoderState::kForcedFallback:
      return &fallback_;
  }
  return nullptr;
}

void CodecFallbackController::PrimeEncoder(FallbackEncoder& encoder) const {
  // :250-270: replay rates and channel parameters.
  if (rates_) encoder.SetRates(*rates_);
  if (rtt_) encoder.OnRttUpdate(*rtt_);
  if (packet_loss_) encoder.OnPacketLossRateUpdate(*packet_loss_);
}

bool CodecFallbackController::InitFallbackEncoder(bool is_forced, Timestamp now) {
  // :272-302.
  if (fallback_.InitEncode(codec_) != EncoderStatus::kOk) {
    fallback_.Release();
    SM_EVENT(cfg_.events, now, "codec_fallback", "fallback_init_failed", is_forced ? 1 : 0, 0);
    return false;
  }
  // Switching to the fallback: release the main encoder; it may be
  // re-initialized by a later InitEncode.
  if (state_ == EncoderState::kMainEncoderUsed) main_.Release();
  state_ = is_forced ? EncoderState::kForcedFallback : EncoderState::kFallbackDueToFailure;
  ++(is_forced ? stats_.forced_fallbacks : stats_.fallbacks);
  SM_EVENT(cfg_.events, now, "codec_fallback", is_forced ? "forced" : "failure", 1, 0);
  return true;
}

bool CodecFallbackController::TryInitForcedFallbackEncoder(Timestamp now) {
  // :490-550, the resolution-based switch.
  if (!forced_ || !forced_->SupportsResolutionBasedSwitch(codec_)) return false;
  return InitFallbackEncoder(/*is_forced=*/true, now);
}

EncoderStatus CodecFallbackController::InitEncode(const EncoderCodecSettings& settings, Timestamp now) {
  // :314-362. Keep the settings for a later fallback; forget the rates.
  codec_ = settings;
  rates_.reset();
  if (TryInitForcedFallbackEncoder(now)) {
    PrimeEncoder(*current());
    return EncoderStatus::kOk;
  }
  EncoderStatus ret = main_.InitEncode(settings);
  if (ret == EncoderStatus::kOk) {
    state_ = EncoderState::kMainEncoderUsed;
    PrimeEncoder(main_);
    return ret;
  }
  if (ret == EncoderStatus::kSimulcastParametersNotSupported) return ret;
  if (InitFallbackEncoder(/*is_forced=*/false, now)) {
    PrimeEncoder(*current());
    return EncoderStatus::kOk;
  }
  // The software encoder failed too: the main encoder's error.
  state_ = EncoderState::kUninitialized;
  return ret;
}

void CodecFallbackController::Release() {
  // :370-377.
  if (FallbackEncoder* e = current()) e->Release();
  state_ = EncoderState::kUninitialized;
}

bool CodecFallbackController::OnEncodeResult(EncoderStatus status, Timestamp now) {
  ++stats_.frames;
  if (status != EncoderStatus::kOk) ++stats_.errors;
  // :395-434: only the main encoder's request moves to the fallback.
  if (state_ != EncoderState::kMainEncoderUsed || status != EncoderStatus::kFallbackSoftware) return false;
  if (!InitFallbackEncoder(/*is_forced=*/false, now)) return false;
  PrimeEncoder(fallback_);
  return true;
}

void CodecFallbackController::SetRates(const EncoderRates& rates) {
  rates_ = rates;
  // :181-195: before InitEncode the main encoder gets the calls.
  (current() ? *current() : main_).SetRates(rates);
}

void CodecFallbackController::OnRttUpdate(TimeDelta rtt) {
  rtt_ = rtt;
  (current() ? *current() : main_).OnRttUpdate(rtt);
}

void CodecFallbackController::OnPacketLossRateUpdate(float packet_loss_rate) {
  packet_loss_ = packet_loss_rate;
  (current() ? *current() : main_).OnPacketLossRateUpdate(packet_loss_rate);
}

FallbackEncoderInfo CodecFallbackController::GetEncoderInfo() const {
  // :459-483.
  FallbackEncoderInfo fb = fallback_.GetEncoderInfo();
  FallbackEncoderInfo main = main_.GetEncoderInfo();
  FallbackEncoderInfo info = IsFallbackActive() ? fb : main;
  info.requested_resolution_alignment = std::lcm(fb.requested_resolution_alignment, main.requested_resolution_alignment);
  info.apply_alignment_to_all_simulcast_layers =
      fb.apply_alignment_to_all_simulcast_layers || main.apply_alignment_to_all_simulcast_layers;
  // The fallback's limit always holds: frames the main encoder rejects go to it.
  info.max_pixels_per_frame = fb.max_pixels_per_frame;
  if (forced_ && forced_->vp8_specific_resolution_switch) info.min_pixels_per_frame = forced_->min_pixels;
  return info;
}

void CodecFallbackController::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("hardware", state_ == EncoderState::kMainEncoderUsed ? 1 : 0);
  w.Gauge("state", static_cast<int>(state_));
  w.Counter("frames", stats_.frames);
  w.Counter("errors", stats_.errors);
  w.Counter("fallbacks", stats_.fallbacks);
  w.Counter("forced_fallbacks", stats_.forced_fallbacks);
}

}  // namespace sm::video
