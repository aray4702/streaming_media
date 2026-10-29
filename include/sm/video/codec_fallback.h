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
// loss-notification input).
//
// Decoder side: DecoderFallbackController ports
// api/video_codecs/video_decoder_software_fallback_wrapper.cc (below).
#pragma once

#include <cstddef>
#include <cstdint>
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

// ---------------------------------------------------------------------------
// Decoder fallback: a port of libwebrtc's VideoDecoderSoftwareFallbackWrapper
// (api/video_codecs/video_decoder_software_fallback_wrapper.cc). As on the
// encoder side, the app runs both decoders (`FallbackDecoder`) and the wrapper
// decides which one decodes.
//
// Configure tries the main (hardware) decoder, then the software decoder if
// it fails; with the forced fallback (field trial
// WebRTC-Video-ForcedSwDecoderFallback, off by default) only the software
// decoder. A decode on the main decoder that returns kFallbackSoftware moves
// to the software decoder, releases the main one, and the same frame is
// decoded again by the software decoder. kError counts only on keyframes
// (generic hardware errors are expected, and the keyframe request should fix
// them); the 4th consecutive one (kMaxConsequtiveHwErrors) also moves to the
// software decoder. Any other result counts as a decoded frame and resets the
// error count. Nothing switches back: the state lasts until Release(), and
// the next Configure tries the main decoder again. Each fallback records the
// main decoder's frames so far (libwebrtc's histogram
// WebRTC.Video.HardwareDecodedFramesBetweenSoftwareFallbacks.<codec>; like
// libwebrtc, the count is never reset). Not ported: the decode-complete
// callback, which the app handles itself.

// The WEBRTC_VIDEO_CODEC_* results the decoder wrapper tells apart.
// kOther is any other code (e.g. WEBRTC_VIDEO_CODEC_ERR_PARAMETER), which
// libwebrtc counts as a decoded frame.
enum class DecoderStatus { kOk, kError, kFallbackSoftware, kUninitialized, kOther };

// VideoCodecType, the values libwebrtc's histogram names.
enum class DecoderCodecType { kGeneric, kVP8, kVP9, kAV1, kH264, kH265 };

// VideoDecoder::Settings, the field the wrapper reads.
struct DecoderSettings {
  DecoderCodecType codec_type = DecoderCodecType::kGeneric;
};

// VideoDecoder::DecoderInfo.
struct FallbackDecoderInfo {
  std::string implementation_name;
  bool is_hardware_accelerated = false;
};

// One of the app's decoders, as the wrapper drives it.
class FallbackDecoder {
 public:
  virtual ~FallbackDecoder() = default;
  virtual bool Configure(const DecoderSettings& settings) = 0;
  virtual DecoderStatus Release() = 0;
  virtual FallbackDecoderInfo GetDecoderInfo() const = 0;
};

struct DecoderFallbackConfig {
  // WebRTC-Video-ForcedSwDecoderFallback; off as in libwebrtc.
  bool force_sw_decoder_fallback = false;
  EventSink* events = nullptr;
};

struct DecoderFallbackStats {
  uint64_t frames = 0, errors = 0, fallbacks = 0;
  // The histogram sample of the last fallback.
  int32_t hw_decoded_frames_at_last_fallback = 0;
};

class DecoderFallbackController : public MetricsSource {
 public:
  static constexpr size_t kMaxConsequtiveHwErrors = 4;

  enum class DecoderType { kNone, kHardware, kFallback };

  // `sw` is the software fallback, `hw` the main decoder; both outlive this.
  DecoderFallbackController(DecoderFallbackConfig cfg, FallbackDecoder& sw, FallbackDecoder& hw);

  bool Configure(const DecoderSettings& settings, Timestamp now);
  DecoderStatus Release();

  // The decoder to decode the next frame with; nullptr before Configure
  // (libwebrtc's Decode returns WEBRTC_VIDEO_CODEC_UNINITIALIZED then).
  FallbackDecoder* current();
  // Report the result of decoding a frame with current(). Returns true when
  // the main decoder's result moved the call to the software decoder: the app
  // then decodes the same frame with current() (the fallback) and keeps that
  // result. Otherwise the app keeps `status` as the frame's result.
  bool OnDecodeResult(DecoderStatus status, bool is_keyframe, Timestamp now);

  FallbackDecoderInfo GetDecoderInfo() const;
  std::string ImplementationName() const;

  DecoderType decoder_type() const { return decoder_type_; }
  const DecoderFallbackStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool InitHwDecoder();
  bool InitFallbackDecoder(Timestamp now);

  DecoderFallbackConfig cfg_;
  FallbackDecoder& fallback_;
  FallbackDecoder& hw_;
  DecoderType decoder_type_ = DecoderType::kNone;
  DecoderSettings settings_;
  const std::string fallback_implementation_name_;
  int32_t hw_decoded_frames_since_last_fallback_ = 0;
  size_t hw_consequtive_generic_errors_ = 0;
  DecoderFallbackStats stats_;
};

}  // namespace sm::video
