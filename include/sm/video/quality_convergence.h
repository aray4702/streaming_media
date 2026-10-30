// 7.4 Quality convergence of a static screen-share stream.
//
// libwebrtc M157's QualityConvergenceMonitor and QualityConvergenceController
// (video/quality_convergence_monitor.cc, video/quality_convergence_controller.cc
// in compare/webrtc/reference/, issue #878).
//
// VideoStreamEncoder feeds every encoded frame's QP into the controller
// (video_stream_encoder.cc:2316-2325) and passes the answer to the frame
// cadence adapter (frame_cadence_adapter.h): once every layer is at target
// quality, zero-hertz mode slows its repeats of a static frame from the frame
// rate to one per second.
//
// A layer is at target quality when (QualityConvergenceMonitor::AddSample):
//   0. the QP is at or below the static threshold (VP8 15, VP9 32, AV1 60, or
//      the encoder's min QP if higher; -1, never, for H.264, H.265 and
//      generic), or
//   1. dynamic detection is on (VP8, VP9, AV1) and, over refresh frames only,
//      the mean QP of the older samples (up to 6) is at or below
//      static + 0.06 * max QP (127 VP8, 255 VP9/AV1) and at or below the mean
//      of the 6 most recent samples. A non-refresh frame clears the window.
// The field-trial overrides (WebRTC-QCM-Static-*, WebRTC-QCM-Dynamic-*) are
// not ported; their defaults are.
#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

#include "sm/video/video_stream_adapter.h"

namespace sm::video {

class QualityConvergenceMonitor {
 public:
  struct Parameters {
    int static_qp_threshold = 0;
    bool dynamic_detection_enabled = false;
    size_t recent_window_length = 0;
    size_t past_window_length = 0;
    int dynamic_qp_threshold = 0;
  };

  explicit QualityConvergenceMonitor(const Parameters& params) : params_(params) {}
  // GetParameters (quality_convergence_monitor.cc:39-80) with the default
  // field trials.
  static Parameters DefaultParameters(int static_qp_threshold, VideoCodecType codec);

  // `is_refresh_frame`: the frame repeats the previous one (a zero-hertz
  // repeat), sent to improve its quality.
  void AddSample(int qp, bool is_refresh_frame);
  bool AtTargetQuality() const { return at_target_quality_; }
  const Parameters& parameters() const { return params_; }

 private:
  Parameters params_;
  bool at_target_quality_ = false;
  std::deque<int> qp_window_;
};

class QualityConvergenceController {
 public:
  // GetDefaultStaticQpThreshold, raised to `encoder_min_qp` if that is higher.
  void Initialize(int number_of_layers, std::optional<int> encoder_min_qp, VideoCodecType codec);
  bool AddSampleAndCheckTargetQuality(int layer_index, int qp, bool is_refresh_frame);
  static int DefaultStaticQpThreshold(VideoCodecType codec);

 private:
  bool initialized_ = false;
  int number_of_layers_ = 0;
  std::vector<QualityConvergenceMonitor> convergence_monitors_;
};

}  // namespace sm::video
