// Simulcast layer configuration as libwebrtc M157 (C17, issue #1162):
//  - video/config/simulcast.{h,cc}: the default layers of a simulcast stream,
//    with the bitrate table by resolution (kSimulcastFormatsVP8 for VP8, H.264
//    and generic; kSimulcastFormatsVP9 for VP9, AV1 and H.265), bitrates
//    interpolated between its rows, the maximum layer count by resolution
//    (LimitSimulcastLayerCount, with WebRTC-SimulcastLayerLimitRoundUp's 0.1
//    default round-up ratio), the legacy conference-mode screenshare layers,
//    and the boost of the top layer's maximum to a stream maximum;
//  - video/config/encoder_stream_factory.{h,cc}: the layers a VideoEncoderConfig
//    and an input frame size make (CreateEncoderStreams): the layer
//    resolutions (1/2 steps from the top, or scale_resolution_down_by, on a
//    resolution normalized to the scale factor), the bitrate table's limits
//    with the application's per-encoding overrides (OverrideStreamSettings),
//    and the single-stream default (CreateDefaultVideoStreams).
// Field trials are explicit fields of SimulcastTrials, at M157's defaults.
// Not ported: scale_resolution_down_to (the VideoAdapter path), the VP9 SVC
// branch of CreateDefaultVideoStreams, scalability modes and
// NormalizeSimulcastSizeExperiment (WebRTC-NormalizeSimulcastResolution, unset
// by default).
// Pure functions: no clock, no state.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "sm/core/units.h"
#include "sm/video/video_stream_adapter.h"

namespace sm::video {

// media/base/media_constants.h, api/video_codecs/video_encoder.h.
inline constexpr int kDefaultVideoMaxFramerate = 60;
inline constexpr int kDefaultMinVideoBitrateBps = 30000;
inline constexpr int kDefaultVideoMaxQpVpx = 56;
inline constexpr int kDefaultVideoMaxQpAv1 = 52;
inline constexpr int kDefaultVideoMaxQpH26x = 51;
// api/video_codecs/video_codec.h kMaxSimulcastStreams.
inline constexpr size_t kMaxSimulcastStreams = 3;

struct Resolution {
  int width = 0;
  int height = 0;
  bool operator==(const Resolution&) const = default;
};

// api/video_codecs/video_encoder_config.h VideoStream (without the scalability
// mode and scale_resolution_down_to). -1 means unset, as there.
struct VideoStream {
  int width = 0;
  int height = 0;
  int max_framerate = -1;
  int min_bitrate_bps = -1;
  int target_bitrate_bps = -1;
  int max_bitrate_bps = -1;
  double scale_resolution_down_by = -1.;
  int max_qp = -1;
  std::optional<int> num_temporal_layers;
  std::optional<double> bitrate_priority;
  bool active = true;
};

// The field trials simulcast.cc and encoder_stream_factory.cc read, at M157's defaults.
struct SimulcastTrials {
  // WebRTC-LowresSimulcastBitrateInterpolation (off).
  bool lowres_bitrate_interpolation = false;
  // WebRTC-LegacySimulcastLayerLimit (not disabled: the layer count is limited by resolution).
  bool legacy_layer_limit = true;
  // WebRTC-SimulcastLayerLimitRoundUp max_ratio (unset: kDefaultMaxRoundupRate, 0.1).
  std::optional<double> layer_limit_max_ratio;
  // WebRTC-UseBaseHeavyVP8TL3RateAllocation (RateControlSettings::Vp8BaseHeavyTl3RateAllocation, off).
  bool base_heavy_tl3_rate_allocation = false;
  // GetExperimentalMinVideoBitrate (WebRTC-Video-MinVideoBitrate, unset).
  std::optional<DataRate> experimental_min_bitrate;
};

// Gets the total maximum bitrate for the `streams`: the targets of all but the top one, plus its maximum.
DataRate GetTotalMaxBitrate(const std::vector<VideoStream>& streams);
// Adds any bitrate of `max_bitrate` that is above the total maximum bitrate for the `layers` to the highest one.
void BoostMaxSimulcastLayer(DataRate max_bitrate, std::vector<VideoStream>* layers);
// Number of simulcast streams for a `width` x `height` input, within [min_num_layers, max_num_layers].
size_t LimitSimulcastLayerCount(size_t min_num_layers, size_t max_num_layers, int width, int height,
                                const SimulcastTrials& trials, VideoCodecType codec);
// The simulcast layers for `resolutions` (low to high).
std::vector<VideoStream> GetSimulcastConfig(std::span<const Resolution> resolutions,
                                            bool is_screenshare_with_conference_mode, bool temporal_layers_supported,
                                            const SimulcastTrials& trials, VideoCodecType codec);

// api/video_codecs/video_encoder_config.h VideoEncoderConfig, the fields the stream factory reads.
struct VideoEncoderConfig {
  enum class ContentType { kRealtimeVideo, kScreen };
  VideoCodecType codec_type = VideoCodecType::kH264;
  ContentType content_type = ContentType::kRealtimeVideo;
  // Streams to create; for simulcast the number of encodings (RIDs or SSRCs).
  size_t number_of_streams = 1;
  // Per-encoding overrides from RtpEncodingParameters (active, bitrates, frame rate, scale); at least
  // `number_of_streams` entries.
  std::vector<VideoStream> simulcast_layers = std::vector<VideoStream>(1);
  // b=AS / x-google-max-bitrate; -1 unset.
  int max_bitrate_bps = -1;
  double bitrate_priority = 1.0;
  int max_qp = -1;
  bool legacy_conference_mode = false;
  bool HasScaleResolutionDownBy() const;
};

// SimulcastUtility::IsConferenceModeScreenshare(const VideoEncoderConfig&).
bool IsConferenceModeScreenshare(const VideoEncoderConfig& config);
// EncoderStreamFactory's IsTemporalLayersSupported: VP8, VP9, AV1 and H.265 (not H.264).
bool IsTemporalLayersSupported(VideoCodecType codec);
// EncoderStreamFactory::CreateEncoderStreams for a `frame_width` x `frame_height` input.
std::vector<VideoStream> CreateEncoderStreams(const SimulcastTrials& trials, int frame_width, int frame_height,
                                              const VideoEncoderConfig& config);

}  // namespace sm::video
