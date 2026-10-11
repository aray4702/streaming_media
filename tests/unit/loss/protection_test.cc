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
  auto d = pc.Update({TimeDelta::Millis(10), 0.05, 1.0, 8});
  SM_EXPECT_TRUE(d.nack);
  SM_EXPECT_EQ(d.fec.protection, 0.0);
}

SM_TEST(Protection, HighRttUsesFecMeetingTarget) {
  ProtectionController pc;
  auto d = pc.Update({TimeDelta::Millis(300), 0.05, 1.0, 10});
  SM_EXPECT_GT(d.fec.protection, 0.0);
  SM_EXPECT_LE(d.expected_residual, 0.01);
  SM_EXPECT_LE(d.fec.protection, 0.5);
}

SM_TEST(Protection, FecGrowsWithLossAndRtt) {
  double prev = -1;
  for (double loss : {0.01, 0.03, 0.06, 0.12}) {
    ProtectionController pc;
    auto d = pc.Update({TimeDelta::Millis(200), loss, 1.0, 10});
    SM_EXPECT_GE(d.fec.protection, prev);
    prev = d.fec.protection;
  }
  ProtectionController a, b;
  auto mid = a.Update({TimeDelta::Millis(60), 0.05, 1.0, 10});
  auto high = b.Update({TimeDelta::Millis(200), 0.05, 1.0, 10});
  SM_EXPECT_LT(mid.fec.protection, high.fec.protection);
}

SM_TEST(Protection, BurstyLossPicksInterleavedMasksAndOverheadCapped) {
  ProtectionController pc;
  auto d = pc.Update({TimeDelta::Millis(300), 0.5, 3.0, 10});
  SM_EXPECT_TRUE(d.fec.mask == FecMaskType::kInterleaved);
  SM_EXPECT_LE(d.fec.protection, 0.5);
  ProtectionController pc2;
  SM_EXPECT_TRUE(pc2.Update({TimeDelta::Millis(300), 0.05, 1.0, 10}).fec.mask == FecMaskType::kBlock);
}

SM_TEST(Protection, LossIsSmoothed) {
  ProtectionController pc;
  pc.Update({TimeDelta::Millis(200), 0.0, 1, 10});
  auto d = pc.Update({TimeDelta::Millis(200), 0.5, 1, 10});
  SM_EXPECT_NEAR(d.filtered_loss, 0.15, 1e-9);
}
