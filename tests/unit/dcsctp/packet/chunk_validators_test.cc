/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk_validators_test.cc (compare/webrtc/reference/net/dcsctp/packet/chunk_validators_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/chunk_validators.h"

#include <utility>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/chunk/sack_chunk.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;

SM_TEST(DcsctpChunkValidatorsTest, NoGapAckBlocksAreValid) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 /*gap_ack_blocks=*/{}, {});

  SM_EXPECT_TRUE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));
  SM_EXPECT_THAT(clean.gap_ack_blocks(), IsEmpty());
}

SM_TEST(DcsctpChunkValidatorsTest, OneValidAckBlock) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456, {SackChunk::GapAckBlock(2, 3)}, {});

  SM_EXPECT_TRUE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));
  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(2, 3)));
}

SM_TEST(DcsctpChunkValidatorsTest, TwoValidAckBlocks) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(2, 3), SackChunk::GapAckBlock(5, 6)},
                 {});

  SM_EXPECT_TRUE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));
  SM_EXPECT_THAT(
      clean.gap_ack_blocks(),
      ElementsAre(SackChunk::GapAckBlock(2, 3), SackChunk::GapAckBlock(5, 6)));
}

SM_TEST(DcsctpChunkValidatorsTest, OneInvalidAckBlock) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456, {SackChunk::GapAckBlock(1, 2)}, {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  // It's not strictly valid, but due to the renegable nature of gap ack blocks,
  // the cum_ack_tsn can't simply be moved.
  SackChunk clean = ChunkValidators::Clean(std::move(sack));
  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(1, 2)));
}

SM_TEST(DcsctpChunkValidatorsTest, RemovesInvalidGapAckBlockFromSack) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(2, 3), SackChunk::GapAckBlock(6, 4)},
                 {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(2, 3)));
}

SM_TEST(DcsctpChunkValidatorsTest, SortsGapAckBlocksInOrder) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(6, 7), SackChunk::GapAckBlock(3, 4)},
                 {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_THAT(
      clean.gap_ack_blocks(),
      ElementsAre(SackChunk::GapAckBlock(3, 4), SackChunk::GapAckBlock(6, 7)));
}

SM_TEST(DcsctpChunkValidatorsTest, MergesAdjacentBlocks) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(3, 4), SackChunk::GapAckBlock(5, 6)},
                 {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(3, 6)));
}

SM_TEST(DcsctpChunkValidatorsTest, MergesOverlappingByOne) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(3, 4), SackChunk::GapAckBlock(4, 5)},
                 {});

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(3, 5)));
}

SM_TEST(DcsctpChunkValidatorsTest, MergesOverlappingByMore) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(3, 10), SackChunk::GapAckBlock(4, 5)},
                 {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(3, 10)));
}

SM_TEST(DcsctpChunkValidatorsTest, MergesBlocksStartingWithSameStartOffset) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(3, 7), SackChunk::GapAckBlock(3, 5),
                  SackChunk::GapAckBlock(3, 9)},
                 {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(3, 9)));
}

SM_TEST(DcsctpChunkValidatorsTest, MergesBlocksPartiallyOverlapping) {
  SackChunk sack(TSN(123), /*a_rwnd=*/456,
                 {SackChunk::GapAckBlock(3, 7), SackChunk::GapAckBlock(5, 9)},
                 {});

  SM_EXPECT_FALSE(ChunkValidators::Validate(sack));

  SackChunk clean = ChunkValidators::Clean(std::move(sack));

  SM_EXPECT_THAT(clean.gap_ack_blocks(),
              ElementsAre(SackChunk::GapAckBlock(3, 9)));
}

}  // namespace
}  // namespace sm::dcsctp
