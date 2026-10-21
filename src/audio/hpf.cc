// libwebrtc M157's HighPassFilter (`apm v2`, #239); see sm/audio/hpf.h.
#include "sm/audio/hpf.h"

#include <array>

namespace sm::audio::hpf {

namespace {
using Coefficients = std::span<const CascadedBiQuadFilter::BiQuadCoefficients>;

// high_pass_filter.cc, kHighPassFilterCoefficients16kHz/32kHz/48kHz.
constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 3> kHighPassFilterCoefficients16kHz = {{
    {{0.8773539420715290582f, -1.754683920749088077f, 0.8773539420715289472f},
     {-1.881687317862849707f, 0.8880584644559580410f}},
    {{1.0f, -1.999810143464515022f, 1.0f}, {-1.976035417167170793f, 0.9779708644868606582f}},
    {{1.0f, -1.999669231394235469f, 1.0f}, {-1.994265767864654482f, 0.9954861594635392441f}},
}};

constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 3> kHighPassFilterCoefficients32kHz = {{
    {{0.9102055685511306615f, -1.820404922871161624f, 0.9102055685511306615f},
     {-1.940710875829138482f, 0.9423512845457852061f}},
    {{1.0f, -1.999952541587768806f, 1.0f}, {-1.988434609801665420f, 0.9889212529819323416f}},
    {{1.0f, -1.999917315632020021f, 1.0f}, {-1.997434723613889629f, 0.9977401885079651978f}},
}};

constexpr std::array<CascadedBiQuadFilter::BiQuadCoefficients, 3> kHighPassFilterCoefficients48kHz = {{
    {{0.9213790163564168f, -1.8427552370064049f, 0.9213790163564168f}, {-1.9604500061078971f, 0.9611862979079667f}},
    {{1.0f, -1.9999789078432082f, 1.0f}, {-1.9923834169149972f, 0.9926001112941157f}},
    {{1.0f, -1.9999632520325810f, 1.0f}, {-1.9983570340145236f, 0.9984928491805198f}},
}};

Coefficients ChooseCoefficients(int sample_rate_hz) {
  switch (sample_rate_hz) {
    case 32000:
      return kHighPassFilterCoefficients32kHz;
    case 48000:
      return kHighPassFilterCoefficients48kHz;
    default:  // 16000; libwebrtc DCHECKs on other rates and falls back here.
      return kHighPassFilterCoefficients16kHz;
  }
}
}  // namespace

HighPassFilter::HighPassFilter(int sample_rate_hz, size_t num_channels) : sample_rate_hz_(sample_rate_hz) {
  filters_.reserve(num_channels);
  for (size_t k = 0; k < num_channels; ++k) filters_.emplace_back(ChooseCoefficients(sample_rate_hz_));
}

void HighPassFilter::Process(std::span<float> x, size_t channel) {
  if (channel >= filters_.size()) return;
  filters_[channel].Process(x);
  if (channel == 0) samples_ += x.size();
}

void HighPassFilter::Process(std::vector<std::vector<float>>& audio) {
  for (size_t k = 0; k < audio.size() && k < filters_.size(); ++k) Process(audio[k], k);
}

void HighPassFilter::Reset() {
  for (auto& f : filters_) f.Reset();
}

void HighPassFilter::Reset(size_t num_channels) {
  const size_t old_num_channels = filters_.size();
  if (num_channels < old_num_channels) {
    filters_.erase(filters_.begin() + static_cast<std::ptrdiff_t>(num_channels), filters_.end());
    Reset();
    return;
  }
  for (auto& f : filters_) f.Reset();
  for (size_t k = old_num_channels; k < num_channels; ++k) filters_.emplace_back(ChooseCoefficients(sample_rate_hz_));
}

void HighPassFilter::ExportMetrics(MetricsWriter& w) const {
  w.Counter("samples", samples_);
  w.Gauge("sample_rate_hz", sample_rate_hz_);
  w.Gauge("channels", static_cast<double>(filters_.size()));
}

}  // namespace sm::audio::hpf
