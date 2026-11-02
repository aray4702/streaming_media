/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/retransmission_timeout_test.cc (compare/webrtc/reference/net/dcsctp/tx/retransmission_timeout_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/tx/retransmission_timeout.h"

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/types.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::sm::TimeDelta;

constexpr TimeDelta kMaxRtt = TimeDelta::Millis(8'000);
constexpr TimeDelta kInitialRto = TimeDelta::Millis(200);
constexpr TimeDelta kMaxRto = TimeDelta::Millis(800);
constexpr TimeDelta kMinRto = TimeDelta::Millis(120);
constexpr TimeDelta kMinRttVariance = TimeDelta::Millis(220);

DcSctpOptions MakeOptions() {
  DcSctpOptions options;
  options.rtt_max = DurationMs(kMaxRtt);
  options.rto_initial = DurationMs(kInitialRto);
  options.rto_max = DurationMs(kMaxRto);
  options.rto_min = DurationMs(kMinRto);
  options.min_rtt_variance = DurationMs(kMinRttVariance);
  return options;
}

SM_TEST(DcsctpRetransmissionTimeoutTest, HasValidInitialRto) {
  RetransmissionTimeout rto_(MakeOptions());
  SM_EXPECT_EQ(rto_.rto(), kInitialRto);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, HasValidInitialSrtt) {
  RetransmissionTimeout rto_(MakeOptions());
  SM_EXPECT_EQ(rto_.srtt(), kInitialRto);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, NegativeValuesDoNotAffectRTO) {
  RetransmissionTimeout rto_(MakeOptions());
  // Initial negative value
  rto_.ObserveRTT(TimeDelta::Millis(-10));
  SM_EXPECT_EQ(rto_.rto(), kInitialRto);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
  // Subsequent negative value
  rto_.ObserveRTT(TimeDelta::Millis(-10));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, TooLargeValuesDoNotAffectRTO) {
  RetransmissionTimeout rto_(MakeOptions());
  // Initial too large value
  rto_.ObserveRTT(kMaxRtt + TimeDelta::Millis(100));
  SM_EXPECT_EQ(rto_.rto(), kInitialRto);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
  // Subsequent too large value
  rto_.ObserveRTT(kMaxRtt + TimeDelta::Millis(100));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, WillNeverGoBelowMinimumRto) {
  RetransmissionTimeout rto_(MakeOptions());
  for (int i = 0; i < 1000; ++i) {
    rto_.ObserveRTT(TimeDelta::Millis(1));
  }
  SM_EXPECT_GE(rto_.rto(), kMinRto);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, WillNeverGoAboveMaximumRto) {
  RetransmissionTimeout rto_(MakeOptions());
  for (int i = 0; i < 1000; ++i) {
    rto_.ObserveRTT(kMaxRtt - TimeDelta::Millis(1));
    // Adding jitter, which would make it RTO be well above RTT.
    rto_.ObserveRTT(kMaxRtt - TimeDelta::Millis(100));
  }
  SM_EXPECT_LE(rto_.rto(), kMaxRto);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, CalculatesRtoForStableRtt) {
  RetransmissionTimeout rto_(MakeOptions());
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
  rto_.ObserveRTT(TimeDelta::Millis(128));
  SM_EXPECT_EQ(rto_.rto().ms(), 315);
  rto_.ObserveRTT(TimeDelta::Millis(123));
  SM_EXPECT_EQ(rto_.rto().ms(), 268);
  rto_.ObserveRTT(TimeDelta::Millis(125));
  SM_EXPECT_EQ(rto_.rto().ms(), 234);
  rto_.ObserveRTT(TimeDelta::Millis(127));
  SM_EXPECT_EQ(rto_.rto().ms(), 235);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, CalculatesRtoForUnstableRtt) {
  RetransmissionTimeout rto_(MakeOptions());
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
  rto_.ObserveRTT(TimeDelta::Millis(402));
  SM_EXPECT_EQ(rto_.rto().ms(), 623);
  rto_.ObserveRTT(TimeDelta::Millis(728));
  SM_EXPECT_EQ(rto_.rto().ms(), 800);
  rto_.ObserveRTT(TimeDelta::Millis(89));
  SM_EXPECT_EQ(rto_.rto().ms(), 800);
  rto_.ObserveRTT(TimeDelta::Millis(126));
  SM_EXPECT_EQ(rto_.rto().ms(), 800);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, WillStabilizeAfterAWhile) {
  RetransmissionTimeout rto_(MakeOptions());
  rto_.ObserveRTT(TimeDelta::Millis(124));
  rto_.ObserveRTT(TimeDelta::Millis(402));
  rto_.ObserveRTT(TimeDelta::Millis(728));
  rto_.ObserveRTT(TimeDelta::Millis(89));
  rto_.ObserveRTT(TimeDelta::Millis(126));
  SM_EXPECT_EQ(rto_.rto().ms(), 800);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 800);
  rto_.ObserveRTT(TimeDelta::Millis(122));
  SM_EXPECT_EQ(rto_.rto().ms(), 709);
  rto_.ObserveRTT(TimeDelta::Millis(123));
  SM_EXPECT_EQ(rto_.rto().ms(), 630);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 562);
  rto_.ObserveRTT(TimeDelta::Millis(122));
  SM_EXPECT_EQ(rto_.rto().ms(), 505);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 454);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 410);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 372);
  rto_.ObserveRTT(TimeDelta::Millis(124));
  SM_EXPECT_EQ(rto_.rto().ms(), 340);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, WillAlwaysStayAboveRTT) {
  // In simulations, it's quite common to have a very stable RTT, and having an
  // RTO at the same value will cause issues as expiry timers will be scheduled
  // to be expire exactly when a packet is supposed to arrive. The RTO must be
  // larger than the RTT. In non-simulated environments, this is a non-issue as
  // any jitter will increase the RTO.
  RetransmissionTimeout rto_(MakeOptions());

  for (int i = 0; i < 1000; ++i) {
    rto_.ObserveRTT(TimeDelta::Millis(124));
  }
  SM_EXPECT_EQ(rto_.rto().ms(), 234);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, CanSpecifySmallerMinimumRttVariance) {
  DcSctpOptions options = MakeOptions();
  options.min_rtt_variance =
      DurationMs(kMinRttVariance - TimeDelta::Millis(100));
  RetransmissionTimeout rto_(options);

  for (int i = 0; i < 1000; ++i) {
    rto_.ObserveRTT(TimeDelta::Millis(124));
  }
  SM_EXPECT_EQ(rto_.rto().ms(), 184);
}

SM_TEST(DcsctpRetransmissionTimeoutTest, CanSpecifyLargerMinimumRttVariance) {
  DcSctpOptions options = MakeOptions();
  options.min_rtt_variance =
      DurationMs(kMinRttVariance + TimeDelta::Millis(100));
  RetransmissionTimeout rto_(options);

  for (int i = 0; i < 1000; ++i) {
    rto_.ObserveRTT(TimeDelta::Millis(124));
  }
  SM_EXPECT_EQ(rto_.rto().ms(), 284);
}

}  // namespace
}  // namespace sm::dcsctp
