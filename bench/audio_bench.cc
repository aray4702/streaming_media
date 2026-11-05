// Section 6 benchmarks. Audio blocks are gated by a real-time factor: how
// many times faster than real time a 10 ms frame is processed (at 16 kHz
// unless noted). The budget is scaled down on slow targets.
#include "../tests/e2e/audio_sims.h"
#include "../tests/e2e/mixer_sims.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "sm/audio/audio_level.h"
#include "sm/audio/audio_mixer.h"
#include "sm/audio/push_resampler.h"
#include "sm/audio/spl_resampler.h"
#include "sm/audio/splitting_filter.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::audio;

namespace {
std::vector<float> Speech() { return sim::SpeechLike(16000, 2.0, 1, true); }

// 4 s of the e2e noise case's capture (talker in white noise, about 7 dB
// SNR), so the noise suppressor and the AGCs work on what they are for.
std::vector<float> NoisySpeech() {
  e2e::CallScenario s;
  s.seconds = 4;
  s.far_talk = false;
  s.noise_amp = 0.03f;
  return e2e::MakeCall(s).capture;
}
}  // namespace

SM_BENCH(audio, Vad_10ms) {
  Vad v;
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    bench::DoNotOptimize(v.VoiceActivity(std::span<const float>(x.data() + off, 160), 16000));
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 500);
}

SM_BENCH(audio, SplittingFilter_48k_10ms) {
  // Three-band analysis plus synthesis of one 10 ms frame at 48 kHz.
  SplittingFilter f(48000);
  auto x = sim::SpeechLike(48000, 2.0, 1, true);
  std::vector<float> bands(480), out(480);
  size_t off = 0;
  for (auto _ : st) {
    f.Analysis(std::span<const float>(x.data() + off, 480), bands);
    f.Synthesis(bands, out);
    bench::DoNotOptimize(out[0]);
    off = (off + 480) % (x.size() - 480);
  }
  st.RequireRealtimeFactor(0.010, 500);
}

SM_BENCH(audio, TransientSuppressor_10ms) {
  TransientSuppressorConfig tc;
  tc.enabled = true;
  TransientSuppressor ts(tc);
  auto x = Speech();
  size_t off = 0;
  for (auto _ : st) {
    ts.Process(std::span<float>(x.data() + off, 160), false);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 2000);
}

SM_BENCH(audio, AudioLevel_20ms) {
  auto x = Speech();
  size_t off = 0;
  int64_t t = 0;
  for (auto _ : st) {
    AudioLevel l{ComputeLevel(std::span<const float>(x.data() + off, 320)), true};
    bench::DoNotOptimize(DecodeAudioLevelExtension(EncodeAudioLevelExtension(l)));
    off = (off + 320) % (x.size() - 320);
    ++t;
  }
  st.RequireRealtimeFactor(0.020, 5000);
}

SM_BENCH(audio, HighPassFilter_10ms) {
  // libwebrtc's high-pass filter (6.5) on one 10 ms frame at 16 kHz.
  hpf::HighPassFilter f(16000);
  auto x = Speech();
  std::vector<float> frame(160);
  size_t off = 0;
  for (auto _ : st) {
    std::copy_n(x.begin() + static_cast<std::ptrdiff_t>(off), 160, frame.begin());
    f.Process(frame);
    bench::DoNotOptimize(frame[0]);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 1000);  // Measured 8400x on an Apple M4 (Release).
}

SM_BENCH(audio, NoiseSuppressor_10ms) {
  // libwebrtc's NS (6.3, kHigh) on one 10 ms frame of noisy speech at
  // 16 kHz: Analyze then Process, as APM runs it.
  auto ns = std::make_unique<ns::NoiseSuppressor>();
  auto x = NoisySpeech();
  std::vector<float> frame(160);
  size_t off = 0;
  for (auto _ : st) {
    std::copy_n(x.begin() + static_cast<std::ptrdiff_t>(off), 160, frame.begin());
    ns->Analyze(frame);
    ns->Process(frame);
    bench::DoNotOptimize(frame[0]);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 500);  // Measured 3900x on an Apple M4 (Release).
}

SM_BENCH(audio, Agc1_10ms) {
  // AGC1 (6.4) as a call runs it, on one 10 ms frame at 16 kHz: the
  // emulated mic volume, AgcManagerDirect (clipping check, loudness meter
  // with its pitch-based VAD) and GainControl's fixed-digital compression
  // and limiter, in e2e::RunChain's order.
  auto manager = std::make_unique<agc1::AgcManagerDirect>(1);
  manager->Initialize();
  agc1::GainControlConfig g1;
  g1.target_level_dbfs = manager->digital_target_level_dbfs();
  g1.compression_gain_db = manager->digital_compression_gain_db();
  g1.enable_limiter = manager->digital_limiter_enabled();
  auto gc = std::make_unique<agc1::GainControl>(g1);
  agc1::CaptureLevelsConfig lc;
  lc.analog_mic_gain_emulation = true;
  agc1::CaptureLevels levels(lc);
  auto x = NoisySpeech();
  std::vector<float> frame(160), s16(160);
  size_t off = 0;
  for (auto _ : st) {
    std::copy_n(x.begin() + static_cast<std::ptrdiff_t>(off), 160, frame.begin());
    manager->set_stream_analog_level(levels.GetAnalogMicGainLevel());
    for (size_t i = 0; i < 160; ++i) s16[i] = frame[i] * 32768.0f;
    std::span<float> ch[1] = {s16};
    levels.ApplyPreLevelAdjustment(ch);
    const std::span<const float> cch[1] = {s16};
    manager->AnalyzePreProcess(cch);
    manager->Process(cch);
    if (auto g = manager->GetDigitalCompressionGain()) gc->set_compression_gain_db(*g);
    for (size_t i = 0; i < 160; ++i) frame[i] = s16[i] / 32768.0f;
    gc->Process(frame);
    levels.SetAnalogMicGainLevel(manager->recommended_analog_level());
    bench::DoNotOptimize(frame[0]);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 100);  // Measured 730x on an Apple M4 (Release).
}

SM_BENCH(audio, Agc2_10ms) {
  // AGC2 (6.4) on one 10 ms frame at 16 kHz: its RNN VAD (24 kHz features,
  // GRU), the adaptive digital gain and the limiter.
  auto agc = std::make_unique<agc2::GainController2>();
  auto x = NoisySpeech();
  std::vector<float> frame(160);
  size_t off = 0;
  for (auto _ : st) {
    std::copy_n(x.begin() + static_cast<std::ptrdiff_t>(off), 160, frame.begin());
    agc->Process(frame);
    bench::DoNotOptimize(frame[0]);
    off = (off + 160) % (x.size() - 160);
  }
  st.RequireRealtimeFactor(0.010, 40);  // Measured 300x on an Apple M4 (Release).
}

SM_BENCH(audio, Resampler_48k_to_16k_10ms) {
  // PushResampler<float> (6.11, PushSincResampler) on one 10 ms mono frame:
  // a 48 kHz device to 16 kHz processing.
  PushResampler<float> r(480, 160, 1);
  auto x = sim::SpeechLike(48000, 2.0, 1, true);
  std::vector<float> out(160);
  size_t off = 0;
  for (auto _ : st) {
    r.Resample(std::span<const float>(x.data() + off, 480), std::span<float>(out));
    bench::DoNotOptimize(out[0]);
    off = (off + 480) % (x.size() - 480);
  }
  st.RequireRealtimeFactor(0.010, 1000);  // Measured 6700x on an Apple M4 (Release).
}

SM_BENCH(audio, Resampler_44k1_to_48k_10ms) {
  // PushResampler<float> (6.11) on one 10 ms mono frame: a 44.1 kHz
  // microphone to 48 kHz.
  PushResampler<float> r(441, 480, 1);
  auto x = sim::SpeechLike(44100, 2.0, 1, true);
  std::vector<float> out(480);
  size_t off = 0;
  for (auto _ : st) {
    r.Resample(std::span<const float>(x.data() + off, 441), std::span<float>(out));
    bench::DoNotOptimize(out[0]);
    off = (off + 441) % (x.size() - 441);
  }
  st.RequireRealtimeFactor(0.010, 300);  // Measured 2000x on an Apple M4 (Release).
}

SM_BENCH(audio, SplResampler_48k_to_16k_10ms) {
  // SplResampler (6.11, libwebrtc's fixed-point resampler.cc) on one 10 ms
  // mono int16 frame, 48 to 16 kHz.
  SplResampler r(48000, 16000, 1);
  auto f = sim::SpeechLike(48000, 2.0, 1, true);
  std::vector<int16_t> x(f.size()), out(160);
  for (size_t i = 0; i < f.size(); ++i) x[i] = static_cast<int16_t>(std::clamp(f[i], -1.0f, 1.0f) * 32767.0f);
  size_t off = 0;
  for (auto _ : st) {
    size_t n = 0;
    r.Push(x.data() + off, 480, out.data(), out.size(), n);
    bench::DoNotOptimize(out[0]);
    off = (off + 480) % (x.size() - 480);
  }
  st.RequireRealtimeFactor(0.010, 150);  // Measured 1100x on an Apple M4 (Release).
}

namespace {
// A remote stream as the mixer sees it: 10 ms int16 frames at 48 kHz, cut
// from a precomputed signal so the benchmark times the mixer, not the source.
class BufferSource : public AudioMixerSource {
 public:
  BufferSource(uint64_t seed, float amp, uint32_t ssrc) : ssrc_(ssrc) {
    auto f = sim::SpeechLike(48000, 2.0, seed, true, amp);
    x_.resize(f.size());
    for (size_t i = 0; i < f.size(); ++i) x_[i] = static_cast<int16_t>(std::clamp(f[i], -1.0f, 1.0f) * 32767.0f);
  }
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz, AudioFrame* frame) override {
    const size_t spc = static_cast<size_t>(sample_rate_hz / 100);
    if (off_ + spc > x_.size()) off_ = 0;
    frame->UpdateFrame(ts_, x_.data() + off_, spc, sample_rate_hz, 1);
    AudioFramePacketInfo info;
    info.ssrc = ssrc_;
    info.rtp_timestamp = ts_;
    frame->packet_infos = {info};
    off_ += spc;
    ts_ += static_cast<uint32_t>(spc);
    return AudioFrameInfo::kNormal;
  }
  uint32_t Ssrc() const override { return ssrc_; }
  int PreferredSampleRate() const override { return 48000; }

 private:
  std::vector<int16_t> x_;
  size_t off_ = 0;
  uint32_t ts_ = 0;
  uint32_t ssrc_;
};
}  // namespace

SM_BENCH(audio, Mixer_6sources_48k_10ms) {
  // AudioMixerImpl (6.13, limiter on, as WebRtcVoiceEngine creates it) on
  // one 10 ms mono playout frame of a 6-way group call at 48 kHz: every
  // source's frame fetched, summed in float and limited.
  auto mixer = std::make_unique<AudioMixerImpl>();
  std::vector<std::unique_ptr<BufferSource>> sources;
  for (uint32_t i = 0; i < 6; ++i) {
    sources.push_back(std::make_unique<BufferSource>(100 + i, 0.6f, 1000 + i));
    mixer->AddSource(sources.back().get());
  }
  auto out = std::make_unique<AudioFrame>();
  for (auto _ : st) {
    mixer->Mix(1, out.get());
    bench::DoNotOptimize(out->data()[0]);
  }
  st.RequireRealtimeFactor(0.010, 500);  // Measured 4500x on an Apple M4 (Release).
}

// --- Quality ------------------------------------------------------------------

SM_BENCH_QUALITY(audio, Q_EchoCanceller_full_duplex_call) {
  e2e::CallScenario s;
  s.seconds = 30;
  s.near_start_s = 15;
  s.noise_amp = 0.0005f;
  auto c = e2e::MakeCall(s);
  auto r = e2e::RunChain(c, {true, true, true, false, false});
  double erle = e2e::SegmentDb(c.echo, 6, 15) - e2e::SegmentDb(r.out, 6, 15, 16000, r.latency);
  double near = e2e::SegmentDb(r.out, 18, 30, 16000, r.latency) - e2e::SegmentDb(c.talker, 18, 30);
  st.Counter("erle_db", erle);
  st.Counter("double_talk_near_level_change_db", near);
  st.Require("erle_db", erle, ">", 25);
  st.Require("double_talk_near_level_change_db", near, ">", -6);
}

SM_BENCH_QUALITY(audio, Q_EchoDelay_route_change) {
  e2e::CallScenario s;
  s.seconds = 24;
  s.echo_delay2_ms = 180;
  s.near_start_s = 1e9;
  s.noise_amp = 0.0005f;
  auto c = e2e::MakeCall(s);
  auto r = e2e::RunChain(c, {true, true, false, false, false});
  double erle_after = e2e::SegmentDb(c.echo, 18, 24) - e2e::SegmentDb(r.out, 18, 24, 16000, r.latency);
  st.Counter("erle_after_change_db", erle_after);
  st.Require("erle_after_change_db", erle_after, ">", 20);
}

SM_BENCH_QUALITY(audio, Q_NoiseSuppressor_7dB_snr) {
  // 6.3: the e2e noise case (talker in white noise at about 7 dB SNR)
  // through HPF -> NS (kHigh), measured as E2E_NoiseSuppressor and
  // sm_apm_compare measure it; the reference is the clean talker through
  // the same HPF, the output is taken at the NS delay (96 samples).
  e2e::CallScenario s;
  s.far_talk = false;
  s.noise_amp = 0.03f;
  auto c = e2e::MakeCall(s);
  const e2e::ChainConfig hpf_only{true, false, false, false, false};
  auto r = e2e::RunChain(c, {true, false, true, false, false});
  auto base = e2e::RunChain(c, hpf_only);
  e2e::CallScenario t = s;
  t.noise_amp = 0;
  auto ref = e2e::RunChain(e2e::MakeCall(t), hpf_only).out;
  const size_t lat = r.latency, start = 3 * 16000, n = ref.size() - start - lat;
  const double snr_in = dsp::SnrDb({ref.data() + start, n}, {base.out.data() + start, n});
  const double snr_out = dsp::SnrDb({ref.data() + start, n}, {r.out.data() + start + lat, n});
  // Speech frames by the clean talker's level; pauses are frames more than
  // 250 ms after it stopped.
  std::vector<int> truth;  // 1 speech, 0 pause, -1 not scored.
  for (size_t f = 0, since = 1000; (f + 1) * 160 <= c.talker.size(); ++f) {
    const bool speech = dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160)) > -45.0f;
    since = speech ? 0 : since + 1;
    truth.push_back(speech ? 1 : since > 25 ? 0 : -1);
  }
  auto frames_db = [&](const std::vector<float>& x, int want, size_t shift) {
    double e = 0;
    size_t cnt = 0;
    for (size_t f = 300; f < 2000 && f < truth.size(); ++f) {
      if (truth[f] != want) continue;
      for (size_t i = f * 160 + shift; i < f * 160 + 160 + shift && i < x.size(); ++i, ++cnt) {
        e += static_cast<double>(x[i]) * x[i];
      }
    }
    return 10 * std::log10(e / static_cast<double>(std::max<size_t>(1, cnt)) + 1e-20);
  };
  const double atten = frames_db(base.out, 0, 0) - frames_db(r.out, 0, lat);
  const double speech_change = frames_db(r.out, 1, lat) - frames_db(base.out, 1, 0);
  st.Counter("snr_in_db", snr_in);
  st.Counter("snr_out_db", snr_out);
  st.Counter("noise_attenuation_db", atten);
  st.Counter("speech_level_change_db", speech_change);
  // libwebrtc M157's NS on this case (docs/results/apm-webrtc.md): noise
  // attenuation in pauses 17.9 dB, speech -2.9 dB, SNR improvement -0.1 dB
  // (it takes the speech down with the noise in speech frames, so the SNR
  // against the clean talker does not rise; the v1 gate "> 4 dB" measured
  // the retired v1 suppressor). The port gives the same: 2.9 dB margin on
  // the attenuation, 2 dB on the speech loss, 1 dB on the SNR.
  st.Require("noise_attenuation_db", atten, ">", 15);
  st.Require("speech_level_change_db", speech_change, ">", -5);
  st.Require("snr_improvement_db", snr_out - snr_in, ">", -1);
}

namespace {
struct QuietThenLoud {
  double quiet_out, loud_change, noise_out, peak_dbfs;
};

// E2E_GainControl's case: a quiet talker (about -33 dBFS) for 20 s, then
// 20 dB louder, through HPF -> AGC1 (as a call runs it) or AGC2.
QuietThenLoud RunQuietThenLoud(bool agc2) {
  e2e::CallScenario q;
  q.far_talk = false;
  q.seconds = 40;
  q.near_gain_db = -10;
  q.noise_amp = 0.0002f;
  auto c = e2e::MakeCall(q);
  for (size_t i = c.capture.size() / 2; i < c.capture.size(); ++i) c.capture[i] *= dsp::DbToLinear(20);
  e2e::ChainConfig cc{true, false, false, false, true};
  cc.agc2 = agc2;
  auto r = e2e::RunChain(c, cc);
  auto talker_level = [&](size_t f) { return dsp::RmsDbfs(std::span<const float>(c.talker.data() + f * 160, 160)); };
  auto level = [&](const std::vector<float>& x, double a, double b, bool speech) {
    double e = 0;
    size_t n = 0;
    for (size_t f = static_cast<size_t>(a * 100); f < static_cast<size_t>(b * 100); ++f) {
      const bool is_speech = talker_level(f) > -60.0f;
      if (is_speech != speech) continue;
      if (!speech && (f < 25 || talker_level(f - 25) > -60.0f)) continue;  // Pauses only, not word tails.
      for (size_t i = f * 160; i < f * 160 + 160; ++i) e += static_cast<double>(x[i]) * x[i];
      n += 160;
    }
    return 10 * std::log10(e / static_cast<double>(std::max<size_t>(1, n)) + 1e-20);
  };
  float peak = 0;
  for (float v : r.out) peak = std::max(peak, std::fabs(v));
  return {level(r.out, 12, 20, true), level(r.out, 32, 40, true) - level(c.capture, 32, 40, true),
          level(r.out, 12, 20, false), dsp::LinearToDb(peak)};
}
}  // namespace

SM_BENCH_QUALITY(audio, Q_Agc_quiet_then_loud_talker) {
  // 6.4: the level AGC1 (the call default) and AGC2 bring a quiet talker
  // to, what they do to a loud one, and the peak.
  const QuietThenLoud a1 = RunQuietThenLoud(false);
  const QuietThenLoud a2 = RunQuietThenLoud(true);
  st.Counter("agc1_quiet_talker_out_dbfs", a1.quiet_out);
  st.Counter("agc1_loud_talker_change_db", a1.loud_change);
  st.Counter("agc1_noise_in_pauses_dbfs", a1.noise_out);
  st.Counter("agc1_peak_dbfs", a1.peak_dbfs);
  st.Counter("agc2_quiet_talker_out_dbfs", a2.quiet_out);
  st.Counter("agc2_loud_talker_change_db", a2.loud_change);
  st.Counter("agc2_noise_in_pauses_dbfs", a2.noise_out);
  st.Counter("agc2_peak_dbfs", a2.peak_dbfs);
  // libwebrtc M157 (docs/results/apm-webrtc.md): AGC1 lifts the quiet
  // talker (about -33 dBFS in) to -20.6 dBFS, AGC2 to -21.0; both peak at
  // 0.0 dBFS. 2.4-2.9 dB margin on the level reached; no clipping.
  st.Require("agc1_quiet_talker_out_dbfs", a1.quiet_out, ">", -23.5);
  st.Require("agc2_quiet_talker_out_dbfs", a2.quiet_out, ">", -23.5);
  st.Require("agc1_peak_dbfs", a1.peak_dbfs, "<=", 0.0);
  st.Require("agc2_peak_dbfs", a2.peak_dbfs, "<=", 0.0);
}

SM_BENCH_QUALITY(audio, Q_Mixer_5_loud_talkers) {
  // 6.13: five talkers near full scale mixed at 48 kHz (E2E_AudioMixer's
  // "five loud talkers 48k", the scenario sm_mixer_compare runs through
  // libwebrtc's own AudioMixerImpl): flat-topped runs in the plain
  // saturating sum and in the mix, and every talker heard in the mix.
  e2e::MixScenario s;
  for (const auto& m : e2e::MixScenarios()) {
    if (m.name == "five loud talkers 48k") s = m;
  }
  const size_t frames = static_cast<size_t>(s.seconds * 100);
  std::vector<std::vector<int16_t>> x;
  for (const auto& tc : s.talkers) {
    e2e::Talker t(tc);
    std::vector<int16_t> sig(frames * 480);
    for (size_t k = 0; k < frames; ++k) t.Next(static_cast<double>(k) * 0.01, 48000, &sig[k * 480]);
    x.push_back(std::move(sig));
  }
  std::vector<int16_t> sum(frames * 480);
  for (size_t i = 0; i < sum.size(); ++i) {
    int32_t v = 0;
    for (const auto& t : x) v += t[i];
    sum[i] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
  }
  const e2e::MixRun mix = e2e::RunOurMix(s);
  double min_corr = 1;
  for (const auto& t : x) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < std::min(t.size(), mix.out.size()); ++i) {
      ab += static_cast<double>(t[i]) * mix.out[i];
      aa += static_cast<double>(t[i]) * t[i];
      bb += static_cast<double>(mix.out[i]) * mix.out[i];
    }
    min_corr = std::min(min_corr, aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0.0);
  }
  const double sum_runs = static_cast<double>(e2e::ClippedRuns(sum, 1));
  const double mix_runs = static_cast<double>(e2e::ClippedRuns(mix.out, 1));
  st.Counter("plain_sum_flat_runs", sum_runs);
  st.Counter("mixer_flat_runs", mix_runs);
  st.Counter("min_talker_correlation", min_corr);
  // Measured (bit-exact with libwebrtc M157 in sm_mixer_compare): 790 flat
  // runs in the sum, 2 in the mix (the limiter maps a peak to exactly full
  // scale). The case must clip without the limiter; the mix may have 5x
  // the measured flat runs, still 100x fewer than the sum.
  st.Require("plain_sum_flat_runs", sum_runs, ">", 500);
  st.Require("mixer_flat_runs", mix_runs, "<=", 10);
  st.Require("min_talker_correlation", min_corr, ">", 0.2);
}
