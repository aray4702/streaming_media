/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/transmission_control_block_test.cc (compare/webrtc/reference/net/dcsctp/socket/transmission_control_block_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/socket/transmission_control_block.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/socket/capabilities.h"
#include "socket/mock_dcsctp_socket_callbacks.h"
#include "sm/dcsctp/socket/packet_sender.h"
#include "sm/dcsctp/timer/timer.h"
#include "tx/mock_send_queue.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::Return;
using ::testing::StrictMock;

constexpr VerificationTag kMyVerificationTag = VerificationTag(123);
constexpr VerificationTag kPeerVerificationTag = VerificationTag(456);
constexpr TSN kMyInitialTsn = TSN(10);
constexpr TSN kPeerInitialTsn = TSN(1000);
constexpr size_t kArwnd = 65536;
constexpr TieTag kTieTag = TieTag(12345678);

class TransmissionControlBlockTest : public testing::Test {
 protected:
  TransmissionControlBlockTest()
      : sender_(callbacks_, on_send_fn_.AsStdFunction()),
        timer_manager_([this](DelayPrecision precision) {
          return callbacks_.CreateTimeout(precision);
        }) {}

  DcSctpOptions options_;
  Capabilities capabilities_;
  StrictMock<MockDcSctpSocketCallbacks> callbacks_;
  StrictMock<MockSendQueue> send_queue_;
  testing::MockFunction<void(std::span<const uint8_t>, SendPacketStatus)>
      on_send_fn_;
  testing::MockFunction<bool()> on_connection_established;
  PacketSender sender_;
  TimerManager timer_manager_;
};

SM_TEST_F(TransmissionControlBlockTest, LogsBasicInfoInToString) {
  EXPECT_CALL(send_queue_, EnableMessageInterleaving);

  capabilities_.negotiated_maximum_incoming_streams = 1000;
  capabilities_.negotiated_maximum_outgoing_streams = 2000;
  TransmissionControlBlock tcb(
      timer_manager_, "log: ", options_, capabilities_, callbacks_, send_queue_,
      kMyVerificationTag, kMyInitialTsn, kPeerVerificationTag, kPeerInitialTsn,
      kArwnd, kTieTag, sender_, on_connection_established.AsStdFunction());

  SM_EXPECT_EQ(tcb.ToString(),
            "verification_tag=000001c8, last_cumulative_ack=999, capabilities= "
            "max_in=1000 max_out=2000");
}

SM_TEST_F(TransmissionControlBlockTest, LogsAllCapabilitiesInToSring) {
  EXPECT_CALL(send_queue_, EnableMessageInterleaving);

  capabilities_.negotiated_maximum_incoming_streams = 1000;
  capabilities_.negotiated_maximum_outgoing_streams = 2000;
  capabilities_.message_interleaving = true;
  capabilities_.partial_reliability = true;
  capabilities_.zero_checksum_method =
      ZeroChecksumAlternateErrorDetectionMethod::LowerLayerDtls();
  capabilities_.reconfig = true;

  TransmissionControlBlock tcb(
      timer_manager_, "log: ", options_, capabilities_, callbacks_, send_queue_,
      kMyVerificationTag, kMyInitialTsn, kPeerVerificationTag, kPeerInitialTsn,
      kArwnd, kTieTag, sender_, on_connection_established.AsStdFunction());

  SM_EXPECT_EQ(
      tcb.ToString(),
      "verification_tag=000001c8, last_cumulative_ack=999, "
      "capabilities=PR,IL,Reconfig,ZeroChecksum, max_in=1000 max_out=2000");
}

SM_TEST_F(TransmissionControlBlockTest, IsInitiallyHandoverReady) {
  EXPECT_CALL(send_queue_, EnableMessageInterleaving);
  EXPECT_CALL(send_queue_, HasStreamsReadyToBeReset).WillOnce(Return(false));

  TransmissionControlBlock tcb(
      timer_manager_, "log: ", options_, capabilities_, callbacks_, send_queue_,
      kMyVerificationTag, kMyInitialTsn, kPeerVerificationTag, kPeerInitialTsn,
      kArwnd, kTieTag, sender_, on_connection_established.AsStdFunction());

  SM_EXPECT_TRUE(tcb.GetHandoverReadiness().IsReady());
}
}  // namespace
}  // namespace sm::dcsctp
