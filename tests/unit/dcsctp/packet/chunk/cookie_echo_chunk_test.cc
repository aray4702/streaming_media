/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/cookie_echo_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/cookie_echo_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/cookie_echo_chunk.h"

#include <cstdint>
#include <vector>

#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpCookieEchoChunkTest, FromCapture) {
  /*
  COOKIE_ECHO chunk (Cookie length: 256 bytes)
      Chunk type: COOKIE_ECHO (10)
      Chunk flags: 0x00
      Chunk length: 260
      Cookie: 12345678
  */

  uint8_t data[] = {0x0a, 0x00, 0x00, 0x08, 0x12, 0x34, 0x56, 0x78};

  ASSERT_HAS_VALUE_AND_ASSIGN(CookieEchoChunk chunk,
                              CookieEchoChunk::Parse(data));

  SM_EXPECT_THAT(chunk.cookie(), ElementsAre(0x12, 0x34, 0x56, 0x78));
}

SM_TEST(DcsctpCookieEchoChunkTest, SerializeAndDeserialize) {
  uint8_t cookie[] = {1, 2, 3, 4};
  CookieEchoChunk chunk(cookie);

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(CookieEchoChunk deserialized,
                              CookieEchoChunk::Parse(serialized));

  SM_EXPECT_THAT(deserialized.cookie(), ElementsAre(1, 2, 3, 4));
  SM_EXPECT_EQ(deserialized.ToString(), "COOKIE-ECHO");
}

}  // namespace
}  // namespace sm::dcsctp
