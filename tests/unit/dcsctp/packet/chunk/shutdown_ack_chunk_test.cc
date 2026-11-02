/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/shutdown_ack_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/shutdown_ack_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/shutdown_ack_chunk.h"

#include <cstdint>
#include <vector>

#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpShutdownAckChunkTest, FromCapture) {
  /*
  SHUTDOWN_ACK chunk
      Chunk type: SHUTDOWN_ACK (8)
      Chunk flags: 0x00
      Chunk length: 4
  */

  uint8_t data[] = {0x08, 0x00, 0x00, 0x04};

  SM_EXPECT_TRUE(ShutdownAckChunk::Parse(data).has_value());
}

SM_TEST(DcsctpShutdownAckChunkTest, SerializeAndDeserialize) {
  ShutdownAckChunk chunk;

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  SM_EXPECT_TRUE(ShutdownAckChunk::Parse(serialized).has_value());
}

}  // namespace
}  // namespace sm::dcsctp
