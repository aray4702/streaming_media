/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/rr_send_queue_test.cc (compare/webrtc/reference/net/dcsctp/tx/rr_send_queue_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/tx/rr_send_queue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/data.h"
#include "sm/dcsctp/public/dcsctp_handover_state.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/types.h"
#include "socket/mock_dcsctp_socket_callbacks.h"
#include "testing_macros.h"
#include "sm/dcsctp/tx/send_queue.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::sm::TimeDelta;
using ::sm::Timestamp;

constexpr Timestamp kNow = Timestamp::Micros(0);
constexpr StreamID kStreamID(1);
constexpr PPID kPPID(53);
constexpr StreamPriority kDefaultPriority(10);
constexpr size_t kBufferedAmountLowThreshold = 500;
constexpr size_t kOneFragmentPacketSize = 100;
constexpr size_t kTwoFragmentPacketSize = 101;
constexpr size_t kMtu = 1100;

SM_TEST(DcsctpRRSendQueueTest, EmptyBuffer) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  SM_EXPECT_TRUE(q.IsEmpty());
  SM_EXPECT_FALSE(q.Produce(kNow, kOneFragmentPacketSize).has_value());
}

SM_TEST(DcsctpRRSendQueueTest, AddAndGetSingleChunk) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, {1, 2, 4, 5, 6}));

  SM_EXPECT_FALSE(q.IsEmpty());
  std::optional<SendQueue::DataToSend> chunk_opt =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_opt.has_value());
  SM_EXPECT_TRUE(chunk_opt->data.is_beginning);
  SM_EXPECT_TRUE(chunk_opt->data.is_end);
}

SM_TEST(DcsctpRRSendQueueTest, CarveOutBeginningMiddleAndEnd) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(60);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  std::optional<SendQueue::DataToSend> chunk_beg =
      q.Produce(kNow, /*max_size=*/20);
  SM_ASSERT_TRUE(chunk_beg.has_value());
  SM_EXPECT_TRUE(chunk_beg->data.is_beginning);
  SM_EXPECT_FALSE(chunk_beg->data.is_end);

  std::optional<SendQueue::DataToSend> chunk_mid =
      q.Produce(kNow, /*max_size=*/20);
  SM_ASSERT_TRUE(chunk_mid.has_value());
  SM_EXPECT_FALSE(chunk_mid->data.is_beginning);
  SM_EXPECT_FALSE(chunk_mid->data.is_end);

  std::optional<SendQueue::DataToSend> chunk_end =
      q.Produce(kNow, /*max_size=*/20);
  SM_ASSERT_TRUE(chunk_end.has_value());
  SM_EXPECT_FALSE(chunk_end->data.is_beginning);
  SM_EXPECT_TRUE(chunk_end->data.is_end);

  SM_EXPECT_FALSE(q.Produce(kNow, kOneFragmentPacketSize).has_value());
}

SM_TEST(DcsctpRRSendQueueTest, GetChunksFromTwoMessages) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(60);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  q.Add(kNow, DcSctpMessage(StreamID(3), PPID(54), payload));

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  SM_EXPECT_EQ(chunk_one->data.ppid, kPPID);
  SM_EXPECT_TRUE(chunk_one->data.is_beginning);
  SM_EXPECT_TRUE(chunk_one->data.is_end);

  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_EQ(chunk_two->data.stream_id, StreamID(3));
  SM_EXPECT_EQ(chunk_two->data.ppid, PPID(54));
  SM_EXPECT_TRUE(chunk_two->data.is_beginning);
  SM_EXPECT_TRUE(chunk_two->data.is_end);
}

SM_TEST(DcsctpRRSendQueueTest, BufferBecomesFullAndEmptied) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(600);
  SM_EXPECT_LT(q.total_buffered_amount(), 1000u);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  SM_EXPECT_LT(q.total_buffered_amount(), 1000u);
  q.Add(kNow, DcSctpMessage(StreamID(3), PPID(54), payload));
  SM_EXPECT_GE(q.total_buffered_amount(), 1000u);
  // However, it's still possible to add messages. It's a soft limit, and it
  // might be necessary to forcefully add messages due to e.g. external
  // fragmentation.
  q.Add(kNow, DcSctpMessage(StreamID(5), PPID(55), payload));
  SM_EXPECT_GE(q.total_buffered_amount(), 1000u);

  std::optional<SendQueue::DataToSend> chunk_one = q.Produce(kNow, 1000);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  SM_EXPECT_EQ(chunk_one->data.ppid, kPPID);

  SM_EXPECT_GE(q.total_buffered_amount(), 1000u);

  std::optional<SendQueue::DataToSend> chunk_two = q.Produce(kNow, 1000);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_EQ(chunk_two->data.stream_id, StreamID(3));
  SM_EXPECT_EQ(chunk_two->data.ppid, PPID(54));

  SM_EXPECT_LT(q.total_buffered_amount(), 1000u);
  SM_EXPECT_FALSE(q.IsEmpty());

  std::optional<SendQueue::DataToSend> chunk_three = q.Produce(kNow, 1000);
  SM_ASSERT_TRUE(chunk_three.has_value());
  SM_EXPECT_EQ(chunk_three->data.stream_id, StreamID(5));
  SM_EXPECT_EQ(chunk_three->data.ppid, PPID(55));

  SM_EXPECT_LT(q.total_buffered_amount(), 1000u);
  SM_EXPECT_TRUE(q.IsEmpty());
}

SM_TEST(DcsctpRRSendQueueTest, DefaultsToOrderedSend) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(20);

  // Default is ordered
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_FALSE(chunk_one->data.is_unordered);

  // Explicitly unordered.
  SendOptions opts;
  opts.unordered = IsUnordered(true);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload), opts);
  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_TRUE(chunk_two->data.is_unordered);
}

SM_TEST(DcsctpRRSendQueueTest, ProduceWithLifetimeExpiry) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(20);

  // Default is no expiry
  Timestamp now = kNow;
  q.Add(now, DcSctpMessage(kStreamID, kPPID, payload));
  now += TimeDelta::Seconds(1000);
  SM_ASSERT_TRUE(q.Produce(now, kOneFragmentPacketSize));

  SendOptions expires_2_seconds;
  expires_2_seconds.lifetime = DurationMs(2000);

  // Add and consume within lifetime
  q.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);
  now += TimeDelta::Millis(2000);
  SM_ASSERT_TRUE(q.Produce(now, kOneFragmentPacketSize));

  // Add and consume just outside lifetime
  q.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);
  now += TimeDelta::Millis(2001);
  SM_ASSERT_FALSE(q.Produce(now, kOneFragmentPacketSize));

  // A long time after expiry
  q.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);
  now += TimeDelta::Seconds(1000);
  SM_ASSERT_FALSE(q.Produce(now, kOneFragmentPacketSize));

  // Expire one message, but produce the second that is not expired.
  q.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_2_seconds);

  SendOptions expires_4_seconds;
  expires_4_seconds.lifetime = DurationMs(4000);

  q.Add(now, DcSctpMessage(kStreamID, kPPID, payload), expires_4_seconds);
  now += TimeDelta::Millis(2001);

  SM_ASSERT_TRUE(q.Produce(now, kOneFragmentPacketSize));
  SM_ASSERT_FALSE(q.Produce(now, kOneFragmentPacketSize));
}

SM_TEST(DcsctpRRSendQueueTest, DiscardPartialPackets) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(120);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  q.Add(kNow, DcSctpMessage(StreamID(2), PPID(54), payload));

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_FALSE(chunk_one->data.is_end);
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  q.Discard(chunk_one->data.stream_id, chunk_one->message_id);

  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_FALSE(chunk_two->data.is_end);
  SM_EXPECT_EQ(chunk_two->data.stream_id, StreamID(2));

  std::optional<SendQueue::DataToSend> chunk_three =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_three.has_value());
  SM_EXPECT_TRUE(chunk_three->data.is_end);
  SM_EXPECT_EQ(chunk_three->data.stream_id, StreamID(2));
  SM_ASSERT_FALSE(q.Produce(kNow, kOneFragmentPacketSize));

  // Calling it again shouldn't cause issues.
  q.Discard(chunk_one->data.stream_id, chunk_one->message_id);
  SM_ASSERT_FALSE(q.Produce(kNow, kOneFragmentPacketSize));
}

SM_TEST(DcsctpRRSendQueueTest, PrepareResetStreamsDiscardsStream) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, {1, 2, 3}));
  q.Add(kNow, DcSctpMessage(StreamID(2), PPID(54), {1, 2, 3, 4, 5}));
  SM_EXPECT_EQ(q.total_buffered_amount(), 8u);

  q.PrepareResetStream(StreamID(1));
  SM_EXPECT_EQ(q.total_buffered_amount(), 5u);

  SM_EXPECT_THAT(q.GetStreamsReadyToBeReset(), UnorderedElementsAre(StreamID(1)));
  q.CommitResetStreams();
  q.PrepareResetStream(StreamID(2));
  SM_EXPECT_EQ(q.total_buffered_amount(), 0u);
}

SM_TEST(DcsctpRRSendQueueTest, PrepareResetStreamsNotPartialPackets) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(120);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  std::optional<SendQueue::DataToSend> chunk_one = q.Produce(kNow, 50);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  SM_EXPECT_EQ(q.total_buffered_amount(), 2 * payload.size() - 50);

  q.PrepareResetStream(StreamID(1));
  SM_EXPECT_EQ(q.total_buffered_amount(), payload.size() - 50);
}

SM_TEST(DcsctpRRSendQueueTest, EnqueuedItemsArePausedDuringStreamReset) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(50);

  q.PrepareResetStream(StreamID(1));
  SM_EXPECT_EQ(q.total_buffered_amount(), 0u);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  SM_EXPECT_EQ(q.total_buffered_amount(), payload.size());

  SM_EXPECT_FALSE(q.Produce(kNow, kOneFragmentPacketSize).has_value());

  SM_EXPECT_TRUE(q.HasStreamsReadyToBeReset());
  SM_EXPECT_THAT(q.GetStreamsReadyToBeReset(), UnorderedElementsAre(StreamID(1)));

  SM_EXPECT_FALSE(q.Produce(kNow, kOneFragmentPacketSize).has_value());

  q.CommitResetStreams();
  SM_EXPECT_EQ(q.total_buffered_amount(), payload.size());

  std::optional<SendQueue::DataToSend> chunk_one = q.Produce(kNow, 50);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  SM_EXPECT_EQ(q.total_buffered_amount(), 0u);
}

SM_TEST(DcsctpRRSendQueueTest, PausedStreamsStillSendPartialMessagesUntilEnd) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  constexpr size_t kPayloadSize = 100;
  constexpr size_t kFragmentSize = 50;
  std::vector<uint8_t> payload(kPayloadSize);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kFragmentSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  SM_EXPECT_EQ(q.total_buffered_amount(), 2 * kPayloadSize - kFragmentSize);

  // This will stop the second message from being sent.
  q.PrepareResetStream(StreamID(1));
  SM_EXPECT_EQ(q.total_buffered_amount(), 1 * kPayloadSize - kFragmentSize);

  // Should still produce fragments until end of message.
  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kFragmentSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_EQ(chunk_two->data.stream_id, kStreamID);
  SM_EXPECT_EQ(q.total_buffered_amount(), 0ul);

  // But shouldn't produce any more messages as the stream is paused.
  SM_EXPECT_FALSE(q.Produce(kNow, kFragmentSize).has_value());
}

SM_TEST(DcsctpRRSendQueueTest, CommittingResetsSSN) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(50);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.ssn, SSN(0));

  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_EQ(chunk_two->data.ssn, SSN(1));

  q.PrepareResetStream(StreamID(1));

  // Buffered
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  SM_EXPECT_TRUE(q.HasStreamsReadyToBeReset());
  SM_EXPECT_THAT(q.GetStreamsReadyToBeReset(), UnorderedElementsAre(StreamID(1)));
  q.CommitResetStreams();

  std::optional<SendQueue::DataToSend> chunk_three =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_three.has_value());
  SM_EXPECT_EQ(chunk_three->data.ssn, SSN(0));
}

SM_TEST(DcsctpRRSendQueueTest, CommittingDoesNotResetMessageId) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(50);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.ssn, SSN(0));
  SM_EXPECT_EQ(chunk1.message_id, OutgoingMessageId(0));

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.ssn, SSN(1));
  SM_EXPECT_EQ(chunk2.message_id, OutgoingMessageId(1));

  q.PrepareResetStream(kStreamID);
  SM_EXPECT_THAT(q.GetStreamsReadyToBeReset(), UnorderedElementsAre(kStreamID));
  q.CommitResetStreams();

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.ssn, SSN(0));
  SM_EXPECT_EQ(chunk3.message_id, OutgoingMessageId(2));
}

SM_TEST(DcsctpRRSendQueueTest, CommittingResetsSSNForPausedStreamsOnly) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(50);

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  q.Add(kNow, DcSctpMessage(StreamID(3), kPPID, payload));

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, StreamID(1));
  SM_EXPECT_EQ(chunk_one->data.ssn, SSN(0));

  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_EQ(chunk_two->data.stream_id, StreamID(3));
  SM_EXPECT_EQ(chunk_two->data.ssn, SSN(0));

  q.PrepareResetStream(StreamID(3));

  // Send two more messages - SID 3 will buffer, SID 1 will send.
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  q.Add(kNow, DcSctpMessage(StreamID(3), kPPID, payload));

  SM_EXPECT_TRUE(q.HasStreamsReadyToBeReset());
  SM_EXPECT_THAT(q.GetStreamsReadyToBeReset(), UnorderedElementsAre(StreamID(3)));

  q.CommitResetStreams();

  std::optional<SendQueue::DataToSend> chunk_three =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_three.has_value());
  SM_EXPECT_EQ(chunk_three->data.stream_id, StreamID(1));
  SM_EXPECT_EQ(chunk_three->data.ssn, SSN(1));

  std::optional<SendQueue::DataToSend> chunk_four =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_four.has_value());
  SM_EXPECT_EQ(chunk_four->data.stream_id, StreamID(3));
  SM_EXPECT_EQ(chunk_four->data.ssn, SSN(0));
}

SM_TEST(DcsctpRRSendQueueTest, RollBackResumesSSN) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(50);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.ssn, SSN(0));

  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_two.has_value());
  SM_EXPECT_EQ(chunk_two->data.ssn, SSN(1));

  q.PrepareResetStream(StreamID(1));

  // Buffered
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));

  SM_EXPECT_TRUE(q.HasStreamsReadyToBeReset());
  SM_EXPECT_THAT(q.GetStreamsReadyToBeReset(), UnorderedElementsAre(StreamID(1)));
  q.RollbackResetStreams();

  std::optional<SendQueue::DataToSend> chunk_three =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_three.has_value());
  SM_EXPECT_EQ(chunk_three->data.ssn, SSN(2));
}

SM_TEST(DcsctpRRSendQueueTest, ReturnsFragmentsForOneMessageBeforeMovingToNext) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(200);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, payload));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.stream_id, StreamID(2));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk4.data.stream_id, StreamID(2));
}

SM_TEST(DcsctpRRSendQueueTest, ReturnsAlsoSmallFragmentsBeforeMovingToNext) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(kTwoFragmentPacketSize);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));
  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, payload));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(kOneFragmentPacketSize));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk2.data.payload,
              SizeIs(kTwoFragmentPacketSize - kOneFragmentPacketSize));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.stream_id, StreamID(2));
  SM_EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk4.data.stream_id, StreamID(2));
  SM_EXPECT_THAT(chunk4.data.payload,
              SizeIs(kTwoFragmentPacketSize - kOneFragmentPacketSize));
}

SM_TEST(DcsctpRRSendQueueTest, WillCycleInRoundRobinFashionBetweenStreams) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(2)));
  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(3)));
  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(4)));
  q.Add(kNow, DcSctpMessage(StreamID(3), kPPID, std::vector<uint8_t>(5)));
  q.Add(kNow, DcSctpMessage(StreamID(3), kPPID, std::vector<uint8_t>(6)));
  q.Add(kNow, DcSctpMessage(StreamID(4), kPPID, std::vector<uint8_t>(7)));
  q.Add(kNow, DcSctpMessage(StreamID(4), kPPID, std::vector<uint8_t>(8)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(2));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(3));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.stream_id, StreamID(3));
  SM_EXPECT_THAT(chunk3.data.payload, SizeIs(5));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk4.data.stream_id, StreamID(4));
  SM_EXPECT_THAT(chunk4.data.payload, SizeIs(7));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk5,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk5.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk5.data.payload, SizeIs(2));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk6,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk6.data.stream_id, StreamID(2));
  SM_EXPECT_THAT(chunk6.data.payload, SizeIs(4));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk7,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk7.data.stream_id, StreamID(3));
  SM_EXPECT_THAT(chunk7.data.payload, SizeIs(6));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk8,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk8.data.stream_id, StreamID(4));
  SM_EXPECT_THAT(chunk8.data.payload, SizeIs(8));
}

SM_TEST(DcsctpRRSendQueueTest, DoesntTriggerOnBufferedAmountLowWhenSetToZero) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);
  q.SetBufferedAmountLowThreshold(StreamID(1), 0u);
}

SM_TEST(DcsctpRRSendQueueTest, TriggersOnBufferedAmountAtZeroLowWhenSent) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 1u);

  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(1));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 0u);
}

SM_TEST(DcsctpRRSendQueueTest, WillRetriggerOnBufferedAmountLowIfAddingMore) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));

  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(1));

  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1)));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 1u);

  // Should now trigger again, as buffer_amount went above the threshold.
  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(1));
}

SM_TEST(DcsctpRRSendQueueTest, OnlyTriggersWhenTransitioningFromAboveToBelowOrEqual) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.SetBufferedAmountLowThreshold(StreamID(1), 1000);

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(10)));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 10u);

  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(10));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 0u);

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(20)));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 20u);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(20));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 0u);
}

SM_TEST(DcsctpRRSendQueueTest, WillTriggerOnBufferedAmountLowSetAboveZero) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);

  q.SetBufferedAmountLowThreshold(StreamID(1), 700);

  std::vector<uint8_t> payload(1000);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, payload));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(kOneFragmentPacketSize));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 900u);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(kOneFragmentPacketSize));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 800u);

  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 700u);

  // Doesn't trigger when reducing even further.
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 600u);
}

SM_TEST(DcsctpRRSendQueueTest, WillRetriggerOnBufferedAmountLowSetAboveZero) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);

  q.SetBufferedAmountLowThreshold(StreamID(1), 700);

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(1000)));

  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, 400));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(400));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 600u);

  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);
  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(200)));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 800u);

  // Will trigger again, as it went above the limit.
  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, 200));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(200));
  SM_EXPECT_EQ(q.buffered_amount(StreamID(1)), 600u);
}

SM_TEST(DcsctpRRSendQueueTest, TriggersOnBufferedAmountLowOnThresholdChanged) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(100)));

  // Modifying the threshold, still under buffered_amount, should not trigger.
  q.SetBufferedAmountLowThreshold(StreamID(1), 50);
  q.SetBufferedAmountLowThreshold(StreamID(1), 99);

  // When the threshold reaches buffered_amount, it will trigger.
  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));
  q.SetBufferedAmountLowThreshold(StreamID(1), 100);

  // But not when it's set low again.
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);
  q.SetBufferedAmountLowThreshold(StreamID(1), 50);

  // But it will trigger when it overshoots.
  EXPECT_CALL(cb, OnBufferedAmountLow(StreamID(1)));
  q.SetBufferedAmountLowThreshold(StreamID(1), 150);

  // But not when it's set low again.
  EXPECT_CALL(cb, OnBufferedAmountLow).Times(0);
  q.SetBufferedAmountLowThreshold(StreamID(1), 0);
}

SM_TEST(DcsctpRRSendQueueTest, OnTotalBufferedAmountLowDoesNotTriggerOnBufferFillingUp) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  EXPECT_CALL(cb, OnTotalBufferedAmountLow).Times(0);
  std::vector<uint8_t> payload(kBufferedAmountLowThreshold - 1);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  SM_EXPECT_EQ(q.total_buffered_amount(), payload.size());

  // Will not trigger if going above but never below.
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID,
                            std::vector<uint8_t>(kOneFragmentPacketSize)));
}

SM_TEST(DcsctpRRSendQueueTest, TriggersOnTotalBufferedAmountLowWhenCrossing) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  EXPECT_CALL(cb, OnTotalBufferedAmountLow).Times(0);
  std::vector<uint8_t> payload(kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload));
  SM_EXPECT_EQ(q.total_buffered_amount(), payload.size());

  // Reaches it.
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, std::vector<uint8_t>(1)));

  // Drain it a bit - will trigger.
  EXPECT_CALL(cb, OnTotalBufferedAmountLow).Times(1);
  std::optional<SendQueue::DataToSend> chunk_two =
      q.Produce(kNow, kOneFragmentPacketSize);
}

SM_TEST(DcsctpRRSendQueueTest, WillStayInAStreamAsLongAsThatMessageIsSending) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.Add(kNow, DcSctpMessage(StreamID(5), kPPID, std::vector<uint8_t>(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk1.data.stream_id, StreamID(5));
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(1));

  // Next, it should pick a different stream.

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID,
                            std::vector<uint8_t>(kOneFragmentPacketSize * 2)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk2.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(kOneFragmentPacketSize));

  // It should still stay on the Stream1 now, even if might be tempted to switch
  // to this stream, as it's the stream following 5.
  q.Add(kNow, DcSctpMessage(StreamID(6), kPPID, std::vector<uint8_t>(1)));

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk3.data.stream_id, StreamID(1));
  SM_EXPECT_THAT(chunk3.data.payload, SizeIs(kOneFragmentPacketSize));

  // After stream id 1 is complete, it's time to do stream 6.
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk4,
                              q.Produce(kNow, kOneFragmentPacketSize));
  SM_EXPECT_EQ(chunk4.data.stream_id, StreamID(6));
  SM_EXPECT_THAT(chunk4.data.payload, SizeIs(1));

  SM_EXPECT_FALSE(q.Produce(kNow, kOneFragmentPacketSize).has_value());
}

SM_TEST(DcsctpRRSendQueueTest, StreamsHaveInitialPriority) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  SM_EXPECT_EQ(q.GetStreamPriority(StreamID(1)), kDefaultPriority);

  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(40)));
  SM_EXPECT_EQ(q.GetStreamPriority(StreamID(2)), kDefaultPriority);
}

SM_TEST(DcsctpRRSendQueueTest, CanChangeStreamPriority) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.SetStreamPriority(StreamID(1), StreamPriority(42));
  SM_EXPECT_EQ(q.GetStreamPriority(StreamID(1)), StreamPriority(42));

  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(40)));
  q.SetStreamPriority(StreamID(2), StreamPriority(42));
  SM_EXPECT_EQ(q.GetStreamPriority(StreamID(2)), StreamPriority(42));
}

SM_TEST(DcsctpRRSendQueueTest, WillHandoverPriority) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.SetStreamPriority(StreamID(1), StreamPriority(42));

  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(40)));
  q.SetStreamPriority(StreamID(2), StreamPriority(42));

  DcSctpSocketHandoverState state;
  q.AddHandoverState(Timestamp::Micros(0), state);

  RRSendQueue q2("log: ", &cb, kMtu, kDefaultPriority,
                 kBufferedAmountLowThreshold);
  q2.RestoreFromState(Timestamp::Micros(0), state);
  SM_EXPECT_EQ(q2.GetStreamPriority(StreamID(1)), StreamPriority(42));
  SM_EXPECT_EQ(q2.GetStreamPriority(StreamID(2)), StreamPriority(42));
}

SM_TEST(DcsctpRRSendQueueTest, RestoreFromHandoverStateQueuedMessages) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  DcSctpSocketHandoverState state;
  state.tx.next_outgoing_message_id = 2;
  state.tx.streams = {{.id = kStreamID.value(),
                       .next_ssn = 11,
                       .next_unordered_mid = 33,
                       .next_ordered_mid = 22,
                       .priority = 42}};
  state.tx.queued_messages = {{.stream_id = kStreamID.value(),
                               .ppid = kPPID.value(),
                               .payload = {1, 2, 3},
                               .expires_in_ms = std::nullopt,
                               .max_retransmissions = 0,
                               .unordered = false,
                               .lifecycle_id = 0,
                               .message_id = 0,
                               .remaining_offset = 1,
                               .mid = 21,
                               .ssn = 10,
                               .fsn = 1},
                              {.stream_id = kStreamID.value(),
                               .ppid = kPPID.value(),
                               .payload = {4, 5, 6},
                               .expires_in_ms = std::nullopt,
                               .max_retransmissions = 0,
                               .unordered = false,
                               .lifecycle_id = 0,
                               .message_id = 1,
                               .remaining_offset = 0,
                               .mid = std::nullopt,
                               .ssn = std::nullopt,
                               .fsn = 0}};

  RRSendQueue q("log: ", &cb, kMtu, kDefaultPriority,
                kBufferedAmountLowThreshold);
  q.RestoreFromState(Timestamp::Micros(0), state);
  SM_EXPECT_EQ(q.GetStreamPriority(kStreamID), StreamPriority(42));
  // Message 1 has payload of 3 bytes, but remaining_offset=1, so 2 bytes left.
  // Message 2 has payload of 3 bytes, with remaining_offset=0, so 3 bytes left.
  // Total buffered amount is the sum of remaining bytes to be sent.
  SM_EXPECT_EQ(q.buffered_amount(kStreamID), 2u + 3u);
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1,
                              q.Produce(kNow, 100));
  SM_EXPECT_EQ(chunk1.data.stream_id, kStreamID);
  SM_EXPECT_EQ(chunk1.data.ppid, kPPID);
  SM_EXPECT_EQ(chunk1.message_id, OutgoingMessageId(0));
  SM_EXPECT_THAT(chunk1.data.payload, ElementsAre(2, 3));
  SM_EXPECT_EQ(chunk1.data.mid, MID(21));
  SM_EXPECT_EQ(chunk1.data.ssn, SSN(10));
  SM_EXPECT_EQ(chunk1.data.fsn, FSN(1));
  SM_EXPECT_FALSE(chunk1.data.is_beginning);
  SM_EXPECT_TRUE(chunk1.data.is_end);

  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q.Produce(kNow, 100));
  SM_EXPECT_EQ(chunk2.data.stream_id, kStreamID);
  SM_EXPECT_EQ(chunk2.data.ppid, kPPID);
  SM_EXPECT_EQ(chunk2.message_id, OutgoingMessageId(1));
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(3));
  SM_EXPECT_EQ(chunk2.data.payload[0], 4);
  SM_EXPECT_EQ(chunk2.data.mid, MID(22));
  SM_EXPECT_EQ(chunk2.data.ssn, SSN(11));
  SM_EXPECT_EQ(chunk2.data.fsn, FSN(0));
  SM_EXPECT_TRUE(chunk2.data.is_beginning);
  SM_EXPECT_TRUE(chunk2.data.is_end);
  SM_EXPECT_EQ(q.Produce(kNow, 1), std::nullopt);
}

SM_TEST(DcsctpRRSendQueueTest, HandoverPartiallySentMessage) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);

  // Message 1: Will be partially sent.
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, {1, 2, 3, 4, 5, 6}));
  // Message 2: Will not be sent.
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, {7, 8, 9}));

  // Produce first fragment of message 1.
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk1, q.Produce(kNow, 4));
  SM_EXPECT_EQ(chunk1.message_id, OutgoingMessageId(0));
  SM_EXPECT_TRUE(chunk1.data.is_beginning);
  SM_EXPECT_FALSE(chunk1.data.is_end);
  SM_EXPECT_THAT(chunk1.data.payload, SizeIs(4));
  SM_EXPECT_EQ(chunk1.data.payload[0], 1);
  SM_EXPECT_EQ(q.buffered_amount(kStreamID),
            5u);  // 2 bytes from msg1, 3 from msg2

  DcSctpSocketHandoverState state;
  q.AddHandoverState(Timestamp::Micros(0), state);

  RRSendQueue q2("log: ", &cb, kMtu, kDefaultPriority,
                 kBufferedAmountLowThreshold);
  q2.RestoreFromState(Timestamp::Micros(0), state);

  SM_EXPECT_EQ(q2.buffered_amount(kStreamID), 5u);

  // Produce second fragment of message 1 from restored queue.
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk2,
                              q2.Produce(kNow, 4));
  SM_EXPECT_EQ(chunk2.message_id, OutgoingMessageId(0));
  SM_EXPECT_FALSE(chunk2.data.is_beginning);
  SM_EXPECT_TRUE(chunk2.data.is_end);
  SM_EXPECT_THAT(chunk2.data.payload, SizeIs(2));
  SM_EXPECT_EQ(chunk2.data.payload[0], 5);
  SM_EXPECT_EQ(q2.buffered_amount(kStreamID), 3u);

  // Produce message 2 from restored queue.
  ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk3,
                              q2.Produce(kNow, 4));
  SM_EXPECT_EQ(chunk3.message_id, OutgoingMessageId(1));
  SM_EXPECT_TRUE(chunk3.data.is_beginning);
  SM_EXPECT_TRUE(chunk3.data.is_end);
  SM_EXPECT_THAT(chunk3.data.payload, SizeIs(3));
  SM_EXPECT_EQ(chunk3.data.payload[0], 7);
  SM_EXPECT_EQ(q2.buffered_amount(kStreamID), 0u);

  SM_EXPECT_EQ(q2.Produce(kNow, 1), std::nullopt);
}

SM_TEST(DcsctpRRSendQueueTest, WillSendMessagesByPrio) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  q.EnableMessageInterleaving(true);
  q.SetStreamPriority(StreamID(1), StreamPriority(10));
  q.SetStreamPriority(StreamID(2), StreamPriority(20));
  q.SetStreamPriority(StreamID(3), StreamPriority(30));

  q.Add(kNow, DcSctpMessage(StreamID(1), kPPID, std::vector<uint8_t>(40)));
  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, std::vector<uint8_t>(20)));
  q.Add(kNow, DcSctpMessage(StreamID(3), kPPID, std::vector<uint8_t>(10)));
  std::vector<uint16_t> expected_streams = {3, 2, 2, 1, 1, 1, 1};

  for (uint16_t stream_num : expected_streams) {
    ASSERT_HAS_VALUE_AND_ASSIGN(SendQueue::DataToSend chunk,
                                q.Produce(kNow, 10));
    SM_EXPECT_EQ(chunk.data.stream_id, StreamID(stream_num));
  }
  SM_EXPECT_FALSE(q.Produce(kNow, 1).has_value());
}

SM_TEST(DcsctpRRSendQueueTest, WillSendLifecycleExpireWhenExpiredInSendQueue) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(kOneFragmentPacketSize);
  q.Add(kNow, DcSctpMessage(StreamID(2), kPPID, payload),
        SendOptions{.lifetime = DurationMs(1000),
                    .lifecycle_id = LifecycleId(1)});

  EXPECT_CALL(cb, OnLifecycleMessageExpired(LifecycleId(1),
                                            /*maybe_delivered=*/false));
  EXPECT_CALL(cb, OnLifecycleEnd(LifecycleId(1)));
  SM_EXPECT_FALSE(q.Produce(kNow + TimeDelta::Millis(1001), kOneFragmentPacketSize)
                   .has_value());
}

SM_TEST(DcsctpRRSendQueueTest, WillSendLifecycleExpireWhenDiscardingDuringPause) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(120);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload),
        SendOptions{.lifecycle_id = LifecycleId(1)});
  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload),
        SendOptions{.lifecycle_id = LifecycleId(2)});

  std::optional<SendQueue::DataToSend> chunk_one = q.Produce(kNow, 50);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  SM_EXPECT_EQ(q.total_buffered_amount(), 2 * payload.size() - 50);

  EXPECT_CALL(cb, OnLifecycleMessageExpired(LifecycleId(2),
                                            /*maybe_delivered=*/false));
  EXPECT_CALL(cb, OnLifecycleEnd(LifecycleId(2)));
  q.PrepareResetStream(StreamID(1));
  SM_EXPECT_EQ(q.total_buffered_amount(), payload.size() - 50);
}

SM_TEST(DcsctpRRSendQueueTest, WillSendLifecycleExpireWhenDiscardingExplicitly) {
  NiceMock<MockDcSctpSocketCallbacks> cb;
  RRSendQueue q("", &cb, kMtu, kDefaultPriority, kBufferedAmountLowThreshold);
  std::vector<uint8_t> payload(kOneFragmentPacketSize + 20);

  q.Add(kNow, DcSctpMessage(kStreamID, kPPID, payload),
        SendOptions{.lifecycle_id = LifecycleId(1)});

  std::optional<SendQueue::DataToSend> chunk_one =
      q.Produce(kNow, kOneFragmentPacketSize);
  SM_ASSERT_TRUE(chunk_one.has_value());
  SM_EXPECT_FALSE(chunk_one->data.is_end);
  SM_EXPECT_EQ(chunk_one->data.stream_id, kStreamID);
  EXPECT_CALL(cb, OnLifecycleMessageExpired(LifecycleId(1),
                                            /*maybe_delivered=*/false));
  EXPECT_CALL(cb, OnLifecycleEnd(LifecycleId(1)));
  q.Discard(chunk_one->data.stream_id, chunk_one->message_id);
}
}  // namespace
}  // namespace sm::dcsctp
