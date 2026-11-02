/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/parameter_test.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/parameter_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/parameter/parameter.h"

#include <cstdint>
#include <span>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/parameter/outgoing_ssn_reset_request_parameter.h"
#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::SizeIs;

SM_TEST(DcsctpParameterTest, SerializeDeserializeParameter) {
  Parameters parameters =
      Parameters::Builder()
          .Add(OutgoingSSNResetRequestParameter(ReconfigRequestSN(123),
                                                ReconfigRequestSN(456),
                                                TSN(789), {StreamID(42)}))
          .Build();

  std::span<const uint8_t> serialized = parameters.data();

  ASSERT_HAS_VALUE_AND_ASSIGN(Parameters parsed, Parameters::Parse(serialized));
  auto descriptors = parsed.descriptors();
  SM_ASSERT_THAT(descriptors, SizeIs(1));
  SM_EXPECT_THAT(descriptors[0].type, OutgoingSSNResetRequestParameter::kType);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      OutgoingSSNResetRequestParameter parsed_param,
      OutgoingSSNResetRequestParameter::Parse(descriptors[0].data));
  SM_EXPECT_EQ(*parsed_param.request_sequence_number(), 123u);
  SM_EXPECT_EQ(*parsed_param.response_sequence_number(), 456u);
  SM_EXPECT_EQ(*parsed_param.sender_last_assigned_tsn(), 789u);
  SM_EXPECT_THAT(parsed_param.stream_ids(), ElementsAre(StreamID(42)));
}

}  // namespace
}  // namespace sm::dcsctp
