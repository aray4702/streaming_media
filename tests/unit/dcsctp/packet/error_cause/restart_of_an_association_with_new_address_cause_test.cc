/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause_test.cc (compare/webrtc/reference/net/dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause.h"

#include <cstdint>
#include <vector>

#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpRestartOfAnAssociationWithNewAddressesCauseTest, SerializeAndDeserialize) {
  uint8_t data[] = {1, 2, 3};
  RestartOfAnAssociationWithNewAddressesCause parameter(data);

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      RestartOfAnAssociationWithNewAddressesCause deserialized,
      RestartOfAnAssociationWithNewAddressesCause::Parse(serialized));

  SM_EXPECT_THAT(deserialized.new_address_tlvs(), ElementsAre(1, 2, 3));
}

}  // namespace
}  // namespace sm::dcsctp
