/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/protocol_violation_cause_test.cc (compare/webrtc/reference/net/dcsctp/packet/error_cause/protocol_violation_cause_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/error_cause/protocol_violation_cause.h"

#include <vector>

#include "sm/dcsctp/packet/parameter/parameter.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::SizeIs;

SM_TEST(DcsctpProtocolViolationCauseTest, EmptyReason) {
  Parameters causes =
      Parameters::Builder().Add(ProtocolViolationCause("")).Build();

  ASSERT_HAS_VALUE_AND_ASSIGN(Parameters deserialized,
                              Parameters::Parse(causes.data()));
  SM_ASSERT_THAT(deserialized.descriptors(), SizeIs(1));
  SM_EXPECT_EQ(deserialized.descriptors()[0].type, ProtocolViolationCause::kType);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      ProtocolViolationCause cause,
      ProtocolViolationCause::Parse(deserialized.descriptors()[0].data));

  SM_EXPECT_EQ(cause.additional_information(), "");
}

SM_TEST(DcsctpProtocolViolationCauseTest, SetReason) {
  Parameters causes = Parameters::Builder()
                          .Add(ProtocolViolationCause("Reason goes here"))
                          .Build();

  ASSERT_HAS_VALUE_AND_ASSIGN(Parameters deserialized,
                              Parameters::Parse(causes.data()));
  SM_ASSERT_THAT(deserialized.descriptors(), SizeIs(1));
  SM_EXPECT_EQ(deserialized.descriptors()[0].type, ProtocolViolationCause::kType);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      ProtocolViolationCause cause,
      ProtocolViolationCause::Parse(deserialized.descriptors()[0].data));

  SM_EXPECT_EQ(cause.additional_information(), "Reason goes here");
}
}  // namespace
}  // namespace sm::dcsctp
