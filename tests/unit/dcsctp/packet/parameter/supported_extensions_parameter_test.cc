/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/supported_extensions_parameter_test.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/supported_extensions_parameter_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/parameter/supported_extensions_parameter.h"

#include <cstdint>
#include <vector>

#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpSupportedExtensionsParameterTest, SerializeAndDeserialize) {
  SupportedExtensionsParameter parameter({1, 2, 3});

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(SupportedExtensionsParameter deserialized,
                              SupportedExtensionsParameter::Parse(serialized));

  SM_EXPECT_THAT(deserialized.chunk_types(), ElementsAre(1, 2, 3));
  SM_EXPECT_TRUE(deserialized.supports(1));
  SM_EXPECT_TRUE(deserialized.supports(2));
  SM_EXPECT_TRUE(deserialized.supports(3));
  SM_EXPECT_FALSE(deserialized.supports(4));
}

}  // namespace
}  // namespace sm::dcsctp
