// E2E 6.13 (#1175): group-call playout through libwebrtc M157's AudioMixerImpl port.
//
// Five talkers near full scale, mixed as a group call's playout mixes its remote streams. A plain
// saturating sum (what the group-call client did before) flattens the peaks; the mixer's AGC2
// limiter keeps them round. M157's AudioMixerImpl mixes every unmuted source: the two quietest
// talkers are in the mix too (there is no "3 loudest" selection in M157, see
// sm/audio/audio_mixer.h). The same scenarios run against libwebrtc's own mixer in
// compare/webrtc/mixer_compare.cc.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "mixer_sims.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::e2e;

namespace {

const MixScenario& Scenario(const char* name) {
  static const std::vector<MixScenario> all = MixScenarios();
  for (const auto& s : all) {
    if (s.name == name) return s;
  }
  return all.front();
}

// Each talker's own frames at `rate`, as the mixer gets them (no join/leave in these scenarios).
std::vector<std::vector<int16_t>> TalkerSignals(const MixScenario& s, int rate) {
  std::vector<std::vector<int16_t>> x;
  const size_t spc = static_cast<size_t>(rate / 100);
  for (const auto& c : s.talkers) {
    Talker t(c);
    std::vector<int16_t> sig(static_cast<size_t>(s.seconds * 100) * spc * c.channels);
    for (size_t k = 0; k < static_cast<size_t>(s.seconds * 100); ++k) t.Next(k * 0.01, rate, &sig[k * spc * c.channels]);
    x.push_back(std::move(sig));
  }
  return x;
}

double Correlation(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
  double ab = 0, aa = 0, bb = 0;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    ab += static_cast<double>(a[i]) * b[i];
    aa += static_cast<double>(a[i]) * a[i];
    bb += static_cast<double>(b[i]) * b[i];
  }
  return aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0;
}

}  // namespace

SM_TEST(E2E_AudioMixer, FiveTalkersNoClippingAndEveryTalkerHeard) {
  const MixScenario& s = Scenario("five loud talkers 48k");
  SM_ASSERT_EQ(s.talkers.size(), 5u);
  const auto x = TalkerSignals(s, 48000);
  // The plain saturating sum the group-call playout used before this port.
  std::vector<int16_t> sum(x[0].size());
  for (size_t i = 0; i < sum.size(); ++i) {
    int32_t v = 0;
    for (const auto& t : x) v += t[i];
    sum[i] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
  }
  const MixRun mix = RunOurMix(s);
  SM_ASSERT_EQ(mix.out.size(), sum.size());
  const size_t sum_clipped = ClippedRuns(sum, 1);
  const size_t mix_clipped = ClippedRuns(mix.out, 1);
  int peak = 0;
  for (int16_t v : mix.out) peak = std::max(peak, std::abs(static_cast<int>(v)));
  const size_t sum_rail = RailSamples(sum);
  const size_t mix_rail = RailSamples(mix.out);
  std::printf("five talkers: plain sum %zu flat-topped runs (%zu samples at the rails), mixer %zu (%zu); mixer peak %d; "
              "limited %llu of %zu mixes\n",
              sum_clipped, sum_rail, mix_clipped, mix_rail, peak, static_cast<unsigned long long>(mix.limited_mixes),
              mix.rates.size());
  // The sum flattens hundreds of peaks. The limiter's saturation region scales a peak to exactly full
  // scale (interpolated_gain_curve.cc), so a peak can touch the rail for a sample or two, as in
  // libwebrtc (bit-exact in sm_mixer_compare): 100 times fewer flat tops, 50 times fewer samples at the rails.
  SM_EXPECT_GT(sum_clipped, 500u);
  SM_EXPECT_LE(mix_clipped * 100, sum_clipped);
  SM_EXPECT_LE(mix_rail * 50, sum_rail);
  SM_EXPECT_GT(mix.limited_mixes, 100u);
  // Every talker is in the mix, the two quietest included (M157 mixes all of them).
  for (size_t i = 0; i < x.size(); ++i) {
    const double c = Correlation(mix.out, x[i]);
    std::printf("  talker %zu (amp %.1f): correlation with the mix %.3f\n", i, s.talkers[i].amp, c);
    SM_EXPECT_GT(c, 0.2);
  }
  SM_EXPECT_EQ(mix.frames_mixed, 5u * mix.rates.size());
}

// Talkers join and leave, one mutes, one fails every 37th frame, and they run at 16, 32 and 48 kHz:
// the mix rate follows the highest preferred rate among the sources present, rounded up to a native rate.
SM_TEST(E2E_AudioMixer, JoinLeaveMuteAndMixRate) {
  const MixScenario& s = Scenario("five talkers, 16/48 kHz, join/leave/mute/error");
  const MixRun mix = RunOurMix(s);
  SM_ASSERT_EQ(mix.rates.size(), 1200u);
  for (size_t k = 0; k < mix.rates.size(); ++k) {
    const double t = static_cast<double>(k) * 0.01;
    const int expected = t < 2.0 ? 16000 : t < 3.0 ? 32000 : t < 9.0 ? 48000 : 32000;
    if (mix.rates[k] != expected) {
      SM_EXPECT_EQ(mix.rates[k], expected);
      break;
    }
  }
  // At most a peak touching the rail now and then (see above), at every rate.
  const size_t runs = ClippedRuns(mix.out, 1);
  std::printf("join/leave: %llu frames mixed, %llu limited mixes, %zu flat-topped runs, %zu samples at the rails\n",
              static_cast<unsigned long long>(mix.frames_mixed), static_cast<unsigned long long>(mix.limited_mixes), runs,
              RailSamples(mix.out));
  SM_EXPECT_LE(runs, 5u);
  SM_EXPECT_LE(RailSamples(mix.out), mix.out.size() / 2000);
}

// A stereo talker and two mono talkers into a stereo mix: the mono ones are upmixed, so the two
// output channels differ only by the stereo talker.
SM_TEST(E2E_AudioMixer, StereoMixKeepsTheStereoTalker) {
  const MixScenario& s = Scenario("stereo out, one stereo talker");
  SM_ASSERT_EQ(s.out_channels, 2u);
  const MixRun mix = RunOurMix(s);
  size_t differ = 0;
  for (size_t i = 0; i + 1 < mix.out.size(); i += 2) differ += mix.out[i] != mix.out[i + 1];
  SM_EXPECT_GT(differ, mix.out.size() / 4);
  SM_EXPECT_LE(ClippedRuns(mix.out, 2), 5u);
  SM_EXPECT_LE(RailSamples(mix.out), mix.out.size() / 2000);
  SM_EXPECT_EQ(mix.remix_unsupported, 0u);
}
