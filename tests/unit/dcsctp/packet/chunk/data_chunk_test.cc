/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/data_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/data_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/data_chunk.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpDataChunkTest, FromCapture) {
  /*
  DATA chunk(ordered, complete segment, TSN: 1426601532, SID: 2, SSN: 1,
    PPID: 53, payload length: 4 bytes)
      Chunk type: DATA (0)
      Chunk flags: 0x03
      Chunk length: 20
      Transmission sequence number: 1426601532
      Stream identifier: 0x0002
      Stream sequence number: 1
      Payload protocol identifier: WebRTC Binary (53)
  */

  uint8_t data[] = {0x00, 0x03, 0x00, 0x14, 0x55, 0x08, 0x36, 0x3c, 0x00, 0x02,
                    0x00, 0x01, 0x00, 0x00, 0x00, 0x35, 0x00, 0x01, 0x02, 0x03};

  ASSERT_HAS_VALUE_AND_ASSIGN(DataChunk chunk, DataChunk::Parse(data));
  SM_EXPECT_EQ(*chunk.tsn(), 1426601532u);
  SM_EXPECT_EQ(*chunk.stream_id(), 2u);
  SM_EXPECT_EQ(*chunk.ssn(), 1u);
  SM_EXPECT_EQ(*chunk.ppid(), 53u);
  SM_EXPECT_TRUE(*chunk.options().is_beginning);
  SM_EXPECT_TRUE(*chunk.options().is_end);
  SM_EXPECT_FALSE(*chunk.options().is_unordered);
  SM_EXPECT_FALSE(*chunk.options().immediate_ack);
  SM_EXPECT_THAT(chunk.payload(), ElementsAre(0x0, 0x1, 0x2, 0x3));
}

SM_TEST(DcsctpDataChunkTest, SerializeAndDeserialize) {
  DataChunk chunk(TSN(123), StreamID(456), SSN(789), PPID(9090),
                  /*payload=*/{1, 2, 3, 4, 5},
                  /*options=*/{});

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(DataChunk deserialized,
                              DataChunk::Parse(serialized));
  SM_EXPECT_EQ(*chunk.tsn(), 123u);
  SM_EXPECT_EQ(*chunk.stream_id(), 456u);
  SM_EXPECT_EQ(*chunk.ssn(), 789u);
  SM_EXPECT_EQ(*chunk.ppid(), 9090u);
  SM_EXPECT_THAT(chunk.payload(), ElementsAre(1, 2, 3, 4, 5));

  SM_EXPECT_EQ(deserialized.ToString(),
            "DATA, type=ordered::middle, tsn=123, sid=456, ssn=789, ppid=9090, "
            "length=5");
}
}  // namespace
}  // namespace sm::dcsctp
