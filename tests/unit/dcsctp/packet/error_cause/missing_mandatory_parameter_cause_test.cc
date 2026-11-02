/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/missing_mandatory_parameter_cause_test.cc (compare/webrtc/reference/net/dcsctp/packet/error_cause/missing_mandatory_parameter_cause_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/error_cause/missing_mandatory_parameter_cause.h"

#include <cstdint>
#include <vector>

#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;

SM_TEST(DcsctpMissingMandatoryParameterCauseTest, SerializeAndDeserialize) {
  uint16_t parameter_types[] = {1, 2, 3};
  MissingMandatoryParameterCause parameter(parameter_types);

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      MissingMandatoryParameterCause deserialized,
      MissingMandatoryParameterCause::Parse(serialized));

  SM_EXPECT_THAT(deserialized.missing_parameter_types(), ElementsAre(1, 2, 3));
}

SM_TEST(DcsctpMissingMandatoryParameterCauseTest, HandlesDeserializeZeroParameters) {
  uint8_t serialized[] = {0, 2, 0, 8, 0, 0, 0, 0};

  ASSERT_HAS_VALUE_AND_ASSIGN(
      MissingMandatoryParameterCause deserialized,
      MissingMandatoryParameterCause::Parse(serialized));

  SM_EXPECT_THAT(deserialized.missing_parameter_types(), IsEmpty());
}

SM_TEST(DcsctpMissingMandatoryParameterCauseTest, HandlesOverflowParameterCount) {
  // 0x80000004 * 2 = 2**32 + 8 -> if overflow, would validate correctly.
  uint8_t serialized[] = {0, 2, 0, 8, 0x80, 0x00, 0x00, 0x04};

  SM_EXPECT_FALSE(MissingMandatoryParameterCause::Parse(serialized).has_value());
}

}  // namespace
}  // namespace sm::dcsctp
