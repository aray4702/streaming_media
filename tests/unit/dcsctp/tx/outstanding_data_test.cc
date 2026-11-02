/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/outstanding_data_test.cc (compare/webrtc/reference/net/dcsctp/tx/outstanding_data_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/tx/outstanding_data.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/common/math.h"
#include "sm/dcsctp/common/sequence_numbers.h"
#include "sm/dcsctp/packet/chunk/data_chunk.h"
#include "sm/dcsctp/packet/chunk/forward_tsn_chunk.h"
#include "sm/dcsctp/packet/chunk/sack_chunk.h"
#include "sm/dcsctp/public/dcsctp_handover_state.h"
#include "sm/dcsctp/public/types.h"
#include "testing/data_generator.h"
#include "testing_macros.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::MockFunction;
using State = ::sm::dcsctp::OutstandingData::State;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::Property;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAre;
using ::sm::TimeDelta;
using ::sm::Timestamp;

constexpr Timestamp kNow = Timestamp::Millis(42);
constexpr OutgoingMessageId kMessageId = OutgoingMessageId(17);

class OutstandingDataTest : public testing::Test {
 protected:
  OutstandingDataTest()
      : gen_(MID(42)),
        buf_(DataChunk::kHeaderSize,
             unwrapper_.Unwrap(TSN(9)),
             on_discard_.AsStdFunction()) {}

  UnwrappedTSN::Unwrapper unwrapper_;
  DataGenerator gen_;
  StrictMock<MockFunction<bool(StreamID, OutgoingMessageId)>> on_discard_;
  OutstandingData buf_;
};

SM_TEST_F(OutstandingDataTest, HasInitialState) {
  SM_EXPECT_TRUE(buf_.empty());
  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 0u);
  SM_EXPECT_EQ(buf_.unacked_packet_bytes(), 0u);
  SM_EXPECT_EQ(buf_.unacked_items(), 0u);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.last_cumulative_tsn_ack().Wrap(), TSN(9));
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(10));
  SM_EXPECT_EQ(buf_.highest_outstanding_tsn().Wrap(), TSN(9));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked)));
  SM_EXPECT_FALSE(buf_.ShouldSendForwardTsn());
}

SM_TEST_F(OutstandingDataTest, InsertChunk) {
  ASSERT_HAS_VALUE_AND_ASSIGN(
      UnwrappedTSN tsn, buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow));

  SM_EXPECT_EQ(tsn.Wrap(), TSN(10));

  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 1u);
  SM_EXPECT_EQ(buf_.unacked_items(), 1u);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.last_cumulative_tsn_ack().Wrap(), TSN(9));
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(11));
  SM_EXPECT_EQ(buf_.highest_outstanding_tsn().Wrap(), TSN(10));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kInFlight)));
}

SM_TEST_F(OutstandingDataTest, AcksSingleChunk) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow);
  OutstandingData::AckInfo ack =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(10)), {}, false);

  SM_EXPECT_EQ(ack.bytes_acked, DataChunk::kHeaderSize + RoundUpTo4(1));
  SM_EXPECT_EQ(ack.highest_tsn_acked.Wrap(), TSN(10));
  SM_EXPECT_FALSE(ack.has_packet_loss);

  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 0u);
  SM_EXPECT_EQ(buf_.unacked_items(), 0u);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.last_cumulative_tsn_ack().Wrap(), TSN(10));
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(11));
  SM_EXPECT_EQ(buf_.highest_outstanding_tsn().Wrap(), TSN(10));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(10), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, AcksPreviousChunkDoesntUpdate) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow);
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), {}, false);

  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 1u);
  SM_EXPECT_EQ(buf_.unacked_items(), 1u);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.last_cumulative_tsn_ack().Wrap(), TSN(9));
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(11));
  SM_EXPECT_EQ(buf_.highest_outstanding_tsn().Wrap(), TSN(10));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kInFlight)));
}

SM_TEST_F(OutstandingDataTest, AcksAndNacksWithGapAckBlocks) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow);

  std::vector<SackChunk::GapAckBlock> gab = {SackChunk::GapAckBlock(2, 2)};
  OutstandingData::AckInfo ack =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab, false);
  SM_EXPECT_EQ(ack.bytes_acked, DataChunk::kHeaderSize + RoundUpTo4(1));
  SM_EXPECT_EQ(ack.highest_tsn_acked.Wrap(), TSN(11));
  SM_EXPECT_FALSE(ack.has_packet_loss);

  // TSN (10) is still outstanding.
  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 1u);
  SM_EXPECT_EQ(buf_.unacked_items(), 1u);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.last_cumulative_tsn_ack().Wrap(), TSN(9));
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(12));
  SM_EXPECT_EQ(buf_.highest_outstanding_tsn().Wrap(), TSN(11));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),    //
                          Pair(TSN(10), State::kNacked),  //
                          Pair(TSN(11), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, NacksThreeTimesWithSameTsnDoesntRetransmit) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow);

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 2)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),    //
                          Pair(TSN(10), State::kNacked),  //
                          Pair(TSN(11), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, NacksThreeTimesResultsInRetransmission) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow);

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 2)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(2, 3)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab2, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(2, 4)};
  OutstandingData::AckInfo ack =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab3, false);
  SM_EXPECT_EQ(ack.bytes_acked, DataChunk::kHeaderSize + RoundUpTo4(1));
  SM_EXPECT_EQ(ack.highest_tsn_acked.Wrap(), TSN(13));
  SM_EXPECT_TRUE(ack.has_packet_loss);

  SM_EXPECT_TRUE(buf_.has_data_to_be_retransmitted());

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),               //
                          Pair(TSN(10), State::kToBeRetransmitted),  //
                          Pair(TSN(11), State::kAcked),              //
                          Pair(TSN(12), State::kAcked),              //
                          Pair(TSN(13), State::kAcked)));

  SM_EXPECT_THAT(buf_.GetChunksToBeFastRetransmitted(kNow, 1000),
              ElementsAre(Pair(TSN(10), _)));
  SM_EXPECT_THAT(buf_.GetChunksToBeRetransmitted(kNow, 1000), IsEmpty());
}

SM_TEST_F(OutstandingDataTest, NacksThreeTimesResultsInAbandoning) {
  static constexpr MaxRetransmits kMaxRetransmissions(0);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow, kMaxRetransmissions);

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 2)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(2, 3)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab2, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(2, 4)};
  OutstandingData::AckInfo ack =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab3, false);
  SM_EXPECT_EQ(ack.bytes_acked, DataChunk::kHeaderSize + RoundUpTo4(1));
  SM_EXPECT_EQ(ack.highest_tsn_acked.Wrap(), TSN(13));
  SM_EXPECT_TRUE(ack.has_packet_loss);

  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(14));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),       //
                          Pair(TSN(10), State::kAbandoned),  //
                          Pair(TSN(11), State::kAbandoned),  //
                          Pair(TSN(12), State::kAbandoned),  //
                          Pair(TSN(13), State::kAbandoned)));
}

SM_TEST_F(OutstandingDataTest, NacksThreeTimesResultsInAbandoningWithPlaceholder) {
  static constexpr MaxRetransmits kMaxRetransmissions(0);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, kMaxRetransmissions);

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 2)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(2, 3)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab2, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(true));
  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(2, 4)};
  OutstandingData::AckInfo ack =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab3, false);
  SM_EXPECT_EQ(ack.bytes_acked, DataChunk::kHeaderSize + RoundUpTo4(1));
  SM_EXPECT_EQ(ack.highest_tsn_acked.Wrap(), TSN(13));
  SM_EXPECT_TRUE(ack.has_packet_loss);

  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(15));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),       //
                          Pair(TSN(10), State::kAbandoned),  //
                          Pair(TSN(11), State::kAbandoned),  //
                          Pair(TSN(12), State::kAbandoned),  //
                          Pair(TSN(13), State::kAbandoned),  //
                          Pair(TSN(14), State::kAbandoned)));
}

SM_TEST_F(OutstandingDataTest, ExpiresChunkBeforeItIsInserted) {
  static constexpr Timestamp kExpiresAt = kNow + TimeDelta::Millis(1);
  SM_EXPECT_TRUE(buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow,
                          MaxRetransmits::NoLimit(), kExpiresAt)
                  .has_value());
  SM_EXPECT_TRUE(buf_.Insert(kMessageId, gen_.Ordered({1}, ""),
                          kNow + TimeDelta::Millis(0),
                          MaxRetransmits::NoLimit(), kExpiresAt)
                  .has_value());

  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  SM_EXPECT_FALSE(buf_.Insert(kMessageId, gen_.Ordered({1}, "E"),
                           kNow + TimeDelta::Millis(1),
                           MaxRetransmits::NoLimit(), kExpiresAt)
                   .has_value());

  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_EQ(buf_.last_cumulative_tsn_ack().Wrap(), TSN(9));
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(13));
  SM_EXPECT_EQ(buf_.highest_outstanding_tsn().Wrap(), TSN(12));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),       //
                          Pair(TSN(10), State::kAbandoned),  //
                          Pair(TSN(11), State::kAbandoned),
                          Pair(TSN(12), State::kAbandoned)));
}

SM_TEST_F(OutstandingDataTest, CanGenerateForwardTsn) {
  static constexpr MaxRetransmits kMaxRetransmissions(0);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, kMaxRetransmissions);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow, kMaxRetransmissions);

  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  buf_.NackAll(kNow);

  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),       //
                          Pair(TSN(10), State::kAbandoned),  //
                          Pair(TSN(11), State::kAbandoned),
                          Pair(TSN(12), State::kAbandoned)));

  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  ForwardTsnChunk chunk = buf_.CreateForwardTsn();
  SM_EXPECT_EQ(chunk.new_cumulative_tsn(), TSN(12));
}

SM_TEST_F(OutstandingDataTest, AckWithGapBlocksFromRFC4960Section334) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow);

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),      //
                          Pair(TSN(10), State::kInFlight),  //
                          Pair(TSN(11), State::kInFlight),  //
                          Pair(TSN(12), State::kInFlight),  //
                          Pair(TSN(13), State::kInFlight),  //
                          Pair(TSN(14), State::kInFlight),  //
                          Pair(TSN(15), State::kInFlight),  //
                          Pair(TSN(16), State::kInFlight),  //
                          Pair(TSN(17), State::kInFlight)));

  std::vector<SackChunk::GapAckBlock> gab = {SackChunk::GapAckBlock(2, 3),
                                             SackChunk::GapAckBlock(5, 5)};
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(12)), gab, false);

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(12), State::kAcked),   //
                          Pair(TSN(13), State::kNacked),  //
                          Pair(TSN(14), State::kAcked),   //
                          Pair(TSN(15), State::kAcked),   //
                          Pair(TSN(16), State::kNacked),  //
                          Pair(TSN(17), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, MeasureRTT) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow + TimeDelta::Millis(1));
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow + TimeDelta::Millis(2));

  static constexpr TimeDelta kDuration = TimeDelta::Millis(123);
  TimeDelta duration =
      buf_.MeasureRTT(kNow + kDuration, unwrapper_.Unwrap(TSN(11)));

  SM_EXPECT_EQ(duration, kDuration - TimeDelta::Millis(1));
}

SM_TEST_F(OutstandingDataTest, MustRetransmitBeforeGettingNackedAgain) {
  // This test case verifies that a chunk that has been nacked, and scheduled to
  // be retransmitted, doesn't get nacked again until it has been actually sent
  // on the wire.

  static constexpr MaxRetransmits kOneRetransmission(1);
  for (int tsn = 10; tsn <= 20; ++tsn) {
    buf_.Insert(kMessageId,
                gen_.Ordered({1}, tsn == 10   ? "B"
                                  : tsn == 20 ? "E"
                                              : ""),
                kNow, kOneRetransmission);
  }

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 2)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(2, 3)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab2, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(2, 4)};
  OutstandingData::AckInfo ack =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab3, false);
  SM_EXPECT_TRUE(ack.has_packet_loss);
  SM_EXPECT_TRUE(buf_.has_data_to_be_retransmitted());

  // Don't call GetChunksToBeRetransmitted yet - simulate that the congestion
  // window doesn't allow it to be retransmitted yet. It does however get more
  // SACKs indicating packet loss.

  std::vector<SackChunk::GapAckBlock> gab4 = {SackChunk::GapAckBlock(2, 5)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab4, false)
                   .has_packet_loss);
  SM_EXPECT_TRUE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab5 = {SackChunk::GapAckBlock(2, 6)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab5, false)
                   .has_packet_loss);
  SM_EXPECT_TRUE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab6 = {SackChunk::GapAckBlock(2, 7)};
  OutstandingData::AckInfo ack2 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab6, false);

  SM_EXPECT_FALSE(ack2.has_packet_loss);
  SM_EXPECT_TRUE(buf_.has_data_to_be_retransmitted());

  // Now it's retransmitted.
  SM_EXPECT_THAT(buf_.GetChunksToBeFastRetransmitted(kNow, 1000),
              ElementsAre(Pair(TSN(10), _)));
  SM_EXPECT_THAT(buf_.GetChunksToBeRetransmitted(kNow, 1000), IsEmpty());

  // And obviously lost, as it will get NACKed and abandoned.
  std::vector<SackChunk::GapAckBlock> gab7 = {SackChunk::GapAckBlock(2, 8)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab7, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab8 = {SackChunk::GapAckBlock(2, 9)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab8, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));

  std::vector<SackChunk::GapAckBlock> gab9 = {SackChunk::GapAckBlock(2, 10)};
  OutstandingData::AckInfo ack3 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab9, false);

  SM_EXPECT_TRUE(ack3.has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());
}

SM_TEST_F(OutstandingDataTest, LifecyleReturnsAckedItemsInAckInfo) {
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE"), kNow,
              MaxRetransmits::NoLimit(), Timestamp::PlusInfinity(),
              LifecycleId(42));
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({1}, "BE"), kNow,
              MaxRetransmits::NoLimit(), Timestamp::PlusInfinity(),
              LifecycleId(43));
  buf_.Insert(OutgoingMessageId(3), gen_.Ordered({1}, "BE"), kNow,
              MaxRetransmits::NoLimit(), Timestamp::PlusInfinity(),
              LifecycleId(44));

  OutstandingData::AckInfo ack1 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(11)), {}, false);

  SM_EXPECT_THAT(ack1.acked_lifecycle_ids,
              ElementsAre(LifecycleId(42), LifecycleId(43)));

  OutstandingData::AckInfo ack2 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(12)), {}, false);

  SM_EXPECT_THAT(ack2.acked_lifecycle_ids, ElementsAre(LifecycleId(44)));
}

SM_TEST_F(OutstandingDataTest, LifecycleReturnsAbandonedNackedThreeTimes) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow, MaxRetransmits(0));
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, MaxRetransmits(0));
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, MaxRetransmits(0));
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow, MaxRetransmits(0),
              Timestamp::PlusInfinity(), LifecycleId(42));

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 2)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(2, 3)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab2, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(2, 4)};
  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  OutstandingData::AckInfo ack1 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab3, false);
  SM_EXPECT_TRUE(ack1.has_packet_loss);
  SM_EXPECT_THAT(ack1.abandoned_lifecycle_ids, IsEmpty());

  // This will generate a FORWARD-TSN, which is acked
  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  ForwardTsnChunk chunk = buf_.CreateForwardTsn();
  SM_EXPECT_EQ(chunk.new_cumulative_tsn(), TSN(13));

  OutstandingData::AckInfo ack2 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(13)), {}, false);
  SM_EXPECT_FALSE(ack2.has_packet_loss);
  SM_EXPECT_THAT(ack2.abandoned_lifecycle_ids, ElementsAre(LifecycleId(42)));
}

SM_TEST_F(OutstandingDataTest, LifecycleReturnsAbandonedAfterT3rtxExpired) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow, MaxRetransmits(0));
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, MaxRetransmits(0));
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow, MaxRetransmits(0));
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow, MaxRetransmits(0),
              Timestamp::PlusInfinity(), LifecycleId(42));

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),      //
                          Pair(TSN(10), State::kInFlight),  //
                          Pair(TSN(11), State::kInFlight),  //
                          Pair(TSN(12), State::kInFlight),  //
                          Pair(TSN(13), State::kInFlight)));

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(2, 4)};
  SM_EXPECT_FALSE(buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab1, false)
                   .has_packet_loss);
  SM_EXPECT_FALSE(buf_.has_data_to_be_retransmitted());

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),    //
                          Pair(TSN(10), State::kNacked),  //
                          Pair(TSN(11), State::kAcked),   //
                          Pair(TSN(12), State::kAcked),   //
                          Pair(TSN(13), State::kAcked)));

  // T3-rtx triggered.
  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  buf_.NackAll(kNow);

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),       //
                          Pair(TSN(10), State::kAbandoned),  //
                          Pair(TSN(11), State::kAbandoned),  //
                          Pair(TSN(12), State::kAbandoned),  //
                          Pair(TSN(13), State::kAbandoned)));

  // This will generate a FORWARD-TSN, which is acked
  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  ForwardTsnChunk chunk = buf_.CreateForwardTsn();
  SM_EXPECT_EQ(chunk.new_cumulative_tsn(), TSN(13));

  OutstandingData::AckInfo ack2 =
      buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(13)), {}, false);
  SM_EXPECT_FALSE(ack2.has_packet_loss);
  SM_EXPECT_THAT(ack2.abandoned_lifecycle_ids, ElementsAre(LifecycleId(42)));
}

SM_TEST_F(OutstandingDataTest, GeneratesForwardTsnUntilNextStreamResetTsn) {
  // This test generates:
  // * Stream 1: TSN 10, 11, 12 <RESET>
  // * Stream 2: TSN 13, 14 <RESET>
  // * Stream 3: TSN 15, 16
  //
  // Then it expires chunk 12-15, and ensures that the generated FORWARD-TSN
  // only includes up till TSN 12 until the cum ack TSN has reached 12, and then
  // 13 and 14 are included, and then after the cum ack TSN has reached 14, then
  // 15 is included.
  //
  // What it shouldn't do, is to generate a FORWARD-TSN directly at the start
  // with new TSN=15, and setting [(sid=1, ssn=44), (sid=2, ssn=46),
  // (sid=3, ssn=47)], because that will confuse the receiver at TSN=17,
  // receiving SID=1, SSN=0 (it's reset!), expecting SSN to be 45.
  constexpr DataGeneratorOptions kStream1 = {.stream_id = StreamID(1)};
  constexpr DataGeneratorOptions kStream2 = {.stream_id = StreamID(2)};
  constexpr DataGeneratorOptions kStream3 = {.stream_id = StreamID(3)};
  constexpr MaxRetransmits kNoRtx = MaxRetransmits(0);
  EXPECT_CALL(on_discard_, Call).WillRepeatedly(Return(false));

  // TSN 10-12
  buf_.Insert(OutgoingMessageId(0), gen_.Ordered({1}, "BE", kStream1), kNow,
              kNoRtx);
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE", kStream1), kNow,
              kNoRtx);
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({1}, "BE", kStream1), kNow,
              kNoRtx);

  buf_.BeginResetStreams();

  // TSN 13, 14
  buf_.Insert(OutgoingMessageId(3), gen_.Ordered({1}, "BE", kStream2), kNow,
              kNoRtx);
  buf_.Insert(OutgoingMessageId(4), gen_.Ordered({1}, "BE", kStream2), kNow,
              kNoRtx);

  buf_.BeginResetStreams();

  // TSN 15, 16
  buf_.Insert(OutgoingMessageId(5), gen_.Ordered({1}, "BE", kStream3), kNow,
              kNoRtx);
  buf_.Insert(OutgoingMessageId(6), gen_.Ordered({1}, "BE", kStream3), kNow);

  SM_EXPECT_FALSE(buf_.ShouldSendForwardTsn());

  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(11)), {}, false);
  buf_.NackAll(kNow);
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(11), State::kAcked),      //
                          Pair(TSN(12), State::kAbandoned),  //
                          Pair(TSN(13), State::kAbandoned),  //
                          Pair(TSN(14), State::kAbandoned),  //
                          Pair(TSN(15), State::kAbandoned),  //
                          Pair(TSN(16), State::kToBeRetransmitted)));

  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  SM_EXPECT_THAT(
      buf_.CreateForwardTsn(),
      AllOf(Property(&ForwardTsnChunk::new_cumulative_tsn, TSN(12)),
            Property(&ForwardTsnChunk::skipped_streams,
                     UnorderedElementsAre(ForwardTsnChunk::SkippedStream(
                         StreamID(1), SSN(44))))));

  // Ack 12, allowing a FORWARD-TSN that spans to TSN=14 to be created.
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(12)), {}, false);
  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  SM_EXPECT_THAT(
      buf_.CreateForwardTsn(),
      AllOf(Property(&ForwardTsnChunk::new_cumulative_tsn, TSN(14)),
            Property(&ForwardTsnChunk::skipped_streams,
                     UnorderedElementsAre(ForwardTsnChunk::SkippedStream(
                         StreamID(2), SSN(46))))));

  // Ack 13, allowing a FORWARD-TSN that spans to TSN=14 to be created.
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(13)), {}, false);
  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  SM_EXPECT_THAT(
      buf_.CreateForwardTsn(),
      AllOf(Property(&ForwardTsnChunk::new_cumulative_tsn, TSN(14)),
            Property(&ForwardTsnChunk::skipped_streams,
                     UnorderedElementsAre(ForwardTsnChunk::SkippedStream(
                         StreamID(2), SSN(46))))));

  // Ack 14, allowing a FORWARD-TSN that spans to TSN=15 to be created.
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(14)), {}, false);
  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  SM_EXPECT_THAT(
      buf_.CreateForwardTsn(),
      AllOf(Property(&ForwardTsnChunk::new_cumulative_tsn, TSN(15)),
            Property(&ForwardTsnChunk::skipped_streams,
                     UnorderedElementsAre(ForwardTsnChunk::SkippedStream(
                         StreamID(3), SSN(47))))));

  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(15)), {}, false);
  SM_EXPECT_FALSE(buf_.ShouldSendForwardTsn());
}

SM_TEST_F(OutstandingDataTest, TreatsUnackedPayloadBytesDifferentFromPacketBytes) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow);

  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 1u);
  SM_EXPECT_EQ(buf_.unacked_packet_bytes(),
            DataChunk::kHeaderSize + RoundUpTo4(1));
  SM_EXPECT_EQ(buf_.unacked_items(), 1u);

  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow);
  SM_EXPECT_EQ(buf_.unacked_payload_bytes(), 2u);
  SM_EXPECT_EQ(buf_.unacked_packet_bytes(),
            2 * (DataChunk::kHeaderSize + RoundUpTo4(1)));
  SM_EXPECT_EQ(buf_.unacked_items(), 2u);
}

SM_TEST_F(OutstandingDataTest, FastRecoveryIncrementsNackCountWhenCumulativeTsnAdvances) {
  // This test verifies that the Fast Recovery retransmission rules are
  // correctly applied when the Cumulative TSN Ack point advances. RFC 9260
  // Section 7.2.4: "If an endpoint is in Fast Recovery and a SACK arrives that
  // advances the Cumulative TSN Ack Point, the miss indications are incremented
  // for all TSNs reported missing in the SACK."

  for (int i = 10; i <= 16; ++i) {
    buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  }

  // SACK 1: Cumulative Ack = 10. Gap blocks for 12, 14, 16.
  // Missing: 11, 13, 15.
  // This marks 12, 14, 16 as Acked.
  // TSNs 11, 13, 15 get their 1st miss indication each.
  std::vector<SackChunk::GapAckBlock> gab1 = {
      SackChunk::GapAckBlock(2, 2),  // TSN 12
      SackChunk::GapAckBlock(4, 4),  // TSN 14
      SackChunk::GapAckBlock(6, 6)   // TSN 16
  };
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(10)), gab1,
                  /*is_in_fast_recovery=*/false);

  // SACK 2: Cumulative Ack advances to 11. Same gap blocks (12, 14, 16).
  // Endpoint is now in Fast Recovery (is_in_fast_recovery = true). Because the
  // Cumulative TSN Ack Point advanced from 10 to 11, 13 and 15 should get their
  // 2nd miss indication.
  std::vector<SackChunk::GapAckBlock> gab2 = {
      SackChunk::GapAckBlock(1, 1),  // TSN 12
      SackChunk::GapAckBlock(3, 3),  // TSN 14
      SackChunk::GapAckBlock(5, 5)   // TSN 16
  };
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(11)), gab2,
                  /*is_in_fast_recovery=*/true);

  // SACK 3: Cumulative Ack advances to 12.
  // Note: TSN 12 was already acked via gap block, so this just advances the
  // Cumulative Ack. 13 and 15 should get their 3rd miss indication and trigger
  // retransmission.
  std::vector<SackChunk::GapAckBlock> gab3 = {
      SackChunk::GapAckBlock(2, 2),  // TSN 14
      SackChunk::GapAckBlock(4, 4)   // TSN 16
  };
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(12)), gab3,
                  /*is_in_fast_recovery=*/true);

  // 13 and 15 should now be retransmitted.
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(12), State::kAcked),
                          Pair(TSN(13), State::kToBeRetransmitted),
                          Pair(TSN(14), State::kAcked),
                          Pair(TSN(15), State::kToBeRetransmitted),
                          Pair(TSN(16), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, NackBetweenAckBlocksDoesNotAccessOutOfBounds) {
  for (int i = 0; i < 5; ++i) {
    buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  }

  // Inject a malformed SACK where the GapAckBlock exceeds the number of
  // outstanding items, potentially triggering an OOB read/write.
  std::vector<SackChunk::GapAckBlock> malformed_blocks = {
      SackChunk::GapAckBlock(1, 40000)};

  // This should not crash or trigger ASAN errors.
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(10)), malformed_blocks,
                  /*is_in_fast_recovery=*/false);
}

SM_TEST_F(OutstandingDataTest, HandlesSacksWithOutOfBoundsTsns) {
  // Send chunks with TSNs 10, 11, 12, 13, 14, 15, 16
  for (int i = 0; i < 7; ++i) {
    buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);
  }

  // This NACKs TSN 11, 13, 15 (1st miss indication)
  SackChunk::GapAckBlock sack1[] = {{2, 2},   // TSN 12
                                    {4, 4},   // TSN 14
                                    {6, 6}};  // TSN 16
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(10)), sack1,
                  /*is_in_fast_recovery=*/false);
  SM_EXPECT_EQ(buf_.unacked_items(), 3u);  // 11, 13, 15

  // The gap between block1-end (12) and block2-start (1011) causes
  // NackBetweenAckBlocks to loop TSN 13..1010, but only TSN 13..16 are valid.
  SackChunk::GapAckBlock sack2[] = {{1, 1},          // TSN 12
                                    {1000, 60000}};  // TSN 1011..60011

  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(11)), sack2,
                  /*is_in_fast_recovery=*/true);
  // Packet 11 has been acknowledged.
  SM_EXPECT_EQ(buf_.unacked_items(), 2u);  // 13, 15
}

SM_TEST_F(OutstandingDataTest, RestoreFromStateOverridesDummyInitialTsn) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, ""), kNow);

  DcSctpSocketHandoverState state;
  buf_.AddHandoverState(kNow, state);

  SM_EXPECT_THAT(state.tx.outstanding_data, SizeIs(2));

  // In production, OutstandingData is instantiated before the socket has fully
  // restored from state, so it receives a stale/dummy initial TSN.
  // RestoreFromState is later called with the true last_cumulative_tsn_ack.
  OutstandingData restored_buf(DataChunk::kHeaderSize,
                               unwrapper_.Unwrap(TSN(0)),
                               on_discard_.AsStdFunction());
  restored_buf.RestoreFromState(kNow, unwrapper_.Unwrap(TSN(9)), state);

  SM_EXPECT_EQ(restored_buf.unacked_items(), 0u);
  SM_EXPECT_EQ(restored_buf.last_cumulative_tsn_ack().Wrap(), TSN(9));
}

SM_TEST_F(OutstandingDataTest, HandoverAndRestorePreservesRetransmissionCount) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow);
  buf_.NackAll(kNow);
  buf_.GetChunksToBeRetransmitted(kNow, 1000);

  DcSctpSocketHandoverState state;
  buf_.AddHandoverState(kNow, state);
  SM_ASSERT_THAT(state.tx.outstanding_data, SizeIs(1));
  SM_EXPECT_EQ(state.tx.outstanding_data[0].retransmission_count, 1u);
  SM_EXPECT_TRUE(state.tx.outstanding_data[0].is_to_be_retransmitted);

  OutstandingData restored_buf(DataChunk::kHeaderSize,
                               unwrapper_.Unwrap(TSN(0)),
                               on_discard_.AsStdFunction());
  restored_buf.RestoreFromState(kNow, unwrapper_.Unwrap(TSN(9)), state);

  SM_EXPECT_THAT(restored_buf.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kToBeRetransmitted)));
  SM_EXPECT_EQ(restored_buf.unacked_payload_bytes(), 0u);
  SM_EXPECT_EQ(restored_buf.unacked_packet_bytes(), 0u);
  SM_EXPECT_EQ(restored_buf.unacked_items(), 0u);

  DcSctpSocketHandoverState restored_state;
  restored_buf.AddHandoverState(kNow, restored_state);
  SM_ASSERT_THAT(restored_state.tx.outstanding_data, SizeIs(1));
  SM_EXPECT_EQ(restored_state.tx.outstanding_data[0].retransmission_count, 1u);
  SM_EXPECT_TRUE(restored_state.tx.outstanding_data[0].is_to_be_retransmitted);
}

SM_TEST_F(OutstandingDataTest, HandoverAndRestorePreservesAbandonedState) {
  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow, MaxRetransmits(0));
  buf_.NackAll(kNow);

  DcSctpSocketHandoverState state;
  buf_.AddHandoverState(kNow, state);
  SM_ASSERT_THAT(state.tx.outstanding_data, SizeIs(1));
  SM_EXPECT_TRUE(state.tx.outstanding_data[0].is_abandoned);

  OutstandingData restored_buf(DataChunk::kHeaderSize,
                               unwrapper_.Unwrap(TSN(0)),
                               on_discard_.AsStdFunction());
  restored_buf.RestoreFromState(kNow, unwrapper_.Unwrap(TSN(9)), state);

  SM_EXPECT_THAT(restored_buf.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kAbandoned)));

  DcSctpSocketHandoverState restored_state;
  restored_buf.AddHandoverState(kNow, restored_state);
  SM_ASSERT_THAT(restored_state.tx.outstanding_data, SizeIs(1));
  SM_EXPECT_TRUE(restored_state.tx.outstanding_data[0].is_abandoned);
}

SM_TEST_F(OutstandingDataTest, HandoverAndRestorePreservesNackedState) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "B"), kNow);
  buf_.Insert(kMessageId, gen_.Ordered({1}, "E"), kNow);

  std::vector<SackChunk::GapAckBlock> gab = {SackChunk::GapAckBlock(2, 2)};
  buf_.HandleSack(kNow, unwrapper_.Unwrap(TSN(9)), gab, false);

  DcSctpSocketHandoverState state;
  buf_.AddHandoverState(kNow, state);

  // We expect chunk 10 to be Nacked
  SM_ASSERT_THAT(state.tx.outstanding_data, SizeIs(2));
  SM_EXPECT_TRUE(state.tx.outstanding_data[0].is_nacked);

  OutstandingData restored_buf(DataChunk::kHeaderSize,
                               unwrapper_.Unwrap(TSN(0)),
                               on_discard_.AsStdFunction());
  restored_buf.RestoreFromState(kNow, unwrapper_.Unwrap(TSN(9)), state);

  SM_EXPECT_THAT(restored_buf.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kToBeRetransmitted),
                          Pair(TSN(11), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, ExpiresChunkBeforeRetransmittingWhenBehindNonExpiredChunk) {
  // TSN 10 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE"), kNow);
  // TSN 11 and 12 have a limited lifetime and expire at kNow + 50ms.
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({2}, "B"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({3}, "E"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));

  buf_.NackAll(kNow);

  // ExpireOutstandingChunks stops at the first non-expired chunk (TSN 10).
  buf_.ExpireOutstandingChunks(kNow + TimeDelta::Millis(50));
  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kToBeRetransmitted),
                          Pair(TSN(11), State::kToBeRetransmitted),
                          Pair(TSN(12), State::kToBeRetransmitted)));

  // When extracting chunks for retransmission, TSN 11 and 12 are expired and
  // abandoned instead of being retransmitted.
  EXPECT_CALL(on_discard_, Call(StreamID(1), OutgoingMessageId(2)))
      .WillOnce(Return(false));
  SM_EXPECT_THAT(
      buf_.GetChunksToBeRetransmitted(kNow + TimeDelta::Millis(50), 1000),
      ElementsAre(Pair(TSN(10), _)));

  SM_EXPECT_THAT(
      buf_.GetChunkStatesForTesting(),
      ElementsAre(Pair(TSN(9), State::kAcked), Pair(TSN(10), State::kInFlight),
                  Pair(TSN(11), State::kAbandoned),
                  Pair(TSN(12), State::kAbandoned)));
  SM_EXPECT_FALSE(buf_.ShouldSendForwardTsn());

  // Once TSN 10 is acknowledged, FORWARD-TSN can be sent for TSN 11 and 12.
  buf_.HandleSack(kNow + TimeDelta::Millis(50), unwrapper_.Unwrap(TSN(10)), {},
                  false);
  SM_EXPECT_TRUE(buf_.ShouldSendForwardTsn());
  SM_EXPECT_EQ(buf_.CreateForwardTsn().new_cumulative_tsn(), TSN(12));
}

SM_TEST_F(OutstandingDataTest, ExpiresChunkOnNackAll) {
  // TSN 10 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE"), kNow);
  // TSN 11 and 12 have a limited lifetime and expire at kNow + 50ms.
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({2}, "B"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({3}, "E"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));

  EXPECT_CALL(on_discard_, Call(StreamID(1), OutgoingMessageId(2)))
      .WillOnce(Return(false));
  buf_.NackAll(kNow + TimeDelta::Millis(50));

  SM_EXPECT_THAT(buf_.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kToBeRetransmitted),
                          Pair(TSN(11), State::kAbandoned),
                          Pair(TSN(12), State::kAbandoned)));
}

SM_TEST_F(OutstandingDataTest, ExpiresChunkWhenNackedThreeTimes) {
  // TSN 10 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE"), kNow);
  // TSN 11 has a limited lifetime and expires at kNow + 50ms.
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({2}, "BE"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  // TSN 12, 13, 14 are used to trigger 3 miss indications.
  buf_.Insert(OutgoingMessageId(3), gen_.Ordered({3}, "BE"), kNow);
  buf_.Insert(OutgoingMessageId(4), gen_.Ordered({4}, "BE"), kNow);
  buf_.Insert(OutgoingMessageId(5), gen_.Ordered({5}, "BE"), kNow);

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(3, 3)};
  buf_.HandleSack(kNow + TimeDelta::Millis(10), unwrapper_.Unwrap(TSN(9)), gab1,
                  false);

  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(3, 4)};
  buf_.HandleSack(kNow + TimeDelta::Millis(20), unwrapper_.Unwrap(TSN(9)), gab2,
                  false);

  EXPECT_CALL(on_discard_, Call(StreamID(1), OutgoingMessageId(2)))
      .WillOnce(Return(false));
  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(3, 5)};
  buf_.HandleSack(kNow + TimeDelta::Millis(50), unwrapper_.Unwrap(TSN(9)), gab3,
                  false);

  SM_EXPECT_THAT(
      buf_.GetChunkStatesForTesting(),
      ElementsAre(
          Pair(TSN(9), State::kAcked), Pair(TSN(10), State::kToBeRetransmitted),
          Pair(TSN(11), State::kAbandoned), Pair(TSN(12), State::kAcked),
          Pair(TSN(13), State::kAcked), Pair(TSN(14), State::kAcked)));
  SM_EXPECT_THAT(
      buf_.GetChunksToBeFastRetransmitted(kNow + TimeDelta::Millis(50), 1000),
      ElementsAre(Pair(TSN(10), _)));
}

SM_TEST_F(OutstandingDataTest, ExpiredChunkDoesNotConsumeRetransmissionBudget) {
  std::vector<uint8_t> payload(100);
  // TSN 10 has a limited lifetime and expires at kNow + 50ms.
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered(payload, "BE"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  // TSN 11 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered(payload, "BE"), kNow);

  buf_.NackAll(kNow);

  EXPECT_CALL(on_discard_, Call(StreamID(1), OutgoingMessageId(1)))
      .WillOnce(Return(false));
  // Budget fits exactly one chunk. TSN 10 should be expired without consuming
  // budget, allowing TSN 11 to be returned.
  size_t one_chunk_budget = DataChunk::kHeaderSize + payload.size();
  SM_EXPECT_THAT(buf_.GetChunksToBeRetransmitted(kNow + TimeDelta::Millis(50),
                                              one_chunk_budget),
              ElementsAre(Pair(TSN(11), _)));

  SM_EXPECT_THAT(
      buf_.GetChunkStatesForTesting(),
      ElementsAre(Pair(TSN(9), State::kAcked), Pair(TSN(10), State::kAbandoned),
                  Pair(TSN(11), State::kInFlight)));
}

SM_TEST_F(OutstandingDataTest, DoesNotExpireChunkBeforeExpiryTimestamp) {
  // TSN 10 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE"), kNow);
  // TSN 11 and 12 expire at kNow + 50ms.
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({2}, "B"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({3}, "E"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));

  buf_.NackAll(kNow);

  // At 1ms before expiry, all three chunks are still valid and returned.
  SM_EXPECT_THAT(
      buf_.GetChunksToBeRetransmitted(kNow + TimeDelta::Millis(49), 1000),
      ElementsAre(Pair(TSN(10), _), Pair(TSN(11), _), Pair(TSN(12), _)));
}

SM_TEST_F(OutstandingDataTest, ExpiresFastRetransmitLeftoverChunkOnNormalRetransmit) {
  std::vector<uint8_t> payload(100);
  // TSN 10 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered(payload, "BE"), kNow);
  // TSN 11 expires at kNow + 50ms.
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered(payload, "BE"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  // TSN 12, 13, 14 are used to trigger 3 miss indications.
  buf_.Insert(OutgoingMessageId(3), gen_.Ordered({3}, "BE"), kNow);
  buf_.Insert(OutgoingMessageId(4), gen_.Ordered({4}, "BE"), kNow);
  buf_.Insert(OutgoingMessageId(5), gen_.Ordered({5}, "BE"), kNow);

  std::vector<SackChunk::GapAckBlock> gab1 = {SackChunk::GapAckBlock(3, 3)};
  buf_.HandleSack(kNow + TimeDelta::Millis(10), unwrapper_.Unwrap(TSN(9)), gab1,
                  false);
  std::vector<SackChunk::GapAckBlock> gab2 = {SackChunk::GapAckBlock(3, 4)};
  buf_.HandleSack(kNow + TimeDelta::Millis(20), unwrapper_.Unwrap(TSN(9)), gab2,
                  false);
  // 3rd SACK arrives before TSN 11 expires (at kNow + 30ms).
  std::vector<SackChunk::GapAckBlock> gab3 = {SackChunk::GapAckBlock(3, 5)};
  buf_.HandleSack(kNow + TimeDelta::Millis(30), unwrapper_.Unwrap(TSN(9)), gab3,
                  false);

  // Only TSN 10 fits in the fast-retransmit packet; TSN 11 is moved to
  // `to_be_retransmitted_`.
  size_t one_chunk_budget = DataChunk::kHeaderSize + payload.size();
  SM_EXPECT_THAT(buf_.GetChunksToBeFastRetransmitted(kNow + TimeDelta::Millis(30),
                                                  one_chunk_budget),
              ElementsAre(Pair(TSN(10), _)));

  // Later, at kNow + 50ms, TSN 11 has expired and is abandoned when extracted.
  EXPECT_CALL(on_discard_, Call(StreamID(1), OutgoingMessageId(2)))
      .WillOnce(Return(false));
  SM_EXPECT_THAT(
      buf_.GetChunksToBeRetransmitted(kNow + TimeDelta::Millis(50), 1000),
      IsEmpty());
  SM_EXPECT_THAT(
      buf_.GetChunkStatesForTesting(),
      ElementsAre(Pair(TSN(9), State::kAcked), Pair(TSN(10), State::kInFlight),
                  Pair(TSN(11), State::kAbandoned),
                  Pair(TSN(12), State::kAcked), Pair(TSN(13), State::kAcked),
                  Pair(TSN(14), State::kAcked)));
}

SM_TEST_F(OutstandingDataTest, ExpiresChunkBeforeRetransmittingWithPlaceholder) {
  // TSN 10 is reliable (never expires).
  buf_.Insert(OutgoingMessageId(1), gen_.Ordered({1}, "BE"), kNow);
  // TSN 11 is the beginning of a multi-fragment message that expires at
  // kNow + 50ms, with remaining unsent fragments still in the SendQueue.
  buf_.Insert(OutgoingMessageId(2), gen_.Ordered({2}, "B"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));

  buf_.NackAll(kNow);

  EXPECT_CALL(on_discard_, Call(StreamID(1), OutgoingMessageId(2)))
      .WillOnce(Return(true));
  SM_EXPECT_THAT(
      buf_.GetChunksToBeRetransmitted(kNow + TimeDelta::Millis(50), 1000),
      ElementsAre(Pair(TSN(10), _)));

  // An unsent end placeholder was appended at TSN 12 and marked abandoned.
  SM_EXPECT_EQ(buf_.next_tsn().Wrap(), TSN(13));
  SM_EXPECT_THAT(
      buf_.GetChunkStatesForTesting(),
      ElementsAre(Pair(TSN(9), State::kAcked), Pair(TSN(10), State::kInFlight),
                  Pair(TSN(11), State::kAbandoned),
                  Pair(TSN(12), State::kAbandoned)));
}

SM_TEST_F(OutstandingDataTest, ExpiresRestoredChunkBeforeRetransmitting) {
  buf_.Insert(kMessageId, gen_.Ordered({1}, "BE"), kNow,
              MaxRetransmits::NoLimit(), kNow + TimeDelta::Millis(50));
  buf_.NackAll(kNow);

  Timestamp handover_time = kNow + TimeDelta::Millis(60);
  DcSctpSocketHandoverState state;
  buf_.AddHandoverState(handover_time, state);
  SM_ASSERT_THAT(state.tx.outstanding_data, SizeIs(1));
  SM_EXPECT_LT(state.tx.outstanding_data[0].expires_in_ms, 0);

  OutstandingData restored_buf(DataChunk::kHeaderSize,
                               unwrapper_.Unwrap(TSN(0)),
                               on_discard_.AsStdFunction());
  restored_buf.RestoreFromState(handover_time, unwrapper_.Unwrap(TSN(9)),
                                state);

  EXPECT_CALL(on_discard_, Call(StreamID(1), kMessageId))
      .WillOnce(Return(false));
  SM_EXPECT_THAT(restored_buf.GetChunksToBeRetransmitted(handover_time, 1000),
              IsEmpty());
  SM_EXPECT_THAT(restored_buf.GetChunkStatesForTesting(),
              ElementsAre(Pair(TSN(9), State::kAcked),
                          Pair(TSN(10), State::kAbandoned)));
}

}  // namespace
}  // namespace sm::dcsctp
