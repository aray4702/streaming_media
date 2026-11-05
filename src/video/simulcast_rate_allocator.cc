#include "sm/video/simulcast_rate_allocator.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>

namespace sm::video {

// ---- api/video/video_bitrate_allocation.cc ---------------------------------

bool VideoBitrateAllocation::SetBitrate(size_t spatial_index, size_t temporal_index, uint32_t bitrate_bps) {
  int64_t new_sum = sum_;
  std::optional<uint32_t>& layer = bitrates_[spatial_index][temporal_index];
  if (layer) new_sum -= *layer;
  new_sum += bitrate_bps;
  if (new_sum > static_cast<int64_t>(kMaxBitrateBps)) return false;
  layer = bitrate_bps;
  sum_ = static_cast<uint32_t>(new_sum);
  return true;
}

bool VideoBitrateAllocation::HasBitrate(size_t spatial_index, size_t temporal_index) const {
  return bitrates_[spatial_index][temporal_index].has_value();
}

uint32_t VideoBitrateAllocation::GetBitrate(size_t spatial_index, size_t temporal_index) const {
  return bitrates_[spatial_index][temporal_index].value_or(0);
}

bool VideoBitrateAllocation::IsSpatialLayerUsed(size_t spatial_index) const {
  for (const auto& b : bitrates_[spatial_index]) {
    if (b) return true;
  }
  return false;
}

uint32_t VideoBitrateAllocation::GetSpatialLayerSum(size_t spatial_index) const {
  uint32_t sum = 0;
  for (const auto& b : bitrates_[spatial_index]) sum += b.value_or(0);
  return sum;
}

std::vector<uint32_t> VideoBitrateAllocation::GetTemporalLayerAllocation(size_t spatial_index) const {
  std::vector<uint32_t> rates;
  // The highest temporal layer with a defined bitrate sets the size.
  for (size_t i = kMaxTemporalStreams; i > 0; --i) {
    if (bitrates_[spatial_index][i - 1]) {
      rates.resize(i);
      break;
    }
  }
  for (size_t i = 0; i < rates.size(); ++i) rates[i] = bitrates_[spatial_index][i].value_or(0);
  return rates;
}

// ---- VideoCodecInitializer::SetupCodec (not in reference/) -----------------

namespace {
// modules/video_coding/include/video_codec_initializer.h: kEncoderMinBitrateKbps.
constexpr uint32_t kEncoderMinBitrateKbps = 10;
}  // namespace

VideoCodec SetupCodec(const VideoEncoderConfig& config, const std::vector<VideoStream>& streams,
                      DataRate start_bitrate) {
  VideoCodec c;
  c.codec_type = config.codec_type;
  c.mode = config.content_type == VideoEncoderConfig::ContentType::kScreen ? VideoCodecMode::kScreensharing
                                                                           : VideoCodecMode::kRealtimeVideo;
  c.legacy_conference_mode = config.legacy_conference_mode;
  c.number_of_simulcast_streams = std::min(streams.size(), kMaxSimulcastStreams);
  c.min_bitrate_kbps = streams.empty() ? 0 : static_cast<uint32_t>(std::max(0, streams[0].min_bitrate_bps) / 1000);
  // Active configuration might not be fully copied to `streams` for SVC yet, so `config` is checked.
  c.active = std::any_of(config.simulcast_layers.begin(), config.simulcast_layers.end(),
                         [](const VideoStream& l) { return l.active; });
  if (c.min_bitrate_kbps < kEncoderMinBitrateKbps) c.min_bitrate_kbps = kEncoderMinBitrateKbps;
  int max_framerate = 0;
  for (size_t i = 0; i < c.number_of_simulcast_streams; ++i) {
    const VideoStream& s = streams[i];
    SimulcastStream& ss = c.simulcast_stream[i];
    ss.width = s.width;
    ss.height = s.height;
    ss.max_framerate = s.max_framerate;
    ss.min_bitrate_kbps = static_cast<uint32_t>(std::max(0, s.min_bitrate_bps) / 1000);
    ss.target_bitrate_kbps = static_cast<uint32_t>(std::max(0, s.target_bitrate_bps) / 1000);
    ss.max_bitrate_kbps = static_cast<uint32_t>(std::max(0, s.max_bitrate_bps) / 1000);
    ss.qp_max = s.max_qp;
    ss.number_of_temporal_layers = s.num_temporal_layers.value_or(1);
    ss.active = s.active;
    c.width = std::max(c.width, s.width);
    c.height = std::max(c.height, s.height);
    c.min_bitrate_kbps = std::min(c.min_bitrate_kbps, ss.min_bitrate_kbps);
    c.max_bitrate_kbps += ss.max_bitrate_kbps;
    c.qp_max = std::max(c.qp_max, s.max_qp);
    max_framerate = std::max(max_framerate, s.max_framerate);
  }
  c.max_framerate = static_cast<uint32_t>(std::max(max_framerate, 0));
  if (c.max_bitrate_kbps == 0) {
    // Unset max bitrate: cap to one bit per pixel.
    c.max_bitrate_kbps = static_cast<uint32_t>(c.width) * static_cast<uint32_t>(c.height) * c.max_framerate / 1000;
  }
  if (c.max_bitrate_kbps < kEncoderMinBitrateKbps) c.max_bitrate_kbps = kEncoderMinBitrateKbps;
  c.start_bitrate_kbps = static_cast<uint32_t>(std::clamp<int64_t>(start_bitrate.bps() / 1000, c.min_bitrate_kbps,
                                                                   c.max_bitrate_kbps));
  if (!streams.empty() && streams[0].num_temporal_layers) c.codec_temporal_layers = *streams[0].num_temporal_layers;
  // A single stream is not simulcast (VideoCodecInitializer leaves numberOfSimulcastStreams at the stream count,
  // and SimulcastRateAllocator's simulcast path then treats it as one layer).
  return c;
}

// ---- modules/video_coding/utility/simulcast_utility.cc ---------------------

namespace simulcast_utility {

uint32_t SumStreamMaxBitrate(int streams, const VideoCodec& codec) {
  uint32_t sum = 0;
  for (int i = 0; i < streams; ++i) sum += codec.simulcast_stream[static_cast<size_t>(i)].max_bitrate_kbps;
  return sum;
}

int NumberOfSimulcastStreams(const VideoCodec& codec) {
  int streams = codec.number_of_simulcast_streams < 1 ? 1 : static_cast<int>(codec.number_of_simulcast_streams);
  if (SumStreamMaxBitrate(streams, codec) == 0) streams = 1;
  return streams;
}

bool ValidSimulcastParameters(const VideoCodec& codec, int num_streams) {
  const auto& s = codec.simulcast_stream;
  const auto n = static_cast<size_t>(num_streams);
  // Resolution.
  if (codec.width != s[n - 1].width || codec.height != s[n - 1].height) return false;
  for (size_t i = 0; i < n; ++i) {
    if (codec.width * s[i].height != codec.height * s[i].width) return false;
  }
  for (size_t i = 1; i < n; ++i) {
    if (s[i].width < s[i - 1].width) return false;
  }
  // Frame rate.
  for (size_t i = 1; i < n; ++i) {
    if (std::fabs(s[i].max_framerate - s[i - 1].max_framerate) > 1e-9) return false;
  }
  // Temporal layers.
  for (size_t i = 0; i + 1 < n; ++i) {
    if (s[i].number_of_temporal_layers != s[i + 1].number_of_temporal_layers) return false;
  }
  return true;
}

bool IsConferenceModeScreenshare(const VideoCodec& codec) {
  return codec.mode == VideoCodecMode::kScreensharing && codec.legacy_conference_mode &&
         (codec.codec_type == VideoCodecType::kVP8 || codec.codec_type == VideoCodecType::kH264);
}

int NumberOfTemporalLayers(const VideoCodec& codec, int spatial_id) {
  int num_temporal_layers = 0;
  if (codec.codec_type == VideoCodecType::kVP8 || codec.codec_type == VideoCodecType::kVP9 ||
      codec.codec_type == VideoCodecType::kH264) {
    num_temporal_layers = codec.codec_temporal_layers;
  }
  if (codec.number_of_simulcast_streams > 0) {
    num_temporal_layers = std::max(
        num_temporal_layers, codec.simulcast_stream[static_cast<size_t>(spatial_id)].number_of_temporal_layers);
  }
  return std::max(1, num_temporal_layers);
}

}  // namespace simulcast_utility

// ---- modules/video_coding/utility/simulcast_rate_allocator.cc --------------

namespace {
// Ratio allocation between temporal streams, as required for VP8 (accumulating).
constexpr float kLayerRateAllocation[kMaxTemporalStreams][kMaxTemporalStreams] = {
    {1.0f, 1.0f, 1.0f, 1.0f},  // 1 layer
    {0.6f, 1.0f, 1.0f, 1.0f},  // 2 layers {60%, 40%}
    {0.4f, 0.6f, 1.0f, 1.0f},  // 3 layers {40%, 20%, 40%}
    {0.25f, 0.4f, 0.6f, 1.0f}  // 4 layers {25%, 15%, 20%, 40%}
};
constexpr float kBaseHeavy3TlRateAllocation[kMaxTemporalStreams] = {0.6f, 0.8f, 1.0f, 1.0f};  // {60%, 20%, 20%}
constexpr uint32_t kLegacyScreenshareTl0BitrateKbps = 200;
constexpr uint32_t kLegacyScreenshareTl1BitrateKbps = 1000;
constexpr double kVideoHysteresisFactor = 1.2;
constexpr double kScreenshareHysteresisFactor = 1.35;
}  // namespace

float SimulcastRateAllocator::GetTemporalRateAllocation(int num_layers, int temporal_id, bool base_heavy_tl3_alloc) {
  num_layers = std::clamp(num_layers, 1, static_cast<int>(kMaxTemporalStreams));
  temporal_id = std::clamp(temporal_id, 0, num_layers - 1);
  if (num_layers == 3 && base_heavy_tl3_alloc) return kBaseHeavy3TlRateAllocation[temporal_id];
  return kLayerRateAllocation[num_layers - 1][temporal_id];
}

VideoBitrateAllocation SimulcastRateAllocator::Allocate(DataRate total_bitrate, double /*framerate*/) {
  VideoBitrateAllocation allocated;
  const DataRate stable_rate = total_bitrate;
  DistributeAllocationToSimulcastLayers(total_bitrate, stable_rate, &allocated);
  DistributeAllocationToTemporalLayers(&allocated);
  return allocated;
}

void SimulcastRateAllocator::DistributeAllocationToSimulcastLayers(DataRate total_bitrate, DataRate stable_bitrate,
                                                                   VideoBitrateAllocation* allocated_bitrates) {
  DataRate left_in_total_allocation = total_bitrate;
  DataRate left_in_stable_allocation = stable_bitrate;
  if (codec_.max_bitrate_kbps) {
    const DataRate max_rate = DataRate::KilobitsPerSec(codec_.max_bitrate_kbps);
    left_in_total_allocation = std::min(left_in_total_allocation, max_rate);
    left_in_stable_allocation = std::min(left_in_stable_allocation, max_rate);
  }
  const size_t n = codec_.number_of_simulcast_streams;
  if (n == 0) {
    // No simulcast: the target, already capped.
    if (codec_.active) {
      allocated_bitrates->SetBitrate(
          0, 0,
          static_cast<uint32_t>(std::max(DataRate::KilobitsPerSec(codec_.min_bitrate_kbps), left_in_total_allocation).bps()));
    }
    return;
  }
  // Sort the layers by maxBitrate; they might not always be from smallest to biggest.
  std::vector<size_t> layer_index(n);
  std::iota(layer_index.begin(), layer_index.end(), 0);
  std::stable_sort(layer_index.begin(), layer_index.end(), [this](size_t a, size_t b) {
    return std::tie(codec_.simulcast_stream[a].max_bitrate_kbps) < std::tie(codec_.simulcast_stream[b].max_bitrate_kbps);
  });
  // Find the first active layer; inactive layers get nothing.
  size_t active_layer = 0;
  for (; active_layer < n; ++active_layer) {
    if (codec_.simulcast_stream[layer_index[active_layer]].active) break;
  }
  if (active_layer == n) return;  // All streams inactive.

  // Always allocate the minimum bitrate of the first active layer; suspending below it is controlled outside.
  const DataRate min_rate = DataRate::KilobitsPerSec(codec_.simulcast_stream[layer_index[active_layer]].min_bitrate_kbps);
  left_in_total_allocation = std::max(left_in_total_allocation, min_rate);
  left_in_stable_allocation = std::max(left_in_stable_allocation, min_rate);

  // Allocate to the simulcast streams, all in temporal layer 0; it is split over temporal layers afterwards.
  bool first_allocation = false;
  if (stream_enabled_.empty()) {
    // First allocation: no hysteresis, in case this is a reconfiguration of an enabled stream.
    first_allocation = true;
    stream_enabled_.resize(n, false);
  }
  size_t top_active_layer = active_layer;
  // Allocate up to the target bitrate of each active simulcast layer.
  for (; active_layer < n; ++active_layer) {
    const SimulcastStream& stream = codec_.simulcast_stream[layer_index[active_layer]];
    if (!stream.active) {
      stream_enabled_[layer_index[active_layer]] = false;
      continue;
    }
    // A layer that can't be allocated stops the higher ones: they need a higher minimum.
    DataRate min_bitrate = DataRate::KilobitsPerSec(stream.min_bitrate_kbps);
    const DataRate target_bitrate = DataRate::KilobitsPerSec(stream.target_bitrate_kbps);
    const double hysteresis_factor =
        codec_.mode == VideoCodecMode::kRealtimeVideo ? kVideoHysteresisFactor : kScreenshareHysteresisFactor;
    if (!first_allocation && !stream_enabled_[layer_index[active_layer]]) {
      min_bitrate = std::min(min_bitrate * hysteresis_factor, target_bitrate);
    }
    if (left_in_stable_allocation < min_bitrate) {
      allocated_bitrates->set_bw_limited(true);
      break;
    }
    // This layer is allocated, so it is the current top active layer.
    top_active_layer = layer_index[active_layer];
    stream_enabled_[layer_index[active_layer]] = true;
    const DataRate layer_rate = std::min(left_in_total_allocation, target_bitrate);
    allocated_bitrates->SetBitrate(layer_index[active_layer], 0, static_cast<uint32_t>(layer_rate.bps()));
    left_in_total_allocation -= layer_rate;
    left_in_stable_allocation -= std::min(left_in_stable_allocation, target_bitrate);
  }
  // All layers above this one are off.
  for (; active_layer < n; ++active_layer) stream_enabled_[layer_index[active_layer]] = false;

  // The rest, up to its max bitrate, goes to the top active stream.
  if (left_in_total_allocation > DataRate::Zero()) {
    const SimulcastStream& stream = codec_.simulcast_stream[top_active_layer];
    const DataRate initial_layer_rate = DataRate::BitsPerSec(allocated_bitrates->GetSpatialLayerSum(top_active_layer));
    const DataRate additional_allocation =
        std::min(left_in_total_allocation, DataRate::KilobitsPerSec(stream.max_bitrate_kbps) - initial_layer_rate);
    allocated_bitrates->SetBitrate(top_active_layer, 0,
                                   static_cast<uint32_t>((initial_layer_rate + additional_allocation).bps()));
  }
}

void SimulcastRateAllocator::DistributeAllocationToTemporalLayers(VideoBitrateAllocation* allocated) const {
  const int num_spatial_streams = std::max(1, static_cast<int>(codec_.number_of_simulcast_streams));
  // Distribute the bitrate of each simulcast stream across its temporal layers.
  for (int simulcast_id = 0; simulcast_id < num_spatial_streams; ++simulcast_id) {
    const auto sid = static_cast<size_t>(simulcast_id);
    uint32_t target_bitrate_kbps = allocated->GetBitrate(sid, 0) / 1000;
    if (target_bitrate_kbps == 0) continue;
    const int num_temporal_streams = NumTemporalStreams(sid);
    uint32_t max_bitrate_kbps;
    if (codec_.mode == VideoCodecMode::kScreensharing && legacy_conference_mode_ && simulcast_id == 0) {
      // Legacy screenshare: the start bitrate is the encoder target; it may overshoot up to the max.
      max_bitrate_kbps = std::min(kLegacyScreenshareTl1BitrateKbps, target_bitrate_kbps);
      target_bitrate_kbps = std::min(kLegacyScreenshareTl0BitrateKbps, target_bitrate_kbps);
    } else if (num_spatial_streams == 1) {
      max_bitrate_kbps = codec_.max_bitrate_kbps;
    } else {
      max_bitrate_kbps = codec_.simulcast_stream[sid].max_bitrate_kbps;
    }
    std::vector<uint32_t> tl_allocation;
    if (num_temporal_streams == 1) {
      tl_allocation.push_back(target_bitrate_kbps);
    } else if (codec_.mode == VideoCodecMode::kScreensharing && legacy_conference_mode_ && simulcast_id == 0) {
      tl_allocation = ScreenshareTemporalLayerAllocation(static_cast<int>(target_bitrate_kbps),
                                                         static_cast<int>(max_bitrate_kbps), simulcast_id);
    } else {
      tl_allocation = DefaultTemporalLayerAllocation(static_cast<int>(target_bitrate_kbps),
                                                     static_cast<int>(max_bitrate_kbps), simulcast_id);
    }
    for (size_t tl = 0; tl < tl_allocation.size(); ++tl) {
      if (tl_allocation[tl] > 0) allocated->SetBitrate(sid, tl, tl_allocation[tl] * 1000);
    }
  }
}

std::vector<uint32_t> SimulcastRateAllocator::DefaultTemporalLayerAllocation(int bitrate_kbps, int /*max_bitrate_kbps*/,
                                                                             int simulcast_id) const {
  const size_t num_temporal_layers = static_cast<size_t>(NumTemporalStreams(static_cast<size_t>(simulcast_id)));
  std::vector<uint32_t> bitrates;
  for (size_t i = 0; i < num_temporal_layers; ++i) {
    const float layer_bitrate =
        static_cast<float>(bitrate_kbps) *
        GetTemporalRateAllocation(static_cast<int>(num_temporal_layers), static_cast<int>(i), base_heavy_tl3_alloc_);
    bitrates.push_back(static_cast<uint32_t>(layer_bitrate + 0.5));
  }
  // The table is of aggregates: transform to individual rates.
  uint32_t sum = 0;
  for (size_t i = 0; i < num_temporal_layers; ++i) {
    const uint32_t layer_bitrate = bitrates[i];
    bitrates[i] -= sum;
    sum = layer_bitrate;
    if (sum >= static_cast<uint32_t>(bitrate_kbps)) {
      // Sum adds up; any further layers are 0.
      bitrates.resize(i + 1);
      break;
    }
  }
  return bitrates;
}

std::vector<uint32_t> SimulcastRateAllocator::ScreenshareTemporalLayerAllocation(int bitrate_kbps, int max_bitrate_kbps,
                                                                                 int simulcast_id) const {
  if (simulcast_id > 0) return DefaultTemporalLayerAllocation(bitrate_kbps, max_bitrate_kbps, simulcast_id);
  std::vector<uint32_t> allocation;
  allocation.push_back(static_cast<uint32_t>(bitrate_kbps));
  if (max_bitrate_kbps > bitrate_kbps) allocation.push_back(static_cast<uint32_t>(max_bitrate_kbps - bitrate_kbps));
  return allocation;
}

int SimulcastRateAllocator::NumTemporalStreams(size_t simulcast_id) const {
  // Per-stream formats (mixed-codec simulcast) are not modelled: the codec's type decides.
  const bool is_vp8 = codec_.codec_type == VideoCodecType::kVP8;
  return std::max(1, is_vp8 && codec_.number_of_simulcast_streams == 0
                         ? codec_.codec_temporal_layers
                         : codec_.simulcast_stream[simulcast_id].number_of_temporal_layers);
}

}  // namespace sm::video
