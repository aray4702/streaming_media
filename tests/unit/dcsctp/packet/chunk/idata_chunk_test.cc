/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/idata_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/idata_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/idata_chunk.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/data.h"
#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpIDataChunkTest, AtBeginningFromCapture) {
  /*
  I_DATA chunk(ordered, first segment, TSN: 2487901653, SID: 1, MID: 0,
    payload length: 1180 bytes)
      Chunk type: I_DATA (64)
      Chunk flags: 0x02
      Chunk length: 1200
      Transmission sequence number: 2487901653
      Stream identifier: 0x0001
      Reserved: 0
      Message identifier: 0
      Payload protocol identifier: WebRTC Binary (53)
      Reassembled Message in frame: 39
  */

  uint8_t data[] = {0x40, 0x02, 0x00, 0x15, 0x94, 0x4a, 0x5d, 0xd5,
                    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x35, 0x01, 0x00, 0x00, 0x00};

  ASSERT_HAS_VALUE_AND_ASSIGN(IDataChunk chunk, IDataChunk::Parse(data));
  SM_EXPECT_EQ(*chunk.tsn(), 2487901653);
  SM_EXPECT_EQ(*chunk.stream_id(), 1);
  SM_EXPECT_EQ(*chunk.mid(), 0u);
  SM_EXPECT_EQ(*chunk.ppid(), 53u);
  SM_EXPECT_EQ(*chunk.fsn(), 0u);  // Not provided (so set to zero)
}

SM_TEST(DcsctpIDataChunkTest, AtBeginningSerializeAndDeserialize) {
  IDataChunk::Options options;
  options.is_beginning = Data::IsBeginning(true);
  IDataChunk chunk(TSN(123), StreamID(456), MID(789), PPID(53), FSN(0), {1},
                   options);

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(IDataChunk deserialized,
                              IDataChunk::Parse(serialized));
  SM_EXPECT_EQ(*deserialized.tsn(), 123u);
  SM_EXPECT_EQ(*deserialized.stream_id(), 456u);
  SM_EXPECT_EQ(*deserialized.mid(), 789u);
  SM_EXPECT_EQ(*deserialized.ppid(), 53u);
  SM_EXPECT_EQ(*deserialized.fsn(), 0u);

  SM_EXPECT_EQ(deserialized.ToString(),
            "I-DATA, type=ordered::first, tsn=123, stream_id=456, "
            "mid=789, ppid=53, length=1");
}

SM_TEST(DcsctpIDataChunkTest, InMiddleFromCapture) {
  /*
  I_DATA chunk(ordered, last segment, TSN: 2487901706, SID: 3, MID: 1,
    FSN: 8, payload length: 560 bytes)
      Chunk type: I_DATA (64)
      Chunk flags: 0x01
      Chunk length: 580
      Transmission sequence number: 2487901706
      Stream identifier: 0x0003
      Reserved: 0
      Message identifier: 1
      Fragment sequence number: 8
      Reassembled SCTP Fragments (10000 bytes, 9 fragments):
  */

  uint8_t data[] = {0x40, 0x01, 0x00, 0x15, 0x94, 0x4a, 0x5e, 0x0a,
                    0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                    0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00};

  ASSERT_HAS_VALUE_AND_ASSIGN(IDataChunk chunk, IDataChunk::Parse(data));
  SM_EXPECT_EQ(*chunk.tsn(), 2487901706);
  SM_EXPECT_EQ(*chunk.stream_id(), 3u);
  SM_EXPECT_EQ(*chunk.mid(), 1u);
  SM_EXPECT_EQ(*chunk.ppid(), 0u);  // Not provided (so set to zero)
  SM_EXPECT_EQ(*chunk.fsn(), 8u);
}

SM_TEST(DcsctpIDataChunkTest, InMiddleSerializeAndDeserialize) {
  IDataChunk chunk(TSN(123), StreamID(456), MID(789), PPID(0), FSN(101112),
                   {1, 2, 3}, /*options=*/{});

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(IDataChunk deserialized,
                              IDataChunk::Parse(serialized));
  SM_EXPECT_EQ(*deserialized.tsn(), 123u);
  SM_EXPECT_EQ(*deserialized.stream_id(), 456u);
  SM_EXPECT_EQ(*deserialized.mid(), 789u);
  SM_EXPECT_EQ(*deserialized.ppid(), 0u);
  SM_EXPECT_EQ(*deserialized.fsn(), 101112u);
  SM_EXPECT_THAT(deserialized.payload(), ElementsAre(1, 2, 3));

  SM_EXPECT_EQ(deserialized.ToString(),
            "I-DATA, type=ordered::middle, tsn=123, stream_id=456, "
            "mid=789, fsn=101112, length=3");
}

}  // namespace
}  // namespace sm::dcsctp
