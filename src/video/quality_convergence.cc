#include "sm/video/quality_convergence.h"

#include <algorithm>
#include <numeric>

namespace sm::video {

namespace {
// quality_convergence_controller.cc:24-29.
constexpr int kVp8DefaultStaticQpThreshold = 15;
constexpr int kVp9DefaultStaticQpThreshold = 32;
constexpr int kAv1DefaultStaticQpThreshold = 60;
// quality_convergence_monitor.cc:24-26.
constexpr size_t kDefaultRecentWindowLength = 6;
constexpr size_t kDefaultPastWindowLength = 6;
constexpr float kDefaultAlpha = 0.06f;
}  // namespace

QualityConvergenceMonitor::Parameters QualityConvergenceMonitor::DefaultParameters(int static_qp_threshold,
                                                                                   VideoCodecType codec) {
  Parameters params;
  params.static_qp_threshold = static_qp_threshold;
  bool enabled = false;
  int max_qp = 0;
  switch (codec) {
    case VideoCodecType::kVP8:
      enabled = true;
      max_qp = 127;
      break;
    case VideoCodecType::kVP9:
    case VideoCodecType::kAV1:
      enabled = true;
      max_qp = 255;
      break;
    case VideoCodecType::kGeneric:
    case VideoCodecType::kH264:
    case VideoCodecType::kH265:
      break;
  }
  if (enabled) {
    params.dynamic_detection_enabled = true;
    // `static_qp_threshold + max_qp * dynamic_config.alpha` with alpha a
    // double there: the int truncation of the double sum.
    params.dynamic_qp_threshold =
        static_cast<int>(static_qp_threshold + max_qp * static_cast<double>(kDefaultAlpha));
    params.recent_window_length = kDefaultRecentWindowLength;
    params.past_window_length = kDefaultPastWindowLength;
  }
  return params;
}

// QualityConvergenceMonitor::AddSample (quality_convergence_monitor.cc:142-197).
void QualityConvergenceMonitor::AddSample(int qp, bool is_refresh_frame) {
  if (qp < 0) {  // Invalid QP.
    qp_window_.clear();
    at_target_quality_ = false;
    return;
  }
  // 0. Static threshold.
  if (qp <= params_.static_qp_threshold) {
    at_target_quality_ = true;
    return;
  }
  // 1. Not a refresh frame, or no dynamic detection.
  if (!is_refresh_frame || !params_.dynamic_detection_enabled) {
    qp_window_.clear();
    at_target_quality_ = false;
    return;
  }
  // 2. Already converged.
  if (at_target_quality_) return;
  // 3. QP history.
  qp_window_.push_back(qp);
  if (qp_window_.size() > params_.recent_window_length + params_.past_window_length) qp_window_.pop_front();
  // 4. Enough data.
  if (qp_window_.size() <= params_.recent_window_length) return;
  // 5. Means, as float from double sums.
  const auto recent_begin = qp_window_.end() - static_cast<std::ptrdiff_t>(params_.recent_window_length);
  float qp_past_average = static_cast<float>(std::accumulate(qp_window_.begin(), recent_begin, 0.0) /
                                             static_cast<double>(qp_window_.size() - params_.recent_window_length));
  float qp_recent_average = static_cast<float>(std::accumulate(recent_begin, qp_window_.end(), 0.0) /
                                               static_cast<double>(params_.recent_window_length));
  // 6. Convergence.
  if (qp_past_average <= static_cast<float>(params_.dynamic_qp_threshold) && qp_past_average <= qp_recent_average) {
    at_target_quality_ = true;
  }
}

int QualityConvergenceController::DefaultStaticQpThreshold(VideoCodecType codec) {
  switch (codec) {
    case VideoCodecType::kVP8:
      return kVp8DefaultStaticQpThreshold;
    case VideoCodecType::kVP9:
      return kVp9DefaultStaticQpThreshold;
    case VideoCodecType::kAV1:
      return kAv1DefaultStaticQpThreshold;
    case VideoCodecType::kGeneric:
    case VideoCodecType::kH264:
    case VideoCodecType::kH265:
      break;
  }
  return -1;  // QP is never below 0: no static threshold.
}

void QualityConvergenceController::Initialize(int number_of_layers, std::optional<int> encoder_min_qp,
                                              VideoCodecType codec) {
  number_of_layers_ = std::max(number_of_layers, 1);
  int qp_threshold = DefaultStaticQpThreshold(codec);
  if (encoder_min_qp.has_value()) qp_threshold = std::max(qp_threshold, *encoder_min_qp);
  convergence_monitors_.assign(static_cast<size_t>(number_of_layers_),
                               QualityConvergenceMonitor(QualityConvergenceMonitor::DefaultParameters(qp_threshold, codec)));
  initialized_ = true;
}

bool QualityConvergenceController::AddSampleAndCheckTargetQuality(int layer_index, int qp, bool is_refresh_frame) {
  if (!initialized_ || layer_index < 0 || layer_index >= number_of_layers_) return false;
  auto& monitor = convergence_monitors_[static_cast<size_t>(layer_index)];
  monitor.AddSample(qp, is_refresh_frame);
  return monitor.AtTargetQuality();
}

}  // namespace sm::video
