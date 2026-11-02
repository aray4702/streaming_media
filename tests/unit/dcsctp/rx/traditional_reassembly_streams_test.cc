/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/rx/traditional_reassembly_streams_test.cc (compare/webrtc/reference/net/dcsctp/rx/traditional_reassembly_streams_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/rx/traditional_reassembly_streams.h"

#include <cstdint>
#include <utility>

#include "common/handover_testing.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/common/sequence_numbers.h"
#include "sm/dcsctp/packet/chunk/forward_tsn_chunk.h"
#include "sm/dcsctp/packet/chunk/forward_tsn_common.h"
#include "sm/dcsctp/packet/data.h"
#include "sm/dcsctp/public/dcsctp_handover_state.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/rx/reassembly_streams.h"
#include "testing/data_generator.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Property;

class TraditionalReassemblyStreamsTest : public testing::Test {
 protected:
  UnwrappedTSN tsn(uint32_t value) { return tsn_.Unwrap(TSN(value)); }

  TraditionalReassemblyStreamsTest() {}
  DataGenerator gen_;
  UnwrappedTSN::Unwrapper tsn_;
};

SM_TEST_F(TraditionalReassemblyStreamsTest, AddUnorderedMessageReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Unordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(2), gen_.Unordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Unordered({5, 6})), 2);
  // Adding the end fragment should make it empty again.
  SM_EXPECT_EQ(streams.Add(tsn(4), gen_.Unordered({7}, "E")), -6);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, AddSimpleOrderedMessageReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(2), gen_.Ordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Ordered({5, 6})), 2);
  SM_EXPECT_EQ(streams.Add(tsn(4), gen_.Ordered({7}, "E")), -6);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, AddMoreComplexOrderedMessageReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  Data late = gen_.Ordered({2, 3, 4});
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Ordered({5, 6})), 2);
  SM_EXPECT_EQ(streams.Add(tsn(4), gen_.Ordered({7}, "E")), 1);

  SM_EXPECT_EQ(streams.Add(tsn(5), gen_.Ordered({1}, "BE")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(6), gen_.Ordered({5, 6}, "B")), 2);
  SM_EXPECT_EQ(streams.Add(tsn(7), gen_.Ordered({7}, "E")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(2), std::move(late)), -8);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, DeleteUnorderedMessageReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Unordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(2), gen_.Unordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Unordered({5, 6})), 2);

  SM_EXPECT_EQ(streams.HandleForwardTsn(tsn(3), {}), 6u);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, DeleteSimpleOrderedMessageReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(2), gen_.Ordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Ordered({5, 6})), 2);

  ForwardTsnChunk::SkippedStream skipped[] = {
      ForwardTsnChunk::SkippedStream(StreamID(1), SSN(0))};
  SM_EXPECT_EQ(streams.HandleForwardTsn(tsn(3), skipped), 6u);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, DeleteManyOrderedMessagesReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  gen_.Ordered({2, 3, 4});
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Ordered({5, 6})), 2);
  SM_EXPECT_EQ(streams.Add(tsn(4), gen_.Ordered({7}, "E")), 1);

  SM_EXPECT_EQ(streams.Add(tsn(5), gen_.Ordered({1}, "BE")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(6), gen_.Ordered({5, 6}, "B")), 2);
  SM_EXPECT_EQ(streams.Add(tsn(7), gen_.Ordered({7}, "E")), 1);

  // Expire all three messages
  ForwardTsnChunk::SkippedStream skipped[] = {
      ForwardTsnChunk::SkippedStream(StreamID(1), SSN(2))};
  SM_EXPECT_EQ(streams.HandleForwardTsn(tsn(8), skipped), 8u);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, DeleteOrderedMessageDelivesTwoReturnsCorrectSize) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  gen_.Ordered({2, 3, 4});
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Ordered({5, 6})), 2);
  SM_EXPECT_EQ(streams.Add(tsn(4), gen_.Ordered({7}, "E")), 1);

  SM_EXPECT_EQ(streams.Add(tsn(5), gen_.Ordered({1}, "BE")), 1);
  SM_EXPECT_EQ(streams.Add(tsn(6), gen_.Ordered({5, 6}, "B")), 2);
  SM_EXPECT_EQ(streams.Add(tsn(7), gen_.Ordered({7}, "E")), 1);

  // The first ordered message expire, and the following two are delivered.
  ForwardTsnChunk::SkippedStream skipped[] = {
      ForwardTsnChunk::SkippedStream(StreamID(1), SSN(0))};
  SM_EXPECT_EQ(streams.HandleForwardTsn(tsn(4), skipped), 8u);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, NoStreamsCanBeHandedOver) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams1("", on_assembled.AsStdFunction());
  SM_EXPECT_TRUE(streams1.GetHandoverReadiness().IsReady());

  DcSctpSocketHandoverState state;
  streams1.AddHandoverState(state);
  g_handover_state_transformer_for_test(&state);
  TraditionalReassemblyStreams streams2("", on_assembled.AsStdFunction());
  streams2.RestoreFromState(state);

  SM_EXPECT_EQ(streams2.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams2.Add(tsn(2), gen_.Ordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(streams2.Add(tsn(1), gen_.Unordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams2.Add(tsn(2), gen_.Unordered({2, 3, 4})), 3);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, OrderedStreamsCanBeHandedOverWhenNoUnassembledChunksExist) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams1("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams1.Add(tsn(1), gen_.Ordered({1}, "B")), 1);
  SM_EXPECT_EQ(streams1.GetHandoverReadiness(),
            HandoverReadinessStatus(
                HandoverUnreadinessReason::kOrderedStreamHasUnassembledChunks));
  SM_EXPECT_EQ(streams1.Add(tsn(2), gen_.Ordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(streams1.GetHandoverReadiness(),
            HandoverReadinessStatus(
                HandoverUnreadinessReason::kOrderedStreamHasUnassembledChunks));
  SM_EXPECT_EQ(streams1.Add(tsn(3), gen_.Ordered({5, 6})), 2);
  SM_EXPECT_EQ(streams1.GetHandoverReadiness(),
            HandoverReadinessStatus(
                HandoverUnreadinessReason::kOrderedStreamHasUnassembledChunks));

  ForwardTsnChunk::SkippedStream skipped[] = {
      ForwardTsnChunk::SkippedStream(StreamID(1), SSN(0))};
  SM_EXPECT_EQ(streams1.HandleForwardTsn(tsn(3), skipped), 6u);
  SM_EXPECT_TRUE(streams1.GetHandoverReadiness().IsReady());

  DcSctpSocketHandoverState state;
  streams1.AddHandoverState(state);
  g_handover_state_transformer_for_test(&state);
  TraditionalReassemblyStreams streams2("", on_assembled.AsStdFunction());
  streams2.RestoreFromState(state);
  SM_EXPECT_EQ(streams2.Add(tsn(4), gen_.Ordered({7})), 1);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, UnorderedStreamsCanBeHandedOverWhenNoUnassembledChunksExist) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  TraditionalReassemblyStreams streams1("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams1.Add(tsn(1), gen_.Unordered({1}, "B")), 1);
  SM_EXPECT_EQ(
      streams1.GetHandoverReadiness(),
      HandoverReadinessStatus(
          HandoverUnreadinessReason::kUnorderedStreamHasUnassembledChunks));
  SM_EXPECT_EQ(streams1.Add(tsn(2), gen_.Unordered({2, 3, 4})), 3);
  SM_EXPECT_EQ(
      streams1.GetHandoverReadiness(),
      HandoverReadinessStatus(
          HandoverUnreadinessReason::kUnorderedStreamHasUnassembledChunks));
  SM_EXPECT_EQ(streams1.Add(tsn(3), gen_.Unordered({5, 6})), 2);
  SM_EXPECT_EQ(
      streams1.GetHandoverReadiness(),
      HandoverReadinessStatus(
          HandoverUnreadinessReason::kUnorderedStreamHasUnassembledChunks));

  SM_EXPECT_EQ(streams1.HandleForwardTsn(tsn(3), {}), 6u);
  SM_EXPECT_TRUE(streams1.GetHandoverReadiness().IsReady());

  DcSctpSocketHandoverState state;
  streams1.AddHandoverState(state);
  g_handover_state_transformer_for_test(&state);
  TraditionalReassemblyStreams streams2("", on_assembled.AsStdFunction());
  streams2.RestoreFromState(state);
  SM_EXPECT_EQ(streams2.Add(tsn(4), gen_.Unordered({7})), 1);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, CanDeleteFirstOrderedMessage) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;
  EXPECT_CALL(on_assembled,
              Call(ElementsAre(tsn(2)),
                   Property(&DcSctpMessage::payload, ElementsAre(2, 3, 4))));

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  // Not received, SID=1. TSN=1, SSN=0
  gen_.Ordered({1}, "BE");
  // And deleted (SID=1, TSN=1, SSN=0)
  ForwardTsnChunk::SkippedStream skipped[] = {
      ForwardTsnChunk::SkippedStream(StreamID(1), SSN(0))};
  SM_EXPECT_EQ(streams.HandleForwardTsn(tsn(1), skipped), 0u);

  // Receive SID=1, TSN=2, SSN=1
  SM_EXPECT_EQ(streams.Add(tsn(2), gen_.Ordered({2, 3, 4}, "BE")), 0);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, CanReassembleFastPathUnordered) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  {
    testing::InSequence s;
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(1)),
                     Property(&DcSctpMessage::payload, ElementsAre(1))));
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(3)),
                     Property(&DcSctpMessage::payload, ElementsAre(3))));
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(2)),
                     Property(&DcSctpMessage::payload, ElementsAre(2))));
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(4)),
                     Property(&DcSctpMessage::payload, ElementsAre(4))));
  }

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  SM_EXPECT_EQ(streams.Add(tsn(1), gen_.Unordered({1}, "BE")), 0);
  SM_EXPECT_EQ(streams.Add(tsn(3), gen_.Unordered({3}, "BE")), 0);
  SM_EXPECT_EQ(streams.Add(tsn(2), gen_.Unordered({2}, "BE")), 0);
  SM_EXPECT_EQ(streams.Add(tsn(4), gen_.Unordered({4}, "BE")), 0);
}

SM_TEST_F(TraditionalReassemblyStreamsTest, CanReassembleFastPathOrdered) {
  NiceMock<MockFunction<ReassemblyStreams::OnAssembledMessage>> on_assembled;

  {
    testing::InSequence s;
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(1)),
                     Property(&DcSctpMessage::payload, ElementsAre(1))));
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(2)),
                     Property(&DcSctpMessage::payload, ElementsAre(2))));
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(3)),
                     Property(&DcSctpMessage::payload, ElementsAre(3))));
    EXPECT_CALL(on_assembled,
                Call(ElementsAre(tsn(4)),
                     Property(&DcSctpMessage::payload, ElementsAre(4))));
  }

  TraditionalReassemblyStreams streams("", on_assembled.AsStdFunction());

  Data data1 = gen_.Ordered({1}, "BE");
  Data data2 = gen_.Ordered({2}, "BE");
  Data data3 = gen_.Ordered({3}, "BE");
  Data data4 = gen_.Ordered({4}, "BE");
  SM_EXPECT_EQ(streams.Add(tsn(1), std::move(data1)), 0);
  SM_EXPECT_EQ(streams.Add(tsn(3), std::move(data3)), 1);
  SM_EXPECT_EQ(streams.Add(tsn(2), std::move(data2)), -1);
  SM_EXPECT_EQ(streams.Add(tsn(4), std::move(data4)), 0);
}
}  // namespace
}  // namespace sm::dcsctp
