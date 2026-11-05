// sm_mixer_compare: this library's AudioMixerImpl port (sm/audio/audio_mixer.h, #1175 F6.13)
// and libwebrtc M157's own AudioMixerImpl (modules/audio_mixer/, linked from the prebuilt
// libwebrtc.a) on the same group-call scenarios.
//
// Each scenario (tests/e2e/mixer_sims.h, also run by E2E_AudioMixer) has talkers that join,
// leave, mute, fail now and then and run at their own rates; both mixers get the same int16
// frames, in the same source order, and every 10 ms mix is compared sample for sample, with its
// rate, channel count and RTP timestamp. Any difference fails the build (it runs after every
// build, so the compare-webrtc job fails), like sm_rtcp_interop.
//
//   out/compare/sm_mixer_compare           # Markdown table on stdout
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "api/audio/audio_frame.h"
#include "api/audio/audio_mixer.h"
#include "api/scoped_refptr.h"
#include "mixer_sims.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/audio_mixer/default_output_rate_calculator.h"

namespace {

using sm::e2e::MixRun;
using sm::e2e::MixScenario;
using sm::e2e::Talker;
using sm::e2e::TalkerConfig;
using sm::e2e::TalkerFrame;

// A talker as libwebrtc's AudioMixer::Source, filled as sm::e2e::TalkerSource fills ours.
class WebrtcTalkerSource : public webrtc::AudioMixer::Source {
 public:
  WebrtcTalkerSource(const TalkerConfig& c, int ssrc) : talker_(c), ssrc_(ssrc) {}
  void set_time(double t_s) { t_s_ = t_s; }
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz, webrtc::AudioFrame* audio_frame) override {
    const size_t spc = static_cast<size_t>(sample_rate_hz / 100);
    const size_t channels = talker_.config().channels;
    buf_.resize(spc * channels);
    const TalkerFrame f = talker_.Next(t_s_, sample_rate_hz, buf_.data());
    const uint32_t rtp_timestamp = static_cast<uint32_t>(frames_ * 480);
    audio_frame->UpdateFrame(rtp_timestamp, f == TalkerFrame::kMuted ? nullptr : buf_.data(), spc, sample_rate_hz,
                             webrtc::AudioFrame::kNormalSpeech, webrtc::AudioFrame::kVadActive, channels);
    audio_frame->elapsed_time_ms_ = static_cast<int64_t>(frames_) * 10;
    audio_frame->ntp_time_ms_ = -1;
    ++frames_;
    return f == TalkerFrame::kNormal ? AudioFrameInfo::kNormal
           : f == TalkerFrame::kMuted ? AudioFrameInfo::kMuted
                                      : AudioFrameInfo::kError;
  }
  int Ssrc() const override { return ssrc_; }
  int PreferredSampleRate() const override { return talker_.preferred_rate(); }

 private:
  Talker talker_;
  int ssrc_;
  double t_s_ = 0;
  uint64_t frames_ = 0;
  std::vector<int16_t> buf_;
};

MixRun RunWebrtcMix(const MixScenario& s) {
  webrtc::scoped_refptr<webrtc::AudioMixerImpl> mixer =
      webrtc::AudioMixerImpl::Create(std::make_unique<webrtc::DefaultOutputRateCalculator>(), s.use_limiter);
  std::vector<std::unique_ptr<WebrtcTalkerSource>> sources;
  for (size_t i = 0; i < s.talkers.size(); ++i) {
    sources.push_back(std::make_unique<WebrtcTalkerSource>(s.talkers[i], static_cast<int>(1000 + i)));
  }
  std::vector<bool> added(s.talkers.size(), false);
  MixRun r;
  webrtc::AudioFrame frame;
  const int frames = static_cast<int>(s.seconds * 100);
  for (int k = 0; k < frames; ++k) {
    const double t = k * 0.01;
    for (size_t i = 0; i < s.talkers.size(); ++i) {
      const bool present = t >= s.talkers[i].join_s && t < s.talkers[i].leave_s;
      if (present && !added[i]) mixer->AddSource(sources[i].get());
      if (!present && added[i]) mixer->RemoveSource(sources[i].get());
      added[i] = present;
      sources[i]->set_time(t);
    }
    mixer->Mix(s.out_channels, &frame);
    const int16_t* d = frame.data();
    r.out.insert(r.out.end(), d, d + frame.samples_per_channel_ * frame.num_channels_);
    r.rates.push_back(frame.sample_rate_hz_);
    r.timestamps.push_back(frame.timestamp_);
    r.channels.push_back(frame.num_channels_);
  }
  return r;
}

}  // namespace

int main() {
  bool all_equal = true;
  std::printf("| scenario | mixes | samples | differing samples | max abs diff | frame fields differ | limited mixes | "
              "flat-topped runs (ours / libwebrtc) |\n|---|---|---|---|---|---|---|---|\n");
  for (const MixScenario& s : sm::e2e::MixScenarios()) {
    const MixRun ours = sm::e2e::RunOurMix(s);
    const MixRun theirs = RunWebrtcMix(s);
    size_t differ = 0;
    int max_diff = 0;
    const size_t n = std::min(ours.out.size(), theirs.out.size());
    for (size_t i = 0; i < n; ++i) {
      const int d = std::abs(static_cast<int>(ours.out[i]) - theirs.out[i]);
      differ += d != 0;
      max_diff = std::max(max_diff, d);
    }
    differ += std::max(ours.out.size(), theirs.out.size()) - n;
    size_t fields = 0;
    for (size_t k = 0; k < ours.rates.size() && k < theirs.rates.size(); ++k) {
      fields += ours.rates[k] != theirs.rates[k] || ours.timestamps[k] != theirs.timestamps[k] ||
                ours.channels[k] != theirs.channels[k];
    }
    fields += ours.rates.size() != theirs.rates.size();
    const bool equal = differ == 0 && fields == 0 && ours.remix_unsupported == 0;
    all_equal = all_equal && equal;
    std::printf("| %s | %zu | %zu | %zu | %d | %zu | %llu | %zu / %zu |\n", s.name.c_str(), ours.rates.size(), n,
                differ, max_diff, fields, static_cast<unsigned long long>(ours.limited_mixes),
                sm::e2e::ClippedRuns(ours.out, s.out_channels), sm::e2e::ClippedRuns(theirs.out, s.out_channels));
  }
  std::printf("\n%s\n", all_equal ? "sm_mixer_compare: bit-exact with libwebrtc's AudioMixerImpl on every scenario"
                                  : "sm_mixer_compare: FAILED, the mix differs from libwebrtc's AudioMixerImpl");
  return all_equal ? 0 : 1;
}
