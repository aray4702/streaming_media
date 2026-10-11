// Unit tests: echo delay estimator, echo canceller.
#include "sm/audio/echo_canceller.h"
#include "sm/audio/echo_delay_estimator.h"
#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::audio;

namespace {
// Capture = far delayed by `delay` samples through an optional room response.
std::vector<float> Echo(const std::vector<float>& far, size_t delay, const std::vector<float>& rir, float gain) {
  std::vector<float> d(far.size(), 0.0f);
  auto conv = rir.empty() ? far : sim::Convolve(far, rir);
  for (size_t i = delay; i < far.size(); ++i) d[i] = gain * conv[i - delay];
  return d;
}
}  // namespace

SM_TEST(EchoDelay, FindsKnownDelay) {
  const int fs = 16000;
  auto far = sim::SpeechLike(fs, 4.0, 1, false);
  auto near = Echo(far, 1600, {}, 0.5f);  // 100 ms.
  auto noise = sim::WhiteNoise(fs, 4.0, 0.002f, 2);
  for (size_t i = 0; i < near.size(); ++i) near[i] += noise[i];
  EchoDelayEstimator est;
  for (size_t i = 0; i + 160 <= far.size(); i += 160) {
    est.AnalyzeRender(std::span<const float>(far.data() + i, 160));
    est.AnalyzeCapture(std::span<const float>(near.data() + i, 160));
  }
  SM_ASSERT_TRUE(est.delay_samples().has_value());
  SM_EXPECT_NEAR(*est.delay_samples(), 1600, 2);
  SM_RECORD("delay_confidence", est.confidence());
}

SM_TEST(EchoDelay, TracksDelayChangeThroughRoom) {
  const int fs = 16000;
  auto far = sim::SpeechLike(fs, 8.0, 3, false);
  auto rir = sim::RoomImpulseResponse(fs, 50, 120, 4);
  auto e1 = Echo(far, 800, rir, 1.0f);   // 50 ms
  auto e2 = Echo(far, 3200, rir, 1.0f);  // 200 ms after a route change
  EchoDelayEstimator est;
  std::optional<int> first;
  for (size_t i = 0; i + 160 <= far.size(); i += 160) {
    const auto& near = i < 4 * 16000 ? e1 : e2;
    est.AnalyzeRender(std::span<const float>(far.data() + i, 160));
    est.AnalyzeCapture(std::span<const float>(near.data() + i, 160));
    if (i == 4 * 16000 - 160) first = est.delay_samples();
  }
  SM_ASSERT_TRUE(first.has_value());
  // Direct path is 8 samples into the room response.
  SM_EXPECT_NEAR(*first, 808, 4);
  SM_ASSERT_TRUE(est.delay_samples().has_value());
  SM_EXPECT_NEAR(*est.delay_samples(), 3208, 4);
}

SM_TEST(EchoCanceller, ConvergesOnFarEndOnly) {
  const int fs = 16000;
  auto far = sim::SpeechLike(fs, 10.0, 5, false);
  auto rir = sim::RoomImpulseResponse(fs, 40, 100, 6);
  auto near = Echo(far, 960, rir, 1.0f);  // 60 ms bulk delay.
  EchoCanceller aec;
  for (size_t i = 0; i + 160 <= far.size(); i += 160) {
    aec.AnalyzeRender(std::span<const float>(far.data() + i, 160));
    aec.ProcessCapture(std::span<float>(near.data() + i, 160));
  }
  SM_RECORD("aec_erle_linear_db", aec.stats().erle_linear_db);
  SM_RECORD("aec_erle_db", aec.stats().erle_db);
  SM_RECORD("aec_bulk_delay_ms", aec.stats().delay_samples / 16.0);
  SM_EXPECT_GT(aec.stats().erle_linear_db, 15.0f);
  SM_EXPECT_GT(aec.stats().erle_db, 25.0f);
  SM_EXPECT_GE(aec.stats().delay_changes, 1u);
}

SM_TEST(EchoCanceller, KeepsNearEndDuringDoubleTalk) {
  const int fs = 16000;
  auto far = sim::SpeechLike(fs, 12.0, 7, false);
  auto talker = sim::SpeechLike(fs, 12.0, 8, false, 0.2f);
  auto rir = sim::RoomImpulseResponse(fs, 40, 100, 9);
  auto echo = Echo(far, 640, rir, 1.0f);
  std::vector<float> near(far.size());
  // Near-end talks during 8-12 s (after convergence): double talk.
  for (size_t i = 0; i < near.size(); ++i) near[i] = echo[i] + (i >= 8u * fs ? talker[i] : 0.0f);
  EchoCanceller aec;
  for (size_t i = 0; i + 160 <= far.size(); i += 160) {
    aec.AnalyzeRender(std::span<const float>(far.data() + i, 160));
    aec.ProcessCapture(std::span<float>(near.data() + i, 160));
  }
  size_t lat = aec.latency_samples();
  std::span<const float> ref(talker.data() + 9 * fs, 3u * fs - lat);
  std::span<const float> out(near.data() + 9 * fs + lat, 3u * fs - lat);
  double kept_db = dsp::RmsDbfs(out) - dsp::RmsDbfs(ref);
  SM_RECORD("aec_double_talk_near_level_change_db", kept_db);
  SM_RECORD("aec_double_talk_snr_db", dsp::SnrDb(ref, out));
  SM_EXPECT_GT(kept_db, -6.0);
  SM_EXPECT_GT(dsp::SnrDb(ref, out), 5.0);
  SM_EXPECT_GT(aec.stats().background_resets + aec.stats().foreground_updates, 0u);
}
