/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/reconfiguration_response_parameter_test.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/reconfiguration_response_parameter_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/parameter/reconfiguration_response_parameter.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpReconfigurationResponseParameterTest, SerializeAndDeserializeFirstForm) {
  ReconfigurationResponseParameter parameter(
      ReconfigRequestSN(1),
      ReconfigurationResponseParameter::Result::kSuccessPerformed);

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      ReconfigurationResponseParameter deserialized,
      ReconfigurationResponseParameter::Parse(serialized));

  SM_EXPECT_EQ(*deserialized.response_sequence_number(), 1u);
  SM_EXPECT_EQ(deserialized.result(),
            ReconfigurationResponseParameter::Result::kSuccessPerformed);
  SM_EXPECT_EQ(deserialized.sender_next_tsn(), std::nullopt);
  SM_EXPECT_EQ(deserialized.receiver_next_tsn(), std::nullopt);
}

SM_TEST(DcsctpReconfigurationResponseParameterTest,
     SerializeAndDeserializeFirstFormSecondForm) {
  ReconfigurationResponseParameter parameter(
      ReconfigRequestSN(1),
      ReconfigurationResponseParameter::Result::kSuccessPerformed, TSN(2),
      TSN(3));

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      ReconfigurationResponseParameter deserialized,
      ReconfigurationResponseParameter::Parse(serialized));

  SM_EXPECT_EQ(*deserialized.response_sequence_number(), 1u);
  SM_EXPECT_EQ(deserialized.result(),
            ReconfigurationResponseParameter::Result::kSuccessPerformed);
  SM_EXPECT_TRUE(deserialized.sender_next_tsn().has_value());
  SM_EXPECT_EQ(**deserialized.sender_next_tsn(), 2u);
  SM_EXPECT_TRUE(deserialized.receiver_next_tsn().has_value());
  SM_EXPECT_EQ(**deserialized.receiver_next_tsn(), 3u);
}

}  // namespace
}  // namespace sm::dcsctp
