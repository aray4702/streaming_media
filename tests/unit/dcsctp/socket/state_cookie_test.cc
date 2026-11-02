/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/state_cookie_test.cc (compare/webrtc/reference/net/dcsctp/socket/state_cookie_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/socket/state_cookie.h"

#include <cstdint>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/socket/capabilities.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::SizeIs;

SM_TEST(DcsctpStateCookieTest, SerializeAndDeserialize) {
  Capabilities capabilities = {
      .partial_reliability = true,
      .message_interleaving = false,
      .reconfig = true,
      .zero_checksum_method = ZeroChecksumAlternateErrorDetectionMethod(1),
      .negotiated_maximum_incoming_streams = 123,
      .negotiated_maximum_outgoing_streams = 234};
  StateCookie cookie(/*peer_tag=*/VerificationTag(123),
                     /*my_tag=*/VerificationTag(321),
                     /*peer_initial_tsn=*/TSN(456), /*my_initial_tsn=*/TSN(654),
                     /*a_rwnd=*/789, TieTag(101112), capabilities);
  std::vector<uint8_t> serialized = cookie.Serialize();
  SM_EXPECT_THAT(serialized, SizeIs(StateCookie::kCookieSize));
  ASSERT_HAS_VALUE_AND_ASSIGN(StateCookie deserialized,
                              StateCookie::Deserialize(serialized));
  SM_EXPECT_EQ(deserialized.peer_tag(), VerificationTag(123));
  SM_EXPECT_EQ(deserialized.my_tag(), VerificationTag(321));
  SM_EXPECT_EQ(deserialized.peer_initial_tsn(), TSN(456));
  SM_EXPECT_EQ(deserialized.my_initial_tsn(), TSN(654));
  SM_EXPECT_EQ(deserialized.a_rwnd(), 789u);
  SM_EXPECT_EQ(deserialized.tie_tag(), TieTag(101112));
  SM_EXPECT_TRUE(deserialized.peer_capabilities().partial_reliability);
  SM_EXPECT_FALSE(deserialized.peer_capabilities().message_interleaving);
  SM_EXPECT_TRUE(deserialized.peer_capabilities().reconfig);
  SM_EXPECT_EQ(deserialized.peer_capabilities().zero_checksum_method,
            ZeroChecksumAlternateErrorDetectionMethod(1));
  SM_EXPECT_EQ(
      deserialized.peer_capabilities().negotiated_maximum_incoming_streams,
      123);
  SM_EXPECT_EQ(
      deserialized.peer_capabilities().negotiated_maximum_outgoing_streams,
      234);
}

SM_TEST(DcsctpStateCookieTest, ValidateMagicValue) {
  Capabilities capabilities = {.partial_reliability = true,
                               .message_interleaving = false,
                               .reconfig = true};
  StateCookie cookie(/*peer_tag=*/VerificationTag(123),
                     /*my_tag=*/VerificationTag(321),
                     /*peer_initial_tsn=*/TSN(456), /*my_initial_tsn=*/TSN(654),
                     /*a_rwnd=*/789, TieTag(101112), capabilities);
  std::vector<uint8_t> serialized = cookie.Serialize();
  SM_ASSERT_THAT(serialized, SizeIs(StateCookie::kCookieSize));

  std::string_view magic(reinterpret_cast<const char*>(serialized.data()), 8);
  SM_EXPECT_EQ(magic, "dcSCTP00");
}

}  // namespace
}  // namespace sm::dcsctp
