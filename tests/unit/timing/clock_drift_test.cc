#include <cmath>

#include "sm/core/random.h"
#include "sm/sim/signals.h"
#include "sm/core/dsp.h"
#include "sm/timing/clock_drift.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::timing;

SM_TEST(ClockDrift, RlsFitsLine) {
  RlsLineFit f;
  for (int i = 0; i < 100; ++i) f.Update(1000 + i, 3.0 * (1000 + i) - 7);
  // The finite initial covariance acts as a weak prior: tiny bias only.
  SM_EXPECT_NEAR(f.slope(), 3.0, 1e-4);
  SM_EXPECT_NEAR(f.intercept(), -7, 1e-2);
  SM_EXPECT_NEAR(f.Predict(2000), 5993, 0.1);
}

SM_TEST(ClockDrift, EstimatesPpmUnderJitter) {
  for (double ppm : {-200.0, 80.0, 300.0}) {
    ClockDriftEstimator est;
    Random rng(5);
    // Sender clock: media time advances (1 - ppm) per real second.
    for (int i = 0; i < 3000; ++i) {
      double real_ms = i * 20.0;
      uint32_t ts = static_cast<uint32_t>(std::llround(real_ms * (1 - ppm * 1e-6) * 16));
      double jitter = std::fabs(rng.Gaussian(0, 15));
      est.OnPacket(ts, 16000, Timestamp::Micros(static_cast<int64_t>((real_ms + 40 + jitter) * 1000)));
    }
    SM_EXPECT_TRUE(est.converged());
    SM_EXPECT_NEAR(est.drift_ppm(), ppm, 15);
  }
}

SM_TEST(ClockDrift, CompensatorRatioAndQuality) {
  DriftCompensator c;
  c.SetRatio(1.001);  // Produce 0.1% more samples.
  auto x = sim::Sine(440, 16000, 1.0, 0.5f);
  std::vector<float> out;
  for (size_t i = 0; i < x.size(); i += 160) c.Process(std::span<const float>(x.data() + i, 160), out);
  SM_EXPECT_NEAR(static_cast<double>(out.size()), 16016, 3);
  // Output is the same tone, stretched by 0.1%: compare with a 439.56 Hz sine.
  auto ref = sim::Sine(440 / 1.001, 16000, 1.1, 0.5f);
  // Output k reads input position 1 + k/ratio of a stream that is prefixed
  // with 3 history samples: a 2-sample delay.
  double snr = dsp::SnrDb(std::span<const float>(ref.data(), 15000), std::span<const float>(out.data() + 2, 15000));
  SM_RECORD("compensator_snr_db", snr);
  SM_EXPECT_GT(snr, 30.0);
}

SM_TEST(ClockDrift, PiControllerDirection) {
  DriftCompensator c;
  c.UpdateFromLevel(80, 60);  // Too full -> consume faster -> ratio < 1.
  SM_EXPECT_LT(c.ratio(), 1.0);
  DriftCompensator d;
  d.UpdateFromLevel(40, 60);
  SM_EXPECT_GT(d.ratio(), 1.0);
  for (int i = 0; i < 100000; ++i) d.UpdateFromLevel(1000, 0);
  SM_EXPECT_GE(d.ratio(), 1.0 - 0.005 - 1e-12);
}
