#include <algorithm>
#include <cmath>
#include <numbers>

#include "sm/core/dsp.h"
#include "sm/loss/plc.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

SM_TEST(Plc, ContinuesPeriodicSignal) {
  const int fs = 16000, frame = 160;
  auto x = sim::Sine(200, fs, 1.0, 0.5f);
  PacketLossConcealer plc;
  std::vector<float> buf(frame);
  for (int f = 0; f < 20; ++f) {
    std::copy(x.begin() + f * frame, x.begin() + (f + 1) * frame, buf.begin());
    plc.OnGoodFrame(buf);
  }
  plc.Conceal(buf);  // Frame 20 lost.
  std::span<const float> ref(x.data() + 20 * frame, frame);
  double snr = dsp::SnrDb(ref, buf);
  SM_RECORD("plc_sine_snr_db", snr);
  SM_EXPECT_GT(snr, 20.0);
  SM_EXPECT_EQ(plc.stats().last_pitch_lag, 80);
}

SM_TEST(Plc, FadesToSilenceOnLongLoss) {
  PacketLossConcealer plc;
  auto x = sim::Sine(150, 16000, 0.2, 0.5f);
  std::vector<float> buf(160);
  for (int f = 0; f < 10; ++f) {
    std::copy(x.begin() + f * 160, x.begin() + (f + 1) * 160, buf.begin());
    plc.OnGoodFrame(buf);
  }
  for (int f = 0; f < 8; ++f) plc.Conceal(buf);  // 80 ms > 10 + 50 ms.
  SM_EXPECT_LT(dsp::Rms(buf), 1e-6f);
  SM_EXPECT_EQ(plc.stats().concealment_events, 1u);
  SM_EXPECT_EQ(plc.stats().concealed_frames, 8u);
}

SM_TEST(Plc, MergeAvoidsDiscontinuity) {
  const int frame = 160;
  auto x = sim::Sine(200, 16000, 1.0, 0.5f);
  PacketLossConcealer plc;
  std::vector<float> buf(frame);
  for (int f = 0; f < 10; ++f) {
    std::copy(x.begin() + f * frame, x.begin() + (f + 1) * frame, buf.begin());
    plc.OnGoodFrame(buf);
  }
  plc.Conceal(buf);
  float last = buf.back();
  std::copy(x.begin() + 11 * frame, x.begin() + 12 * frame, buf.begin());
  plc.OnGoodFrame(buf);
  // Step at the boundary stays within a normal per-sample change of the sine.
  float max_step = 2 * 3.1416f * 200 / 16000 * 0.5f * 1.5f;
  SM_EXPECT_LT(std::fabs(buf[0] - last), max_step);
  SM_EXPECT_EQ(plc.stats().merges, 1u);
}

SM_TEST(Plc, UnvoicedHistoryUsesNoise) {
  auto noise = sim::WhiteNoise(16000, 0.2, 0.1f, 3);
  PacketLossConcealer plc;
  std::vector<float> buf(160);
  for (int f = 0; f < 10; ++f) {
    std::copy(noise.begin() + f * 160, noise.begin() + (f + 1) * 160, buf.begin());
    plc.OnGoodFrame(buf);
  }
  plc.Conceal(buf);
  SM_EXPECT_LT(plc.stats().last_correlation, 0.5f);
  // Level roughly preserved (within 6 dB).
  SM_EXPECT_NEAR(dsp::RmsDbfs(buf), dsp::LinearToDb(0.1f), 6.0);
}
