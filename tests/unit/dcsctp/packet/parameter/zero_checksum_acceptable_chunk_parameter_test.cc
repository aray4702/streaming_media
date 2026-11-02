/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter_test.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpZeroChecksumAcceptableChunkParameterTest, SerializeAndDeserialize) {
  ZeroChecksumAcceptableChunkParameter parameter(
      ZeroChecksumAlternateErrorDetectionMethod::LowerLayerDtls());

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  SM_EXPECT_THAT(serialized,
              ElementsAre(0x80, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      ZeroChecksumAcceptableChunkParameter deserialized,
      ZeroChecksumAcceptableChunkParameter::Parse(serialized));
}

SM_TEST(DcsctpZeroChecksumAcceptableChunkParameterTest, FailToDeserializePrevVersion) {
  // This is how the draft described the chunk as, in version 00.
  std::vector<uint8_t> invalid = {0x80, 0x01, 0x00, 0x04};

  SM_EXPECT_FALSE(
      ZeroChecksumAcceptableChunkParameter::Parse(invalid).has_value());
}

SM_TEST(DcsctpZeroChecksumAcceptableChunkParameterTest, FailToDeserialize) {
  std::vector<uint8_t> invalid = {0x00, 0x00, 0x00, 0x00};

  SM_EXPECT_FALSE(
      ZeroChecksumAcceptableChunkParameter::Parse(invalid).has_value());
}

SM_TEST(DcsctpZeroChecksumAcceptableChunkParameterTest, HasToString) {
  ZeroChecksumAcceptableChunkParameter parameter(
      ZeroChecksumAlternateErrorDetectionMethod::LowerLayerDtls());

  SM_EXPECT_EQ(parameter.ToString(), "Zero Checksum Acceptable (1)");
}

}  // namespace
}  // namespace sm::dcsctp
