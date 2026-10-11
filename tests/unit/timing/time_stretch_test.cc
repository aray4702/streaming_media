#include <cmath>

#include "sm/core/dsp.h"
#include "sm/sim/signals.h"
#include "sm/timing/time_stretch.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

SM_TEST(TimeStretch, AccelerateRemovesWholePeriods) {
  TimeStretcher ts;
  auto x = sim::Sine(200, 16000, 0.03, 0.5f);  // 480 samples, period 80.
  std::vector<float> out;
  size_t removed = ts.Accelerate(x, out);
  SM_EXPECT_GT(removed, 0u);
  SM_EXPECT_EQ(removed % 80, 0u);
  SM_EXPECT_EQ(out.size(), x.size() - removed);
  // Output is still the same sine: compare to the unshifted reference.
  auto ref = sim::Sine(200, 16000, 0.03, 0.5f);
  double snr = dsp::SnrDb(std::span<const float>(ref.data(), out.size()), out);
  SM_RECORD("accelerate_sine_snr_db", snr);
  SM_EXPECT_GT(snr, 30.0);
}

SM_TEST(TimeStretch, ExpandInsertsWholePeriods) {
  TimeStretcher ts;
  auto x = sim::Sine(250, 16000, 0.03, 0.5f);  // Period 64.
  std::vector<float> out;
  size_t added = ts.PreemptiveExpand(x, out);
  SM_EXPECT_GT(added, 0u);
  SM_EXPECT_EQ(added % 64, 0u);
  SM_EXPECT_EQ(out.size(), x.size() + added);
  auto ref = sim::Sine(250, 16000, 0.04, 0.5f);
  SM_EXPECT_GT(dsp::SnrDb(std::span<const float>(ref.data(), out.size()), out), 30.0);
}

SM_TEST(TimeStretch, RejectsNoiseAcceptsSilence) {
  TimeStretcher ts;
  auto noise = sim::WhiteNoise(16000, 0.03, 0.3f, 4);
  std::vector<float> out;
  SM_EXPECT_EQ(ts.Accelerate(noise, out), 0u);
  SM_EXPECT_EQ(out.size(), noise.size());
  std::vector<float> silence(480, 0.0f);
  SM_EXPECT_GT(ts.Accelerate(silence, out), 0u);
  SM_EXPECT_EQ(ts.stats().accelerate_rejected, 1u);
  SM_EXPECT_EQ(ts.stats().accelerate_ok, 1u);
}

SM_TEST(TimeStretch, TooShortInputIsCopied) {
  TimeStretcher ts;
  std::vector<float> x(100, 0.1f), out;
  SM_EXPECT_EQ(ts.PreemptiveExpand(x, out), 0u);
  SM_EXPECT_EQ(out.size(), 100u);
}
