// Simulcast rate allocation as libwebrtc M157 (C17, issue #1162):
//  - modules/video_coding/utility/simulcast_rate_allocator.{h,cc}: the stream's
//    target split over its simulcast layers. Each active layer, lowest first,
//    gets up to its target bitrate while the stable rate covers its minimum
//    (raised by the 1.2 hysteresis, 1.35 for screen content, for a layer that
//    was off at the last allocation); a layer that can't be covered and every
//    layer above it get nothing (the allocation is then bandwidth limited); the
//    rest, up to its maximum, goes to the top layer allocated. The first active
//    layer always gets at least its minimum. Each layer's rate is then split
//    over its temporal layers (kLayerRateAllocation, 40/20/40 for three) in
//    whole kbps. Without simulcast streams the codec's own min/max apply.
//  - modules/video_coding/utility/simulcast_utility.{h,cc}.
//  - api/video/video_bitrate_allocation.{h,cc}: VideoBitrateAllocation.
//  - SetupCodec: VideoCodecInitializer::SetupCodec's VideoCodec for a set of
//    VideoStreams (modules/video_coding/video_codec_initializer.cc, not in
//    compare/webrtc/reference/; written from M157's class): per-stream min,
//    target and max in kbps, the codec minimum the lowest stream's (at least
//    kEncoderMinBitrateKbps, 10), the codec maximum the sum of the streams'
//    maximums, the frame size the largest stream's.
// Field trials are explicit config, at M157's defaults. Pure computation: no clock.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sm/core/units.h"
#include "sm/video/simulcast.h"
#include "sm/video/video_stream_adapter.h"

namespace sm::video {

// api/video/video_codec_constants.h.
inline constexpr size_t kMaxSpatialLayers = 5;
inline constexpr size_t kMaxTemporalStreams = 4;

// api/video/video_bitrate_allocation.h: bitrates per spatial (simulcast) and temporal layer.
class VideoBitrateAllocation {
 public:
  static constexpr uint32_t kMaxBitrateBps = 0xFFFFFFFFu;
  // False (and no change) if the sum would exceed kMaxBitrateBps.
  bool SetBitrate(size_t spatial_index, size_t temporal_index, uint32_t bitrate_bps);
  bool HasBitrate(size_t spatial_index, size_t temporal_index) const;
  uint32_t GetBitrate(size_t spatial_index, size_t temporal_index) const;
  // Whether the spatial layer has any temporal layer set (even to zero).
  bool IsSpatialLayerUsed(size_t spatial_index) const;
  uint32_t GetSpatialLayerSum(size_t spatial_index) const;
  // The temporal layers' rates up to the last one set, unset ones as 0.
  std::vector<uint32_t> GetTemporalLayerAllocation(size_t spatial_index) const;
  uint32_t get_sum_bps() const { return sum_; }
  uint32_t get_sum_kbps() const { return sum_ / 1000; }  // Rounded down, not to exceed the allocation.
  void set_bw_limited(bool limited) { is_bw_limited_ = limited; }
  bool is_bw_limited() const { return is_bw_limited_; }
  bool operator==(const VideoBitrateAllocation&) const = default;

 private:
  uint32_t sum_ = 0;
  std::array<std::array<std::optional<uint32_t>, kMaxTemporalStreams>, kMaxSpatialLayers> bitrates_{};
  bool is_bw_limited_ = false;
};

// api/video_codecs/video_codec.h SimulcastStream (rates in kbps).
struct SimulcastStream {
  int width = 0;
  int height = 0;
  double max_framerate = 0;
  int number_of_temporal_layers = 1;
  uint32_t max_bitrate_kbps = 0;
  uint32_t target_bitrate_kbps = 0;
  uint32_t min_bitrate_kbps = 0;
  int qp_max = 0;
  bool active = false;
};

enum class VideoCodecMode { kRealtimeVideo, kScreensharing };

// api/video_codecs/video_codec.h VideoCodec, the fields the allocator and the adapter read.
struct VideoCodec {
  VideoCodecType codec_type = VideoCodecType::kGeneric;
  int width = 0;
  int height = 0;
  uint32_t start_bitrate_kbps = 0;
  uint32_t max_bitrate_kbps = 0;
  uint32_t min_bitrate_kbps = 0;
  uint32_t max_framerate = 0;
  bool active = true;
  int qp_max = 0;
  VideoCodecMode mode = VideoCodecMode::kRealtimeVideo;
  bool legacy_conference_mode = false;
  // VideoCodecVP8 / VideoCodecH264 numberOfTemporalLayers (for the single-stream case).
  int codec_temporal_layers = 1;
  // numberOfSimulcastStreams is the number of entries used (0 for no simulcast).
  size_t number_of_simulcast_streams = 0;
  std::array<SimulcastStream, kMaxSimulcastStreams> simulcast_stream{};
};

// VideoCodecInitializer::SetupCodec for simulcast (see above). `start_bitrate` is the encoder's start rate.
VideoCodec SetupCodec(const VideoEncoderConfig& config, const std::vector<VideoStream>& streams,
                      DataRate start_bitrate = DataRate::Zero());

namespace simulcast_utility {
uint32_t SumStreamMaxBitrate(int streams, const VideoCodec& codec);
int NumberOfSimulcastStreams(const VideoCodec& codec);
bool ValidSimulcastParameters(const VideoCodec& codec, int num_streams);
bool IsConferenceModeScreenshare(const VideoCodec& codec);
int NumberOfTemporalLayers(const VideoCodec& codec, int spatial_id);
}  // namespace simulcast_utility

class SimulcastRateAllocator {
 public:
  // `base_heavy_tl3_alloc`: WebRTC-UseBaseHeavyVP8TL3RateAllocation (off by default).
  explicit SimulcastRateAllocator(const VideoCodec& codec, bool base_heavy_tl3_alloc = false)
      : codec_(codec), base_heavy_tl3_alloc_(base_heavy_tl3_alloc) {}

  // VideoBitrateAllocationParameters with the stable rate equal to the total, as Allocate() uses it.
  VideoBitrateAllocation Allocate(DataRate total_bitrate, double framerate = 0);
  const VideoCodec& codec() const { return codec_; }
  static float GetTemporalRateAllocation(int num_layers, int temporal_id, bool base_heavy_tl3_alloc);
  void SetLegacyConferenceMode(bool enabled) { legacy_conference_mode_ = enabled; }
  // Which streams were allocated a rate at the last allocation (stream_enabled_; empty before the first).
  const std::vector<bool>& stream_enabled() const { return stream_enabled_; }

 private:
  void DistributeAllocationToSimulcastLayers(DataRate total_bitrate, DataRate stable_bitrate,
                                             VideoBitrateAllocation* allocated_bitrates);
  void DistributeAllocationToTemporalLayers(VideoBitrateAllocation* allocated_bitrates) const;
  std::vector<uint32_t> DefaultTemporalLayerAllocation(int bitrate_kbps, int max_bitrate_kbps, int simulcast_id) const;
  std::vector<uint32_t> ScreenshareTemporalLayerAllocation(int bitrate_kbps, int max_bitrate_kbps,
                                                           int simulcast_id) const;
  int NumTemporalStreams(size_t simulcast_id) const;

  const VideoCodec codec_;
  const bool base_heavy_tl3_alloc_;
  std::vector<bool> stream_enabled_;
  bool legacy_conference_mode_ = false;
};

}  // namespace sm::video
