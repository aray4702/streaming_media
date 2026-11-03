// Hardware H.264 through Apple's VideoToolbox, for macOS and iOS (C11 part 1,
// issue #1104). Built when SM_WITH_VIDEOTOOLBOX is on (the default on Apple
// platforms) as sm::videotoolbox; it links the system VideoToolbox, CoreMedia,
// CoreVideo and CoreFoundation frameworks and nothing else.
//
// A port of libwebrtc's RTCVideoEncoderH264 / RTCVideoDecoderH264
// (sdk/objc/components/video_codec/) and their Annex B <-> AVCC helpers
// (nalu_rewriter.cc), in C++ on the frameworks' C API. Those files are not in
// compare/webrtc/reference/ yet; the settings below follow upstream M157 and
// are to be re-checked once they are added.
//
// Encoder (VTCompressionSession), as RTCVideoEncoderH264:
//  - H.264 Constrained Baseline at the level of profile-level-id 42e01f
//    (level 3.1, kVTProfileLevel_H264_Baseline_3_1), hardware encoder
//    enabled (not required) on macOS;
//  - kVTCompressionPropertyKey_RealTime on, AllowFrameReordering off,
//    MaxKeyFrameInterval 7200 frames, MaxKeyFrameIntervalDuration 240 s;
//  - AverageBitRate = the BitrateAdjuster's adjusted rate (min 0.5, max 0.95
//    of the target), DataRateLimits = 1.5x that rate over 1 s, and
//    ExpectedFrameRate, set again whenever the rate or frame rate changes;
//  - input I420 copied into an NV12 (full range) pixel buffer from the
//    session's pool; a frame of another size restarts the session;
//  - a keyframe on request (kVTEncodeFrameOptionKey_ForceKeyFrame);
//  - Annex B output with 4-byte start codes, the SPS and PPS before the
//    slices of every keyframe.
// Differences, because the library starts no threads and reads no clock: an
// Encode returns its access unit before it returns
// (VTCompressionSessionCompleteFrames after each frame), and the
// BitrateAdjuster runs on the caller's `now`. Not ported: the QP read-out
// (H264BitstreamParser) for the quality scaler, and the iOS background
// handling.
//
// Decoder (VTDecompressionSession), as RTCVideoDecoderH264: an access unit
// that carries an SPS and PPS makes a format description; a new one
// (CMFormatDescriptionEqual false) restarts the session. Without one, a frame
// can't be decoded (kError, the caller asks for a keyframe). The SPS and PPS
// are taken out and the rest of the units are passed in AVCC form (4-byte
// lengths). Output is NV12 full range, converted to I420. Difference: decoding
// is synchronous (no kVTDecodeFrame_EnableAsynchronousDecompression), so the
// frame is ready when Decode returns.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/video/codec_fallback.h"
#include "sm/video/frame.h"

namespace sm::video {

struct H264EncodedFrame {
  std::vector<uint8_t> annexb;  // One access unit, 4-byte start codes.
  bool keyframe = false;
  uint32_t rtp_timestamp = 0;
  int width = 0;
  int height = 0;
};

struct VideoToolboxEncoderConfig {
  // VideoCodec::startBitrate and maxFramerate, used by InitEncode until SetRates.
  DataRate start_bitrate = DataRate::KilobitsPerSec(300);
  double max_framerate = 30;
  EventSink* events = nullptr;
};

struct VideoToolboxEncoderStats {
  uint64_t frames_in = 0, frames_out = 0, keyframes = 0, dropped = 0, errors = 0, bytes_out = 0;
  uint64_t session_restarts = 0;
};

class VideoToolboxH264Encoder : public FallbackEncoder, public MetricsSource {
 public:
  explicit VideoToolboxH264Encoder(VideoToolboxEncoderConfig cfg = {});
  ~VideoToolboxH264Encoder() override;
  VideoToolboxH264Encoder(const VideoToolboxH264Encoder&) = delete;
  VideoToolboxH264Encoder& operator=(const VideoToolboxH264Encoder&) = delete;

  // FallbackEncoder.
  EncoderStatus InitEncode(const EncoderCodecSettings& settings) override;
  void Release() override;
  // The target rate goes through the BitrateAdjuster; the session gets the
  // adjusted rate.
  void SetRates(const EncoderRates& rates) override;
  void OnRttUpdate(TimeDelta rtt) override {}
  void OnPacketLossRateUpdate(float packet_loss_rate) override {}
  FallbackEncoderInfo GetEncoderInfo() const override;

  // Encodes `frame`. `out` gets the access unit, or nothing when the encoder
  // dropped the frame. kError before InitEncode or on a session failure.
  EncoderStatus Encode(const I420Frame& frame, uint32_t rtp_timestamp, bool request_keyframe, Timestamp now,
                       std::optional<H264EncodedFrame>& out);

  // The rate the session was last given (the BitrateAdjuster's adjusted rate).
  DataRate encoder_bitrate() const;
  const VideoToolboxEncoderStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

  struct Impl;

 private:
  VideoToolboxEncoderConfig cfg_;
  VideoToolboxEncoderStats stats_;
  std::unique_ptr<Impl> impl_;
};

struct VideoToolboxDecoderStats {
  uint64_t frames_in = 0, frames_out = 0, errors = 0, format_changes = 0;
};

class VideoToolboxH264Decoder : public FallbackDecoder, public MetricsSource {
 public:
  explicit VideoToolboxH264Decoder(EventSink* events = nullptr);
  ~VideoToolboxH264Decoder() override;
  VideoToolboxH264Decoder(const VideoToolboxH264Decoder&) = delete;
  VideoToolboxH264Decoder& operator=(const VideoToolboxH264Decoder&) = delete;

  // FallbackDecoder.
  bool Configure(const DecoderSettings& settings) override;
  DecoderStatus Release() override;
  FallbackDecoderInfo GetDecoderInfo() const override;

  // Decodes one Annex B access unit into `out`. kError when no SPS/PPS has
  // been seen yet, the unit can't be parsed, or VideoToolbox fails;
  // kUninitialized before Configure.
  DecoderStatus Decode(std::span<const uint8_t> annexb, Timestamp now, I420Frame& out);

  const VideoToolboxDecoderStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

  struct Impl;

 private:
  EventSink* events_;
  VideoToolboxDecoderStats stats_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sm::video
