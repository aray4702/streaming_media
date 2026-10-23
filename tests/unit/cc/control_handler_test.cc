#include "sm/cc/control_handler.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
TargetTransferRate Msg(int64_t kbps, uint8_t loss = 0, int64_t rtt_ms = 100, double ratio = 0) {
  TargetTransferRate m;
  m.target_rate = DataRate::KilobitsPerSec(kbps);
  m.fraction_loss = loss;
  m.round_trip_time = TimeDelta::Millis(rtt_ms);
  m.cwnd_reduce_ratio = ratio;
  return m;
}
}  // namespace

// libwebrtc has no unit test for CongestionControlHandler; these follow control_handler.cc (M157).
SM_TEST(ControlHandler, NothingBeforeTheFirstTarget) {
  CongestionControlHandler h;
  SM_EXPECT_FALSE(h.GetUpdate().has_value());
  h.SetPacerQueue(TimeDelta::Millis(3000));
  SM_EXPECT_FALSE(h.GetUpdate().has_value());
}

SM_TEST(ControlHandler, PassesOnOnlyWhatChanged) {
  CongestionControlHandler h;
  h.SetTargetRate(Msg(1000));
  auto u = h.GetUpdate();
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_EQ(u->target_rate.kbps(), 1000);
  // The same message again, or polled by the pacer-queue task: nothing new.
  h.SetTargetRate(Msg(1000));
  SM_EXPECT_FALSE(h.GetUpdate().has_value());
  SM_EXPECT_FALSE(h.GetUpdate().has_value());
  // Each of the target, the reduce ratio, the loss fraction and the RTT is enough.
  h.SetTargetRate(Msg(1100));
  SM_EXPECT_TRUE(h.GetUpdate().has_value());
  h.SetTargetRate(Msg(1100, 0, 100, 0.1));
  SM_EXPECT_TRUE(h.GetUpdate().has_value());
  h.SetTargetRate(Msg(1100, 20, 100, 0.1));
  SM_EXPECT_TRUE(h.GetUpdate().has_value());
  h.SetTargetRate(Msg(1100, 20, 120, 0.1));
  SM_EXPECT_TRUE(h.GetUpdate().has_value());
}

SM_TEST(ControlHandler, PacerQueueAboveTwoSecondsPausesTheEncoder) {
  CongestionControlHandler h;
  h.SetTargetRate(Msg(1000));
  SM_EXPECT_TRUE(h.GetUpdate().has_value());
  h.SetPacerQueue(TimeDelta::Millis(2000));  // Not above kMaxExpectedQueueLength.
  SM_EXPECT_FALSE(h.GetUpdate().has_value());
  h.SetPacerQueue(TimeDelta::Millis(2001));
  auto u = h.GetUpdate();
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_TRUE(u->target_rate.IsZero());
  SM_EXPECT_TRUE(h.encoder_paused());
  // While paused, a new loss fraction or RTT is not passed on; a new target is (still as zero) only
  // if it changes what was reported, which it does not.
  h.SetTargetRate(Msg(900, 30, 200));
  SM_EXPECT_FALSE(h.GetUpdate().has_value());
  // The queue drains: the encoder resumes at the latest target.
  h.SetPacerQueue(TimeDelta::Millis(100));
  u = h.GetUpdate();
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_EQ(u->target_rate.kbps(), 900);
  SM_EXPECT_FALSE(h.encoder_paused());
}

SM_TEST(ControlHandler, NetworkUnavailablePausesTheEncoder) {
  CongestionControlHandler h;
  h.SetTargetRate(Msg(1000));
  SM_EXPECT_TRUE(h.GetUpdate().has_value());
  h.SetNetworkAvailability(false);
  auto u = h.GetUpdate();
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_TRUE(u->target_rate.IsZero());
  h.SetNetworkAvailability(true);
  u = h.GetUpdate();
  SM_ASSERT_TRUE(u.has_value());
  SM_EXPECT_EQ(u->target_rate.kbps(), 1000);
}
