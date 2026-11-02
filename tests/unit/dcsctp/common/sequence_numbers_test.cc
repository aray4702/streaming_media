/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/common/sequence_numbers_test.cc (compare/webrtc/reference/net/dcsctp/common/sequence_numbers_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/common/sequence_numbers.h"

#include <cstdint>

#include "sm/dcsctp/rtc_compat.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

using Wrapped = StrongAlias<class WrappedTag, uint16_t>;
using TestSequence = UnwrappedSequenceNumber<Wrapped>;

SM_TEST(DcsctpSequenceNumbersTest, SimpleUnwrapping) {
  TestSequence::Unwrapper unwrapper;

  TestSequence s0 = unwrapper.Unwrap(Wrapped(0));
  TestSequence s1 = unwrapper.Unwrap(Wrapped(1));
  TestSequence s2 = unwrapper.Unwrap(Wrapped(2));
  TestSequence s3 = unwrapper.Unwrap(Wrapped(3));

  SM_EXPECT_LT(s0, s1);
  SM_EXPECT_LT(s0, s2);
  SM_EXPECT_LT(s0, s3);
  SM_EXPECT_LT(s1, s2);
  SM_EXPECT_LT(s1, s3);
  SM_EXPECT_LT(s2, s3);

  SM_EXPECT_EQ(TestSequence::Difference(s1, s0), 1);
  SM_EXPECT_EQ(TestSequence::Difference(s2, s0), 2);
  SM_EXPECT_EQ(TestSequence::Difference(s3, s0), 3);

  SM_EXPECT_GT(s1, s0);
  SM_EXPECT_GT(s2, s0);
  SM_EXPECT_GT(s3, s0);
  SM_EXPECT_GT(s2, s1);
  SM_EXPECT_GT(s3, s1);
  SM_EXPECT_GT(s3, s2);

  s0.Increment();
  SM_EXPECT_EQ(s0, s1);
  s1.Increment();
  SM_EXPECT_EQ(s1, s2);
  s2.Increment();
  SM_EXPECT_EQ(s2, s3);

  SM_EXPECT_EQ(TestSequence::AddTo(s0, 2), s3);
}

SM_TEST(DcsctpSequenceNumbersTest, MidValueUnwrapping) {
  TestSequence::Unwrapper unwrapper;

  TestSequence s0 = unwrapper.Unwrap(Wrapped(0x7FFE));
  TestSequence s1 = unwrapper.Unwrap(Wrapped(0x7FFF));
  TestSequence s2 = unwrapper.Unwrap(Wrapped(0x8000));
  TestSequence s3 = unwrapper.Unwrap(Wrapped(0x8001));

  SM_EXPECT_LT(s0, s1);
  SM_EXPECT_LT(s0, s2);
  SM_EXPECT_LT(s0, s3);
  SM_EXPECT_LT(s1, s2);
  SM_EXPECT_LT(s1, s3);
  SM_EXPECT_LT(s2, s3);

  SM_EXPECT_EQ(TestSequence::Difference(s1, s0), 1);
  SM_EXPECT_EQ(TestSequence::Difference(s2, s0), 2);
  SM_EXPECT_EQ(TestSequence::Difference(s3, s0), 3);

  SM_EXPECT_GT(s1, s0);
  SM_EXPECT_GT(s2, s0);
  SM_EXPECT_GT(s3, s0);
  SM_EXPECT_GT(s2, s1);
  SM_EXPECT_GT(s3, s1);
  SM_EXPECT_GT(s3, s2);

  s0.Increment();
  SM_EXPECT_EQ(s0, s1);
  s1.Increment();
  SM_EXPECT_EQ(s1, s2);
  s2.Increment();
  SM_EXPECT_EQ(s2, s3);

  SM_EXPECT_EQ(TestSequence::AddTo(s0, 2), s3);
}

SM_TEST(DcsctpSequenceNumbersTest, WrappedUnwrapping) {
  TestSequence::Unwrapper unwrapper;

  TestSequence s0 = unwrapper.Unwrap(Wrapped(0xFFFE));
  TestSequence s1 = unwrapper.Unwrap(Wrapped(0xFFFF));
  TestSequence s2 = unwrapper.Unwrap(Wrapped(0x0000));
  TestSequence s3 = unwrapper.Unwrap(Wrapped(0x0001));

  SM_EXPECT_LT(s0, s1);
  SM_EXPECT_LT(s0, s2);
  SM_EXPECT_LT(s0, s3);
  SM_EXPECT_LT(s1, s2);
  SM_EXPECT_LT(s1, s3);
  SM_EXPECT_LT(s2, s3);

  SM_EXPECT_EQ(TestSequence::Difference(s1, s0), 1);
  SM_EXPECT_EQ(TestSequence::Difference(s2, s0), 2);
  SM_EXPECT_EQ(TestSequence::Difference(s3, s0), 3);

  SM_EXPECT_GT(s1, s0);
  SM_EXPECT_GT(s2, s0);
  SM_EXPECT_GT(s3, s0);
  SM_EXPECT_GT(s2, s1);
  SM_EXPECT_GT(s3, s1);
  SM_EXPECT_GT(s3, s2);

  s0.Increment();
  SM_EXPECT_EQ(s0, s1);
  s1.Increment();
  SM_EXPECT_EQ(s1, s2);
  s2.Increment();
  SM_EXPECT_EQ(s2, s3);

  SM_EXPECT_EQ(TestSequence::AddTo(s0, 2), s3);
}

SM_TEST(DcsctpSequenceNumbersTest, WrapAroundAFewTimes) {
  TestSequence::Unwrapper unwrapper;

  TestSequence s0 = unwrapper.Unwrap(Wrapped(0));
  TestSequence prev = s0;

  for (uint32_t i = 1; i < 65536 * 3; i++) {
    uint16_t wrapped = static_cast<uint16_t>(i);
    TestSequence si = unwrapper.Unwrap(Wrapped(wrapped));

    SM_EXPECT_LT(s0, si);
    SM_EXPECT_LT(prev, si);
    prev = si;
  }
}

SM_TEST(DcsctpSequenceNumbersTest, IncrementIsSameAsWrapped) {
  TestSequence::Unwrapper unwrapper;

  TestSequence s0 = unwrapper.Unwrap(Wrapped(0));

  for (uint32_t i = 1; i < 65536 * 2; i++) {
    uint16_t wrapped = static_cast<uint16_t>(i);
    TestSequence si = unwrapper.Unwrap(Wrapped(wrapped));

    s0.Increment();
    SM_EXPECT_EQ(s0, si);
  }
}

SM_TEST(DcsctpSequenceNumbersTest, UnwrappingLargerNumberIsAlwaysLarger) {
  TestSequence::Unwrapper unwrapper;

  for (uint32_t i = 1; i < 65536 * 2; i++) {
    uint16_t wrapped = static_cast<uint16_t>(i);
    TestSequence si = unwrapper.Unwrap(Wrapped(wrapped));

    SM_EXPECT_GT(unwrapper.Unwrap(Wrapped(wrapped + 1)), si);
    SM_EXPECT_GT(unwrapper.Unwrap(Wrapped(wrapped + 5)), si);
    SM_EXPECT_GT(unwrapper.Unwrap(Wrapped(wrapped + 10)), si);
    SM_EXPECT_GT(unwrapper.Unwrap(Wrapped(wrapped + 100)), si);
  }
}

SM_TEST(DcsctpSequenceNumbersTest, UnwrappingSmallerNumberIsAlwaysSmaller) {
  TestSequence::Unwrapper unwrapper;

  for (uint32_t i = 1; i < 65536 * 2; i++) {
    uint16_t wrapped = static_cast<uint16_t>(i);
    TestSequence si = unwrapper.Unwrap(Wrapped(wrapped));

    SM_EXPECT_LT(unwrapper.Unwrap(Wrapped(wrapped - 1)), si);
    SM_EXPECT_LT(unwrapper.Unwrap(Wrapped(wrapped - 5)), si);
    SM_EXPECT_LT(unwrapper.Unwrap(Wrapped(wrapped - 10)), si);
    SM_EXPECT_LT(unwrapper.Unwrap(Wrapped(wrapped - 100)), si);
  }
}

SM_TEST(DcsctpSequenceNumbersTest, DifferenceIsAbsolute) {
  TestSequence::Unwrapper unwrapper;

  TestSequence this_value = unwrapper.Unwrap(Wrapped(10));
  TestSequence other_value = TestSequence::AddTo(this_value, 100);

  SM_EXPECT_EQ(TestSequence::Difference(this_value, other_value), 100);
  SM_EXPECT_EQ(TestSequence::Difference(other_value, this_value), 100);

  TestSequence minus_value = TestSequence::AddTo(this_value, -100);

  SM_EXPECT_EQ(TestSequence::Difference(this_value, minus_value), 100);
  SM_EXPECT_EQ(TestSequence::Difference(minus_value, this_value), 100);
}

}  // namespace
}  // namespace sm::dcsctp
