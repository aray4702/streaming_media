#include "sm/video/video_stream_adapter.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace sm::video {

namespace {
constexpr int kMax = std::numeric_limits<int>::max();

// For frame rate, the steps are 2/3 (down) and 3/2 (up).
int GetLowerFrameRateThan(int fps) { return (fps * 2) / 3; }
int GetHigherFrameRateThan(int fps) { return fps != kMax ? (fps * 3) / 2 : kMax; }

int GetIncreasedMaxPixelsWanted(int target_pixels) {
  if (target_pixels == kMax) return kMax;
  return static_cast<int>(int64_t{target_pixels} * 12 / 5);
}

bool CanDecreaseFrameRateTo(int max_frame_rate, const VideoSourceRestrictions& r) {
  const int fps_wanted = std::max(kMinFrameRateFps, max_frame_rate);
  return fps_wanted < (r.max_frame_rate ? static_cast<int>(*r.max_frame_rate) : kMax);
}

bool CanIncreaseFrameRateTo(int max_frame_rate, const VideoSourceRestrictions& r) {
  return max_frame_rate > (r.max_frame_rate ? static_cast<int>(*r.max_frame_rate) : kMax);
}

// balanced_degradation_settings.cc.
constexpr int kMinFps = 1;
constexpr int kMaxFps = 100;  // 100 means unlimited fps.
using Balanced = BalancedDegradationSettings;

std::optional<int> Positive(int v) { return v > 0 ? std::optional<int>(v) : std::nullopt; }

bool IsValidConfig(const Balanced::CodecTypeSpecific& c) {
  if ((c.qp_low > 0) != (c.qp_high > 0)) return false;  // Neither or both thresholds.
  if (c.qp_low > 0 && c.qp_high > 0 && c.qp_low >= c.qp_high) return false;
  if (c.fps > 0 && (c.fps < kMinFps || c.fps > kMaxFps)) return false;
  return true;
}

bool IsValid(const Balanced::CodecTypeSpecific& c1, const Balanced::CodecTypeSpecific& c2) {
  const bool both_or_none_set =
      (c1.qp_low > 0) == (c2.qp_low > 0) && (c1.qp_high > 0) == (c2.qp_high > 0) && (c1.fps > 0) == (c2.fps > 0);
  if (!both_or_none_set) return false;
  if (c1.fps > 0 && c1.fps < c2.fps) return false;
  return true;
}

bool IsValid(const std::vector<Balanced::Config>& configs) {
  if (configs.size() <= 1) return false;
  for (const auto& c : configs) {
    if (c.fps < kMinFps || c.fps > kMaxFps) return false;
  }
  int last_kbps = configs[0].kbps;
  for (size_t i = 1; i < configs.size(); ++i) {
    if (configs[i].kbps > 0) {
      if (configs[i].kbps < last_kbps) return false;
      last_kbps = configs[i].kbps;
    }
  }
  for (size_t i = 1; i < configs.size(); ++i) {
    if (configs[i].pixels < configs[i - 1].pixels || configs[i].fps < configs[i - 1].fps) return false;
    if (!IsValid(configs[i].vp8, configs[i - 1].vp8) || !IsValid(configs[i].vp9, configs[i - 1].vp9) ||
        !IsValid(configs[i].h264, configs[i - 1].h264) || !IsValid(configs[i].av1, configs[i - 1].av1) ||
        !IsValid(configs[i].generic, configs[i - 1].generic)) {
      return false;
    }
  }
  for (const auto& c : configs) {
    if (!IsValidConfig(c.vp8) || !IsValidConfig(c.vp9) || !IsValidConfig(c.h264) || !IsValidConfig(c.av1) ||
        !IsValidConfig(c.generic)) {
      return false;
    }
  }
  return true;
}

// The codec's settings for frame rate and bitrate (GetFps, GetKbps,
// GetKbpsRes: H.265 uses VP9's), and for QP thresholds (GetThresholds: H.265
// uses H.264's).
const Balanced::CodecTypeSpecific* RateSettings(VideoCodecType type, const Balanced::Config& c) {
  switch (type) {
    case VideoCodecType::kVP8: return &c.vp8;
    case VideoCodecType::kH265:
    case VideoCodecType::kVP9: return &c.vp9;
    case VideoCodecType::kH264: return &c.h264;
    case VideoCodecType::kAV1: return &c.av1;
    case VideoCodecType::kGeneric: return &c.generic;
  }
  return nullptr;
}

const Balanced::CodecTypeSpecific* QpSettings(VideoCodecType type, const Balanced::Config& c) {
  return type == VideoCodecType::kH265 ? &c.h264 : RateSettings(type, c);
}

int GetFps(VideoCodecType type, const std::optional<Balanced::Config>& c) {
  if (!c) return kMax;
  const auto* s = RateSettings(type, *c);
  const int framerate = (s && s->fps > 0) ? s->fps : c->fps;
  return framerate == kMaxFps ? kMax : framerate;
}

std::optional<int> GetKbps(VideoCodecType type, const std::optional<Balanced::Config>& c) {
  if (!c) return std::nullopt;
  const auto* s = RateSettings(type, *c);
  if (s && s->kbps > 0) return s->kbps;
  return Positive(c->kbps);
}

std::optional<int> GetKbpsRes(VideoCodecType type, const std::optional<Balanced::Config>& c) {
  if (!c) return std::nullopt;
  const auto* s = RateSettings(type, *c);
  if (s && s->kbps_res > 0) return s->kbps_res;
  return Positive(c->kbps_res);
}

// api/video/video_adapter.cc.
struct Fraction {
  int numerator;
  int denominator;
  void DivideByGcd() {
    const int g = std::gcd(numerator, denominator);
    numerator /= g;
    denominator /= g;
  }
  int scale_pixel_count(int input_pixels) const {
    return static_cast<int>(int64_t{numerator} * numerator * input_pixels / (int64_t{denominator} * denominator));
  }
};

int RoundUp(int value_to_round, int multiple, int max_value) {
  const int rounded = (value_to_round + multiple - 1) / multiple * multiple;
  return rounded <= max_value ? rounded : (max_value / multiple * multiple);
}

Fraction FindScale(int input_width, int input_height, int target_pixels, int max_pixels) {
  const int input_pixels = input_width * input_height;
  if (target_pixels >= input_pixels) return {1, 1};
  Fraction current{1, 1};
  Fraction best{1, 1};
  if (input_width % 3 == 0 && input_height % 3 == 0) current = {6, 6};
  if (input_width % 9 == 0 && input_height % 9 == 0) current = {36, 36};
  int min_pixel_diff = kMax;
  if (input_pixels <= max_pixels) min_pixel_diff = std::abs(input_pixels - target_pixels);
  while (current.scale_pixel_count(input_pixels) > target_pixels) {
    if (current.numerator % 3 == 0 && current.denominator % 2 == 0) {
      current.numerator /= 3;
      current.denominator /= 2;
    } else {
      current.numerator *= 3;
      current.denominator *= 4;
    }
    const int output_pixels = current.scale_pixel_count(input_pixels);
    if (output_pixels <= max_pixels) {
      const int diff = std::abs(target_pixels - output_pixels);
      if (diff < min_pixel_diff) {
        min_pixel_diff = diff;
        best = current;
      }
    }
  }
  best.DivideByGcd();
  return best;
}
}  // namespace

bool DidIncreaseResolution(const VideoSourceRestrictions& before, const VideoSourceRestrictions& after) {
  if (!before.max_pixels_per_frame) return false;
  if (!after.max_pixels_per_frame) return true;
  return *after.max_pixels_per_frame > *before.max_pixels_per_frame;
}

bool DidDecreaseResolution(const VideoSourceRestrictions& before, const VideoSourceRestrictions& after) {
  if (!after.max_pixels_per_frame) return false;
  if (!before.max_pixels_per_frame) return true;
  return *after.max_pixels_per_frame < *before.max_pixels_per_frame;
}

VideoSourceRestrictions FilterRestrictionsByDegradationPreference(VideoSourceRestrictions r, DegradationPreference p) {
  switch (p) {
    case DegradationPreference::kBalanced:
      break;
    case DegradationPreference::kMaintainFramerate:
      r.max_frame_rate.reset();
      break;
    case DegradationPreference::kMaintainResolution:
      r.max_pixels_per_frame.reset();
      r.target_pixels_per_frame.reset();
      break;
    case DegradationPreference::kMaintainFramerateAndResolution:
      r = {};
      break;
  }
  return r;
}

std::vector<Balanced::Config> BalancedDegradationSettings::DefaultConfigs() {
  return {{320 * 240, 7, 0, 0, kNoFpsDiff, {}, {}, {}, {}, {}},
          {480 * 360, 10, 0, 0, 1, {}, {}, {}, {}, {}},
          {640 * 480, 15, 0, 0, 1, {}, {}, {}, {}, {}}};
}

BalancedDegradationSettings::BalancedDegradationSettings(std::vector<Config> configs)
    : configs_(IsValid(configs) ? std::move(configs) : DefaultConfigs()) {}

std::optional<Balanced::Config> BalancedDegradationSettings::GetMinFpsConfig(int pixels) const {
  for (const auto& c : configs_) {
    if (pixels <= c.pixels) return c;
  }
  return std::nullopt;
}

std::optional<Balanced::Config> BalancedDegradationSettings::GetMaxFpsConfig(int pixels) const {
  for (size_t i = 0; i + 1 < configs_.size(); ++i) {
    if (pixels <= configs_[i].pixels) return configs_[i + 1];
  }
  return std::nullopt;
}

int BalancedDegradationSettings::MinFps(VideoCodecType type, int pixels) const {
  return GetFps(type, GetMinFpsConfig(pixels));
}

int BalancedDegradationSettings::MaxFps(VideoCodecType type, int pixels) const {
  return GetFps(type, GetMaxFpsConfig(pixels));
}

bool BalancedDegradationSettings::CanAdaptUp(VideoCodecType type, int pixels, uint32_t bitrate_bps) const {
  const std::optional<int> min_kbps = GetKbps(type, GetMaxFpsConfig(pixels));
  if (!min_kbps || bitrate_bps == 0) return true;  // No limit configured or bitrate provided.
  return bitrate_bps >= static_cast<uint32_t>(*min_kbps * 1000);
}

bool BalancedDegradationSettings::CanAdaptUpResolution(VideoCodecType type, int pixels, uint32_t bitrate_bps) const {
  const std::optional<int> min_kbps = GetKbpsRes(type, GetMaxFpsConfig(pixels));
  if (!min_kbps || bitrate_bps == 0) return true;
  return bitrate_bps >= static_cast<uint32_t>(*min_kbps * 1000);
}

std::optional<int> BalancedDegradationSettings::MinFpsDiff(int pixels) const {
  for (const auto& c : configs_) {
    if (pixels <= c.pixels) return c.fps_diff > kNoFpsDiff ? std::optional<int>(c.fps_diff) : std::nullopt;
  }
  return std::nullopt;
}

std::optional<Balanced::QpThresholds> BalancedDegradationSettings::GetQpThresholds(VideoCodecType type,
                                                                                   int pixels) const {
  // GetConfig: the last config above the highest pixels.
  const Config* config = &configs_.back();
  for (const auto& c : configs_) {
    if (pixels <= c.pixels) {
      config = &c;
      break;
    }
  }
  const auto* s = QpSettings(type, *config);
  if (s && s->qp_low > 0 && s->qp_high > 0) return QpThresholds{s->qp_low, s->qp_high};
  return std::nullopt;
}

int GetLowerResolutionThan(int pixel_count) { return static_cast<int>(int64_t{pixel_count} * 3 / 5); }

int GetHigherResolutionThan(int pixel_count) {
  return pixel_count != kMax ? static_cast<int>(int64_t{pixel_count} * 5 / 3) : kMax;
}

void VideoStreamAdapter::SetDegradationPreference(DegradationPreference p) {
  if (preference_ == p) return;
  const bool balanced_switch = p == DegradationPreference::kBalanced || preference_ == DegradationPreference::kBalanced;
  preference_ = p;
  if (balanced_switch) ClearRestrictions();
}

void VideoStreamAdapter::ClearRestrictions() {
  restrictions_ = {};
  counters_ = {};
  awaiting_.reset();
}

bool VideoStreamAdapter::HasSufficientInput() const {
  // HasInputFrameSizeAndFramesPerSecond: a frame size (the fps may be 0).
  return frame_size_pixels_ > 0 &&
         (preference_ != DegradationPreference::kMaintainResolution || frames_per_second_ >= kMinFrameRateFps);
}

VideoStreamAdapter::Adaptation VideoStreamAdapter::ToAdaptation(const Step& s) {
  Adaptation a;
  a.status = s.status;
  if (s.status == Status::kValid) {
    a.restrictions = s.restrictions;
    a.resolution_adaptations = s.counters.resolution_adaptations;
    a.fps_adaptations = s.counters.fps_adaptations;
  }
  return a;
}

VideoStreamAdapter::Step VideoStreamAdapter::DecreaseResolution(const Step& current) const {
  const int target = GetLowerResolutionThan(frame_size_pixels_);
  // CanDecreaseResolutionTo; single_active_stream_pixels is the input size for one stream.
  if (!(target < current.restrictions.max_pixels_per_frame.value_or(kMax) && target >= min_pixels_)) {
    return Fail(Status::kLimitReached);
  }
  Step s = current;
  s.restrictions.max_pixels_per_frame = target;
  s.restrictions.target_pixels_per_frame.reset();
  ++s.counters.resolution_adaptations;
  return s;
}

VideoStreamAdapter::Step VideoStreamAdapter::DecreaseFramerate(const Step& current) const {
  int max_frame_rate;
  if (preference_ == DegradationPreference::kBalanced) {
    max_frame_rate = balanced_.MinFps(codec_type_, frame_size_pixels_);
  } else {
    max_frame_rate = GetLowerFrameRateThan(frames_per_second_);
  }
  if (!CanDecreaseFrameRateTo(max_frame_rate, current.restrictions)) return Fail(Status::kLimitReached);
  Step s = current;
  max_frame_rate = std::max(kMinFrameRateFps, max_frame_rate);
  s.restrictions.max_frame_rate =
      max_frame_rate != kMax ? std::optional<double>(max_frame_rate) : std::nullopt;
  ++s.counters.fps_adaptations;
  return s;
}

VideoStreamAdapter::Step VideoStreamAdapter::IncreaseResolution(const Step& current) const {
  int target = frame_size_pixels_;
  if (current.counters.resolution_adaptations == 1) target = kMax;  // Removes the down-scaling.
  target = GetHigherResolutionThan(target);
  const int max_wanted = GetIncreasedMaxPixelsWanted(target);
  // CanIncreaseResolutionTo.
  if (!(max_wanted > current.restrictions.max_pixels_per_frame.value_or(kMax))) return Fail(Status::kLimitReached);
  Step s = current;
  s.restrictions.max_pixels_per_frame = max_wanted != kMax ? std::optional<int>(max_wanted) : std::nullopt;
  s.restrictions.target_pixels_per_frame = max_wanted != kMax ? std::optional<int>(target) : std::nullopt;
  --s.counters.resolution_adaptations;
  return s;
}

VideoStreamAdapter::Step VideoStreamAdapter::IncreaseFramerate(const Step& current) const {
  int max_frame_rate;
  if (preference_ == DegradationPreference::kBalanced) {
    max_frame_rate = balanced_.MaxFps(codec_type_, frame_size_pixels_);
    // Fewer frame-rate steps up than down: keep the step count equal.
    if (max_frame_rate == kMax && current.counters.fps_adaptations > 1) {
      max_frame_rate -= current.counters.fps_adaptations;
    }
    // MaxFps may be the current fps, and then the resolution goes up instead.
    if (!CanIncreaseFrameRateTo(max_frame_rate, current.restrictions)) return Fail(Status::kLimitReached);
  } else {
    max_frame_rate = GetHigherFrameRateThan(frames_per_second_);
  }
  if (current.counters.fps_adaptations == 1) max_frame_rate = kMax;  // Removes the down-scaling.
  if (!CanIncreaseFrameRateTo(max_frame_rate, current.restrictions)) return Fail(Status::kLimitReached);
  Step s = current;
  s.restrictions.max_frame_rate =
      max_frame_rate != kMax ? std::optional<double>(max_frame_rate) : std::nullopt;
  --s.counters.fps_adaptations;
  return s;
}

VideoStreamAdapter::Step VideoStreamAdapter::GetAdaptationDownStep(const Step& current) const {
  if (!HasSufficientInput()) return Fail(Status::kInsufficientInput);
  // Don't adapt while awaiting the effect of a previous step.
  if (awaiting_ && !awaiting_->pixels_increased && preference_ == DegradationPreference::kMaintainFramerate &&
      frame_size_pixels_ >= awaiting_->frame_size_pixels) {
    return Fail(Status::kAwaitingPreviousAdaptation);
  }
  switch (preference_) {
    case DegradationPreference::kBalanced: {
      Step fps = DecreaseFramerate(current);
      if (fps.status == Status::kValid) return fps;
      [[fallthrough]];
    }
    case DegradationPreference::kMaintainFramerate:
      return DecreaseResolution(current);
    case DegradationPreference::kMaintainResolution:
      return DecreaseFramerate(current);
    case DegradationPreference::kMaintainFramerateAndResolution:
      return Fail(Status::kAdaptationDisabled);
  }
  return Fail(Status::kAdaptationDisabled);
}

VideoStreamAdapter::Step VideoStreamAdapter::AdaptIfFpsDiffInsufficient(const Step& step) const {
  const std::optional<int> min_fps_diff = balanced_.MinFpsDiff(frame_size_pixels_);
  if (counters_.fps_adaptations < step.counters.fps_adaptations && min_fps_diff && frames_per_second_ > 0) {
    const int fps_diff = frames_per_second_ - static_cast<int>(*step.restrictions.max_frame_rate);
    if (fps_diff < *min_fps_diff) return GetAdaptationDownStep(step);
  }
  return step;
}

VideoStreamAdapter::Step VideoStreamAdapter::GetAdaptationUpStep() const {
  if (!HasSufficientInput()) return Fail(Status::kInsufficientInput);
  if (awaiting_ && awaiting_->pixels_increased && preference_ == DegradationPreference::kMaintainFramerate &&
      frame_size_pixels_ <= awaiting_->frame_size_pixels) {
    return Fail(Status::kAwaitingPreviousAdaptation);
  }
  switch (preference_) {
    case DegradationPreference::kBalanced: {
      Step fps = IncreaseFramerate(Current());
      if (fps.status == Status::kValid) return fps;
      [[fallthrough]];
    }
    case DegradationPreference::kMaintainFramerate:
      return IncreaseResolution(Current());
    case DegradationPreference::kMaintainResolution:
      return IncreaseFramerate(Current());
    case DegradationPreference::kMaintainFramerateAndResolution:
      return Fail(Status::kAdaptationDisabled);
  }
  return Fail(Status::kAdaptationDisabled);
}

bool VideoStreamAdapter::BalancedAllowsUp(const VideoSourceRestrictions& after) const {
  // BalancedConstraint::IsAdaptationUpAllowed.
  if (preference_ != DegradationPreference::kBalanced) return true;
  const uint32_t bps = encoder_target_bps_.value_or(0);
  if (!balanced_.CanAdaptUp(codec_type_, frame_size_pixels_, bps)) return false;
  if (DidIncreaseResolution(restrictions_, after) &&
      !balanced_.CanAdaptUpResolution(codec_type_, frame_size_pixels_, bps)) {
    return false;
  }
  return true;
}

VideoStreamAdapter::Adaptation VideoStreamAdapter::GetAdaptationDown() const {
  Step s = GetAdaptationDownStep(Current());
  // Check for min_fps.
  if (preference_ == DegradationPreference::kBalanced && s.status == Status::kValid) s = AdaptIfFpsDiffInsufficient(s);
  return ToAdaptation(s);
}

VideoStreamAdapter::Adaptation VideoStreamAdapter::GetAdaptationUp() const {
  Step s = GetAdaptationUpStep();
  if (s.status == Status::kValid && !BalancedAllowsUp(s.restrictions)) s = Fail(Status::kRejectedByConstraint);
  return ToAdaptation(s);
}

VideoStreamAdapter::Adaptation VideoStreamAdapter::GetAdaptDownResolution() const {
  switch (preference_) {
    case DegradationPreference::kMaintainFramerateAndResolution:
      return ToAdaptation(Fail(Status::kAdaptationDisabled));
    case DegradationPreference::kMaintainResolution:
      return ToAdaptation(Fail(Status::kLimitReached));
    case DegradationPreference::kMaintainFramerate:
      return GetAdaptationDown();
    case DegradationPreference::kBalanced: {
      // GetAdaptDownResolutionStepForBalanced: adapt twice if the first step
      // did not decrease the resolution.
      const Step first = GetAdaptationDownStep(Current());
      if (first.status != Status::kValid) return ToAdaptation(first);
      if (first.counters.resolution_adaptations > counters_.resolution_adaptations) return ToAdaptation(first);
      const Step second = DecreaseResolution(first);
      return ToAdaptation(second.status == Status::kValid ? second : first);
    }
  }
  return ToAdaptation(Fail(Status::kAdaptationDisabled));
}

VideoStreamAdapter::Status VideoStreamAdapter::AdaptDown() {
  const Adaptation a = GetAdaptationDown();
  if (a.status == Status::kValid) ApplyAdaptation(a);
  return a.status;
}

VideoStreamAdapter::Status VideoStreamAdapter::AdaptUp() {
  const Adaptation a = GetAdaptationUp();
  if (a.status == Status::kValid) ApplyAdaptation(a);
  return a.status;
}

VideoStreamAdapter::Adaptation VideoStreamAdapter::GetAdaptationTo(const VideoAdaptationCounters& counters,
                                                                 const VideoSourceRestrictions& restrictions) const {
  // Adapts up/down from the current levels so counters are equal.
  return ToAdaptation(Step{Status::kValid, restrictions, counters});
}

void VideoStreamAdapter::ApplyAdaptation(const Adaptation& a) {
  if (a.status != Status::kValid) return;
  if (DidIncreaseResolution(restrictions_, a.restrictions)) {
    awaiting_ = AwaitingFrameSizeChange{true, frame_size_pixels_};
  } else if (DidDecreaseResolution(restrictions_, a.restrictions)) {
    awaiting_ = AwaitingFrameSizeChange{false, frame_size_pixels_};
  } else {
    awaiting_.reset();
  }
  restrictions_ = a.restrictions;
  counters_ = a.counters();
}

void VideoAdapter::OnSinkWants(const VideoSourceRestrictions& r) {
  max_pixels_ = r.max_pixels_per_frame.value_or(kMax);
  target_pixels_ = r.target_pixels_per_frame.value_or(max_pixels_);
}

bool VideoAdapter::AdaptFrameResolution(int in_width, int in_height, int* out_width, int* out_height) const {
  const int max_pixel_count = max_pixels_;
  const int target_pixel_count = std::min(target_pixels_, max_pixel_count);
  if (max_pixel_count <= 0) return false;
  const Fraction scale = FindScale(in_width, in_height, target_pixel_count, max_pixel_count);
  const int cropped_width = RoundUp(in_width, scale.denominator * alignment_, in_width);
  const int cropped_height = RoundUp(in_height, scale.denominator * alignment_, in_height);
  *out_width = cropped_width / scale.denominator * scale.numerator;
  *out_height = cropped_height / scale.denominator * scale.numerator;
  return true;
}

}  // namespace sm::video
