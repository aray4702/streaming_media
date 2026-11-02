/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/heartbeat_handler_test.cc (compare/webrtc/reference/net/dcsctp/socket/heartbeat_handler_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/socket/heartbeat_handler.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/packet/chunk/heartbeat_ack_chunk.h"
#include "sm/dcsctp/packet/chunk/heartbeat_request_chunk.h"
#include "sm/dcsctp/packet/parameter/heartbeat_info_parameter.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/sctp_packet.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/types.h"
#include "socket/mock_context.h"
#include "socket/mock_dcsctp_socket_callbacks.h"
#include "testing_macros.h"
#include "sm/dcsctp/timer/timer.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SizeIs;
using ::sm::TimeDelta;

constexpr DurationMs kHeartbeatInterval = DurationMs(30'000);

DcSctpOptions MakeOptions(DurationMs heartbeat_interval) {
  DcSctpOptions options;
  options.heartbeat_interval_include_rtt = false;
  options.heartbeat_interval = heartbeat_interval;
  options.zero_checksum_alternate_error_detection_method =
      ZeroChecksumAlternateErrorDetectionMethod::None();
  return options;
}

class HeartbeatHandlerTestBase : public testing::Test {
 protected:
  explicit HeartbeatHandlerTestBase(DurationMs heartbeat_interval)
      : options_(MakeOptions(heartbeat_interval)),
        context_(&callbacks_),
        timer_manager_([this](DelayPrecision precision) {
          return callbacks_.CreateTimeout(precision);
        }),
        handler_("log: ", options_, &context_, &timer_manager_) {}

  void AdvanceTime(TimeDelta duration) {
    callbacks_.AdvanceTime(duration);
    for (;;) {
      std::optional<TimeoutID> timeout_id = callbacks_.GetNextExpiredTimeout();
      if (!timeout_id.has_value()) {
        break;
      }
      timer_manager_.HandleTimeout(*timeout_id);
    }
  }

  const DcSctpOptions options_;
  NiceMock<MockDcSctpSocketCallbacks> callbacks_;
  NiceMock<MockContext> context_;
  TimerManager timer_manager_;
  HeartbeatHandler handler_;
};

class HeartbeatHandlerTest : public HeartbeatHandlerTestBase {
 protected:
  HeartbeatHandlerTest() : HeartbeatHandlerTestBase(kHeartbeatInterval) {}
};

class DisabledHeartbeatHandlerTest : public HeartbeatHandlerTestBase {
 protected:
  DisabledHeartbeatHandlerTest() : HeartbeatHandlerTestBase(DurationMs(0)) {}
};

SM_TEST_F(HeartbeatHandlerTest, HasRunningHeartbeatIntervalTimer) {
  AdvanceTime(options_.heartbeat_interval.ToTimeDelta());

  // Validate that a heartbeat request was sent.
  std::vector<uint8_t> payload = callbacks_.ConsumeSentPacket();
  ASSERT_HAS_VALUE_AND_ASSIGN(SctpPacket packet,
                              SctpPacket::Parse(payload, options_));
  SM_ASSERT_THAT(packet.descriptors(), SizeIs(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      HeartbeatRequestChunk request,
      HeartbeatRequestChunk::Parse(packet.descriptors()[0].data));

  SM_EXPECT_TRUE(request.info().has_value());
}

SM_TEST_F(HeartbeatHandlerTest, RepliesToHeartbeatRequests) {
  uint8_t info_data[] = {1, 2, 3, 4, 5};
  HeartbeatRequestChunk request(
      Parameters::Builder().Add(HeartbeatInfoParameter(info_data)).Build());

  handler_.HandleHeartbeatRequest(std::move(request));

  std::vector<uint8_t> payload = callbacks_.ConsumeSentPacket();
  ASSERT_HAS_VALUE_AND_ASSIGN(SctpPacket packet,
                              SctpPacket::Parse(payload, options_));
  SM_ASSERT_THAT(packet.descriptors(), SizeIs(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      HeartbeatAckChunk response,
      HeartbeatAckChunk::Parse(packet.descriptors()[0].data));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      HeartbeatInfoParameter param,
      response.parameters().get<HeartbeatInfoParameter>());

  SM_EXPECT_THAT(param.info(), ElementsAre(1, 2, 3, 4, 5));
}

SM_TEST_F(HeartbeatHandlerTest, SendsHeartbeatRequestsOnIdleChannel) {
  AdvanceTime(options_.heartbeat_interval.ToTimeDelta());

  // Grab the request, and make a response.
  std::vector<uint8_t> payload = callbacks_.ConsumeSentPacket();
  ASSERT_HAS_VALUE_AND_ASSIGN(SctpPacket packet,
                              SctpPacket::Parse(payload, options_));
  SM_ASSERT_THAT(packet.descriptors(), SizeIs(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      HeartbeatRequestChunk req,
      HeartbeatRequestChunk::Parse(packet.descriptors()[0].data));

  HeartbeatAckChunk ack(std::move(req).extract_parameters());

  // Respond a while later. This RTT will be measured by the handler
  constexpr TimeDelta rtt = TimeDelta::Millis(313);

  EXPECT_CALL(context_, ObserveRTT(rtt)).Times(1);

  callbacks_.AdvanceTime(rtt);
  handler_.HandleHeartbeatAck(std::move(ack));
}

SM_TEST_F(HeartbeatHandlerTest, DoesntObserveInvalidHeartbeats) {
  AdvanceTime(options_.heartbeat_interval.ToTimeDelta());

  // Grab the request, and make a response.
  std::vector<uint8_t> payload = callbacks_.ConsumeSentPacket();
  ASSERT_HAS_VALUE_AND_ASSIGN(SctpPacket packet,
                              SctpPacket::Parse(payload, options_));
  SM_ASSERT_THAT(packet.descriptors(), SizeIs(1));

  ASSERT_HAS_VALUE_AND_ASSIGN(
      HeartbeatRequestChunk req,
      HeartbeatRequestChunk::Parse(packet.descriptors()[0].data));

  HeartbeatAckChunk ack(std::move(req).extract_parameters());

  EXPECT_CALL(context_, ObserveRTT).Times(0);

  // Go backwards in time - which make the HEARTBEAT-ACK have an invalid
  // timestamp in it, as it will be in the future.
  callbacks_.AdvanceTime(TimeDelta::Millis(-100));

  handler_.HandleHeartbeatAck(std::move(ack));
}

SM_TEST_F(HeartbeatHandlerTest, IncreasesErrorIfNotAckedInTime) {
  TimeDelta rto = TimeDelta::Millis(105);
  EXPECT_CALL(context_, current_rto).WillOnce(Return(rto));
  AdvanceTime(options_.heartbeat_interval.ToTimeDelta());

  // Validate that a request was sent.
  SM_EXPECT_THAT(callbacks_.ConsumeSentPacket(), Not(IsEmpty()));

  EXPECT_CALL(context_, IncrementTxErrorCounter).Times(1);
  AdvanceTime(rto);
}

SM_TEST_F(DisabledHeartbeatHandlerTest, IsReallyDisabled) {
  AdvanceTime(options_.heartbeat_interval.ToTimeDelta());

  // Validate that a request was NOT sent.
  SM_EXPECT_THAT(callbacks_.ConsumeSentPacket(), IsEmpty());
}

}  // namespace
}  // namespace sm::dcsctp
