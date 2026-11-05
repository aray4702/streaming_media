#include "sm/video/simulcast.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numeric>

#include "sm/video/simulcast_rate_allocator.h"

namespace sm::video {

// ---- video/config/simulcast.cc ---------------------------------------------

namespace {

constexpr double kDefaultMaxRoundupRate = 0.1;

// Limits for legacy conference screensharing mode. Currently used for the lower of the two simulcast streams.
constexpr DataRate kScreenshareDefaultTl0Bitrate = DataRate::KilobitsPerSec(200);
constexpr DataRate kScreenshareDefaultTl1Bitrate = DataRate::KilobitsPerSec(1000);
// Min/max bitrate for the higher one of the two simulcast streams used for screen content.
constexpr DataRate kScreenshareHighStreamMinBitrate = DataRate::KilobitsPerSec(600);
constexpr DataRate kScreenshareHighStreamMaxBitrate = DataRate::KilobitsPerSec(1250);

constexpr int kDefaultNumTemporalLayers = 3;
constexpr int kScreenshareMaxSimulcastLayers = 2;
constexpr int kScreenshareTemporalLayers = 2;

struct SimulcastFormat {
  int width;
  int height;
  // The maximum number of simulcast layers that can be used for resolutions at `width`x`height`.
  size_t max_layers;
  // The maximum bitrate for encoding a stream at `width`x`height`, when the next higher spatial stream is not sent.
  DataRate max_bitrate;
  // The target bitrate for encoding a stream at `width`x`height`, when this layer is not the highest one.
  DataRate target_bitrate;
  // The minimum bitrate needed for encoding a stream at `width`x`height`.
  DataRate min_bitrate;
};

// From which resolution how many simulcast layers at what bitrates (maximum, target, minimum). High to low.
constexpr SimulcastFormat kSimulcastFormatsVP8[] = {
    {1920, 1080, 3, DataRate::KilobitsPerSec(5000), DataRate::KilobitsPerSec(4000), DataRate::KilobitsPerSec(800)},
    {1280, 720, 3, DataRate::KilobitsPerSec(2500), DataRate::KilobitsPerSec(2500), DataRate::KilobitsPerSec(600)},
    {960, 540, 3, DataRate::KilobitsPerSec(1200), DataRate::KilobitsPerSec(1200), DataRate::KilobitsPerSec(350)},
    {640, 360, 2, DataRate::KilobitsPerSec(700), DataRate::KilobitsPerSec(500), DataRate::KilobitsPerSec(150)},
    {480, 270, 2, DataRate::KilobitsPerSec(450), DataRate::KilobitsPerSec(350), DataRate::KilobitsPerSec(150)},
    {320, 180, 1, DataRate::KilobitsPerSec(200), DataRate::KilobitsPerSec(150), DataRate::KilobitsPerSec(30)},
    // As the resolution goes down, interpolate the target and max bitrates down towards zero. The min bitrate is
    // still limited at 30 kbps and the target and the max will be capped from below accordingly.
    {0, 0, 1, DataRate::KilobitsPerSec(0), DataRate::KilobitsPerSec(0), DataRate::KilobitsPerSec(30)}};

constexpr SimulcastFormat kSimulcastFormatsVP9[] = {
    {1920, 1080, 3, DataRate::KilobitsPerSec(3367), DataRate::KilobitsPerSec(3367), DataRate::KilobitsPerSec(769)},
    {1280, 720, 3, DataRate::KilobitsPerSec(1524), DataRate::KilobitsPerSec(1524), DataRate::KilobitsPerSec(481)},
    {960, 540, 3, DataRate::KilobitsPerSec(879), DataRate::KilobitsPerSec(879), DataRate::KilobitsPerSec(337)},
    {640, 360, 2, DataRate::KilobitsPerSec(420), DataRate::KilobitsPerSec(420), DataRate::KilobitsPerSec(193)},
    {480, 270, 2, DataRate::KilobitsPerSec(257), DataRate::KilobitsPerSec(257), DataRate::KilobitsPerSec(121)},
    {320, 180, 1, DataRate::KilobitsPerSec(142), DataRate::KilobitsPerSec(142), DataRate::KilobitsPerSec(30)},
    {240, 135, 1, DataRate::KilobitsPerSec(101), DataRate::KilobitsPerSec(101), DataRate::KilobitsPerSec(30)},
    {0, 0, 1, DataRate::KilobitsPerSec(0), DataRate::KilobitsPerSec(0), DataRate::KilobitsPerSec(30)}};

DataRate Interpolate(DataRate a, DataRate b, float rate) { return a * (1.0 - rate) + b * static_cast<double>(rate); }

int GetDefaultSimulcastTemporalLayers(VideoCodecType codec) {
  // H.265 has no software fallback, so L1T1 is the only safe initial mode.
  return codec == VideoCodecType::kH265 ? 1 : kDefaultNumTemporalLayers;
}

std::vector<SimulcastFormat> GetSimulcastFormats(bool enable_lowres_bitrate_interpolation, VideoCodecType codec) {
  std::vector<SimulcastFormat> formats;
  switch (codec) {
    case VideoCodecType::kGeneric:
    case VideoCodecType::kVP8:
    case VideoCodecType::kH264:
      formats.assign(std::begin(kSimulcastFormatsVP8), std::end(kSimulcastFormatsVP8));
      break;
    case VideoCodecType::kVP9:
    case VideoCodecType::kAV1:
    case VideoCodecType::kH265:
      formats.assign(std::begin(kSimulcastFormatsVP9), std::end(kSimulcastFormatsVP9));
      break;
  }
  if (!enable_lowres_bitrate_interpolation) {
    SimulcastFormat& format0x0 = formats[formats.size() - 1];
    const SimulcastFormat& format_prev = formats[formats.size() - 2];
    format0x0.max_bitrate = format_prev.max_bitrate;
    format0x0.target_bitrate = format_prev.target_bitrate;
    format0x0.min_bitrate = format_prev.min_bitrate;
  }
  return formats;
}

int FindSimulcastFormatIndex(int width, int height, const std::vector<SimulcastFormat>& formats) {
  for (size_t i = 0; i < formats.size(); ++i) {
    if (width * height >= formats[i].width * formats[i].height) return static_cast<int>(i);
  }
  return -1;
}

SimulcastFormat InterpolateSimulcastFormat(int width, int height, std::optional<double> max_roundup_rate,
                                           bool enable_lowres_bitrate_interpolation, VideoCodecType codec) {
  const auto formats = GetSimulcastFormats(enable_lowres_bitrate_interpolation, codec);
  const int index = FindSimulcastFormatIndex(width, height, formats);
  if (index == 0) return formats[0];
  const auto i = static_cast<size_t>(index);
  const int total_pixels_up = formats[i - 1].width * formats[i - 1].height;
  const int total_pixels_down = formats[i].width * formats[i].height;
  const int total_pixels = width * height;
  const float rate = static_cast<float>(total_pixels_up - total_pixels) /
                     static_cast<float>(total_pixels_up - total_pixels_down);
  // Use the upper resolution's layer count if `rate` is below the configured threshold.
  const size_t max_layers = (rate < max_roundup_rate.value_or(kDefaultMaxRoundupRate)) ? formats[i - 1].max_layers
                                                                                         : formats[i].max_layers;
  return {width,
          height,
          max_layers,
          Interpolate(formats[i - 1].max_bitrate, formats[i].max_bitrate, rate),
          Interpolate(formats[i - 1].target_bitrate, formats[i].target_bitrate, rate),
          Interpolate(formats[i - 1].min_bitrate, formats[i].min_bitrate, rate)};
}

std::vector<VideoStream> GetNormalSimulcastLayers(std::span<const Resolution> resolutions, bool temporal_layers_supported,
                                                  bool base_heavy_tl3_rate_alloc, const SimulcastTrials& trials,
                                                  VideoCodecType codec) {
  const int num_temporal_layers = temporal_layers_supported ? GetDefaultSimulcastTemporalLayers(codec) : 1;
  // Simulcast streams from the lowest resolution (s = 0) to the highest.
  std::vector<VideoStream> layers(resolutions.size());
  for (size_t s = 0; s < resolutions.size(); ++s) {
    layers[s].width = resolutions[s].width;
    layers[s].height = resolutions[s].height;
    layers[s].num_temporal_layers = num_temporal_layers;
    const SimulcastFormat f = InterpolateSimulcastFormat(layers[s].width, layers[s].height, std::nullopt,
                                                         trials.lowres_bitrate_interpolation, codec);
    layers[s].max_bitrate_bps = static_cast<int>(f.max_bitrate.bps());
    layers[s].target_bitrate_bps = static_cast<int>(f.target_bitrate.bps());
    layers[s].min_bitrate_bps = static_cast<int>(f.min_bitrate.bps());
    if (s == 0) {
      // With the base-heavy temporal allocation, the lowest stream's rates are lowered so that its base temporal
      // layer gets what it would with the default allocation.
      float rate_factor = 1.0;
      if (num_temporal_layers == 3 && base_heavy_tl3_rate_alloc) rate_factor = 0.4 / 0.6;
      layers[s].max_bitrate_bps = static_cast<int>(layers[s].max_bitrate_bps * rate_factor);
      layers[s].target_bitrate_bps = static_cast<int>(layers[s].target_bitrate_bps * rate_factor);
    }
    // Ensure consistency.
    layers[s].max_bitrate_bps = std::max(layers[s].min_bitrate_bps, layers[s].max_bitrate_bps);
    layers[s].target_bitrate_bps = std::max(layers[s].min_bitrate_bps, layers[s].target_bitrate_bps);
    layers[s].max_framerate = kDefaultVideoMaxFramerate;
  }
  return layers;
}

std::vector<VideoStream> GetScreenshareLayers(size_t max_layers, int width, int height, bool temporal_layers_supported,
                                              bool base_heavy_tl3_rate_alloc) {
  const size_t num_simulcast_layers = std::min<size_t>(max_layers, kScreenshareMaxSimulcastLayers);
  std::vector<VideoStream> layers(num_simulcast_layers);
  // For legacy screenshare in conference mode, tl0 and tl1 bitrates ride on the target and max bitrates.
  layers[0].width = width;
  layers[0].height = height;
  layers[0].max_framerate = 5;
  layers[0].min_bitrate_bps = kDefaultMinVideoBitrateBps;
  layers[0].target_bitrate_bps = static_cast<int>(kScreenshareDefaultTl0Bitrate.bps());
  layers[0].max_bitrate_bps = static_cast<int>(kScreenshareDefaultTl1Bitrate.bps());
  layers[0].num_temporal_layers = temporal_layers_supported ? 2 : 1;
  if (num_simulcast_layers == static_cast<size_t>(kScreenshareMaxSimulcastLayers)) {
    int max_bitrate_bps;
    bool using_boosted_bitrate = false;
    if (!temporal_layers_supported) {
      // The max bitrate where the base layer would have been with temporal layers.
      max_bitrate_bps = static_cast<int>(
          static_cast<double>(kScreenshareHighStreamMaxBitrate.bps()) *
          SimulcastRateAllocator::GetTemporalRateAllocation(kScreenshareTemporalLayers, 0, base_heavy_tl3_rate_alloc));
    } else {
      max_bitrate_bps = static_cast<int>(kScreenshareHighStreamMaxBitrate.bps());
      using_boosted_bitrate = true;
    }
    layers[1].width = width;
    layers[1].height = height;
    layers[1].max_framerate = kDefaultVideoMaxFramerate;
    layers[1].num_temporal_layers = temporal_layers_supported ? kScreenshareTemporalLayers : 1;
    layers[1].min_bitrate_bps = using_boosted_bitrate ? static_cast<int>(kScreenshareHighStreamMinBitrate.bps())
                                                      : layers[0].target_bitrate_bps * 2;
    layers[1].target_bitrate_bps = max_bitrate_bps;
    layers[1].max_bitrate_bps = max_bitrate_bps;
  }
  return layers;
}

}  // namespace

size_t LimitSimulcastLayerCount(size_t min_num_layers, size_t max_num_layers, int width, int height,
                                const SimulcastTrials& trials, VideoCodecType codec) {
  if (trials.legacy_layer_limit) {
    // The layer count of one higher resolution in the table is used when (pixels_up - pixels) /
    // (pixels_up - pixels_down) is below `max_ratio`.
    const size_t reduced_num_layers =
        std::max(min_num_layers,
                 InterpolateSimulcastFormat(width, height, trials.layer_limit_max_ratio,
                                            /*enable_lowres_bitrate_interpolation=*/false, codec)
                     .max_layers);
    if (max_num_layers > reduced_num_layers) return reduced_num_layers;
  }
  return max_num_layers;
}

void BoostMaxSimulcastLayer(DataRate max_bitrate, std::vector<VideoStream>* layers) {
  if (layers->empty()) return;
  const DataRate total_bitrate = GetTotalMaxBitrate(*layers);
  // Spend the bits that are left on the max layer.
  if (total_bitrate < max_bitrate) layers->back().max_bitrate_bps += static_cast<int>((max_bitrate - total_bitrate).bps());
}

DataRate GetTotalMaxBitrate(const std::vector<VideoStream>& layers) {
  if (layers.empty()) return DataRate::Zero();
  int64_t total_max_bitrate_bps = 0;
  for (size_t s = 0; s + 1 < layers.size(); ++s) total_max_bitrate_bps += layers[s].target_bitrate_bps;
  total_max_bitrate_bps += layers.back().max_bitrate_bps;
  return DataRate::BitsPerSec(total_max_bitrate_bps);
}

std::vector<VideoStream> GetSimulcastConfig(std::span<const Resolution> resolutions,
                                            bool is_screenshare_with_conference_mode, bool temporal_layers_supported,
                                            const SimulcastTrials& trials, VideoCodecType codec) {
  if (resolutions.empty()) return {};
  if (is_screenshare_with_conference_mode) {
    return GetScreenshareLayers(resolutions.size(), resolutions[0].width, resolutions[0].height,
                                temporal_layers_supported, trials.base_heavy_tl3_rate_allocation);
  }
  return GetNormalSimulcastLayers(resolutions, temporal_layers_supported, trials.base_heavy_tl3_rate_allocation, trials,
                                  codec);
}

// ---- video/config/encoder_stream_factory.cc --------------------------------

bool VideoEncoderConfig::HasScaleResolutionDownBy() const {
  for (const VideoStream& l : simulcast_layers) {
    if (l.scale_resolution_down_by >= 1.0) return true;
  }
  return false;
}

bool IsConferenceModeScreenshare(const VideoEncoderConfig& c) {
  return c.content_type == VideoEncoderConfig::ContentType::kScreen && c.legacy_conference_mode &&
         (c.codec_type == VideoCodecType::kVP8 || c.codec_type == VideoCodecType::kH264);
}

bool IsTemporalLayersSupported(VideoCodecType codec) {
  return codec == VideoCodecType::kVP8 || codec == VideoCodecType::kVP9 || codec == VideoCodecType::kAV1 ||
         codec == VideoCodecType::kH265;
}

namespace {

constexpr int kMinLayerSize = 16;

Resolution ScaleResolutionDownBy(const Resolution& resolution, double scale_down_by) {
  if (resolution.width <= kMinLayerSize || resolution.height <= kMinLayerSize) return resolution;
  const int short_side = static_cast<int>(std::min(resolution.width / scale_down_by, resolution.height / scale_down_by));
  if (short_side < kMinLayerSize) {
    // Adjust the scale factor to make the short side equal to kMinLayerSize.
    scale_down_by = std::min(resolution.width / static_cast<double>(kMinLayerSize),
                             resolution.height / static_cast<double>(kMinLayerSize));
  }
  return {static_cast<int>(std::round(resolution.width / scale_down_by)),
          static_cast<int>(std::round(resolution.height / scale_down_by))};
}

bool PowerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }

bool IsScaleFactorsPowerOfTwo(const VideoEncoderConfig& config) {
  for (const auto& layer : config.simulcast_layers) {
    const double scale = std::max(layer.scale_resolution_down_by, 1.0);
    if (std::round(scale) != scale || !PowerOfTwo(static_cast<int>(scale))) return false;
  }
  return true;
}

size_t FindRequiredActiveLayers(const VideoEncoderConfig& config) {
  // Need enough layers so that at least the first active one is present.
  for (size_t i = 0; i < config.number_of_streams; ++i) {
    if (config.simulcast_layers[i].active) return i + 1;
  }
  return 0;
}

// The selected thresholds for QVGA and VGA corresponded to a QP around 10.
int GetMaxDefaultVideoBitrateKbps(int width, int height, bool is_screenshare) {
  int max_bitrate;
  if (width * height <= 320 * 240) {
    max_bitrate = 600;
  } else if (width * height <= 640 * 480) {
    max_bitrate = 1700;
  } else if (width * height <= 960 * 540) {
    max_bitrate = 2000;
  } else {
    max_bitrate = 2500;
  }
  if (is_screenshare) max_bitrate = std::max(max_bitrate, 1200);
  return max_bitrate;
}

int GetDefaultMaxQp(VideoCodecType codec) {
  switch (codec) {
    case VideoCodecType::kH264:
    case VideoCodecType::kH265:
      return kDefaultVideoMaxQpH26x;
    case VideoCodecType::kVP8:
    case VideoCodecType::kVP9:
    case VideoCodecType::kGeneric:
      return kDefaultVideoMaxQpVpx;
    case VideoCodecType::kAV1:
      return kDefaultVideoMaxQpAv1;
  }
  return kDefaultVideoMaxQpVpx;
}

Resolution NormalizeResolution(const Resolution& resolution, const VideoEncoderConfig& config, int max_num_layers) {
  if (resolution.width < kMinLayerSize || resolution.height < kMinLayerSize) return resolution;
  // NormalizeSimulcastSizeExperiment is unset by default. Without scale_resolution_down_by, 1/2 steps; with it,
  // the resolution is normalized to a multiple of the largest factor when all factors are powers of two.
  std::optional<int> log2_scale_factor;
  if (!config.HasScaleResolutionDownBy()) {
    log2_scale_factor = max_num_layers - 1;
  } else if (IsScaleFactorsPowerOfTwo(config)) {
    double max_scale_factor = -1;
    for (const auto& l : config.simulcast_layers) max_scale_factor = std::max(max_scale_factor, l.scale_resolution_down_by);
    if (max_scale_factor > 1.0) log2_scale_factor = static_cast<int>(std::log2(max_scale_factor));
  }
  if (!log2_scale_factor || resolution.width < (1 << *log2_scale_factor) ||
      resolution.height < (1 << *log2_scale_factor)) {
    return resolution;  // Too large a scale factor.
  }
  return {(resolution.width >> *log2_scale_factor) << *log2_scale_factor,
          (resolution.height >> *log2_scale_factor) << *log2_scale_factor};
}

// Overrides the bitrate limits and other stream settings with the values from `config.simulcast_layers`, which
// come from RtpEncodingParameters.
void OverrideStreamSettings(const VideoEncoderConfig& config, const std::optional<DataRate>& experimental_min_bitrate,
                            std::vector<VideoStream>& layers) {
  // An experiment may override the minimum bitrate of the lowest layer; it has the lowest priority.
  layers[0].min_bitrate_bps =
      static_cast<int>(experimental_min_bitrate.value_or(DataRate::BitsPerSec(kDefaultMinVideoBitrateBps)).bps());
  const bool temporal_layers_supported = IsTemporalLayersSupported(config.codec_type);
  for (size_t i = 0; i < layers.size(); ++i) {
    const VideoStream& overrides = config.simulcast_layers[i];
    VideoStream& layer = layers[i];
    // Zero is a configured value meaning "do not send this layer"; only a negative value means unset.
    const bool max_bitrate_configured = overrides.max_bitrate_bps >= 0;
    layer.active = overrides.active;
    layer.scale_resolution_down_by = overrides.scale_resolution_down_by;
    if (overrides.num_temporal_layers.value_or(0) > 0 && temporal_layers_supported) {
      layer.num_temporal_layers = *overrides.num_temporal_layers;
    }
    if (overrides.max_framerate > 0) layer.max_framerate = overrides.max_framerate;
    if (overrides.min_bitrate_bps > 0) layer.min_bitrate_bps = overrides.min_bitrate_bps;
    if (max_bitrate_configured) layer.max_bitrate_bps = overrides.max_bitrate_bps;
    if (overrides.target_bitrate_bps > 0) layer.target_bitrate_bps = overrides.target_bitrate_bps;
    if (overrides.min_bitrate_bps > 0 && max_bitrate_configured) {
      // Min and max configured: target at 3/4 of the max (or the max if that is below the min).
      if (overrides.target_bitrate_bps <= 0) layer.target_bitrate_bps = layer.max_bitrate_bps * 3 / 4;
      if (layer.target_bitrate_bps < layer.min_bitrate_bps) layer.target_bitrate_bps = layer.max_bitrate_bps;
    } else if (overrides.min_bitrate_bps > 0) {
      // Only the min configured: target and max at least the min.
      layer.target_bitrate_bps = std::max(layer.target_bitrate_bps, layer.min_bitrate_bps);
      layer.max_bitrate_bps = std::max(layer.max_bitrate_bps, layer.min_bitrate_bps);
    } else if (max_bitrate_configured) {
      // Only the max configured: min and target at most the max; the target at least 3/4 of it unless set.
      layer.min_bitrate_bps = std::min(layer.min_bitrate_bps, layer.max_bitrate_bps);
      if (overrides.target_bitrate_bps <= 0) {
        layer.target_bitrate_bps = std::max(layer.target_bitrate_bps, layer.max_bitrate_bps * 3 / 4);
      }
      layer.target_bitrate_bps = std::clamp(layer.target_bitrate_bps, layer.min_bitrate_bps, layer.max_bitrate_bps);
    }
    if (overrides.max_qp > 0) {
      layer.max_qp = overrides.max_qp;
    } else if (config.max_qp > 0) {
      layer.max_qp = config.max_qp;
    } else {
      layer.max_qp = GetDefaultMaxQp(config.codec_type);
    }
  }
  const bool is_highest_layer_max_bitrate_configured = config.simulcast_layers[layers.size() - 1].max_bitrate_bps >= 0;
  const bool is_screencast = config.content_type == VideoEncoderConfig::ContentType::kScreen;
  if (!is_screencast && !is_highest_layer_max_bitrate_configured && config.max_bitrate_bps > 0) {
    // No application maximum for the largest layer: any leftover goes to it.
    BoostMaxSimulcastLayer(DataRate::BitsPerSec(config.max_bitrate_bps), &layers);
  }
  // Sort the layers by max_bitrate_bps; they might not always be from smallest to biggest.
  std::vector<size_t> index(layers.size());
  std::iota(index.begin(), index.end(), 0);
  std::stable_sort(index.begin(), index.end(),
                   [&layers](size_t a, size_t b) { return layers[a].max_bitrate_bps < layers[b].max_bitrate_bps; });
  if (!layers[index[0]].active) {
    // Let the first active layer go as low as the lowest (now inactive) layer could; otherwise a single HD stream
    // would always be allocated its 600 kbps minimum.
    const int min_configured_bitrate = layers[index[0]].min_bitrate_bps;
    for (size_t i = 0; i < layers.size(); ++i) {
      if (layers[index[i]].active) {
        layers[index[i]].min_bitrate_bps = min_configured_bitrate;
        break;
      }
    }
  }
}

std::vector<Resolution> GetStreamResolutions(const SimulcastTrials& trials, int width, int height,
                                             const VideoEncoderConfig& config) {
  std::vector<Resolution> resolutions;
  if (IsConferenceModeScreenshare(config)) {
    for (size_t i = 0; i < config.number_of_streams; ++i) resolutions.push_back({width, height});
    return resolutions;
  }
  const size_t min_num_layers = FindRequiredActiveLayers(config);
  const size_t max_num_layers =
      LimitSimulcastLayerCount(min_num_layers, config.number_of_streams, width, height, trials, config.codec_type);
  const Resolution norm = NormalizeResolution({width, height}, config, static_cast<int>(max_num_layers));
  const bool has_scale_resolution_down_by = config.HasScaleResolutionDownBy();
  resolutions.resize(max_num_layers);
  for (size_t i = 0; i < max_num_layers; i++) {
    if (has_scale_resolution_down_by) {
      resolutions[i] = ScaleResolutionDownBy(norm, std::max(config.simulcast_layers[i].scale_resolution_down_by, 1.0));
    } else {
      // Resolutions with the default 1/2 scale factor, from low to high.
      resolutions[i].width = norm.width >> (max_num_layers - i - 1);
      resolutions[i].height = norm.height >> (max_num_layers - i - 1);
    }
  }
  return resolutions;
}

std::vector<VideoStream> CreateDefaultVideoStreams(int width, int height, const VideoEncoderConfig& config,
                                                   const std::optional<DataRate>& experimental_min_bitrate) {
  const bool is_screencast = config.content_type == VideoEncoderConfig::ContentType::kScreen;
  const VideoStream& first = config.simulcast_layers[0];
  // The max bitrate from the API: the first encoding's, then b=AS / x-google-max-bitrate.
  const bool encoding_max_bitrate_configured = first.max_bitrate_bps >= 0;
  std::optional<int> api_max_bitrate_bps;
  if (encoding_max_bitrate_configured) api_max_bitrate_bps = first.max_bitrate_bps;
  if (config.max_bitrate_bps > 0) {
    api_max_bitrate_bps = api_max_bitrate_bps.has_value() ? std::min(config.max_bitrate_bps, *api_max_bitrate_bps)
                                                          : config.max_bitrate_bps;
  }
  int max_bitrate_bps = api_max_bitrate_bps.has_value()
                            ? *api_max_bitrate_bps
                            : GetMaxDefaultVideoBitrateKbps(width, height, is_screencast) * 1000;
  int min_bitrate_bps = experimental_min_bitrate ? static_cast<int>(experimental_min_bitrate->bps())
                                                 : kDefaultMinVideoBitrateBps;
  if (first.min_bitrate_bps > 0) {
    min_bitrate_bps = first.min_bitrate_bps;
    if (!api_max_bitrate_bps.has_value()) max_bitrate_bps = std::max(min_bitrate_bps, max_bitrate_bps);
  }
  const int max_framerate = first.max_framerate > 0 ? first.max_framerate : kDefaultVideoMaxFramerate;
  VideoStream layer;
  layer.max_framerate = max_framerate;
  layer.scale_resolution_down_by = first.scale_resolution_down_by;
  layer.active = std::any_of(config.simulcast_layers.begin(), config.simulcast_layers.end(),
                             [](const VideoStream& l) { return l.active; });
  if (first.scale_resolution_down_by > 1.) {
    const Resolution res = ScaleResolutionDownBy({width, height}, first.scale_resolution_down_by);
    layer.width = res.width;
    layer.height = res.height;
  } else {
    layer.width = width;
    layer.height = height;
  }
  // An application max below the min is adjusted down (bugs.webrtc.org/9141).
  layer.min_bitrate_bps = std::min(min_bitrate_bps, max_bitrate_bps);
  layer.target_bitrate_bps =
      first.target_bitrate_bps <= 0 ? max_bitrate_bps : std::min(first.target_bitrate_bps, max_bitrate_bps);
  layer.max_bitrate_bps = max_bitrate_bps;
  layer.bitrate_priority = config.bitrate_priority;
  layer.max_qp = config.max_qp > 0 ? config.max_qp : GetDefaultMaxQp(config.codec_type);
  if (IsTemporalLayersSupported(config.codec_type) && first.num_temporal_layers) {
    layer.num_temporal_layers = *first.num_temporal_layers;
  }
  return {layer};
}

}  // namespace

std::vector<VideoStream> CreateEncoderStreams(const SimulcastTrials& trials, int frame_width, int frame_height,
                                              const VideoEncoderConfig& config) {
  const std::optional<DataRate>& experimental_min_bitrate = trials.experimental_min_bitrate;
  const bool is_simulcast = config.number_of_streams > 1;
  std::vector<VideoStream> streams;
  if (is_simulcast || IsConferenceModeScreenshare(config)) {
    const std::vector<Resolution> resolutions = GetStreamResolutions(trials, frame_width, frame_height, config);
    // Legacy simulcast screenshare if conference mode is on, else the generic simulcast configuration.
    streams = GetSimulcastConfig(resolutions, IsConferenceModeScreenshare(config),
                                 IsTemporalLayersSupported(config.codec_type), trials, config.codec_type);
    OverrideStreamSettings(config, experimental_min_bitrate, streams);
  } else {
    streams = CreateDefaultVideoStreams(frame_width, frame_height, config, experimental_min_bitrate);
  }
  // The bitrate priority is per sender, so it is set on the first layer only.
  if (!streams.empty()) streams[0].bitrate_priority = config.bitrate_priority;
  return streams;
}

}  // namespace sm::video
