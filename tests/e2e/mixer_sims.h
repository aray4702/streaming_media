// Group-call mixing scenarios for 6.13 (#1175): talkers that join, leave, mute and run at their own
// rates, as remote audio streams reach AudioMixerImpl in a group call. Shared by the e2e test
// (tests/e2e/audio_mixer_e2e_test.cc) and the libwebrtc comparison (compare/webrtc/mixer_compare.cc),
// so both mixers get the same int16 frames.
//
// A talker plays SpeechLike audio at its native rate, made one 10 ms frame at a time; asked for
// another rate it resamples with PushResampler<int16_t>, as ChannelReceive's resampler helper
// does for NetEq's output (channel_receive.cc MaybeResample). Everything is deterministic, so two
// copies of a talker asked the same things give the same samples.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sm/audio/audio_mixer.h"
#include "sm/audio/push_resampler.h"
#include "sm/sim/signals.h"

namespace sm::e2e {

struct TalkerConfig {
  int native_rate = 48000;
  size_t channels = 1;
  float amp = 0.3f;           // SpeechLike amplitude: about 0.3 peaks near -10 dBFS, 0.9 near full scale.
  uint64_t seed = 1;
  bool with_pauses = true;
  double join_s = 0;          // Added to the mixer at this time.
  double leave_s = 1e9;       // Removed at this time.
  double mute_from_s = 1e9;   // kMuted in [mute_from_s, mute_to_s).
  double mute_to_s = 1e9;
  int error_every = 0;        // kError on every n-th frame (0: never).
};

enum class TalkerFrame { kNormal, kMuted, kError };

class Talker {
 public:
  explicit Talker(const TalkerConfig& c) : c_(c) {
    for (size_t ch = 0; ch < c.channels; ++ch) {
      gens_.emplace_back(c.native_rate, c.seed * 7919 + ch, c.with_pauses, c.amp);
    }
  }
  const TalkerConfig& config() const { return c_; }
  int preferred_rate() const { return c_.native_rate; }

  // The talker's next 10 ms at `rate` (interleaved, channels() channels) into `out`, which holds
  // rate / 100 * channels samples.
  TalkerFrame Next(double t_s, int rate, int16_t* out) {
    const size_t n = static_cast<size_t>(c_.native_rate / 100);
    native_.resize(n * c_.channels);
    for (size_t i = 0; i < n; ++i) {
      for (size_t ch = 0; ch < c_.channels; ++ch) {
        const float v = std::clamp(gens_[ch].Next(), -1.0f, 1.0f) * 32767.0f;
        native_[i * c_.channels + ch] = static_cast<int16_t>(std::lround(v));
      }
    }
    ++frames_;
    const size_t dst = static_cast<size_t>(rate / 100) * c_.channels;
    resampler_.Resample(native_, std::span<int16_t>(out, dst), c_.channels);
    if (c_.error_every > 0 && frames_ % static_cast<uint64_t>(c_.error_every) == 0) return TalkerFrame::kError;
    if (t_s >= c_.mute_from_s && t_s < c_.mute_to_s) return TalkerFrame::kMuted;
    return TalkerFrame::kNormal;
  }
  bool present(double t_s) const { return t_s >= c_.join_s && t_s < c_.leave_s; }

 private:
  TalkerConfig c_;
  std::vector<sim::SpeechLikeGenerator> gens_;
  std::vector<int16_t> native_;
  audio::PushResampler<int16_t> resampler_;
  uint64_t frames_ = 0;
};

struct MixScenario {
  std::string name;
  std::vector<TalkerConfig> talkers;
  double seconds = 10;
  size_t out_channels = 1;
  bool use_limiter = true;
};

// The scenarios both the e2e test and sm_mixer_compare run.
inline std::vector<MixScenario> MixScenarios() {
  std::vector<MixScenario> s;
  s.push_back({"one talker (copy path)", {{.amp = 0.9f, .seed = 1}}, 5});
  s.push_back({"two quiet talkers", {{.amp = 0.2f, .seed = 2}, {.amp = 0.25f, .seed = 3}}, 5});
  {
    MixScenario m{"five loud talkers 48k", {}, 10};
    for (uint64_t i = 0; i < 5; ++i) m.talkers.push_back({.amp = 0.9f - 0.1f * static_cast<float>(i), .seed = 10 + i});
    s.push_back(m);
  }
  {
    MixScenario m{"five talkers, 16/48 kHz, join/leave/mute/error", {}, 12};
    m.talkers.push_back({.native_rate = 16000, .amp = 0.8f, .seed = 20});
    m.talkers.push_back({.native_rate = 16000, .amp = 0.6f, .seed = 21, .join_s = 1.0});
    m.talkers.push_back({.native_rate = 48000, .amp = 0.7f, .seed = 22, .join_s = 3.0, .leave_s = 9.0});
    m.talkers.push_back({.native_rate = 16000, .amp = 0.9f, .seed = 23, .mute_from_s = 4.0, .mute_to_s = 7.0});
    m.talkers.push_back({.native_rate = 32000, .amp = 0.5f, .seed = 24, .join_s = 2.0, .error_every = 37});
    s.push_back(m);
  }
  {
    MixScenario m{"stereo out, one stereo talker", {}, 6, 2};
    m.talkers.push_back({.channels = 2, .amp = 0.8f, .seed = 30});
    m.talkers.push_back({.amp = 0.7f, .seed = 31});
    m.talkers.push_back({.amp = 0.6f, .seed = 32});
    s.push_back(m);
  }
  {
    MixScenario m{"stereo talkers into mono", {}, 6, 1};
    m.talkers.push_back({.channels = 2, .amp = 0.8f, .seed = 40});
    m.talkers.push_back({.channels = 2, .amp = 0.8f, .seed = 41});
    s.push_back(m);
  }
  s.push_back({"8 kHz talkers", {{.native_rate = 8000, .amp = 0.9f, .seed = 50}, {.native_rate = 8000, .amp = 0.9f, .seed = 51}}, 5});
  {
    MixScenario m{"five loud talkers, no limiter", {}, 5};
    for (uint64_t i = 0; i < 5; ++i) m.talkers.push_back({.amp = 0.9f, .seed = 60 + i});
    m.use_limiter = false;
    s.push_back(m);
  }
  return s;
}

// One talker as this library's AudioMixerSource.
class TalkerSource : public audio::AudioMixerSource {
 public:
  TalkerSource(const TalkerConfig& c, uint32_t ssrc) : talker_(c), ssrc_(ssrc) {}
  void set_time(double t_s) { t_s_ = t_s; }
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz, audio::AudioFrame* audio_frame) override {
    const size_t spc = static_cast<size_t>(sample_rate_hz / 100);
    const size_t channels = talker_.config().channels;
    buf_.resize(spc * channels);
    const TalkerFrame f = talker_.Next(t_s_, sample_rate_hz, buf_.data());
    const uint32_t rtp_timestamp = static_cast<uint32_t>(frames_ * 480);  // A 48 kHz RTP clock.
    audio_frame->UpdateFrame(rtp_timestamp, f == TalkerFrame::kMuted ? nullptr : buf_.data(), spc, sample_rate_hz,
                             channels);
    audio_frame->elapsed_time_ms = static_cast<int64_t>(frames_) * 10;
    audio_frame->ntp_time_ms = -1;
    audio_frame->packet_infos = {{.ssrc = ssrc_, .rtp_timestamp = rtp_timestamp}};
    ++frames_;
    return f == TalkerFrame::kNormal ? AudioFrameInfo::kNormal
           : f == TalkerFrame::kMuted ? AudioFrameInfo::kMuted
                                      : AudioFrameInfo::kError;
  }
  uint32_t Ssrc() const override { return ssrc_; }
  int PreferredSampleRate() const override { return talker_.preferred_rate(); }

 private:
  Talker talker_;
  uint32_t ssrc_;
  double t_s_ = 0;
  uint64_t frames_ = 0;
  std::vector<int16_t> buf_;
};

struct MixRun {
  std::vector<int16_t> out;       // Every 10 ms mix, interleaved, one after the other.
  std::vector<int> rates;         // Each frame's output rate,
  std::vector<uint32_t> timestamps;  // RTP timestamp
  std::vector<size_t> channels;      // and channel count.
  uint64_t limited_mixes = 0;
  uint64_t frames_mixed = 0;
  uint64_t remix_unsupported = 0;
};

// Runs a scenario through this library's AudioMixerImpl, adding and removing the talkers as
// they join and leave (in talker order, so a second mixer sees the same source order).
inline MixRun RunOurMix(const MixScenario& s) {
  audio::AudioMixerImpl mixer(std::make_unique<audio::DefaultOutputRateCalculator>(), s.use_limiter);
  std::vector<std::unique_ptr<TalkerSource>> sources;
  for (size_t i = 0; i < s.talkers.size(); ++i) {
    sources.push_back(std::make_unique<TalkerSource>(s.talkers[i], static_cast<uint32_t>(1000 + i)));
  }
  std::vector<bool> added(s.talkers.size(), false);
  MixRun run;
  audio::AudioFrame frame;
  const int frames = static_cast<int>(s.seconds * 100);
  for (int k = 0; k < frames; ++k) {
    const double t = k * 0.01;
    for (size_t i = 0; i < s.talkers.size(); ++i) {
      const bool present = t >= s.talkers[i].join_s && t < s.talkers[i].leave_s;
      if (present && !added[i]) mixer.AddSource(sources[i].get());
      if (!present && added[i]) mixer.RemoveSource(sources[i].get());
      added[i] = present;
      sources[i]->set_time(t);
    }
    mixer.Mix(s.out_channels, &frame);
    const auto d = frame.data();
    run.out.insert(run.out.end(), d.begin(), d.end());
    run.rates.push_back(frame.sample_rate_hz);
    run.timestamps.push_back(frame.timestamp);
    run.channels.push_back(frame.num_channels);
  }
  run.limited_mixes = mixer.stats().limited_mixes;
  run.frames_mixed = mixer.stats().frames_mixed;
  run.remix_unsupported = mixer.frame_combiner().remix_unsupported();
  return run;
}

// Samples at the int16 rails.
inline size_t RailSamples(const std::vector<int16_t>& x) {
  return static_cast<size_t>(std::count_if(x.begin(), x.end(), [](int16_t v) { return v >= 32767 || v <= -32768; }));
}

// Flat tops: runs of at least two consecutive samples of one channel at the int16 rails.
inline size_t ClippedRuns(const std::vector<int16_t>& x, size_t channels) {
  size_t runs = 0;
  for (size_t ch = 0; ch < channels; ++ch) {
    size_t len = 0;
    for (size_t i = ch; i < x.size(); i += channels) {
      const bool rail = x[i] >= 32767 || x[i] <= -32768;
      if (rail) {
        if (++len == 2) ++runs;
      } else {
        len = 0;
      }
    }
  }
  return runs;
}

}  // namespace sm::e2e
