/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/mock_dcsctp_socket_callbacks.h (compare/webrtc/reference/net/dcsctp/socket/mock_dcsctp_socket_callbacks.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#ifndef SM_DCSCTP_SOCKET_MOCK_DCSCTP_SOCKET_CALLBACKS_H_
#define SM_DCSCTP_SOCKET_MOCK_DCSCTP_SOCKET_CALLBACKS_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/core/random.h"
#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/timeout.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/timer/fake_timeout.h"
#include "gmock_lite.h"

namespace sm::dcsctp {

namespace internal {
// It can be argued if a mocked random number generator should be deterministic
// or if it should be have as a "real" random number generator. In this
// implementation, each instantiation of `MockDcSctpSocketCallbacks` will have
// their `GetRandomInt` return different sequences, but each instantiation will
// always generate the same sequence of random numbers. This to make it easier
// to compare logs from tests, but still to let e.g. two different sockets (used
// in the same test) get different random numbers, so that they don't start e.g.
// on the same sequence number. While that isn't an issue in the protocol, it
// just makes debugging harder as the two sockets would look exactly the same.
//
// In a real implementation of `DcSctpSocketCallbacks` the random number
// generator backing `GetRandomInt` should be seeded externally and correctly.
inline int GetUniqueSeed() {
  static int seed = 0;
  return ++seed;
}
}  // namespace internal

class MockDcSctpSocketCallbacks : public DcSctpSocketCallbacks {
 public:
  explicit MockDcSctpSocketCallbacks(std::string_view name = "")
      : log_prefix_(name.empty() ? "" : std::string(name) + ": "),
        random_(internal::GetUniqueSeed()),
        timeout_manager_([this]() { return now_; }) {
    ON_CALL(*this, SendPacketWithStatus)
        .WillByDefault([this](std::span<const uint8_t> data) {
          sent_packets_.emplace_back(
              std::vector<uint8_t>(data.begin(), data.end()));
          return SendPacketStatus::kSuccess;
        });
    ON_CALL(*this, OnMessageReceived)
        .WillByDefault([this](DcSctpMessage message) {
          received_messages_.emplace_back(std::move(message));
        });

    ON_CALL(*this, OnError)
        .WillByDefault([this](ErrorKind error, std::string_view message) {
          SM_DCSCTP_LOG(kWarning)
              << log_prefix_ << "Socket error: " << ToString(error) << "; "
              << message;
        });
    ON_CALL(*this, OnAborted)
        .WillByDefault([this](ErrorKind error, std::string_view message) {
          SM_DCSCTP_LOG(kWarning)
              << log_prefix_ << "Socket abort: " << ToString(error) << "; "
              << message;
        });
    ON_CALL(*this, Now).WillByDefault([this]() { return now_; });
  }

  mutable ::testing::internal::FunctionMocker<SendPacketStatus(std::span<const uint8_t>)> gmock_SendPacketWithStatus{"SendPacketWithStatus"};
  SendPacketStatus SendPacketWithStatus(std::span<const uint8_t> data) override { return gmock_SendPacketWithStatus.Invoke(std::forward<std::span<const uint8_t>>(data)); }

  std::unique_ptr<Timeout> CreateTimeout(
      DelayPrecision /* precision */) override {
    // The fake timeout manager does not implement |precision|.
    return timeout_manager_.CreateTimeout();
  }

  mutable ::testing::internal::FunctionMocker<Timestamp()> gmock_Now{"Now"};
  Timestamp Now() override { return gmock_Now.Invoke(); }
  uint32_t GetRandomInt(uint32_t low, uint32_t high) override {
    return static_cast<uint32_t>(random_.UniformInt(low, high));
  }

  mutable ::testing::internal::FunctionMocker<void(DcSctpMessage)> gmock_OnMessageReceived{"OnMessageReceived"};
  void OnMessageReceived(DcSctpMessage message) override { return gmock_OnMessageReceived.Invoke(std::forward<DcSctpMessage>(message)); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_OnMessageReady{"OnMessageReady"};
  void OnMessageReady() override { return gmock_OnMessageReady.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void(ErrorKind, std::string_view)> gmock_OnError{"OnError"};
  void OnError(ErrorKind error, std::string_view message) override { return gmock_OnError.Invoke(std::forward<ErrorKind>(error), std::forward<std::string_view>(message)); }
  mutable ::testing::internal::FunctionMocker<void(ErrorKind, std::string_view)> gmock_OnAborted{"OnAborted"};
  void OnAborted(ErrorKind error, std::string_view message) override { return gmock_OnAborted.Invoke(std::forward<ErrorKind>(error), std::forward<std::string_view>(message)); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_OnConnected{"OnConnected"};
  void OnConnected() override { return gmock_OnConnected.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_OnClosed{"OnClosed"};
  void OnClosed() override { return gmock_OnClosed.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_OnConnectionRestarted{"OnConnectionRestarted"};
  void OnConnectionRestarted() override { return gmock_OnConnectionRestarted.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void(std::span<const StreamID>, std::string_view)> gmock_OnStreamsResetFailed{"OnStreamsResetFailed"};
  void OnStreamsResetFailed(std::span<const StreamID> outgoing_streams, std::string_view reason) override { return gmock_OnStreamsResetFailed.Invoke(std::forward<std::span<const StreamID>>(outgoing_streams), std::forward<std::string_view>(reason)); }
  mutable ::testing::internal::FunctionMocker<void(std::span<const StreamID>)> gmock_OnStreamsResetPerformed{"OnStreamsResetPerformed"};
  void OnStreamsResetPerformed(std::span<const StreamID> outgoing_streams) override { return gmock_OnStreamsResetPerformed.Invoke(std::forward<std::span<const StreamID>>(outgoing_streams)); }
  mutable ::testing::internal::FunctionMocker<void(std::span<const StreamID>)> gmock_OnIncomingStreamsReset{"OnIncomingStreamsReset"};
  void OnIncomingStreamsReset(std::span<const StreamID> incoming_streams) override { return gmock_OnIncomingStreamsReset.Invoke(std::forward<std::span<const StreamID>>(incoming_streams)); }
  mutable ::testing::internal::FunctionMocker<void(StreamID)> gmock_OnBufferedAmountLow{"OnBufferedAmountLow"};
  void OnBufferedAmountLow(StreamID stream_id) override { return gmock_OnBufferedAmountLow.Invoke(std::forward<StreamID>(stream_id)); }
  mutable ::testing::internal::FunctionMocker<void()> gmock_OnTotalBufferedAmountLow{"OnTotalBufferedAmountLow"};
  void OnTotalBufferedAmountLow() override { return gmock_OnTotalBufferedAmountLow.Invoke(); }
  mutable ::testing::internal::FunctionMocker<void(LifecycleId, bool)> gmock_OnLifecycleMessageExpired{"OnLifecycleMessageExpired"};
  void OnLifecycleMessageExpired(LifecycleId lifecycle_id, bool maybe_delivered) override { return gmock_OnLifecycleMessageExpired.Invoke(std::forward<LifecycleId>(lifecycle_id), std::forward<bool>(maybe_delivered)); }
  mutable ::testing::internal::FunctionMocker<void(LifecycleId)> gmock_OnLifecycleMessageFullySent{"OnLifecycleMessageFullySent"};
  void OnLifecycleMessageFullySent(LifecycleId lifecycle_id) override { return gmock_OnLifecycleMessageFullySent.Invoke(std::forward<LifecycleId>(lifecycle_id)); }
  mutable ::testing::internal::FunctionMocker<void(LifecycleId)> gmock_OnLifecycleMessageDelivered{"OnLifecycleMessageDelivered"};
  void OnLifecycleMessageDelivered(LifecycleId lifecycle_id) override { return gmock_OnLifecycleMessageDelivered.Invoke(std::forward<LifecycleId>(lifecycle_id)); }
  mutable ::testing::internal::FunctionMocker<void(LifecycleId)> gmock_OnLifecycleEnd{"OnLifecycleEnd"};
  void OnLifecycleEnd(LifecycleId lifecycle_id) override { return gmock_OnLifecycleEnd.Invoke(std::forward<LifecycleId>(lifecycle_id)); }

  bool HasPacket() const { return !sent_packets_.empty(); }

  std::vector<uint8_t> ConsumeSentPacket() {
    if (sent_packets_.empty()) {
      return {};
    }
    std::vector<uint8_t> ret = std::move(sent_packets_.front());
    sent_packets_.pop_front();
    return ret;
  }
  std::optional<DcSctpMessage> ConsumeReceivedMessage() {
    if (received_messages_.empty()) {
      return std::nullopt;
    }
    DcSctpMessage ret = std::move(received_messages_.front());
    received_messages_.pop_front();
    return ret;
  }

  void AdvanceTime(TimeDelta duration) { now_ = now_ + duration; }
  void SetTime(Timestamp now) { now_ = now; }

  std::optional<TimeoutID> GetNextExpiredTimeout() {
    return timeout_manager_.GetNextExpiredTimeout();
  }

  TimeDelta GetTimeToNextTimeout() const {
    return timeout_manager_.GetTimeToNextTimeout();
  }

 private:
  const std::string log_prefix_;
  Timestamp now_ = Timestamp::Micros(0);
  ::sm::Random random_;
  FakeTimeoutManager timeout_manager_;
  std::deque<std::vector<uint8_t>> sent_packets_;
  std::deque<DcSctpMessage> received_messages_;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_SOCKET_MOCK_DCSCTP_SOCKET_CALLBACKS_H_
