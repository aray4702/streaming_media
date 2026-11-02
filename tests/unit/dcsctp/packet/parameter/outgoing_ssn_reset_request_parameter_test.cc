/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter_test.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpOutgoingSSNResetRequestParameterTest, SerializeAndDeserialize) {
  OutgoingSSNResetRequestParameter parameter(
      ReconfigRequestSN(1), ReconfigRequestSN(2), TSN(3),
      {StreamID(4), StreamID(5), StreamID(6)});

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter deserialized,
      OutgoingSSNResetRequestParameter::Parse(serialized));

  SM_EXPECT_EQ(*deserialized.request_sequence_number(), 1u);
  SM_EXPECT_EQ(*deserialized.response_sequence_number(), 2u);
  SM_EXPECT_EQ(*deserialized.sender_last_assigned_tsn(), 3u);
  SM_EXPECT_THAT(deserialized.stream_ids(),
              ElementsAre(StreamID(4), StreamID(5), StreamID(6)));
}

}  // namespace
}  // namespace sm::dcsctp
