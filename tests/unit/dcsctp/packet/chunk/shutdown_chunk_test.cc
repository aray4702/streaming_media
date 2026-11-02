/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/shutdown_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/shutdown_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/shutdown_chunk.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
SM_TEST(DcsctpShutdownChunkTest, FromCapture) {
  /*
  SHUTDOWN chunk (Cumulative TSN ack: 101831101)
      Chunk type: SHUTDOWN (7)
      Chunk flags: 0x00
      Chunk length: 8
      Cumulative TSN Ack: 101831101
  */

  uint8_t data[] = {0x07, 0x00, 0x00, 0x08, 0x06, 0x11, 0xd1, 0xbd};

  ASSERT_HAS_VALUE_AND_ASSIGN(ShutdownChunk chunk, ShutdownChunk::Parse(data));
  SM_EXPECT_EQ(chunk.cumulative_tsn_ack(), TSN(101831101u));
}

SM_TEST(DcsctpShutdownChunkTest, SerializeAndDeserialize) {
  ShutdownChunk chunk(TSN(12345678));

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(ShutdownChunk deserialized,
                              ShutdownChunk::Parse(serialized));

  SM_EXPECT_EQ(deserialized.cumulative_tsn_ack(), TSN(12345678u));
}

}  // namespace
}  // namespace sm::dcsctp
