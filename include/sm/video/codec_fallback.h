// 7.2 Hardware -> software encoder fallback.
//
// A port of libwebrtc's VideoEncoderSoftwareFallbackWrapper
// (api/video_codecs/video_encoder_software_fallback_wrapper.cc). The app runs
// the codecs (program.md 0.1), so it hands this wrapper its two encoders
// through `FallbackEncoder`, and the wrapper decides which one is used, as
// libwebrtc's does.
//
// InitEncode tries the forced fallback first (below), then the main
// (hardware) encoder. If the main encoder fails, other than with
// kSimulcastParametersNotSupported, the software encoder is initialized
// instead; if that fails too, the main encoder's error is returned. An encode
// that returns kFallbackSoftware from the main encoder moves to the software
// encoder, releases the main one, and the same frame is encoded again by the
// software encoder (EncodeWithMainEncoder). Any other error is returned as
// is: there is no error counting and no error rate. Nothing switches back on
// a timer: the state lasts until Release(), and the next InitEncode tries the
// main encoder again. On every switch the new encoder gets the last rates,
// RTT and packet loss (PrimeEncoder). A freshly initialized encoder starts
// with a keyframe, so the wrapper requests none.
//
// Forced fallback: the field trial WebRTC-Video-EncoderFallbackSettings
// (`resolution_threshold_px`), or, without it,
// WebRTC-VP8-Forced-Fallback-Encoder-v2 ("Enabled-<min_pixels>,<max_pixels>,
// <min_bps>", VP8 single stream only), uses the software encoder for every
// resolution up to `max_pixels` (default 320x240). Both are off by default,
// as in libwebrtc. GetEncoderInfo merges the two encoders' info as libwebrtc
// does.
//
// Not ported: the temporal-layer based forced switch
// (`prefer_temporal_support`), because temporal layers are removed (#572),
// and the OnLossNotification replay (the library has no encoder
// loss-notification input). Decoder fallback
// (video_decoder_software_fallback_wrapper.cc) is not in
// compare/webrtc/reference/ and is not provided.
#pragma once

#include <optional>
#include <string>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::video {

// The WEBRTC_VIDEO_CODEC_* results the wrapper tells apart.
enum class EncoderStatus { kOk, kError, kFallbackSoftware, kSimulcastParametersNotSupported };

// VideoCodec, the fields the wrapper reads.
struct EncoderCodecSettings {
  int width = 0;
  int height = 0;
  bool vp8 = false;
  int simulcast_streams = 1;
};

// VideoEncoder::EncoderInfo, the fields the wrapper reads or merges.
struct FallbackEncoderInfo {
  int requested_resolution_alignment = 1;
  bool apply_alignment_to_all_simulcast_layers = false;
  std::optional<size_t> max_pixels_per_frame;
  int min_pixels_per_frame = 320 * 180;  // ScalingSettings, kDefaultMinPixelsPerFrame.
  bool supports_native_handle = false;
};

struct EncoderRates {
  DataRate bitrate = DataRate::Zero();
  double framerate_fps = 0;
};

// One of the app's encoders, as the wrapper drives it.
class FallbackEncoder {
 public:
  virtual ~FallbackEncoder() = default;
  virtual EncoderStatus InitEncode(const EncoderCodecSettings& settings) = 0;
  virtual void Release() = 0;
  virtual void SetRates(const EncoderRates& rates) = 0;
  virtual void OnRttUpdate(TimeDelta rtt) = 0;
  virtual void OnPacketLossRateUpdate(float packet_loss_rate) = 0;
  virtual FallbackEncoderInfo GetEncoderInfo() const = 0;
};

struct CodecFallbackConfig {
  // WebRTC-Video-EncoderFallbackSettings/resolution_threshold_px:N/; unset is off.
  std::optional<int> resolution_threshold_px;
  // WebRTC-VP8-Forced-Fallback-Encoder-v2's value; off unless it starts with "Enabled".
  std::string vp8_forced_fallback;
  EventSink* events = nullptr;
};

// libwebrtc's ForcedFallbackParams (resolution-based switch only).
struct ForcedFallbackParams {
  bool vp8_specific_resolution_switch = false;
  int min_pixels = 320 * 180;
  int max_pixels = 320 * 240;
  bool SupportsResolutionBasedSwitch(const EncoderCodecSettings& codec) const;
};

// ParseFallbackParamsFromFieldTrials; `main_info` is the main encoder's info.
std::optional<ForcedFallbackParams> ParseForcedFallbackParams(const CodecFallbackConfig& cfg,
                                                              const FallbackEncoderInfo& main_info);

enum class EncoderState { kUninitialized, kMainEncoderUsed, kFallbackDueToFailure, kForcedFallback };

struct CodecFallbackStats {
  uint64_t frames = 0, errors = 0, fallbacks = 0, forced_fallbacks = 0;
};

class CodecFallbackController : public MetricsSource {
 public:
  // `sw` is the software fallback, `hw` the main encoder; both outlive this.
  CodecFallbackController(CodecFallbackConfig cfg, FallbackEncoder& sw, FallbackEncoder& hw);

  EncoderStatus InitEncode(const EncoderCodecSettings& settings, Timestamp now);
  void Release();

  // The encoder to encode the next frame with; nullptr while uninitialized
  // (libwebrtc's Encode returns WEBRTC_VIDEO_CODEC_ERROR then).
  FallbackEncoder* current();
  // Report the result of encoding a frame with current(). Returns true when
  // the main encoder asked for the software fallback and it initialized: the
  // app then encodes the same frame with current() (the fallback), converted
  // to I420 at the codec size unless the fallback supports native handles.
  // Otherwise the app keeps `status` as the frame's result.
  bool OnEncodeResult(EncoderStatus status, Timestamp now);

  void SetRates(const EncoderRates& rates);
  void OnRttUpdate(TimeDelta rtt);
  void OnPacketLossRateUpdate(float packet_loss_rate);
  FallbackEncoderInfo GetEncoderInfo() const;

  EncoderState state() const { return state_; }
  bool IsFallbackActive() const {
    return state_ == EncoderState::kForcedFallback || state_ == EncoderState::kFallbackDueToFailure;
  }
  const std::optional<ForcedFallbackParams>& forced_params() const { return forced_; }
  const CodecFallbackStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool InitFallbackEncoder(bool is_forced, Timestamp now);
  bool TryInitForcedFallbackEncoder(Timestamp now);
  void PrimeEncoder(FallbackEncoder& encoder) const;

  CodecFallbackConfig cfg_;
  FallbackEncoder& fallback_;
  FallbackEncoder& main_;
  std::optional<ForcedFallbackParams> forced_;
  EncoderState state_ = EncoderState::kUninitialized;
  EncoderCodecSettings codec_;
  std::optional<EncoderRates> rates_;
  std::optional<TimeDelta> rtt_;
  std::optional<float> packet_loss_;
  CodecFallbackStats stats_;
};

}  // namespace sm::video
