// 6.13 Receive-side audio mixing for group calls: libwebrtc M157's `AudioMixerImpl` (#1175).
//
// Ported from compare/webrtc/reference/modules/audio_mixer/ and api/audio/audio_mixer.h.
//
// - AudioMixer::Source (api/audio/audio_mixer.h): one remote stream. The mixer pulls 10 ms of
// int16 audio from it at the mix rate with GetAudioFrameWithInfo, which says whether the frame
// is normal, muted (counts as zeros) or an error (left out).
//
// - DefaultOutputRateCalculator (default_output_rate_calculator.cc): the mix rate is the highest
// source's preferred rate rounded up to a native rate (8, 16, 32 or 48 kHz); 48 kHz with no
// source.
//
// - AudioMixerImpl (audio_mixer_impl.cc): per Mix, asks every source for its preferred rate,
// picks the output rate, pulls a frame from every source and hands the kNormal ones to the
// FrameCombiner. M157 mixes **every** unmuted source: there is no "3 loudest" selection and no
// ramp-in/out in M157's AudioMixerImpl (both were in older releases; audio_frame_manipulator.cc
// still has `Ramp` and `AudioMixerCalculateEnergy`, ported below, but M157's mixer calls neither).
//
// - FrameCombiner (frame_combiner.cc): remixes each frame to the output channel count. With at
// most one registered source it copies the frame (or mutes the output); otherwise it sums the
// frames in FloatS16, runs AGC2's `Limiter` (the library's port, agc2.h, made for 480 samples per
// channel as `Limiter(..., kMaximumChannelSize, "AudioMixer")`) when `use_limiter` (the default),
// and rounds back to int16 (FloatS16ToS16). The output's timestamp and NTP time are the minimum
// over the mixed frames, its elapsed time the maximum, and its packet infos their union.
//
// - audio_frame_manipulator.cc: RemixFrame (mono <-> stereo through AudioFrameOperations'
// Upmix/DownmixChannels, an int32 sum and truncating division), Ramp and
// AudioMixerCalculateEnergy.
//
// Differences from libwebrtc that remain (catalog.py 6.13):
//
// - No mutex and no reference counting: like every module here it is single-threaded, and the
// caller serializes AddSource/RemoveSource/Mix (libwebrtc takes a lock in each).
//
// - RemixFrame with more than two channels on either side: libwebrtc's ChannelMixer
// (audio/utility/channel_mixer.cc, channel_mixing_matrix.cc) is ported only for its VoIP case,
// mono into N >= 3 channels (front left and right get the sample, the rest zeros). Other layouts
// (for example 5.1 into stereo) are not ported; such a frame is left out of the mix and counted
// in `remix_unsupported`. A call's Opus streams are mono or stereo.
//
// - The UMA histogram WebRTC.Audio.AudioMixer.NewHighestSourceCount is the metric
// `max_source_count`.
//
// - The frame keeps the fields the mixer reads and writes, not the whole webrtc::AudioFrame (no
// speech type, VAD activity or channel layout; packet infos are AudioFramePacketInfo).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sm/audio/agc2.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::audio {

// The fields of webrtc::RtpPacketInfo a mix carries (frame_combiner.cc unions them).
struct AudioFramePacketInfo {
  uint32_t ssrc = 0;
  std::vector<uint32_t> csrcs;
  uint32_t rtp_timestamp = 0;
  Timestamp receive_time = Timestamp::MinusInfinity();
  std::optional<uint8_t> audio_level;
  bool operator==(const AudioFramePacketInfo&) const = default;
};

// webrtc::AudioFrame's sample buffer, mute state and timing (api/audio/audio_frame.{h,cc}):
// interleaved int16, at most kMaxDataSizeSamples samples. A muted frame reads as zeros.
class AudioFrame {
 public:
  static constexpr size_t kMaxDataSizeSamples = 7680;
  static constexpr size_t kMaxNumberOfAudioChannels = 24;

  // UpdateFrame with no data: sets the layout and mutes the frame.
  void UpdateFrame(uint32_t timestamp, const int16_t* data, size_t samples_per_channel, int sample_rate_hz,
                   size_t num_channels);
  void CopyFrom(const AudioFrame& src);
  // Interleaved samples, samples_per_channel * num_channels of them (zeros when muted).
  std::span<const int16_t> data() const;
  // Unmutes (zero-filling first if muted) and returns the whole buffer.
  std::span<int16_t> mutable_data();
  // Sets the layout, unmutes (zero-filling the first spc * channels samples if muted) and
  // returns those samples.
  std::span<int16_t> mutable_data(size_t samples_per_channel, size_t num_channels);
  void Mute() { muted_ = true; }
  bool muted() const { return muted_; }

  uint32_t timestamp = 0;
  int64_t elapsed_time_ms = -1;
  int64_t ntp_time_ms = -1;
  size_t samples_per_channel = 0;
  int sample_rate_hz = 0;
  size_t num_channels = 0;
  std::vector<AudioFramePacketInfo> packet_infos;

 private:
  std::array<int16_t, kMaxDataSizeSamples> data_{};
  bool muted_ = true;
};

// api/audio/audio_frame.h: samples in one 10 ms frame at `sample_rate_hz`.
constexpr size_t SampleRateToDefaultChannelSize(int sample_rate_hz) { return static_cast<size_t>(sample_rate_hz / 100); }

// common_audio/include/audio_util.h: FloatS16 to int16, saturated and rounded half away from zero.
int16_t FloatS16ToS16(float v);

// audio_frame_manipulator.{h,cc}.
uint32_t AudioMixerCalculateEnergy(const AudioFrame& audio_frame);
// Ramps the frame's gain linearly from start_gain to target_gain over its samples
// (int16 *= float truncation per sample, as libwebrtc).
void Ramp(float start_gain, float target_gain, AudioFrame* audio_frame);
// Remixes `frame` to `target_number_of_channels`. False (frame unchanged) for a layout change
// this port doesn't have (see the header comment).
bool RemixFrame(size_t target_number_of_channels, AudioFrame* frame);

// api/audio/audio_mixer.h.
class AudioMixerSource {
 public:
  enum class AudioFrameInfo {
    kNormal,  // The samples in audio_frame are valid and should be used.
    kMuted,   // The samples should not be used, but read as zeros; other fields are meaningful.
    kError,   // The audio_frame will not be used.
  };
  virtual ~AudioMixerSource() = default;
  // Overwrites `audio_frame` with 10 ms of new audio (1 or 2 interleaved channels) at
  // `sample_rate_hz`. Every field must be updated.
  virtual AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz, AudioFrame* audio_frame) = 0;
  // A way for a mixer implementation to distinguish participants.
  virtual uint32_t Ssrc() const = 0;
  // GetAudioFrameWithInfo at this rate or higher causes no quality loss.
  virtual int PreferredSampleRate() const = 0;
};

// output_rate_calculator.h.
class OutputRateCalculator {
 public:
  virtual ~OutputRateCalculator() = default;
  virtual int CalculateOutputRateFromRange(std::span<const int> preferred_sample_rates) = 0;
};

// default_output_rate_calculator.{h,cc}.
class DefaultOutputRateCalculator : public OutputRateCalculator {
 public:
  static constexpr int kDefaultFrequency = 48000;
  // The highest preferred rate rounded up to 8, 16, 32 or 48 kHz; kDefaultFrequency if none.
  int CalculateOutputRateFromRange(std::span<const int> preferred_sample_rates) override;
};

// frame_combiner.{h,cc}.
class FrameCombiner {
 public:
  // Stereo, 48 kHz, 10 ms.
  static constexpr size_t kMaximumNumberOfChannels = 8;
  static constexpr size_t kMaximumChannelSize = 48 * 10;

  explicit FrameCombiner(bool use_limiter);

  // Combines the frames of `mix_list` (each at `sample_rate`, 10 ms; remixed in place to
  // `number_of_channels`) into `audio_frame_for_mixing`. `mix_list` may be empty.
  // `number_of_streams` (the registered sources) decides between a plain copy (<= 1) and the
  // float sum with the limiter.
  void Combine(std::span<AudioFrame* const> mix_list, size_t number_of_channels, int sample_rate,
               size_t number_of_streams, AudioFrame* audio_frame_for_mixing);

  const agc2::Limiter& limiter() const { return limiter_; }
  uint64_t remix_unsupported() const { return remix_unsupported_; }

 private:
  agc2::Limiter limiter_;
  const bool use_limiter_;
  // Deinterleaved FloatS16: channel c at [c * spc, (c + 1) * spc). libwebrtc keeps it inline, but
  // its mixer only lives on the heap (AudioMixerImpl::Create); this one may be a local, and 15 KB
  // inline would take a quarter of Emscripten's 64 KB default stack (#1177).
  using MixingBuffer = std::array<float, kMaximumChannelSize * kMaximumNumberOfChannels>;
  std::unique_ptr<MixingBuffer> mixing_buffer_;
  std::vector<AudioFrame*> kept_;  // The frames RemixFrame could remix.
  uint64_t remix_unsupported_ = 0;
};

struct AudioMixerStats {
  uint64_t mixes = 0;               // Mix calls.
  uint64_t frames_mixed = 0;        // kNormal frames handed to the combiner.
  uint64_t frames_muted = 0;        // kMuted frames (read as zeros, left out).
  uint64_t frames_error = 0;        // kError frames (left out).
  uint64_t limited_mixes = 0;       // Mixes whose limiter gain went below 1.
  size_t max_source_count = 0;      // WebRTC.Audio.AudioMixer.NewHighestSourceCount.
  int last_output_rate_hz = 0;
  size_t last_mixed_sources = 0;
};

// audio_mixer_impl.{h,cc}.
class AudioMixerImpl : public MetricsSource {
 public:
  // AudioProcessing only accepts 10 ms frames.
  static constexpr int kFrameDurationInMs = 10;

  // AudioMixerImpl::Create(): DefaultOutputRateCalculator and the limiter on.
  AudioMixerImpl();
  AudioMixerImpl(std::unique_ptr<OutputRateCalculator> output_rate_calculator, bool use_limiter);
  AudioMixerImpl(const AudioMixerImpl&) = delete;
  AudioMixerImpl& operator=(const AudioMixerImpl&) = delete;

  // Returns true if added. A source is never added twice; the mixer doesn't own it.
  bool AddSource(AudioMixerSource* audio_source);
  // Only for a source that was added.
  void RemoveSource(AudioMixerSource* audio_source);
  // Mixes 10 ms from every source into `audio_frame_for_mixing`, with `number_of_channels`
  // channels at the rate the OutputRateCalculator picks.
  void Mix(size_t number_of_channels, AudioFrame* audio_frame_for_mixing);

  size_t num_sources() const { return audio_source_list_.size(); }
  const AudioMixerStats& stats() const { return stats_; }
  const FrameCombiner& frame_combiner() const { return frame_combiner_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct SourceStatus {
    explicit SourceStatus(AudioMixerSource* s) : audio_source(s) {}
    AudioMixerSource* audio_source = nullptr;
    // The frame passed to audio_source->GetAudioFrameWithInfo.
    AudioFrame audio_frame;
  };
  std::span<AudioFrame* const> GetAudioFromSources(int output_frequency);

  std::unique_ptr<OutputRateCalculator> output_rate_calculator_;
  std::vector<std::unique_ptr<SourceStatus>> audio_source_list_;
  std::vector<AudioFrame*> audio_to_mix_;
  std::vector<int> preferred_rates_;
  FrameCombiner frame_combiner_;
  AudioMixerStats stats_;
};

}  // namespace sm::audio
