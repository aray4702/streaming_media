/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/mock_send_queue.h (compare/webrtc/reference/net/dcsctp/tx/mock_send_queue.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#ifndef SM_DCSCTP_TX_MOCK_SEND_QUEUE_H_
#define SM_DCSCTP_TX_MOCK_SEND_QUEUE_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/public/dcsctp_handover_state.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/tx/send_queue.h"
#include "gmock_lite.h"

namespace sm::dcsctp {

class MockSendQueue : public SendQueue {
 public:
  mutable ::testing::internal::FunctionMocker<std::optional<SendQueue::DataToSend>(Timestamp, size_t)> gmock_Produce{"Produce"};
  std::optional<SendQueue::DataToSend> Produce(Timestamp now, size_t max_size) override { return gmock_Produce.Invoke(std::forward<Timestamp>(now), std::forward<size_t>(max_size)); }
  mutable ::testing::internal::FunctionMocker<bool(StreamID, OutgoingMessageId)> gmock_Discard{"Discard"};
  bool Discard(StreamID stream_id, OutgoingMessageId message_id) override { return gmock_Discard.Invoke(std::forward<StreamID>(stream_id), std::forward<OutgoingMessageId>(message_id)); }
  mutable ::testing::internal::FunctionMocker<void(StreamID)> gmock_PrepareResetStream{"PrepareResetStream"};
  void PrepareResetStream(StreamID stream_id) override { return gmock_PrepareResetStream.Invoke(std::forward<StreamID>(stream_id)); }
  mutable ::testing::internal::FunctionMocker<bool()> gmock_HasStreamsReadyToBeReset{"HasStreamsReadyToBeReset"};
  bool HasStreamsReadyToBeReset() const override { return gmock_HasStreamsReadyToBeReset.Invoke(); }
  mutable ::testing::internal::FunctionMocker<std::vector<StreamID>()> gmock_GetStreamsReadyToBeReset{"GetStreamsReadyToBeReset"};
  std::vector<StreamID> GetStreamsReadyToBeReset() override { return gmock_GetStreamsReadyToBeReset.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_CommitResetStreams{"CommitResetStreams"};
  void CommitResetStreams() override { return gmock_CommitResetStreams.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_RollbackResetStreams{"RollbackResetStreams"};
  void RollbackResetStreams() override { return gmock_RollbackResetStreams.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_Reset{"Reset"};
  void Reset() override { return gmock_Reset.Invoke(); }
  mutable ::testing::internal::FunctionMocker<size_t(StreamID)> gmock_buffered_amount{"buffered_amount"};
  size_t buffered_amount(StreamID stream_id) const override { return gmock_buffered_amount.Invoke(std::forward<StreamID>(stream_id)); }
  mutable ::testing::internal::FunctionMocker<size_t()> gmock_total_buffered_amount{"total_buffered_amount"};
  size_t total_buffered_amount() const override { return gmock_total_buffered_amount.Invoke(); }
  mutable ::testing::internal::FunctionMocker<size_t(StreamID)> gmock_buffered_amount_low_threshold{"buffered_amount_low_threshold"};
  size_t buffered_amount_low_threshold(StreamID stream_id) const override { return gmock_buffered_amount_low_threshold.Invoke(std::forward<StreamID>(stream_id)); }
  mutable ::testing::internal::FunctionMocker<void(StreamID, size_t)> gmock_SetBufferedAmountLowThreshold{"SetBufferedAmountLowThreshold"};
  void SetBufferedAmountLowThreshold(StreamID stream_id, size_t bytes) override { return gmock_SetBufferedAmountLowThreshold.Invoke(std::forward<StreamID>(stream_id), std::forward<size_t>(bytes)); }
  mutable ::testing::internal::FunctionMocker<void(bool)> gmock_EnableMessageInterleaving{"EnableMessageInterleaving"};
  void EnableMessageInterleaving(bool enabled) override { return gmock_EnableMessageInterleaving.Invoke(std::forward<bool>(enabled)); }
  mutable ::testing::internal::FunctionMocker<void(Timestamp, DcSctpSocketHandoverState&)> gmock_AddHandoverState{"AddHandoverState"};
  void AddHandoverState(Timestamp now, DcSctpSocketHandoverState& state) const override { return gmock_AddHandoverState.Invoke(std::forward<Timestamp>(now), std::forward<DcSctpSocketHandoverState&>(state)); }
  mutable ::testing::internal::FunctionMocker<void(Timestamp, const DcSctpSocketHandoverState&)> gmock_RestoreFromState{"RestoreFromState"};
  void RestoreFromState(Timestamp now, const DcSctpSocketHandoverState& state) override { return gmock_RestoreFromState.Invoke(std::forward<Timestamp>(now), std::forward<const DcSctpSocketHandoverState&>(state)); }
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_TX_MOCK_SEND_QUEUE_H_
