/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/forward_tsn_chunk_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/forward_tsn_chunk_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk/forward_tsn_chunk.h"

#include <cstdint>
#include <span>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/chunk/forward_tsn_common.h"
#include "sm/dcsctp/public/types.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpForwardTsnChunkTest, FromCapture) {
  /*
  FORWARD_TSN chunk(Cumulative TSN: 1905748778)
      Chunk type: FORWARD_TSN (192)
      Chunk flags: 0x00
      Chunk length: 8
      New cumulative TSN: 1905748778
  */

  uint8_t data[] = {0xc0, 0x00, 0x00, 0x08, 0x71, 0x97, 0x6b, 0x2a};

  ASSERT_HAS_VALUE_AND_ASSIGN(ForwardTsnChunk chunk,
                              ForwardTsnChunk::Parse(data));
  SM_EXPECT_EQ(*chunk.new_cumulative_tsn(), 1905748778u);
}

SM_TEST(DcsctpForwardTsnChunkTest, SerializeAndDeserialize) {
  ForwardTsnChunk chunk(
      TSN(123), {ForwardTsnChunk::SkippedStream(StreamID(1), SSN(23)),
                 ForwardTsnChunk::SkippedStream(StreamID(42), SSN(99))});

  std::vector<uint8_t> serialized;
  chunk.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(ForwardTsnChunk deserialized,
                              ForwardTsnChunk::Parse(serialized));
  SM_EXPECT_EQ(*deserialized.new_cumulative_tsn(), 123u);
  SM_EXPECT_THAT(
      deserialized.skipped_streams(),
      ElementsAre(ForwardTsnChunk::SkippedStream(StreamID(1), SSN(23)),
                  ForwardTsnChunk::SkippedStream(StreamID(42), SSN(99))));

  SM_EXPECT_EQ(deserialized.ToString(),
            "FORWARD-TSN, new_cumulative_tsn=123, skip 1:23, skip 42:99");
}

}  // namespace
}  // namespace sm::dcsctp
