/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/incoming_ssn_reset_request_parameter_test.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/incoming_ssn_reset_request_parameter_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/parameter/incoming_ssn_reset_request_parameter.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpIncomingSSNResetRequestParameterTest, SerializeAndDeserialize) {
  IncomingSSNResetRequestParameter parameter(
      ReconfigRequestSN(1), {StreamID(2), StreamID(3), StreamID(4)});

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      IncomingSSNResetRequestParameter deserialized,
      IncomingSSNResetRequestParameter::Parse(serialized));

  SM_EXPECT_EQ(*deserialized.request_sequence_number(), 1u);
  SM_EXPECT_THAT(deserialized.stream_ids(),
              ElementsAre(StreamID(2), StreamID(3), StreamID(4)));
}

}  // namespace
}  // namespace sm::dcsctp
