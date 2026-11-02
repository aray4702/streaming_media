/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/common/math_test.cc (compare/webrtc/reference/net/dcsctp/common/math_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/common/math.h"

#include <cstdint>

#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpMathUtilTest, CanRoundUpTo4) {
  // Signed numbers
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(-5)), -4);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(-4)), -4);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(-3)), 0);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(-2)), 0);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(-1)), 0);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(0)), 0);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(1)), 4);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(2)), 4);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(3)), 4);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(4)), 4);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(5)), 8);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(6)), 8);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(7)), 8);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int>(8)), 8);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int64_t>(10000000000)), 10000000000);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<int64_t>(10000000001)), 10000000004);

  // Unsigned numbers
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(0)), 0u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(1)), 4u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(2)), 4u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(3)), 4u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(4)), 4u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(5)), 8u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(6)), 8u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(7)), 8u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<unsigned int>(8)), 8u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<uint64_t>(10000000000)), 10000000000u);
  SM_EXPECT_EQ(RoundUpTo4(static_cast<uint64_t>(10000000001)), 10000000004u);
}

SM_TEST(DcsctpMathUtilTest, CanRoundDownTo4) {
  // Signed numbers
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(-5)), -8);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(-4)), -4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(-3)), -4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(-2)), -4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(-1)), -4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(0)), 0);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(1)), 0);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(2)), 0);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(3)), 0);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(4)), 4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(5)), 4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(6)), 4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(7)), 4);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int>(8)), 8);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int64_t>(10000000000)), 10000000000);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<int64_t>(10000000001)), 10000000000);

  // Unsigned numbers
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(0)), 0u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(1)), 0u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(2)), 0u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(3)), 0u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(4)), 4u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(5)), 4u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(6)), 4u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(7)), 4u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<unsigned int>(8)), 8u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<uint64_t>(10000000000)), 10000000000u);
  SM_EXPECT_EQ(RoundDownTo4(static_cast<uint64_t>(10000000001)), 10000000000u);
}

SM_TEST(DcsctpMathUtilTest, IsDivisibleBy4) {
  // Signed numbers
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(-4)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(-3)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(-2)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(-1)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(0)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(1)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(2)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(3)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(4)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(5)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(6)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(7)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int>(8)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int64_t>(10000000000)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<int64_t>(10000000001)), false);

  // Unsigned numbers
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(0)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(1)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(2)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(3)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(4)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(5)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(6)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(7)), false);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<unsigned int>(8)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<uint64_t>(10000000000)), true);
  SM_EXPECT_EQ(IsDivisibleBy4(static_cast<uint64_t>(10000000001)), false);
}

}  // namespace
}  // namespace sm::dcsctp
