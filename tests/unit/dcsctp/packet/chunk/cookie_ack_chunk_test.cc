/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/cookie_ack_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/cookie_ack_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/cookie_ack_chunk.h"

#include <cstdint>
#include <vector>

#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpCookieAckChunkTest, FromCapture) {
  /*
  COOKIE_ACK chunk
      Chunk type: COOKIE_ACK (11)
      Chunk flags: 0x00
      Chunk length: 4
  */

  uint8_t data[] = {0x0b, 0x00, 0x00, 0x04};

  SM_EXPECT_TRUE(CookieAckChunk::Parse(data).has_value());
}

SM_TEST(DcsctpCookieAckChunkTest, SerializeAndDeserialize) {
  CookieAckChunk chunk;

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(CookieAckChunk deserialized,
                              CookieAckChunk::Parse(serialized));
  SM_EXPECT_EQ(deserialized.ToString(), "COOKIE-ACK");
}

}  // namespace
}  // namespace sm::dcsctp
