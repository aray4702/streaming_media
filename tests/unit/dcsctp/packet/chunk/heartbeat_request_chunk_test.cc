/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/heartbeat_request_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/heartbeat_request_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/heartbeat_request_chunk.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "sm/dcsctp/packet/parameter/heartbeat_info_parameter.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpHeartbeatRequestChunkTest, FromCapture) {
  /*
  HEARTBEAT chunk (Information: 40 bytes)
      Chunk type: HEARTBEAT (4)
      Chunk flags: 0x00
      Chunk length: 44
      Heartbeat info parameter (Information: 36 bytes)
          Parameter type: Heartbeat info (0x0001)
          Parameter length: 40
          Heartbeat information: ad2436603726070000000000000000007b10000001…
  */

  uint8_t data[] = {0x04, 0x00, 0x00, 0x2c, 0x00, 0x01, 0x00, 0x28, 0xad,
                    0x24, 0x36, 0x60, 0x37, 0x26, 0x07, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7b, 0x10, 0x00,
                    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  ASSERT_HAS_VALUE_AND_ASSIGN(HeartbeatRequestChunk chunk,
                              HeartbeatRequestChunk::Parse(data));

  ASSERT_HAS_VALUE_AND_ASSIGN(HeartbeatInfoParameter info, chunk.info());

  SM_EXPECT_THAT(
      info.info(),
      ElementsAre(0xad, 0x24, 0x36, 0x60, 0x37, 0x26, 0x07, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7b, 0x10, 0x00, 0x00,
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00));
}

SM_TEST(DcsctpHeartbeatRequestChunkTest, SerializeAndDeserialize) {
  uint8_t info_data[] = {1, 2, 3, 4};
  Parameters parameters =
      Parameters::Builder().Add(HeartbeatInfoParameter(info_data)).Build();
  HeartbeatRequestChunk chunk(std::move(parameters));

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(HeartbeatRequestChunk deserialized,
                              HeartbeatRequestChunk::Parse(serialized));

  ASSERT_HAS_VALUE_AND_ASSIGN(HeartbeatInfoParameter info, deserialized.info());

  SM_EXPECT_THAT(info.info(), ElementsAre(1, 2, 3, 4));

  SM_EXPECT_EQ(deserialized.ToString(), "HEARTBEAT");
}

}  // namespace
}  // namespace sm::dcsctp
