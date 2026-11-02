/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/public/types_test.cc (compare/webrtc/reference/net/dcsctp/public/types_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/public/types.h"

#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpTypesTest, DurationOperators) {
  DurationMs d1(10);
  DurationMs d2(25);
  SM_EXPECT_EQ(d1 + d2, DurationMs(35));
  SM_EXPECT_EQ(d2 - d1, DurationMs(15));

  d1 += d2;
  SM_EXPECT_EQ(d1, DurationMs(35));

  d1 -= DurationMs(5);
  SM_EXPECT_EQ(d1, DurationMs(30));

  d1 *= 1.5;
  SM_EXPECT_EQ(d1, DurationMs(45));

  SM_EXPECT_EQ(DurationMs(10) * 2, DurationMs(20));
}

SM_TEST(DcsctpTypesTest, TimeOperators) {
  SM_EXPECT_EQ(TimeMs(250) + DurationMs(100), TimeMs(350));
  SM_EXPECT_EQ(DurationMs(250) + TimeMs(100), TimeMs(350));
  SM_EXPECT_EQ(TimeMs(250) - DurationMs(100), TimeMs(150));
  SM_EXPECT_EQ(TimeMs(250) - TimeMs(100), DurationMs(150));

  TimeMs t1(150);
  t1 -= DurationMs(50);
  SM_EXPECT_EQ(t1, TimeMs(100));
  t1 += DurationMs(200);
  SM_EXPECT_EQ(t1, TimeMs(300));
}

}  // namespace
}  // namespace sm::dcsctp
