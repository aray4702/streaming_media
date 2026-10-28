// The test bench's model of an H.264 encoder's QP and encode time (issue #84,
// step 1). Both call implementations use it: testbench/call.cc for this
// library and compare/webrtc/webrtc_call.cc's ModelEncoder for libwebrtc, so a
// quality scaler or CPU overuse detector on either side reads the same inputs
// for the same frames.
//
// The library carries no codec. libwebrtc's adaptation reads two things from
// the encoder per frame (compare/webrtc/reference/ at M157):
//   video/video_stream_encoder.cc  OnEncodedImage: EncodedImage::qp_ goes to
//       QualityScalerResource::OnEncodeCompleted -> QualityScaler::ReportQp
//   video/video_stream_encoder.cc  RunPostEncode: encode_duration_us is
//       timing_.encode_finish_ms - timing_.encode_start_ms when timing_ is set,
//       and goes to EncodeUsageResource -> OveruseFrameDetector::FrameSent.
//       The default SendProcessingUsage1 (filter_time_ms 0,
//       video/adaptation/overuse_frame_detector.cc) ignores that argument and
//       measures capture -> encoded callback on the clock instead, so driving
//       it needs the encoder to take the time; that is the CPU overuse step.
// This header models both. The frame sizes stay the existing model (the
// allocated rate per frame, +-20%, keyframes 4x; testbench/call.cc), so adding
// it changes no frame and no packet.
//
// QP. A rate-QP model of the usual form: QP rises by 6 when the bits per frame
// halve (H.264's quantizer step doubles every 6 QP), relative to a reference
// rate at which the encoder sits at QP 30, between the H.264 scaling
// thresholds 24/37 (video::QpThresholds::ForCodec, the values #267's
// sm_video_compare gives libwebrtc's QualityScaler; their source,
// modules/video_coding/codecs/h264/h264_encoder_impl.cc, is not in
// compare/webrtc/reference/ yet, #57):
//   qp = 30 + 6 * log2(reference_bits * complexity / frame_bits)
// rounded to the nearest integer and clamped to H.264's 0..51. A keyframe's
// bits are divided by the size model's keyframe factor 4, so a keyframe has
// the QP of a delta frame at the same rate. The reference is libwebrtc's own
// resolution/bitrate table (video/adaptation/bitrate_constraint_unittest.cc,
// EncoderInfo::resolution_bitrate_limits: 360p starts at 500 kbps, 720p at
// 1000 kbps), at 30 fps: 33 333 bits per 720p frame, scaled with the square
// root of the pixel count, which puts 360p's 500 kbps on the same QP.
// `complexity` (scenario `video_complexity`, default 1) scales the content:
// 2 means the picture needs twice the bits for the same QP.
//
// Encode time. Linear in the pixel count: 10 ms for a 720p frame (a software
// encoder at 30% of a 30 fps frame interval, under OveruseFrameDetector's
// 42% underuse threshold), divided by `speed` (scenario `encoder_speed`,
// default 1; 0.3 makes a 720p frame take 33 ms, 100% of the interval). It is
// reported, not simulated: both implementations still hand the frame to the
// sender at its capture time, as before (sm_compare sets it in timing_).
//
// Encoder behaviours (issue #466), off by default. A real encoder misses its
// rate, which is what libwebrtc's EncoderOvershootDetector,
// EncoderBitrateAdjuster, RateUtilizationTracker and FrameDropper react to
// (compare/webrtc/reference/video/encoder_bitrate_adjuster.cc,
// encoder_overshoot_detector.cc, rate_utilization_tracker.cc,
// modules/video_coding/utility/frame_dropper.cc). DeltaSizeFactor scales a
// delta frame's size; keyframes keep the size model's 4x:
//   overshoot  (scenario `encoder_overshoot <x>`, default 1): every delta
//              frame, for example 1.3 for a hot encoder or 0.7 for one that
//              undershoots;
//   scene changes (scenario `scene_change <interval> <frames> <factor>`):
//              the first `frames` frame intervals of every `interval` after
//              the first are scene cuts, their delta frames `factor` x.
// The factor depends only on the frame's capture time since the call's start,
// which both implementations share, and draws nothing from the size model's
// random stream, so both stacks get the same frames for a seed and a factor of
// 1 changes no frame.
//
// Listed as a deviation in tools/catalog.py (the model stands in for a codec).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sm::testbench {

struct EncoderModel {
  static constexpr int kMinQp = 0;
  static constexpr int kMaxQp = 51;             // H.264.
  static constexpr int kReferenceQp = 30;
  static constexpr double kQpPerDoubling = 6;   // H.264's quantizer step doubles every 6 QP.
  static constexpr double kKeyframeSizeFactor = 4;  // The size model's keyframe factor.
  static constexpr int kReferencePixels = 1280 * 720;
  static constexpr double kReferenceBitsPerFrame = 1000000.0 / 30;  // 720p, 1000 kbps, 30 fps.
  static constexpr int64_t kReferenceEncodeUs = 10000;               // 720p, speed 1.

  // Reference bits per frame at `pixels`: QP kReferenceQp there.
  static double ReferenceBits(int pixels) {
    return kReferenceBitsPerFrame * std::sqrt(static_cast<double>(pixels) / kReferencePixels);
  }

  static int Qp(int64_t bytes, bool keyframe, int width, int height, double complexity = 1) {
    const int pixels = std::max(width * height, 1);
    double bits = static_cast<double>(std::max<int64_t>(bytes, 1)) * 8;
    if (keyframe) bits /= kKeyframeSizeFactor;
    const double qp = kReferenceQp + kQpPerDoubling * std::log2(ReferenceBits(pixels) * complexity / bits);
    return std::clamp(static_cast<int>(std::lround(qp)), kMinQp, kMaxQp);
  }

  struct Behaviour {
    double overshoot = 1;            // `encoder_overshoot`.
    int64_t scene_interval_ms = 0;   // `scene_change`: 0 = off.
    int scene_frames = 0;
    double scene_factor = 1;
  };

  // Factor on a delta frame's size captured `capture_ms` after the call's start.
  static double DeltaSizeFactor(const Behaviour& b, int64_t capture_ms, int fps) {
    double f = b.overshoot;
    if (b.scene_interval_ms > 0 && capture_ms >= b.scene_interval_ms &&
        (capture_ms % b.scene_interval_ms) * fps < static_cast<int64_t>(b.scene_frames) * 1000)
      f *= b.scene_factor;
    return f;
  }

  static int64_t EncodeTimeUs(int width, int height, double speed = 1) {
    const double us = static_cast<double>(kReferenceEncodeUs) * width * height / kReferencePixels / speed;
    return std::max<int64_t>(static_cast<int64_t>(std::lround(us)), 1);
  }
};

}  // namespace sm::testbench
