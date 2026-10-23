// 7.8 Resolution adaptation as libwebrtc M157 (issue #84 step 4): the
// restrictions a resource asks for and the frame size the source produces.
//
// VideoStreamAdapter is the MAINTAIN_FRAMERATE part of libwebrtc's
// VideoStreamAdapter, ported from
// compare/webrtc/reference/call/adaptation/video_stream_adapter.cc and
// video_source_restrictions.cc (the reference files of #302):
//   - adapt down (DecreaseResolution): max_pixels_per_frame = 3/5 of the
//     current input frame's pixels (GetLowerResolutionThan), target unset;
//     refused (kLimitReached) when that is not below the current maximum or
//     falls under min_pixels_per_frame (EncoderInfo::ScalingSettings,
//     kDefaultMinPixelsPerFrame = 320 x 180);
//   - adapt up (IncreaseResolution): target = 5/3 of the input pixels
//     (GetHigherResolutionThan), or unrestricted when this undoes the last
//     step; max_pixels_per_frame = 12/5 of the target
//     (GetIncreasedMaxPixelsWanted); refused when that is not above the
//     current maximum;
//   - after a step it waits for the input frame size to change before taking
//     another in the same direction (awaiting_frame_size_change_).
// GetAdaptationDown / GetAdaptationUp compute a step without applying it and
// ApplyAdaptation applies one, as libwebrtc's Adaptation objects do, so the
// resource bookkeeping (rtp::VideoSendController's port of
// ResourceAdaptationProcessor) can record a step it does not apply. The other
// degradation preferences (BALANCED's frame-rate steps and
// BalancedDegradationSettings, MAINTAIN_RESOLUTION) are not ported: a camera
// call uses MAINTAIN_FRAMERATE. BitrateConstraint allows every step when the encoder reports no
// resolution bitrate limits, which the test bench's encoder model does not.
//
// VideoAdapter is libwebrtc's api/video/video_adapter.cc AdaptFrameResolution
// for one input size and the sink's max/target pixel counts (no output
// format request, no aspect-ratio crop, no frame-rate limit): FindScale
// alternates 3/4 and 2/3 (starting with 2/3 when both sides divide by 3) and
// picks the scale closest to the target that is not above the maximum, then
// rounds the crop to the scale's denominator times the alignment.
// VideoStreamEncoder's VideoSourceSinkController passes the restrictions as
// the sink wants (max_pixel_count, target_pixel_count).
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace sm::video {

// call/adaptation/video_stream_adapter.cc.
int GetLowerResolutionThan(int pixel_count);
int GetHigherResolutionThan(int pixel_count);

struct VideoSourceRestrictions {
  std::optional<int> max_pixels_per_frame;
  std::optional<int> target_pixels_per_frame;
  bool operator==(const VideoSourceRestrictions&) const = default;
};

class VideoStreamAdapter {
 public:
  enum class Status { kValid, kLimitReached, kAwaitingPreviousAdaptation, kInsufficientInput };
  // `min_pixels_per_frame`: EncoderInfo::ScalingSettings::min_pixels_per_frame.
  explicit VideoStreamAdapter(int min_pixels_per_frame = 320 * 180) : min_pixels_(min_pixels_per_frame) {}

  // The size of the last frame the encoder was given
  // (VideoStreamInputStateProvider::OnFrameSizeObserved); 0 = none yet.
  void OnFrameSizeObserved(int pixels) { frame_size_pixels_ = pixels; }
  // A step: the restrictions and the resolution adaptation count it leads to
  // (VideoAdaptationCounters; MAINTAIN_FRAMERATE has no frame-rate steps).
  struct Adaptation {
    Status status = Status::kLimitReached;
    VideoSourceRestrictions restrictions;
    int resolution_adaptations = 0;
  };
  Adaptation GetAdaptationDown() const;
  Adaptation GetAdaptationUp() const;
  // Applies a valid step computed on the current state.
  void ApplyAdaptation(const Adaptation& a);
  // GetAdaptationDown / GetAdaptationUp, then ApplyAdaptation when valid.
  Status AdaptDown();
  Status AdaptUp();

  const VideoSourceRestrictions& restrictions() const { return restrictions_; }
  int resolution_adaptations() const { return resolution_adaptations_; }

 private:
  struct AwaitingFrameSizeChange { bool pixels_increased; int frame_size_pixels; };

  int min_pixels_;
  int frame_size_pixels_ = 0;
  VideoSourceRestrictions restrictions_;
  int resolution_adaptations_ = 0;
  std::optional<AwaitingFrameSizeChange> awaiting_;
};

class VideoAdapter {
 public:
  explicit VideoAdapter(int resolution_alignment = 1) : alignment_(resolution_alignment) {}
  // VideoAdapter::OnSinkWants: max_pixel_count, and target_pixel_count or the
  // maximum when unset.
  void OnSinkWants(const VideoSourceRestrictions& r);
  // AdaptFrameResolution without cropping for an aspect ratio. Returns false
  // (drop) when the maximum is not positive.
  bool AdaptFrameResolution(int in_width, int in_height, int* out_width, int* out_height) const;

 private:
  int alignment_;
  int max_pixels_ = std::numeric_limits<int>::max();
  int target_pixels_ = std::numeric_limits<int>::max();
};

}  // namespace sm::video
