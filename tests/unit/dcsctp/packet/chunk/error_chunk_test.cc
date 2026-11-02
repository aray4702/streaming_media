/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/error_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/error_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/error_chunk.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/packet/error_cause/unrecognized_chunk_type_cause.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpErrorChunkTest, FromCapture) {
  /*
   ERROR chunk
      Chunk type: ERROR (9)
      Chunk flags: 0x00
      Chunk length: 12
      Unrecognized chunk type cause (Type: 73 (unknown))
  */

  uint8_t data[] = {0x09, 0x00, 0x00, 0x0c, 0x00, 0x06,
                    0x00, 0x08, 0x49, 0x00, 0x00, 0x04};

  ASSERT_HAS_VALUE_AND_ASSIGN(ErrorChunk chunk, ErrorChunk::Parse(data));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      UnrecognizedChunkTypeCause cause,
      chunk.error_causes().get<UnrecognizedChunkTypeCause>());

  SM_EXPECT_THAT(cause.unrecognized_chunk(), ElementsAre(0x49, 0x00, 0x00, 0x04));
}

SM_TEST(DcsctpErrorChunkTest, SerializeAndDeserialize) {
  ErrorChunk chunk(Parameters::Builder()
                       .Add(UnrecognizedChunkTypeCause({1, 2, 3, 4}))
                       .Build());

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(ErrorChunk deserialized,
                              ErrorChunk::Parse(serialized));
  ASSERT_HAS_VALUE_AND_ASSIGN(
      UnrecognizedChunkTypeCause cause,
      deserialized.error_causes().get<UnrecognizedChunkTypeCause>());

  SM_EXPECT_THAT(cause.unrecognized_chunk(), ElementsAre(1, 2, 3, 4));
}

}  // namespace
}  // namespace sm::dcsctp
