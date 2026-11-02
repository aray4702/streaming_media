/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/callback_deferrer.cc (compare/webrtc/reference/net/dcsctp/socket/callback_deferrer.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/socket/callback_deferrer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/timeout.h"
#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {

void CallbackDeferrer::Prepare() {
  SM_DCSCTP_DCHECK(!prepared_);
  prepared_ = true;
}

void CallbackDeferrer::TriggerDeferred() {
  // Need to swap here. The client may call into the library from within a
  // callback, and that might result in adding new callbacks to this instance,
  // and the vector can't be modified while iterated on.
  SM_DCSCTP_DCHECK(prepared_);
  prepared_ = false;
  if (deferred_.empty()) {
    return;
  }
  std::vector<std::pair<Callback, CallbackData>> deferred;
  // Reserve a small buffer to prevent too much reallocation on growth.
  deferred.reserve(8);
  deferred.swap(deferred_);
  for (auto& [cb, data] : deferred) {
    cb(std::move(data), underlying_);
  }
}

SendPacketStatus CallbackDeferrer::SendPacketWithStatus(
    std::span<const uint8_t> data) {
  // Will not be deferred - call directly.
  return underlying_.SendPacketWithStatus(data);
}

std::unique_ptr<Timeout> CallbackDeferrer::CreateTimeout(
    DelayPrecision precision) {
  // Will not be deferred - call directly.
  return underlying_.CreateTimeout(precision);
}

TimeMs CallbackDeferrer::TimeMillis() {
  // This should not be called by the library - it's migrated to `Now()`.
  SM_DCSCTP_DCHECK(false);
  // Will not be deferred - call directly.
  return underlying_.TimeMillis();
}

uint32_t CallbackDeferrer::GetRandomInt(uint32_t low, uint32_t high) {
  // Will not be deferred - call directly.
  return underlying_.GetRandomInt(low, high);
}

void CallbackDeferrer::OnMessageReceived(DcSctpMessage message) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        return cb.OnMessageReceived(std::get<DcSctpMessage>(std::move(data)));
      },
      std::move(message));
}

void CallbackDeferrer::OnMessageReady() {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        return cb.OnMessageReady();
      },
      std::monostate{});
}

void CallbackDeferrer::OnError(ErrorKind error, std::string_view message) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        Error error = std::get<Error>(std::move(data));
        return cb.OnError(error.error, error.message);
      },
      Error{.error = error, .message = std::string(message)});
}

void CallbackDeferrer::OnAborted(ErrorKind error, std::string_view message) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        Error error = std::get<Error>(std::move(data));
        return cb.OnAborted(error.error, error.message);
      },
      Error{.error = error, .message = std::string(message)});
}

void CallbackDeferrer::OnConnected() {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData /* data */, DcSctpSocketCallbacks& cb) {
        return cb.OnConnected();
      },
      std::monostate{});
}

void CallbackDeferrer::OnClosed() {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData /* data */, DcSctpSocketCallbacks& cb) {
        return cb.OnClosed();
      },
      std::monostate{});
}

void CallbackDeferrer::OnConnectionRestarted() {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData /* data */, DcSctpSocketCallbacks& cb) {
        return cb.OnConnectionRestarted();
      },
      std::monostate{});
}

void CallbackDeferrer::OnStreamsResetFailed(
    std::span<const StreamID> outgoing_streams,
    std::string_view reason) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        StreamReset stream_reset = std::get<StreamReset>(std::move(data));
        return cb.OnStreamsResetFailed(stream_reset.streams,
                                       stream_reset.message);
      },
      StreamReset{.streams = {outgoing_streams.begin(), outgoing_streams.end()},
                  .message = std::string(reason)});
}

void CallbackDeferrer::OnStreamsResetPerformed(
    std::span<const StreamID> outgoing_streams) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        StreamReset stream_reset = std::get<StreamReset>(std::move(data));
        return cb.OnStreamsResetPerformed(stream_reset.streams);
      },
      StreamReset{
          .streams = {outgoing_streams.begin(), outgoing_streams.end()}});
}

void CallbackDeferrer::OnIncomingStreamsReset(
    std::span<const StreamID> incoming_streams) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        StreamReset stream_reset = std::get<StreamReset>(std::move(data));
        return cb.OnIncomingStreamsReset(stream_reset.streams);
      },
      StreamReset{
          .streams = {incoming_streams.begin(), incoming_streams.end()}});
}

void CallbackDeferrer::OnBufferedAmountLow(StreamID stream_id) {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData data, DcSctpSocketCallbacks& cb) {
        return cb.OnBufferedAmountLow(std::get<StreamID>(std::move(data)));
      },
      stream_id);
}

void CallbackDeferrer::OnTotalBufferedAmountLow() {
  SM_DCSCTP_DCHECK(prepared_);
  deferred_.emplace_back(
      +[](CallbackData /* data */, DcSctpSocketCallbacks& cb) {
        return cb.OnTotalBufferedAmountLow();
      },
      std::monostate{});
}

void CallbackDeferrer::OnLifecycleMessageExpired(LifecycleId lifecycle_id,
                                                 bool maybe_delivered) {
  // Will not be deferred - call directly.
  underlying_.OnLifecycleMessageExpired(lifecycle_id, maybe_delivered);
}
void CallbackDeferrer::OnLifecycleMessageFullySent(LifecycleId lifecycle_id) {
  // Will not be deferred - call directly.
  underlying_.OnLifecycleMessageFullySent(lifecycle_id);
}
void CallbackDeferrer::OnLifecycleMessageDelivered(LifecycleId lifecycle_id) {
  // Will not be deferred - call directly.
  underlying_.OnLifecycleMessageDelivered(lifecycle_id);
}
void CallbackDeferrer::OnLifecycleEnd(LifecycleId lifecycle_id) {
  // Will not be deferred - call directly.
  underlying_.OnLifecycleEnd(lifecycle_id);
}
}  // namespace sm::dcsctp
