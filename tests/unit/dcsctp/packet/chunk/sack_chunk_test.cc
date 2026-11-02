/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/sack_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/sack_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/sack_chunk.h"

#include <cstdint>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpSackChunkTest, FromCapture) {
  /*
  SACK chunk (Cumulative TSN: 916312075, a_rwnd: 126323,
    gaps: 2, duplicate TSNs: 1)
      Chunk type: SACK (3)
      Chunk flags: 0x00
      Chunk length: 28
      Cumulative TSN ACK: 916312075
      Advertised receiver window credit (a_rwnd): 126323
      Number of gap acknowledgement blocks: 2
      Number of duplicated TSNs: 1
      Gap Acknowledgement for TSN 916312077 to 916312081
      Gap Acknowledgement for TSN 916312083 to 916312083
      [Number of TSNs in gap acknowledgement blocks: 6]
      Duplicate TSN: 916312081

  */

  uint8_t data[] = {0x03, 0x00, 0x00, 0x1c, 0x36, 0x9d, 0xd0, 0x0b, 0x00, 0x01,
                    0xed, 0x73, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00, 0x06,
                    0x00, 0x08, 0x00, 0x08, 0x36, 0x9d, 0xd0, 0x11};

  ASSERT_HAS_VALUE_AND_ASSIGN(SackChunk chunk, SackChunk::Parse(data));

  TSN cum_ack_tsn(916312075);
  SM_EXPECT_EQ(chunk.cumulative_tsn_ack(), cum_ack_tsn);
  SM_EXPECT_EQ(chunk.a_rwnd(), 126323u);
  SM_EXPECT_THAT(
      chunk.gap_ack_blocks(),
      ElementsAre(SackChunk::GapAckBlock(
                      static_cast<uint16_t>(916312077 - *cum_ack_tsn),
                      static_cast<uint16_t>(916312081 - *cum_ack_tsn)),
                  SackChunk::GapAckBlock(
                      static_cast<uint16_t>(916312083 - *cum_ack_tsn),
                      static_cast<uint16_t>(916312083 - *cum_ack_tsn))));
  SM_EXPECT_THAT(chunk.duplicate_tsns(), ElementsAre(TSN(916312081)));
}

SM_TEST(DcsctpSackChunkTest, SerializeAndDeserialize) {
  SackChunk chunk(TSN(123), /*a_rwnd=*/456, {SackChunk::GapAckBlock(2, 3)},
                  {TSN(1), TSN(2), TSN(3)});
  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(SackChunk deserialized,
                              SackChunk::Parse(serialized));

  SM_EXPECT_EQ(*deserialized.cumulative_tsn_ack(), 123u);
  SM_EXPECT_EQ(deserialized.a_rwnd(), 456u);
  SM_EXPECT_THAT(deserialized.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(2, 3)));
  SM_EXPECT_THAT(deserialized.duplicate_tsns(),
              ElementsAre(TSN(1), TSN(2), TSN(3)));

  SM_EXPECT_EQ(deserialized.ToString(),
            "SACK, cum_ack_tsn=123, a_rwnd=456, gap=125--126, dup_tsns=1,2,3");
}

}  // namespace
}  // namespace sm::dcsctp
