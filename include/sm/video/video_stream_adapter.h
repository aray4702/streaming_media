// 7.8 Resolution adaptation as libwebrtc M157 (issue #84 step 4): the
// restrictions a resource asks for and the frame size the source produces.
//
// VideoStreamAdapter is libwebrtc's VideoStreamAdapter, ported from
// compare/webrtc/reference/call/adaptation/video_stream_adapter.cc and
// video_source_restrictions.cc (the reference files of #302), with every
// degradation preference (issue #646):
//   - MAINTAIN_FRAMERATE, resolution steps:
//     - adapt down (DecreaseResolution): max_pixels_per_frame = 3/5 of the
//       current input frame's pixels (GetLowerResolutionThan), target unset;
//       refused (kLimitReached) when that is not below the current maximum or
//       falls under min_pixels_per_frame (EncoderInfo::ScalingSettings,
//       kDefaultMinPixelsPerFrame = 320 x 180);
//     - adapt up (IncreaseResolution): target = 5/3 of the input pixels
//       (GetHigherResolutionThan), or unrestricted when this undoes the last
//       step; max_pixels_per_frame = 12/5 of the target
//       (GetIncreasedMaxPixelsWanted); refused when that is not above the
//       current maximum;
//     - after a step it waits for the input frame size to change before
//       taking another in the same direction (awaiting_frame_size_change_;
//       only in MAINTAIN_FRAMERATE).
//   - MAINTAIN_RESOLUTION, frame-rate steps: max_frame_rate = 2/3 of the
//     input fps down (GetLowerFrameRateThan, at least kMinFrameRateFps = 2),
//     3/2 up (GetHigherFrameRateThan), unrestricted when this undoes the last
//     step; it needs an input of at least 2 fps (HasSufficientInputForAdaptation).
//   - BALANCED: down, the frame rate goes to BalancedDegradationSettings'
//     MinFps for the frame size first, then the resolution as
//     MAINTAIN_FRAMERATE; a frame-rate step that leaves less than MinFpsDiff
//     below the input fps takes one more step at once
//     (AdaptIfFpsDiffInsufficient). Up, the frame rate goes to MaxFps first
//     (the next config's fps), then the resolution; BalancedConstraint
//     refuses a step up below the configs' kbps / kbps_res for the encoder
//     target.
//   - MAINTAIN_FRAMERATE_AND_RESOLUTION: kAdaptationDisabled.
//   - SetDegradationPreference to or from BALANCED clears the restrictions;
//     the restrictions the source sees are filtered by the preference
//     (FilterRestrictionsByDegradationPreference).
// GetAdaptationDown / GetAdaptationUp compute a step without applying it and
// ApplyAdaptation applies one, as libwebrtc's Adaptation objects do, so the
// resource bookkeeping (rtp::VideoSendController's port of
// ResourceAdaptationProcessor) can record a step it does not apply.
// GetAdaptDownResolution is the step the initial frame drop and the bandwidth
// quality scaler take (one forced resolution step in BALANCED). The camera
// call uses MAINTAIN_FRAMERATE (#635), which VideoStreamEncoder sets before any
// adaptation, so that is this adapter's default.
//
// BitrateConstraint (video/adaptation/bitrate_constraint.cc, issue #819), for
// every preference: a step up that raises the resolution is refused
// (kRejectedByConstraint) while the encoder target is below the
// min_start_bitrate_bps of the encoder's ResolutionBitrateLimits for
// GetHigherResolutionThan(current frame size)
// (EncoderInfo::GetEncoderBitrateLimitsForResolution: the smallest limit at
// or above that size). With no limits (libwebrtc's software VP8, VP9, H.264
// and AV1 encoders report none by default, nor does the test bench's encoder
// model or libwebrtc's in sm_compare), an unknown or zero target, or no frame
// yet, every step is allowed, as there. The current frame size stands for
// VideoStreamInputState::single_active_stream_pixels, the one configured
// layer's size; simulcast and SVC, for which libwebrtc skips the limits, are
// not ported: a one-to-one call sends one layer.
//
// BalancedDegradationSettings is
// rtc_base/experiments/balanced_degradation_settings.cc with its default
// configs (320x240: 7 fps;
// 480x360: 10 fps, fps_diff 1; 640x480: 15 fps, fps_diff 1). The field trial
// WebRTC-Video-BalancedDegradationSettings is not parsed; a config list can
// be passed instead and is validated as the trial's (IsValid, else the
// defaults).
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
#include <utility>
#include <vector>

namespace sm::video {

// api/rtp_parameters.h DegradationPreference.
enum class DegradationPreference { kMaintainFramerateAndResolution, kMaintainFramerate, kMaintainResolution, kBalanced };
// api/video/video_codec_type.h, for the codec-specific balanced settings.
enum class VideoCodecType { kGeneric, kVP8, kVP9, kAV1, kH264, kH265 };

// call/adaptation/video_stream_adapter.cc.
inline constexpr int kMinFrameRateFps = 2;
int GetLowerResolutionThan(int pixel_count);
int GetHigherResolutionThan(int pixel_count);

struct VideoSourceRestrictions {
  std::optional<int> max_pixels_per_frame;
  std::optional<int> target_pixels_per_frame;
  std::optional<double> max_frame_rate;
  bool operator==(const VideoSourceRestrictions&) const = default;
};

// api/video/video_adaptation_counters.h.
struct VideoAdaptationCounters {
  int resolution_adaptations = 0;
  int fps_adaptations = 0;
  int Total() const { return resolution_adaptations + fps_adaptations; }
  bool operator==(const VideoAdaptationCounters&) const = default;
};

VideoSourceRestrictions FilterRestrictionsByDegradationPreference(VideoSourceRestrictions r, DegradationPreference p);
// video_source_restrictions.cc.
bool DidIncreaseResolution(const VideoSourceRestrictions& before, const VideoSourceRestrictions& after);
bool DidDecreaseResolution(const VideoSourceRestrictions& before, const VideoSourceRestrictions& after);

class BalancedDegradationSettings {
 public:
  static constexpr int kNoFpsDiff = -100;
  // 0 = unset, as in libwebrtc.
  struct CodecTypeSpecific {
    int qp_low = 0;
    int qp_high = 0;
    int fps = 0;
    int kbps = 0;
    int kbps_res = 0;
  };
  struct Config {
    int pixels = 0;    // For frame sizes up to this many pixels:
    int fps = 0;       // the min frame rate;
    int kbps = 0;      // the min bitrate to adapt up (resolution or fps);
    int kbps_res = 0;  // the min bitrate to adapt up in resolution;
    int fps_diff = kNoFpsDiff;  // the min fps reduction (input fps - fps).
    CodecTypeSpecific vp8, vp9, h264, av1, generic;
  };
  struct QpThresholds { int low; int high; };

  BalancedDegradationSettings() : BalancedDegradationSettings(DefaultConfigs()) {}
  // The trial's configs if valid, else DefaultConfigs() (GetValidOrDefault).
  explicit BalancedDegradationSettings(std::vector<Config> configs);
  static std::vector<Config> DefaultConfigs();
  const std::vector<Config>& configs() const { return configs_; }

  // INT_MAX = unlimited.
  int MinFps(VideoCodecType type, int pixels) const;
  int MaxFps(VideoCodecType type, int pixels) const;
  // A bitrate of 0 = unknown, which allows the step.
  bool CanAdaptUp(VideoCodecType type, int pixels, uint32_t bitrate_bps) const;
  bool CanAdaptUpResolution(VideoCodecType type, int pixels, uint32_t bitrate_bps) const;
  std::optional<int> MinFpsDiff(int pixels) const;
  std::optional<QpThresholds> GetQpThresholds(VideoCodecType type, int pixels) const;

 private:
  std::optional<Config> GetMinFpsConfig(int pixels) const;
  std::optional<Config> GetMaxFpsConfig(int pixels) const;
  std::vector<Config> configs_;
};

// api/video_codecs/video_encoder.h EncoderInfo::ResolutionBitrateLimits.
struct ResolutionBitrateLimits {
  int frame_size_pixels = 0;      // The frame size the limits are for.
  int min_start_bitrate_bps = 0;  // Recommended minimum bitrate to start encoding.
  int min_bitrate_bps = 0;
  int max_bitrate_bps = 0;
  bool operator==(const ResolutionBitrateLimits&) const = default;
};
// EncoderInfo::GetEncoderBitrateLimitsForResolution: of `limits` sorted by
// frame size, the first whose size is at least `frame_size_pixels`.
std::optional<ResolutionBitrateLimits> GetEncoderBitrateLimitsForResolution(std::vector<ResolutionBitrateLimits> limits,
                                                                             int frame_size_pixels);

class VideoStreamAdapter {
 public:
  enum class Status {
    kValid,
    kLimitReached,
    kAwaitingPreviousAdaptation,
    kInsufficientInput,
    kAdaptationDisabled,
    kRejectedByConstraint,
  };
  // `min_pixels_per_frame`: EncoderInfo::ScalingSettings::min_pixels_per_frame.
  explicit VideoStreamAdapter(int min_pixels_per_frame = 320 * 180, BalancedDegradationSettings balanced = {})
      : min_pixels_(min_pixels_per_frame), balanced_(std::move(balanced)) {}

  // VideoStreamInputState: the size of the last frame the encoder was given
  // (VideoStreamInputStateProvider::OnFrameSizeObserved; 0 = none yet), the
  // input frame rate (GetInputFrameRate) and the codec.
  void OnFrameSizeObserved(int pixels) { frame_size_pixels_ = pixels; }
  void OnFrameRateObserved(int fps) { frames_per_second_ = fps; }
  void SetCodecType(VideoCodecType type) { codec_type_ = type; }
  // BitrateConstraint's and BalancedConstraint's OnEncoderTargetBitrateUpdated (empty = unknown).
  void OnEncoderTargetBitrateUpdated(std::optional<uint32_t> bps) { encoder_target_bps_ = bps; }
  // BitrateConstraint::OnEncoderSettingsUpdated: the encoder's
  // EncoderInfo::resolution_bitrate_limits (empty = none).
  void SetResolutionBitrateLimits(std::vector<ResolutionBitrateLimits> limits) { bitrate_limits_ = std::move(limits); }

  // A switch to or from BALANCED clears the restrictions.
  void SetDegradationPreference(DegradationPreference p);
  DegradationPreference degradation_preference() const { return preference_; }
  void ClearRestrictions();

  // A step: the restrictions and the adaptation counts it leads to.
  struct Adaptation {
    Status status = Status::kLimitReached;
    VideoSourceRestrictions restrictions;
    int resolution_adaptations = 0;
    int fps_adaptations = 0;
    VideoAdaptationCounters counters() const { return {resolution_adaptations, fps_adaptations}; }
  };
  Adaptation GetAdaptationDown() const;
  Adaptation GetAdaptationUp() const;
  Adaptation GetAdaptDownResolution() const;
  // GetAdaptationTo: a step straight to `counters` and `restrictions`, which
  // ResourceAdaptationProcessor takes when the most limiting resource is
  // removed (always valid).
  Adaptation GetAdaptationTo(const VideoAdaptationCounters& counters, const VideoSourceRestrictions& restrictions) const;
  // Applies a valid step computed on the current state.
  void ApplyAdaptation(const Adaptation& a);
  // GetAdaptationDown / GetAdaptationUp, then ApplyAdaptation when valid.
  Status AdaptDown();
  Status AdaptUp();

  // source_restrictions(): unfiltered.
  const VideoSourceRestrictions& restrictions() const { return restrictions_; }
  // What the source is asked for (BroadcastVideoRestrictionsUpdate).
  VideoSourceRestrictions filtered_restrictions() const {
    return FilterRestrictionsByDegradationPreference(restrictions_, preference_);
  }
  const VideoAdaptationCounters& counters() const { return counters_; }
  int resolution_adaptations() const { return counters_.resolution_adaptations; }
  int fps_adaptations() const { return counters_.fps_adaptations; }
  const BalancedDegradationSettings& balanced_settings() const { return balanced_; }

 private:
  struct AwaitingFrameSizeChange { bool pixels_increased; int frame_size_pixels; };
  struct Step {
    Status status = Status::kValid;
    VideoSourceRestrictions restrictions;
    VideoAdaptationCounters counters;
  };

  bool HasSufficientInput() const;
  static Step Fail(Status status) {
    Step s;
    s.status = status;
    return s;
  }
  Step Current() const { return Step{Status::kValid, restrictions_, counters_}; }
  Step GetAdaptationDownStep(const Step& current) const;
  Step GetAdaptationUpStep() const;
  Step AdaptIfFpsDiffInsufficient(const Step& step) const;
  Step DecreaseResolution(const Step& current) const;
  Step DecreaseFramerate(const Step& current) const;
  Step IncreaseResolution(const Step& current) const;
  Step IncreaseFramerate(const Step& current) const;
  bool BitrateAllowsUp(const VideoSourceRestrictions& after) const;
  bool BalancedAllowsUp(const VideoSourceRestrictions& after) const;
  static Adaptation ToAdaptation(const Step& s);

  int min_pixels_;
  BalancedDegradationSettings balanced_;
  DegradationPreference preference_ = DegradationPreference::kMaintainFramerate;
  int frame_size_pixels_ = 0;
  int frames_per_second_ = 0;
  VideoCodecType codec_type_ = VideoCodecType::kGeneric;
  std::optional<uint32_t> encoder_target_bps_;
  std::vector<ResolutionBitrateLimits> bitrate_limits_;
  VideoSourceRestrictions restrictions_;
  VideoAdaptationCounters counters_;
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
