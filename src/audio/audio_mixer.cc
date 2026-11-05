// libwebrtc M157's receive-side mixer (#1175): modules/audio_mixer/{audio_mixer_impl,frame_combiner,
// default_output_rate_calculator,audio_frame_manipulator}.cc, with the parts of api/audio/audio_frame.cc,
// audio/utility/audio_frame_operations.cc, audio/utility/channel_mixer.cc and common_audio/audio_util.cc
// they use. See sm/audio/audio_mixer.h.
#include "sm/audio/audio_mixer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>

namespace sm::audio {

// ---------------------------------------------------------------------------- api/audio/audio_frame.cc

void AudioFrame::UpdateFrame(uint32_t ts, const int16_t* data, size_t spc, int rate, size_t channels) {
  assert(channels <= kMaxNumberOfAudioChannels);
  timestamp = ts;
  samples_per_channel = spc;
  sample_rate_hz = rate;
  num_channels = channels;
  const size_t length = spc * channels;
  assert(length <= data_.size());
  if (data != nullptr) {
    std::copy_n(data, std::min(length, data_.size()), data_.begin());
    muted_ = false;
  } else {
    muted_ = true;
  }
}

void AudioFrame::CopyFrom(const AudioFrame& src) {
  if (this == &src) return;
  if (muted_ && !src.muted()) data_.fill(0);
  timestamp = src.timestamp;
  elapsed_time_ms = src.elapsed_time_ms;
  ntp_time_ms = src.ntp_time_ms;
  packet_infos = src.packet_infos;
  muted_ = src.muted();
  samples_per_channel = src.samples_per_channel;
  sample_rate_hz = src.sample_rate_hz;
  num_channels = src.num_channels;
  const std::span<const int16_t> d = src.data();
  if (!muted_ && !d.empty()) std::copy(d.begin(), d.end(), data_.begin());
}

std::span<const int16_t> AudioFrame::data() const {
  static const std::array<int16_t, kMaxDataSizeSamples> kZeros{};
  const size_t n = std::min(samples_per_channel * num_channels, kMaxDataSizeSamples);
  return std::span<const int16_t>(muted_ ? kZeros.data() : data_.data(), n);
}

std::span<int16_t> AudioFrame::mutable_data() {
  if (muted_) {
    data_.fill(0);
    muted_ = false;
  }
  return data_;
}

std::span<int16_t> AudioFrame::mutable_data(size_t spc, size_t channels) {
  const size_t total = spc * channels;
  assert(total <= data_.size());
  assert(channels <= kMaxNumberOfAudioChannels);
  if (muted_) {
    std::fill_n(data_.begin(), std::min(total, data_.size()), int16_t{0});
    muted_ = false;
  }
  samples_per_channel = spc;
  num_channels = channels;
  return std::span<int16_t>(data_.data(), std::min(total, data_.size()));
}

// ---------------------------------------------------------------------------- common_audio/include/audio_util.h

int16_t FloatS16ToS16(float v) {
  v = std::min(v, 32767.f);
  v = std::max(v, -32768.f);
  return static_cast<int16_t>(v + std::copysign(0.5f, v));
}

// ---------------------------------------------------------------------------- audio_frame_manipulator.cc

uint32_t AudioMixerCalculateEnergy(const AudioFrame& audio_frame) {
  if (audio_frame.muted()) return 0;
  uint32_t energy = 0;
  const std::span<const int16_t> d = audio_frame.data();
  for (int16_t v : d) {
    // libwebrtc: `energy += frame_data[i] * frame_data[i]` (an int product, wrapping in uint32).
    energy += static_cast<uint32_t>(static_cast<int>(v) * static_cast<int>(v));
  }
  return energy;
}

void Ramp(float start_gain, float target_gain, AudioFrame* audio_frame) {
  assert(audio_frame);
  assert(start_gain >= 0.0f && target_gain >= 0.0f);
  if (start_gain == target_gain || audio_frame->muted()) return;
  const size_t samples = audio_frame->samples_per_channel;
  assert(samples > 0);
  const float increment = (target_gain - start_gain) / static_cast<float>(samples);
  float gain = start_gain;
  const size_t channels = audio_frame->num_channels;
  std::span<int16_t> d = audio_frame->mutable_data();
  for (size_t i = 0; i < samples; ++i) {
    // The same gain for the i-th sample of every channel; `int16_t *= float` truncates.
    for (size_t ch = 0; ch < channels; ++ch) {
      int16_t& s = d[channels * i + ch];
      s = static_cast<int16_t>(static_cast<float>(s) * gain);
    }
    gain += increment;
  }
}

namespace {

// AudioFrameOperations::DownmixChannels(dst_channels = 1, frame): DownmixInterleavedToMono<int16_t>
// (common_audio/audio_util.cc), an int32 sum divided by the channel count.
void DownmixToMono(AudioFrame* frame) {
  if (!frame->muted()) {
    const size_t channels = frame->num_channels;
    const size_t spc = frame->samples_per_channel;
    std::span<int16_t> d = frame->mutable_data();
    for (size_t i = 0; i < spc; ++i) {
      int32_t value = d[i * channels];
      for (size_t ch = 1; ch < channels; ++ch) value += d[i * channels + ch];
      d[i] = static_cast<int16_t>(value / static_cast<int32_t>(channels));
    }
  }
  frame->num_channels = 1;
}

// AudioFrameOperations::UpmixChannels: in place, backwards; a muted frame only changes its count.
void UpmixFromMono(size_t target, AudioFrame* frame) {
  if (frame->num_channels != 1 || frame->samples_per_channel * target > AudioFrame::kMaxDataSizeSamples) return;
  if (!frame->muted()) {
    std::span<int16_t> d = frame->mutable_data(frame->samples_per_channel, target);
    for (size_t i = frame->samples_per_channel; i-- > 0;) {
      for (size_t j = 0; j < target; ++j) d[target * i + j] = d[i];
    }
  } else {
    frame->num_channels = target;
  }
}

// ChannelMixer::Transform with ChannelMixingMatrix's VoIP case: mono into N >= 3 channels puts the
// sample in front left and right (matrix rows 0 and 1) and zeros in the others.
void MonoToVoipLayout(size_t target, AudioFrame* frame) {
  if (frame->muted()) {
    frame->num_channels = target;
    return;
  }
  const size_t spc = frame->samples_per_channel;
  assert(spc * target <= AudioFrame::kMaxDataSizeSamples);
  std::span<int16_t> d = frame->mutable_data(spc, target);
  for (size_t i = spc; i-- > 0;) {
    const int16_t v = d[i];
    for (size_t ch = 0; ch < target; ++ch) d[target * i + ch] = ch < 2 ? v : int16_t{0};
  }
}

}  // namespace

bool RemixFrame(size_t target_number_of_channels, AudioFrame* frame) {
  assert(target_number_of_channels >= 1);
  if (frame->num_channels == target_number_of_channels) return true;
  // Mono <-> stereo (and stereo -> mono) through the legacy AudioFrameOperations, as libwebrtc.
  if (target_number_of_channels < 3 && frame->num_channels < 3) {
    if (frame->num_channels > target_number_of_channels) {
      DownmixToMono(frame);
    } else {
      UpmixFromMono(target_number_of_channels, frame);
    }
    return frame->num_channels == target_number_of_channels;
  }
  // The generic ChannelMixer: only its mono-input case is ported.
  if (frame->num_channels == 1 && target_number_of_channels <= FrameCombiner::kMaximumNumberOfChannels) {
    MonoToVoipLayout(target_number_of_channels, frame);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------- default_output_rate_calculator.cc

int DefaultOutputRateCalculator::CalculateOutputRateFromRange(std::span<const int> preferred_sample_rates) {
  if (preferred_sample_rates.empty()) return kDefaultFrequency;
  const int maximal_frequency = *std::max_element(preferred_sample_rates.begin(), preferred_sample_rates.end());
  static constexpr int kNativeRates[] = {8000, 16000, 32000, 48000};
  assert(maximal_frequency >= kNativeRates[0] && maximal_frequency <= kNativeRates[3]);
  const auto* rounded_up = std::lower_bound(std::begin(kNativeRates), std::end(kNativeRates), maximal_frequency);
  // libwebrtc DCHECKs that it is found; above 48 kHz, stay at 48 kHz here.
  return rounded_up == std::end(kNativeRates) ? kNativeRates[3] : *rounded_up;
}

// ---------------------------------------------------------------------------- frame_combiner.cc

namespace {

void SetAudioFrameFields(std::span<AudioFrame* const> mix_list, size_t number_of_channels, int sample_rate,
                         AudioFrame* out) {
  const size_t samples_per_channel = SampleRateToDefaultChannelSize(sample_rate);
  // The timestamp is a dummy 0 (bugs.webrtc.org/3390), then the mixed frames' minimum.
  out->UpdateFrame(0, nullptr, samples_per_channel, sample_rate, number_of_channels);
  if (mix_list.empty()) {
    out->elapsed_time_ms = -1;
    return;
  }
  out->timestamp = mix_list[0]->timestamp;
  out->elapsed_time_ms = mix_list[0]->elapsed_time_ms;
  out->ntp_time_ms = mix_list[0]->ntp_time_ms;
  std::vector<AudioFramePacketInfo> packet_infos;
  for (const AudioFrame* frame : mix_list) {
    out->timestamp = std::min(out->timestamp, frame->timestamp);
    out->ntp_time_ms = std::min(out->ntp_time_ms, frame->ntp_time_ms);
    out->elapsed_time_ms = std::max(out->elapsed_time_ms, frame->elapsed_time_ms);
    packet_infos.insert(packet_infos.end(), frame->packet_infos.begin(), frame->packet_infos.end());
  }
  out->packet_infos = std::move(packet_infos);
}

void MixFewFramesWithNoLimiter(std::span<AudioFrame* const> mix_list, AudioFrame* out) {
  if (mix_list.empty()) {
    out->Mute();
    return;
  }
  assert(mix_list.size() <= 1);
  const std::span<const int16_t> src = mix_list[0]->data();
  std::span<int16_t> dst = out->mutable_data(mix_list[0]->samples_per_channel, mix_list[0]->num_channels);
  std::copy_n(src.begin(), std::min(src.size(), dst.size()), dst.begin());
}

// FloatS16 sum, deinterleaved into `mixing` (channel c at c * spc).
void MixToFloatFrame(std::span<AudioFrame* const> mix_list, std::span<float> mixing, size_t spc,
                     size_t number_of_channels) {
  std::fill(mixing.begin(), mixing.end(), 0.0f);
  for (const AudioFrame* frame : mix_list) {
    const std::span<const int16_t> frame_data = frame->data();
    assert(!frame_data.empty());
    for (size_t j = 0; j < number_of_channels; ++j) {
      float* channel = mixing.data() + j * spc;
      for (size_t k = 0; k < spc; ++k) channel[k] += frame_data[number_of_channels * k + j];
    }
  }
}

// Interleaves and rounds.
void InterleaveToAudioFrame(std::span<const float> mixing, size_t spc, size_t number_of_channels, AudioFrame* out) {
  std::span<int16_t> d = out->mutable_data(spc, number_of_channels);
  for (size_t i = 0; i < number_of_channels; ++i) {
    const float* channel = mixing.data() + i * spc;
    for (size_t j = 0; j < spc; ++j) d[number_of_channels * j + i] = FloatS16ToS16(channel[j]);
  }
}

}  // namespace

FrameCombiner::FrameCombiner(bool use_limiter)
    : limiter_(static_cast<int>(kMaximumChannelSize)), use_limiter_(use_limiter) {
  static_assert(kMaximumChannelSize * kMaximumNumberOfChannels <= AudioFrame::kMaxDataSizeSamples);
}

void FrameCombiner::Combine(std::span<AudioFrame* const> mix_list_in, size_t number_of_channels, int sample_rate,
                            size_t number_of_streams, AudioFrame* audio_frame_for_mixing) {
  assert(audio_frame_for_mixing);
  assert(sample_rate > 0);
  // `mix_list` may be empty (FrameCombiner.CombiningZeroFramesShouldProduceSilence).
  number_of_channels = std::min(number_of_channels, kMaximumNumberOfChannels);

  // Each frame's channel count may differ from the mix's: remix in place. A layout the port doesn't
  // have leaves the frame out (libwebrtc's ChannelMixer would remix it).
  kept_.clear();
  for (AudioFrame* frame : mix_list_in) {
    if (RemixFrame(number_of_channels, frame)) {
      kept_.push_back(frame);
    } else {
      ++remix_unsupported_;
    }
  }
  const std::span<AudioFrame* const> mix_list(kept_);

  SetAudioFrameFields(mix_list, number_of_channels, sample_rate, audio_frame_for_mixing);
  size_t samples_per_channel = SampleRateToDefaultChannelSize(sample_rate);
  for ([[maybe_unused]] const AudioFrame* frame : mix_list) {
    assert(frame->samples_per_channel == samples_per_channel);
    assert(frame->sample_rate_hz == sample_rate);
  }

  if (number_of_streams <= 1) {
    MixFewFramesWithNoLimiter(mix_list, audio_frame_for_mixing);
    return;
  }

  // libwebrtc DCHECKs this and clamps in release builds (FrameCombinerDeathTest.DebugBuildCrashesWithHighRate).
  assert(samples_per_channel <= kMaximumChannelSize);
  samples_per_channel = std::min(samples_per_channel, kMaximumChannelSize);
  const std::span<float> mixing(mixing_buffer_.data(), samples_per_channel * number_of_channels);
  MixToFloatFrame(mix_list, mixing, samples_per_channel, number_of_channels);

  if (use_limiter_) {
    limiter_.SetSamplesPerChannel(static_cast<int>(samples_per_channel));
    limiter_.Process(mixing, static_cast<int>(number_of_channels));
  }

  InterleaveToAudioFrame(mixing, samples_per_channel, number_of_channels, audio_frame_for_mixing);
}

// ---------------------------------------------------------------------------- audio_mixer_impl.cc

AudioMixerImpl::AudioMixerImpl() : AudioMixerImpl(std::make_unique<DefaultOutputRateCalculator>(), true) {}

AudioMixerImpl::AudioMixerImpl(std::unique_ptr<OutputRateCalculator> output_rate_calculator, bool use_limiter)
    : output_rate_calculator_(std::move(output_rate_calculator)), frame_combiner_(use_limiter) {}

bool AudioMixerImpl::AddSource(AudioMixerSource* audio_source) {
  assert(audio_source);
  assert(std::none_of(audio_source_list_.begin(), audio_source_list_.end(),
                      [&](const auto& s) { return s->audio_source == audio_source; }) &&
         "Source already added to mixer");
  audio_source_list_.push_back(std::make_unique<SourceStatus>(audio_source));
  audio_to_mix_.resize(audio_source_list_.size());
  preferred_rates_.resize(audio_source_list_.size());
  // UpdateSourceCountStats: the histogram logs whenever the highest count grows.
  stats_.max_source_count = std::max(stats_.max_source_count, audio_source_list_.size());
  return true;
}

void AudioMixerImpl::RemoveSource(AudioMixerSource* audio_source) {
  assert(audio_source);
  const auto it = std::find_if(audio_source_list_.begin(), audio_source_list_.end(),
                               [&](const auto& s) { return s->audio_source == audio_source; });
  assert(it != audio_source_list_.end() && "Source not present in mixer");
  if (it != audio_source_list_.end()) audio_source_list_.erase(it);
}

void AudioMixerImpl::Mix(size_t number_of_channels, AudioFrame* audio_frame_for_mixing) {
  assert(number_of_channels >= 1);
  const size_t number_of_streams = audio_source_list_.size();
  for (size_t i = 0; i < number_of_streams; ++i) {
    preferred_rates_[i] = audio_source_list_[i]->audio_source->PreferredSampleRate();
  }
  const int output_frequency = output_rate_calculator_->CalculateOutputRateFromRange(
      std::span<const int>(preferred_rates_.data(), number_of_streams));

  const auto& curve = frame_combiner_.limiter().gain_curve_stats();
  const uint64_t limited_before =
      curve.look_ups_knee_region + curve.look_ups_limiter_region + curve.look_ups_saturation_region;

  const std::span<AudioFrame* const> mix = GetAudioFromSources(output_frequency);
  frame_combiner_.Combine(mix, number_of_channels, output_frequency, number_of_streams, audio_frame_for_mixing);

  ++stats_.mixes;
  stats_.frames_mixed += mix.size();
  stats_.last_mixed_sources = mix.size();
  stats_.last_output_rate_hz = output_frequency;
  if (curve.look_ups_knee_region + curve.look_ups_limiter_region + curve.look_ups_saturation_region !=
      limited_before) {
    ++stats_.limited_mixes;
  }
}

std::span<AudioFrame* const> AudioMixerImpl::GetAudioFromSources(int output_frequency) {
  size_t audio_to_mix_count = 0;
  for (auto& source_and_status : audio_source_list_) {
    const auto info =
        source_and_status->audio_source->GetAudioFrameWithInfo(output_frequency, &source_and_status->audio_frame);
    switch (info) {
      case AudioMixerSource::AudioFrameInfo::kError:
        ++stats_.frames_error;  // libwebrtc logs a warning.
        break;
      case AudioMixerSource::AudioFrameInfo::kMuted:
        ++stats_.frames_muted;
        break;
      case AudioMixerSource::AudioFrameInfo::kNormal:
        audio_to_mix_[audio_to_mix_count++] = &source_and_status->audio_frame;
        break;
    }
  }
  return std::span<AudioFrame* const>(audio_to_mix_.data(), audio_to_mix_count);
}

void AudioMixerImpl::ExportMetrics(MetricsWriter& w) const {
  w.Counter("mixes", stats_.mixes);
  w.Counter("frames_mixed", stats_.frames_mixed);
  w.Counter("frames_muted", stats_.frames_muted);
  w.Counter("frames_error", stats_.frames_error);
  w.Counter("limited_mixes", stats_.limited_mixes);
  w.Counter("remix_unsupported", frame_combiner_.remix_unsupported());
  w.Gauge("sources", static_cast<double>(audio_source_list_.size()));
  w.Gauge("max_source_count", static_cast<double>(stats_.max_source_count));
  w.Gauge("output_rate_hz", static_cast<double>(stats_.last_output_rate_hz));
  w.Gauge("mixed_sources", static_cast<double>(stats_.last_mixed_sources));
  w.Gauge("limiter_gain", static_cast<double>(frame_combiner_.limiter().last_scaling_factor()));
}

}  // namespace sm::audio
