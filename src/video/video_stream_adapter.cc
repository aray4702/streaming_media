#include "sm/video/video_stream_adapter.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace sm::video {

namespace {
constexpr int kMax = std::numeric_limits<int>::max();

int GetIncreasedMaxPixelsWanted(int target_pixels) {
  if (target_pixels == kMax) return kMax;
  return static_cast<int>(int64_t{target_pixels} * 12 / 5);
}

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

int GetLowerResolutionThan(int pixel_count) { return static_cast<int>(int64_t{pixel_count} * 3 / 5); }

int GetHigherResolutionThan(int pixel_count) {
  return pixel_count != kMax ? static_cast<int>(int64_t{pixel_count} * 5 / 3) : kMax;
}

VideoStreamAdapter::Status VideoStreamAdapter::AdaptDown() {
  if (frame_size_pixels_ <= 0) return Status::kInsufficientInput;
  if (awaiting_ && !awaiting_->pixels_increased && frame_size_pixels_ >= awaiting_->frame_size_pixels) {
    return Status::kAwaitingPreviousAdaptation;
  }
  // DecreaseResolution; single_active_stream_pixels is the input size for one stream.
  const int target = GetLowerResolutionThan(frame_size_pixels_);
  if (!(target < restrictions_.max_pixels_per_frame.value_or(kMax) && target >= min_pixels_)) {
    return Status::kLimitReached;
  }
  VideoSourceRestrictions next = restrictions_;
  next.max_pixels_per_frame = target;
  next.target_pixels_per_frame.reset();
  Apply(next, +1);
  return Status::kValid;
}

VideoStreamAdapter::Status VideoStreamAdapter::AdaptUp() {
  if (frame_size_pixels_ <= 0) return Status::kInsufficientInput;
  if (awaiting_ && awaiting_->pixels_increased && frame_size_pixels_ <= awaiting_->frame_size_pixels) {
    return Status::kAwaitingPreviousAdaptation;
  }
  // IncreaseResolution.
  int target = frame_size_pixels_;
  if (resolution_adaptations_ == 1) target = kMax;  // Removes the down-scaling.
  target = GetHigherResolutionThan(target);
  const int max_wanted = GetIncreasedMaxPixelsWanted(target);
  if (!(max_wanted > restrictions_.max_pixels_per_frame.value_or(kMax))) return Status::kLimitReached;
  VideoSourceRestrictions next;
  if (max_wanted != kMax) {
    next.max_pixels_per_frame = max_wanted;
    next.target_pixels_per_frame = target;
  }
  Apply(next, -1);
  return Status::kValid;
}

void VideoStreamAdapter::Apply(const VideoSourceRestrictions& next, int counter_delta) {
  if (DidIncreaseResolution(restrictions_, next)) {
    awaiting_ = AwaitingFrameSizeChange{true, frame_size_pixels_};
  } else if (DidDecreaseResolution(restrictions_, next)) {
    awaiting_ = AwaitingFrameSizeChange{false, frame_size_pixels_};
  } else {
    awaiting_.reset();
  }
  restrictions_ = next;
  resolution_adaptations_ += counter_delta;
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
