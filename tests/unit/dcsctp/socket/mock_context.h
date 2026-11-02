/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/mock_context.h (compare/webrtc/reference/net/dcsctp/socket/mock_context.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#ifndef SM_DCSCTP_SOCKET_MOCK_CONTEXT_H_
#define SM_DCSCTP_SOCKET_MOCK_CONTEXT_H_


#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/sctp_packet.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/socket/context.h"
#include "socket/mock_dcsctp_socket_callbacks.h"
#include "gmock_lite.h"

namespace sm::dcsctp {

class MockContext : public Context {
 public:
  static constexpr TSN MyInitialTsn() { return TSN(990); }
  static constexpr TSN PeerInitialTsn() { return TSN(10); }
  static constexpr VerificationTag PeerVerificationTag() {
    return VerificationTag(0x01234567);
  }

  explicit MockContext(MockDcSctpSocketCallbacks* callbacks)
      : callbacks_(*callbacks) {
    ON_CALL(*this, is_connection_established)
        .WillByDefault(testing::Return(true));
    ON_CALL(*this, my_initial_tsn)
        .WillByDefault(testing::Return(MyInitialTsn()));
    ON_CALL(*this, peer_initial_tsn)
        .WillByDefault(testing::Return(PeerInitialTsn()));
    ON_CALL(*this, callbacks).WillByDefault(testing::ReturnRef(callbacks_));
    ON_CALL(*this, current_rto)
        .WillByDefault(testing::Return(TimeDelta::Millis(123)));
    ON_CALL(*this, Send).WillByDefault([this](SctpPacket::Builder& builder) {
      callbacks_.SendPacketWithStatus(builder.Build());
    });
  }

  mutable ::testing::internal::FunctionMocker<bool()> gmock_is_connection_established{"is_connection_established"};
  bool is_connection_established() const override { return gmock_is_connection_established.Invoke(); }
  mutable ::testing::internal::FunctionMocker<TSN()> gmock_my_initial_tsn{"my_initial_tsn"};
  TSN my_initial_tsn() const override { return gmock_my_initial_tsn.Invoke(); }
  mutable ::testing::internal::FunctionMocker<TSN()> gmock_peer_initial_tsn{"peer_initial_tsn"};
  TSN peer_initial_tsn() const override { return gmock_peer_initial_tsn.Invoke(); }
  mutable ::testing::internal::FunctionMocker<DcSctpSocketCallbacks&()> gmock_callbacks{"callbacks"};
  DcSctpSocketCallbacks& callbacks() const override { return gmock_callbacks.Invoke(); }

  mutable ::testing::internal::FunctionMocker<void(TimeDelta)> gmock_ObserveRTT{"ObserveRTT"};
  void ObserveRTT(TimeDelta rtt) override { return gmock_ObserveRTT.Invoke(std::forward<TimeDelta>(rtt)); }
  mutable ::testing::internal::FunctionMocker<TimeDelta()> gmock_current_rto{"current_rto"};
  TimeDelta current_rto() const override { return gmock_current_rto.Invoke(); }
  mutable ::testing::internal::FunctionMocker<bool(std::string_view)> gmock_IncrementTxErrorCounter{"IncrementTxErrorCounter"};
  bool IncrementTxErrorCounter(std::string_view reason) override { return gmock_IncrementTxErrorCounter.Invoke(std::forward<std::string_view>(reason)); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_ClearTxErrorCounter{"ClearTxErrorCounter"};
  void ClearTxErrorCounter() override { return gmock_ClearTxErrorCounter.Invoke(); }
  mutable ::testing::internal::FunctionMocker<bool()> gmock_HasTooManyTxErrors{"HasTooManyTxErrors"};
  bool HasTooManyTxErrors() const override { return gmock_HasTooManyTxErrors.Invoke(); }
  SctpPacket::Builder PacketBuilder() const override {
    return SctpPacket::Builder(PeerVerificationTag(), options_);
  }
  mutable ::testing::internal::FunctionMocker<void(SctpPacket::Builder &)> gmock_Send{"Send"};
  void Send(SctpPacket::Builder & builder) override { return gmock_Send.Invoke(std::forward<SctpPacket::Builder &>(builder)); }

  DcSctpOptions options_;
  MockDcSctpSocketCallbacks& callbacks_;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_SOCKET_MOCK_CONTEXT_H_
