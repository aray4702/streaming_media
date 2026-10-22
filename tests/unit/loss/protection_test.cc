#include <algorithm>
#include <cmath>

#include "sm/loss/protection_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

SM_TEST(Protection, BinomialTail) {
  SM_EXPECT_NEAR(BinomialTail(10, 0, 0.1), 1 - std::pow(0.9, 10), 1e-9);
  SM_EXPECT_EQ(BinomialTail(5, 5, 0.5), 0.0);
  SM_EXPECT_EQ(BinomialTail(5, 1, 0.0), 0.0);
}

SM_TEST(Protection, LowRttIsNackOnly) {
  ProtectionController pc;
  auto d = pc.Update({TimeDelta::Millis(10), 0.05, 8});
  SM_EXPECT_TRUE(d.nack);
  SM_EXPECT_EQ(d.fec.fec_rate, 0);
}

SM_TEST(Protection, HighRttUsesFecMeetingTarget) {
  ProtectionController pc;
  auto d = pc.Update({TimeDelta::Millis(300), 0.05, 10});
  SM_EXPECT_GT(d.fec.fec_rate, 0);
  SM_EXPECT_LE(d.expected_residual, 0.01);
  SM_EXPECT_LE(d.fec.fec_rate, 128);
}

SM_TEST(Protection, FecGrowsWithLossAndRtt) {
  int prev = -1;
  for (double loss : {0.01, 0.03, 0.06, 0.12}) {
    ProtectionController pc;
    auto d = pc.Update({TimeDelta::Millis(200), loss, 10});
    SM_EXPECT_GE(d.fec.fec_rate, prev);
    prev = d.fec.fec_rate;
  }
  ProtectionController a, b;
  auto mid = a.Update({TimeDelta::Millis(60), 0.05, 10});
  auto high = b.Update({TimeDelta::Millis(200), 0.05, 10});
  SM_EXPECT_LT(mid.fec.fec_rate, high.fec.fec_rate);
}

// FecControllerDefault always asks for random masks and one frame per block
// (fec_controller_default.cc:131-143); the rate is m/k in Q8.
SM_TEST(Protection, RandomMasksQ8RateAndOverheadCapped) {
  ProtectionController pc;
  auto d = pc.Update({TimeDelta::Millis(300), 0.5, 10});
  SM_EXPECT_TRUE(d.fec.fec_mask_type == ulpfec::FecMaskType::kRandom);
  SM_EXPECT_EQ(d.fec.max_fec_frames, 1);
  SM_EXPECT_EQ(d.fec.fec_rate, 128);  // Capped at 5 of 10 packets.
  SM_EXPECT_EQ(ulpfec::ForwardErrorCorrection::NumFecPackets(10, d.fec.fec_rate), 5);
  // At 50% loss the parity count hits the cap, max(1, k / 2): ULPFEC's NumFecPackets gives it back
  // from the Q8 rate for every frame size up to 48 packets.
  for (int k = 1; k <= 48; ++k) {
    ProtectionController c;
    auto e = c.Update({TimeDelta::Millis(300), 0.5, static_cast<double>(k)});
    SM_EXPECT_EQ(ulpfec::ForwardErrorCorrection::NumFecPackets(k, e.fec.fec_rate), std::max(1, k / 2));
  }
}

SM_TEST(Protection, LossIsSmoothed) {
  ProtectionController pc;
  pc.Update({TimeDelta::Millis(200), 0.0, 10});
  auto d = pc.Update({TimeDelta::Millis(200), 0.5, 10});
  SM_EXPECT_NEAR(d.filtered_loss, 0.15, 1e-9);
}
