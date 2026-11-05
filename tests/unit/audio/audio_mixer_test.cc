// Unit tests: libwebrtc M157's receive-side mixer (sm/audio/audio_mixer.h, #1175 F6.13).
//
// Ports of compare/webrtc/reference/modules/audio_mixer/:
//  - audio_mixer_impl_unittest.cc -> `AudioMixer, *` (the gmock source becomes FakeSource, which
//    records the rates it was asked for). Not ported: ConstructFromOtherThread (the library starts
//    no threads; the caller serializes calls) and the death test MultipleChannelsAndHighRate
//    (sm_test has no death tests). UpdatesSourceCountHistogram checks the `max_source_count`
//    metric that stands in for the UMA histogram.
//  - frame_combiner_unittest.cc -> `FrameCombiner, *`, with gain_change_calculator.cc and
//    sine_wave_generator.cc ported below. Not ported: the two death tests.
//  - audio_frame_manipulator_unittest.cc -> `AudioFrameManipulator, *`.
// The bit-exact check against libwebrtc's own AudioMixerImpl is compare/webrtc/mixer_compare.cc.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <numeric>
#include <span>
#include <vector>

#include "sm/audio/agc2.h"
#include "sm/audio/audio_mixer.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

namespace {

constexpr int kDefaultSampleRateHz = 48000;

// ResetFrame: 48 kHz mono, 10 ms.
void ResetFrame(AudioFrame* frame) {
  frame->sample_rate_hz = kDefaultSampleRateHz;
  frame->num_channels = 1;
  frame->samples_per_channel = kDefaultSampleRateHz / 100;
}

// MockMixerAudioSource without gmock.
class FakeSource : public AudioMixerSource {
 public:
  FakeSource() { ResetFrame(&fake_frame); }
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz, AudioFrame* audio_frame) override {
    requested_rates.push_back(sample_rate_hz);
    audio_frame->CopyFrom(fake_frame);
    audio_frame->sample_rate_hz = sample_rate_hz;
    audio_frame->samples_per_channel = static_cast<size_t>(sample_rate_hz / 100);
    audio_frame->packet_infos = packet_infos;
    return info;
  }
  uint32_t Ssrc() const override { return ssrc; }
  int PreferredSampleRate() const override { return preferred_rate; }

  AudioFrame fake_frame;
  AudioFrameInfo info = AudioFrameInfo::kNormal;
  int preferred_rate = kDefaultSampleRateHz;
  uint32_t ssrc = 0;
  std::vector<AudioFramePacketInfo> packet_infos;
  std::vector<int> requested_rates;
};

class CustomRateCalculator : public OutputRateCalculator {
 public:
  explicit CustomRateCalculator(int rate) : rate_(rate) {}
  int CalculateOutputRateFromRange(std::span<const int>) override { return rate_; }

 private:
  const int rate_;
};

void MixMonoAtGivenNativeRate(int native_sample_rate, AudioFrame* mix_frame, AudioMixerImpl& mixer,
                              FakeSource* audio_source) {
  audio_source->preferred_rate = native_sample_rate;
  audio_source->fake_frame.sample_rate_hz = native_sample_rate;
  audio_source->fake_frame.samples_per_channel = static_cast<size_t>(native_sample_rate / 100);
  mixer.Mix(1, mix_frame);
}

AudioFramePacketInfo Info(uint32_t ssrc, std::vector<uint32_t> csrcs, uint32_t rtp_timestamp, int64_t receive_ms,
                          std::optional<uint8_t> level) {
  AudioFramePacketInfo p;
  p.ssrc = ssrc;
  p.csrcs = std::move(csrcs);
  p.rtp_timestamp = rtp_timestamp;
  p.receive_time = Timestamp::Millis(receive_ms);
  p.audio_level = level;
  return p;
}

bool SameElements(std::vector<AudioFramePacketInfo> a, std::vector<AudioFramePacketInfo> b) {
  if (a.size() != b.size()) return false;
  for (const auto& x : a) {
    auto it = std::find(b.begin(), b.end(), x);
    if (it == b.end()) return false;
    b.erase(it);
  }
  return true;
}

}  // namespace

// --------------------------------------------------------------------------- audio_mixer_impl_unittest.cc

SM_TEST(AudioMixer, UpdatesSourceCountHistogram) {
  constexpr int kGroup1 = 5;
  constexpr int kGroup2 = 3;
  AudioMixerImpl mixer;
  // On the heap: each source holds a 15 KB AudioFrame, and 8 of them overflow Emscripten's 64 KB stack.
  std::vector<FakeSource> participants(kGroup1 + kGroup2);
  for (int i = 0; i < kGroup1; ++i) {
    SM_EXPECT_TRUE(mixer.AddSource(&participants[static_cast<size_t>(i)]));
    SM_EXPECT_EQ(mixer.stats().max_source_count, static_cast<size_t>(i + 1));
  }
  for (int i = 0; i < kGroup1; ++i) mixer.RemoveSource(&participants[static_cast<size_t>(i)]);
  // Adding the first group again adds nothing new.
  for (int i = 0; i < kGroup1; ++i) {
    SM_EXPECT_TRUE(mixer.AddSource(&participants[static_cast<size_t>(i)]));
    SM_EXPECT_EQ(mixer.stats().max_source_count, static_cast<size_t>(kGroup1));
  }
  for (int i = kGroup1; i < kGroup1 + kGroup2; ++i) {
    SM_EXPECT_TRUE(mixer.AddSource(&participants[static_cast<size_t>(i)]));
    SM_EXPECT_EQ(mixer.stats().max_source_count, static_cast<size_t>(i + 1));
  }
}

SM_TEST(AudioMixer, FrameNotModifiedForSingleParticipant) {
  AudioMixerImpl mixer;
  FakeSource participant;
  const size_t n_samples = participant.fake_frame.samples_per_channel;
  std::span<int16_t> d = participant.fake_frame.mutable_data();
  for (size_t j = 0; j < n_samples; ++j) d[j] = static_cast<int16_t>(j);
  SM_EXPECT_TRUE(mixer.AddSource(&participant));
  AudioFrame audio_frame;
  for (int i = 0; i < 2; ++i) mixer.Mix(1, &audio_frame);
  SM_EXPECT_EQ(participant.requested_rates.size(), 2u);
  SM_ASSERT_EQ(audio_frame.data().size(), n_samples);
  SM_EXPECT_TRUE(std::equal(audio_frame.data().begin(), audio_frame.data().end(),
                            participant.fake_frame.data().begin()));
}

SM_TEST(AudioMixer, SourceAtNativeRateShouldNeverResample) {
  AudioMixerImpl mixer;
  FakeSource audio_source;
  mixer.AddSource(&audio_source);
  AudioFrame frame_for_mixing;
  for (int frequency : {8000, 16000, 32000, 48000}) {
    audio_source.requested_rates.clear();
    MixMonoAtGivenNativeRate(frequency, &frame_for_mixing, mixer, &audio_source);
    SM_ASSERT_EQ(audio_source.requested_rates.size(), 1u);
    SM_EXPECT_EQ(audio_source.requested_rates[0], frequency);
  }
}

SM_TEST(AudioMixer, MixerShouldMixAtNativeSourceRate) {
  AudioMixerImpl mixer;
  FakeSource audio_source;
  mixer.AddSource(&audio_source);
  AudioFrame frame_for_mixing;
  for (int frequency : {8000, 16000, 32000, 48000}) {
    MixMonoAtGivenNativeRate(frequency, &frame_for_mixing, mixer, &audio_source);
    SM_EXPECT_EQ(frame_for_mixing.sample_rate_hz, frequency);
  }
}

SM_TEST(AudioMixer, MixerShouldAlwaysMixAtNativeRate) {
  AudioMixerImpl mixer;
  FakeSource participant;
  mixer.AddSource(&participant);
  participant.preferred_rate = 44100;
  // The mix rate is native and >= the needed rate.
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  SM_ASSERT_EQ(participant.requested_rates.size(), 1u);
  SM_EXPECT_EQ(participant.requested_rates[0], 48000);
  SM_EXPECT_EQ(frame_for_mixing.sample_rate_hz, 48000);
}

// The mixing rate is always >= every participant's preferred rate.
SM_TEST(AudioMixer, ShouldNotCauseQualityLossForMultipleSources) {
  AudioMixerImpl mixer;
  std::vector<FakeSource> sources(2);
  const int rates[] = {8000, 16000};
  for (int i = 0; i < 2; ++i) {
    sources[static_cast<size_t>(i)].preferred_rate = rates[i];
    mixer.AddSource(&sources[static_cast<size_t>(i)]);
  }
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  for (int i = 0; i < 2; ++i) {
    SM_ASSERT_EQ(sources[static_cast<size_t>(i)].requested_rates.size(), 1u);
    SM_EXPECT_GE(sources[static_cast<size_t>(i)].requested_rates[0], rates[i]);
  }
}

SM_TEST(AudioMixer, ParticipantNumberOfChannels) {
  AudioMixerImpl mixer;
  FakeSource participant;
  SM_EXPECT_TRUE(mixer.AddSource(&participant));
  AudioFrame frame_for_mixing;
  for (size_t number_of_channels : {1u, 2u}) {
    participant.requested_rates.clear();
    mixer.Mix(number_of_channels, &frame_for_mixing);
    SM_ASSERT_EQ(participant.requested_rates.size(), 1u);
    SM_EXPECT_EQ(participant.requested_rates[0], kDefaultSampleRateHz);
    SM_EXPECT_EQ(frame_for_mixing.num_channels, number_of_channels);
  }
}

SM_TEST(AudioMixer, MixingRateShouldBeDecidedByRateCalculator) {
  constexpr int kOutputRate = 22000;
  AudioMixerImpl mixer(std::make_unique<CustomRateCalculator>(kOutputRate), true);
  FakeSource audio_source;
  mixer.AddSource(&audio_source);
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  SM_ASSERT_EQ(audio_source.requested_rates.size(), 1u);
  SM_EXPECT_EQ(audio_source.requested_rates[0], kOutputRate);
}

SM_TEST(AudioMixer, ZeroSourceRateShouldBeDecidedByRateCalculator) {
  constexpr int kOutputRate = 8000;
  AudioMixerImpl mixer(std::make_unique<CustomRateCalculator>(kOutputRate), true);
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  SM_EXPECT_EQ(frame_for_mixing.sample_rate_hz, kOutputRate);
}

SM_TEST(AudioMixer, NoLimiterBasicApiCalls) {
  AudioMixerImpl mixer(std::make_unique<DefaultOutputRateCalculator>(), false);
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  SM_EXPECT_EQ(frame_for_mixing.sample_rate_hz, DefaultOutputRateCalculator::kDefaultFrequency);
}

SM_TEST(AudioMixer, AnyRateIsPossibleWithNoLimiter) {
  // Without the limiter any rate divisible by 100 (10 ms frames) works.
  for (int rate : {8000, 20000, 24000, 32000, 44100}) {
    for (size_t number_of_channels : {1u, 2u}) {
      for (int number_of_sources : {0, 1, 2, 3, 4}) {
        AudioMixerImpl mixer(std::make_unique<CustomRateCalculator>(rate), false);
        std::vector<FakeSource> sources(static_cast<size_t>(number_of_sources));
        for (auto& source : sources) mixer.AddSource(&source);
        AudioFrame frame_for_mixing;
        mixer.Mix(number_of_channels, &frame_for_mixing);
        SM_EXPECT_EQ(frame_for_mixing.sample_rate_hz, rate);
        SM_EXPECT_EQ(frame_for_mixing.num_channels, number_of_channels);
      }
    }
  }
}

SM_TEST(AudioMixer, MultipleChannelsOneParticipant) {
  // A 6-channel source gives a 6-channel mix with its samples.
  constexpr size_t kNumberOfChannels = 6;
  FakeSource source;
  AudioMixerImpl mixer;
  mixer.AddSource(&source);
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  AudioFrame* frame = &source.fake_frame;
  frame->num_channels = kNumberOfChannels;
  std::span<int16_t> d = frame->mutable_data();
  std::fill(d.begin(), d.end(), int16_t{0});
  for (size_t i = 0; i < kNumberOfChannels; ++i) d[100 * kNumberOfChannels + i] = static_cast<int16_t>(1000 * i);
  mixer.Mix(kNumberOfChannels, &frame_for_mixing);
  SM_EXPECT_EQ(frame_for_mixing.num_channels, kNumberOfChannels);
  for (size_t i = 0; i < kNumberOfChannels; ++i) {
    SM_EXPECT_EQ(frame_for_mixing.data()[100 * kNumberOfChannels + i], static_cast<int16_t>(1000 * i));
  }
}

SM_TEST(AudioMixer, MultipleChannelsManyParticipants) {
  // A 6-channel source and a mono one (remixed to 6 channels): the 6-channel samples come out.
  constexpr size_t kNumberOfChannels = 6;
  FakeSource source;
  AudioMixerImpl mixer;
  mixer.AddSource(&source);
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  AudioFrame* frame = &source.fake_frame;
  frame->num_channels = kNumberOfChannels;
  std::span<int16_t> d = frame->mutable_data();
  std::fill(d.begin(), d.end(), int16_t{0});
  for (size_t i = 0; i < kNumberOfChannels; ++i) d[100 * kNumberOfChannels + i] = static_cast<int16_t>(1000 * i);
  FakeSource other_source;
  mixer.AddSource(&other_source);
  mixer.Mix(kNumberOfChannels, &frame_for_mixing);
  SM_EXPECT_EQ(frame_for_mixing.num_channels, kNumberOfChannels);
  for (size_t i = 0; i < kNumberOfChannels; ++i) {
    SM_EXPECT_EQ(frame_for_mixing.data()[100 * kNumberOfChannels + i], static_cast<int16_t>(1000 * i));
  }
  SM_EXPECT_EQ(mixer.frame_combiner().remix_unsupported(), 0u);
}

SM_TEST(AudioMixer, ShouldIncludeRtpPacketInfoFromAllMixedSources) {
  const AudioFramePacketInfo p0 = Info(10, {20, 21}, 300, 10, 10);
  const AudioFramePacketInfo p1 = Info(11, {22}, 400, 20, 40);
  const AudioFramePacketInfo p2 = Info(12, {23}, 400, 20, std::nullopt);
  AudioMixerImpl mixer;
  FakeSource source;
  source.packet_infos = {p0};
  mixer.AddSource(&source);
  AudioFrame frame_for_mixing;
  mixer.Mix(1, &frame_for_mixing);
  FakeSource other_source;
  other_source.packet_infos = {p1, p2};
  mixer.AddSource(&other_source);
  mixer.Mix(1, &frame_for_mixing);
  SM_EXPECT_TRUE(SameElements(frame_for_mixing.packet_infos, {p0, p1, p2}));
}

// Ours: M157 mixes every unmuted source (no "3 loudest" selection), muted and failed frames are
// left out, and the stats count them.
SM_TEST(AudioMixer, MixesEveryUnmutedSourceAsM157) {
  AudioMixerImpl mixer;
  std::vector<FakeSource> sources(6);
  for (size_t s = 0; s < sources.size(); ++s) {
    std::span<int16_t> d = sources[s].fake_frame.mutable_data();
    std::fill_n(d.begin(), 480, static_cast<int16_t>(100 * (s + 1)));
    mixer.AddSource(&sources[s]);
  }
  sources[4].info = AudioMixerSource::AudioFrameInfo::kMuted;
  sources[5].info = AudioMixerSource::AudioFrameInfo::kError;
  AudioFrame mix;
  mixer.Mix(1, &mix);
  // 100 + 200 + 300 + 400: all four normal sources, far below the limiter's knee.
  SM_EXPECT_EQ(mix.data()[0], 1000);
  SM_EXPECT_EQ(mix.data()[479], 1000);
  SM_EXPECT_EQ(mixer.stats().last_mixed_sources, 4u);
  SM_EXPECT_EQ(mixer.stats().frames_muted, 1u);
  SM_EXPECT_EQ(mixer.stats().frames_error, 1u);
  SM_EXPECT_EQ(mixer.stats().limited_mixes, 0u);
  const auto m = CollectMetrics(mixer);
  SM_EXPECT_EQ(m.at("frames_mixed"), 4.0);
  SM_EXPECT_EQ(m.at("sources"), 6.0);
}

// Ours: loud sources are limited, not wrapped or hard-clipped, and the limiter is counted.
SM_TEST(AudioMixer, LoudSumIsLimitedNotClipped) {
  AudioMixerImpl mixer;
  std::vector<FakeSource> sources(3);
  for (auto& s : sources) {
    std::span<int16_t> d = s.fake_frame.mutable_data();
    std::fill_n(d.begin(), 480, int16_t{20000});
    mixer.AddSource(&s);
  }
  AudioFrame mix;
  for (int i = 0; i < 10; ++i) mixer.Mix(1, &mix);
  // 60000 in: the limiter's saturation region scales to exactly full scale, below int16's clamp.
  for (int16_t v : mix.data()) {
    SM_EXPECT_GT(v, 30000);
    SM_EXPECT_LE(v, 32767);
  }
  SM_EXPECT_GT(mixer.stats().limited_mixes, 0u);
  SM_EXPECT_LT(mixer.frame_combiner().limiter().last_scaling_factor(), 0.6f);
}

// --------------------------------------------------------------------------- frame_combiner_unittest.cc

namespace {

AudioFrame frame1;
AudioFrame frame2;

void SetUpFrames(int sample_rate_hz, size_t number_of_channels) {
  frame1.packet_infos = {Info(1001, {}, 1000, 1, std::nullopt)};
  frame2.packet_infos = {Info(4004, {}, 1234, 2, std::nullopt), Info(7007, {}, 1333, 2, std::nullopt)};
  for (AudioFrame* frame : {&frame1, &frame2}) {
    frame->UpdateFrame(0, nullptr, static_cast<size_t>(sample_rate_hz / 100), sample_rate_hz, number_of_channels);
  }
}

// gain_change_calculator.{h,cc}.
class GainChangeCalculator {
 public:
  float CalculateGainChange(std::span<const int16_t> in, std::span<const int16_t> out) {
    std::vector<float> gain(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
      if (std::abs(in[i]) >= kReliabilityThreshold) last_reliable_gain_ = out[i] / static_cast<float>(in[i]);
      gain[i] = last_reliable_gain_;
    }
    float res = 0;
    for (float f : gain) {
      res += std::fabs(f - last_value_);
      last_value_ = f;
    }
    return res;
  }
  float LatestGain() const { return last_reliable_gain_; }

 private:
  static constexpr int16_t kReliabilityThreshold = 100;
  float last_value_ = 0.f;
  float last_reliable_gain_ = 1.0f;
};

// sine_wave_generator.{h,cc}.
class SineWaveGenerator {
 public:
  SineWaveGenerator(float wave_frequency_hz, int16_t amplitude)
      : wave_frequency_hz_(wave_frequency_hz), amplitude_(amplitude) {}
  void GenerateNextFrame(AudioFrame* frame) {
    std::span<int16_t> d = frame->mutable_data();
    for (size_t i = 0; i < frame->samples_per_channel; ++i) {
      for (size_t ch = 0; ch < frame->num_channels; ++ch) {
        const float v = std::clamp(static_cast<float>(amplitude_) * std::sin(phase_), -32768.f, 32767.f);
        d[frame->num_channels * i + ch] = static_cast<int16_t>(v);
      }
      phase_ += wave_frequency_hz_ * 2 * std::numbers::pi_v<float> / static_cast<float>(frame->sample_rate_hz);
    }
  }

 private:
  float phase_ = 0.f;
  const float wave_frequency_hz_;
  const int16_t amplitude_;
};

}  // namespace

// The limiter needs a rate divisible by 2000.
SM_TEST(FrameCombiner, BasicApiCallsLimiter) {
  FrameCombiner combiner(true);
  for (int rate : {8000, 18000, 34000, 48000}) {
    for (size_t number_of_channels : {1u, 2u, 4u, 8u}) {
      std::vector<AudioFrame*> all_frames = {&frame1, &frame2};
      SetUpFrames(rate, number_of_channels);
      for (size_t number_of_frames : {0u, 1u, 2u}) {
        const std::vector<AudioFrame*> frames(all_frames.begin(), all_frames.begin() + static_cast<long>(number_of_frames));
        AudioFrame out;
        combiner.Combine(frames, number_of_channels, rate, frames.size(), &out);
        SM_EXPECT_EQ(out.sample_rate_hz, rate);
        SM_EXPECT_EQ(out.num_channels, number_of_channels);
      }
    }
  }
}

// The mix's packet infos are the union of the mixed frames'.
SM_TEST(FrameCombiner, ContainsAllRtpPacketInfos) {
  constexpr int kSampleRateHz = 48000;
  FrameCombiner combiner(true);
  std::vector<AudioFrame*> all_frames = {&frame1, &frame2};
  SetUpFrames(kSampleRateHz, 1);
  for (size_t number_of_frames : {0u, 1u, 2u}) {
    const std::vector<AudioFrame*> frames(all_frames.begin(), all_frames.begin() + static_cast<long>(number_of_frames));
    std::vector<AudioFramePacketInfo> packet_infos;
    for (const AudioFrame* f : frames) packet_infos.insert(packet_infos.end(), f->packet_infos.begin(), f->packet_infos.end());
    AudioFrame out;
    combiner.Combine(frames, 1, kSampleRateHz, frames.size(), &out);
    SM_EXPECT_TRUE(SameElements(out.packet_infos, packet_infos));
  }
}

// Without the limiter the rate only has to be divisible by 100.
SM_TEST(FrameCombiner, BasicApiCallsNoLimiter) {
  FrameCombiner combiner(false);
  for (int rate : {8000, 10000, 11000, 32000, 44100}) {
    for (size_t number_of_channels : {1u, 2u, 4u, 8u}) {
      std::vector<AudioFrame*> all_frames = {&frame1, &frame2};
      SetUpFrames(rate, number_of_channels);
      for (size_t number_of_frames : {0u, 1u, 2u}) {
        const std::vector<AudioFrame*> frames(all_frames.begin(), all_frames.begin() + static_cast<long>(number_of_frames));
        AudioFrame out;
        combiner.Combine(frames, number_of_channels, rate, frames.size(), &out);
        SM_EXPECT_EQ(out.sample_rate_hz, rate);
      }
    }
  }
}

SM_TEST(FrameCombiner, CombiningZeroFramesShouldProduceSilence) {
  FrameCombiner combiner(false);
  for (int rate : {8000, 10000, 11000, 32000, 44100}) {
    for (size_t number_of_channels : {1u, 2u}) {
      AudioFrame out;
      const std::vector<AudioFrame*> frames;
      combiner.Combine(frames, number_of_channels, rate, frames.size(), &out);
      const std::span<const int16_t> mixed = out.data();
      SM_EXPECT_EQ(mixed.size(), number_of_channels * static_cast<size_t>(rate / 100));
      SM_EXPECT_TRUE(std::all_of(mixed.begin(), mixed.end(), [](int16_t v) { return v == 0; }));
      SM_EXPECT_TRUE(out.packet_infos.empty());
    }
  }
}

SM_TEST(FrameCombiner, CombiningOneFrameShouldNotChangeFrame) {
  FrameCombiner combiner(false);
  for (int rate : {8000, 10000, 11000, 32000, 44100}) {
    // kMaxConcurrentChannels is 8.
    for (size_t number_of_channels : {1u, 2u, 4u, 8u}) {
      AudioFrame out;
      SetUpFrames(rate, number_of_channels);
      std::span<int16_t> d = frame1.mutable_data();
      const size_t n = number_of_channels * static_cast<size_t>(rate / 100);
      std::iota(d.begin(), d.begin() + static_cast<long>(n), int16_t{0});
      const std::vector<AudioFrame*> frames = {&frame1};
      combiner.Combine(frames, number_of_channels, rate, frames.size(), &out);
      std::vector<int16_t> expected(n);
      std::iota(expected.begin(), expected.end(), int16_t{0});
      SM_ASSERT_EQ(out.data().size(), n);
      SM_EXPECT_TRUE(std::equal(expected.begin(), expected.end(), out.data().begin()));
      SM_EXPECT_TRUE(out.packet_infos == frame1.packet_infos);
    }
  }
}

// A sine through the combiner with the number of streams alternating: the gain from input to
// output changes smoothly and stays just below 1 (chromium:695993, chromium:816875).
SM_TEST(FrameCombiner, GainCurveIsSmoothForAlternatingNumberOfStreams) {
  struct Config {
    bool use_limiter;
    int sample_rate_hz;
    size_t number_of_channels;
    float wave_frequency;
  };
  // Rates are divisible by 2000 when the limiter is on.
  const Config configs[] = {
      {false, 30100, 2, 50.f}, {false, 16500, 1, 3200.f}, {true, 8000, 1, 3200.f},
      {true, 16000, 1, 50.f},  {true, 18000, 8, 3200.f},  {true, 10000, 2, 50.f},
  };
  for (const Config& config : configs) {
    FrameCombiner combiner(config.use_limiter);
    constexpr int16_t kWaveAmplitude = 30000;
    SineWaveGenerator wave_generator(config.wave_frequency, kWaveAmplitude);
    GainChangeCalculator change_calculator;
    float cumulative_change = 0.f;
    for (size_t i = 0; i < 100; ++i) {
      SetUpFrames(config.sample_rate_hz, config.number_of_channels);
      wave_generator.GenerateNextFrame(&frame1);
      frame2.Mute();
      std::vector<AudioFrame*> frames = {&frame1};
      if (i % 2 == 0) frames.push_back(&frame2);
      const size_t number_of_samples = frame1.samples_per_channel * config.number_of_channels;
      // Ensures the limiter is on if `use_limiter`.
      constexpr size_t kNumberOfStreams = 2;
      AudioFrame out;
      combiner.Combine(frames, config.number_of_channels, config.sample_rate_hz, kNumberOfStreams, &out);
      cumulative_change += change_calculator.CalculateGainChange(frame1.data().first(number_of_samples),
                                                                 out.data().first(number_of_samples));
    }
    SM_EXPECT_LT(cumulative_change, 10.0f);
    SM_EXPECT_LT(0.9f, change_calculator.LatestGain());
    SM_EXPECT_LT(change_calculator.LatestGain(), 1.01f);
  }
}

// --------------------------------------------------------------------------- audio_frame_manipulator_unittest.cc

namespace {
void FillFrameWithConstants(size_t samples_per_channel, size_t number_of_channels, int16_t value, AudioFrame* frame) {
  frame->num_channels = number_of_channels;
  frame->samples_per_channel = samples_per_channel;
  std::span<int16_t> d = frame->mutable_data();
  std::fill_n(d.begin(), samples_per_channel * number_of_channels, value);
}
}  // namespace

SM_TEST(AudioFrameManipulator, CompareForwardRampWithExpectedResultStereo) {
  AudioFrame frame;
  FillFrameWithConstants(5, 2, 5, &frame);
  Ramp(0.0f, 1.0f, &frame);
  const int16_t expected[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
  SM_EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), frame.data().begin()));
}

SM_TEST(AudioFrameManipulator, CompareBackwardRampWithExpectedResultMono) {
  AudioFrame frame;
  FillFrameWithConstants(5, 1, 5, &frame);
  Ramp(1.0f, 0.0f, &frame);
  const int16_t expected[] = {5, 4, 3, 2, 1};
  SM_EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), frame.data().begin()));
}

// Ours: RemixFrame as AudioFrameOperations (int32 sum, truncating division; in-place upmix) and
// ChannelMixer's mono-input VoIP case; the energy as AudioMixerCalculateEnergy.
SM_TEST(AudioFrameManipulator, RemixAndEnergyAsLibwebrtc) {
  AudioFrame stereo;
  stereo.UpdateFrame(0, nullptr, 3, 300, 2);
  std::span<int16_t> d = stereo.mutable_data(3, 2);
  const int16_t in[] = {3, 4, -3, -4, 32767, 32767};
  std::copy(std::begin(in), std::end(in), d.begin());
  SM_EXPECT_TRUE(RemixFrame(1, &stereo));
  SM_EXPECT_EQ(stereo.num_channels, 1u);
  SM_EXPECT_EQ(stereo.data()[0], 3);   // 7 / 2
  SM_EXPECT_EQ(stereo.data()[1], -3);  // -7 / 2 truncates toward zero
  SM_EXPECT_EQ(stereo.data()[2], 32767);
  SM_EXPECT_TRUE(RemixFrame(2, &stereo));
  const int16_t up[] = {3, 3, -3, -3, 32767, 32767};
  SM_EXPECT_TRUE(std::equal(std::begin(up), std::end(up), stereo.data().begin()));

  AudioFrame mono;
  mono.UpdateFrame(0, nullptr, 2, 200, 1);
  std::span<int16_t> m = mono.mutable_data(2, 1);
  m[0] = 7;
  m[1] = -9;
  SM_EXPECT_TRUE(RemixFrame(6, &mono));
  const int16_t voip[] = {7, 7, 0, 0, 0, 0, -9, -9, 0, 0, 0, 0};
  SM_EXPECT_TRUE(std::equal(std::begin(voip), std::end(voip), mono.data().begin()));
  // 6 -> 2 is ChannelMixer's general case, not ported.
  SM_EXPECT_FALSE(RemixFrame(2, &mono));

  AudioFrame e;
  FillFrameWithConstants(4, 1, 100, &e);
  SM_EXPECT_EQ(AudioMixerCalculateEnergy(e), 40000u);
  e.Mute();
  SM_EXPECT_EQ(AudioMixerCalculateEnergy(e), 0u);
}

SM_TEST(AudioFrameManipulator, FloatS16ToS16RoundsAndSaturatesAsLibwebrtc) {
  SM_EXPECT_EQ(FloatS16ToS16(0.49f), 0);
  SM_EXPECT_EQ(FloatS16ToS16(0.5f), 1);
  SM_EXPECT_EQ(FloatS16ToS16(-0.5f), -1);
  SM_EXPECT_EQ(FloatS16ToS16(-1.49f), -1);
  SM_EXPECT_EQ(FloatS16ToS16(40000.f), 32767);
  SM_EXPECT_EQ(FloatS16ToS16(-40000.f), -32768);
}

// Ours: the AGC2 limiter on several channels uses the peak over all of them and the same gain
// on each (limiter.cc ScaleSamples), so two copies of a channel give that channel's mono result.
SM_TEST(GainController2, LimiterOnTwoIdenticalChannelsEqualsMono) {
  agc2::Limiter mono(480);
  agc2::Limiter stereo(480);
  std::vector<float> m(480);
  std::vector<float> s(960);
  for (int frame = 0; frame < 20; ++frame) {
    for (size_t i = 0; i < 480; ++i) {
      m[i] = 50000.f * std::sin(0.01f * static_cast<float>(i + 480 * static_cast<size_t>(frame)));
      s[i] = m[i];
      s[480 + i] = m[i];
    }
    mono.Process(m);
    stereo.Process(s, 2);
    for (size_t i = 0; i < 480; ++i) {
      SM_EXPECT_EQ(s[i], m[i]);
      SM_EXPECT_EQ(s[480 + i], m[i]);
    }
  }
}
