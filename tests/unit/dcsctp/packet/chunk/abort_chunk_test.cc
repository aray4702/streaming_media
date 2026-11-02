/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/abort_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/abort_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/abort_chunk.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/packet/error_cause/user_initiated_abort_cause.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpAbortChunkTest, FromCapture) {
  /*
  ABORT chunk
      Chunk type: ABORT (6)
      Chunk flags: 0x00
      Chunk length: 8
      User initiated ABORT cause
  */

  uint8_t data[] = {0x06, 0x00, 0x00, 0x08, 0x00, 0x0c, 0x00, 0x04};

  ASSERT_HAS_VALUE_AND_ASSIGN(AbortChunk chunk, AbortChunk::Parse(data));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      UserInitiatedAbortCause cause,
      chunk.error_causes().get<UserInitiatedAbortCause>());

  SM_EXPECT_EQ(cause.upper_layer_abort_reason(), "");
}

SM_TEST(DcsctpAbortChunkTest, SerializeAndDeserialize) {
  AbortChunk chunk(/*filled_in_verification_tag=*/true,
                   Parameters::Builder()
                       .Add(UserInitiatedAbortCause("Close called"))
                       .Build());

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(AbortChunk deserialized,
                              AbortChunk::Parse(serialized));
  ASSERT_HAS_VALUE_AND_ASSIGN(
      UserInitiatedAbortCause cause,
      deserialized.error_causes().get<UserInitiatedAbortCause>());

  SM_EXPECT_EQ(cause.upper_layer_abort_reason(), "Close called");
}

// Validates that AbortChunk doesn't make any alignment assumptions.
SM_TEST(DcsctpAbortChunkTest, SerializeAndDeserializeOneChar) {
  AbortChunk chunk(
      /*filled_in_verification_tag=*/true,
      Parameters::Builder().Add(UserInitiatedAbortCause("!")).Build());

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(AbortChunk deserialized,
                              AbortChunk::Parse(serialized));
  ASSERT_HAS_VALUE_AND_ASSIGN(
      UserInitiatedAbortCause cause,
      deserialized.error_causes().get<UserInitiatedAbortCause>());

  SM_EXPECT_EQ(cause.upper_layer_abort_reason(), "!");
}

}  // namespace
}  // namespace sm::dcsctp
