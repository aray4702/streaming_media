/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/socket/heartbeat_handler.h (compare/webrtc/reference/net/dcsctp/socket/heartbeat_handler.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_SOCKET_HEARTBEAT_HANDLER_H_
#define SM_DCSCTP_SOCKET_HEARTBEAT_HANDLER_H_


#include <memory>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/packet/chunk/heartbeat_ack_chunk.h"
#include "sm/dcsctp/packet/chunk/heartbeat_request_chunk.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/socket/context.h"
#include "sm/dcsctp/timer/timer.h"

namespace sm::dcsctp {

// HeartbeatHandler handles all logic around sending heartbeats and receiving
// the responses, as well as receiving incoming heartbeat requests.
//
// Heartbeats are sent on idle connections to ensure that the connection is
// still healthy and to measure the RTT. If a number of heartbeats time out,
// the connection will eventually be closed.
class HeartbeatHandler {
 public:
  HeartbeatHandler(std::string_view log_prefix,
                   const DcSctpOptions& options,
                   Context* context,
                   TimerManager* timer_manager);

  // Called when the heartbeat interval timer should be restarted. This is
  // generally done every time data is sent, which makes the timer expire when
  // the connection is idle.
  void RestartTimer();

  // Called on received HeartbeatRequestChunk chunks.
  void HandleHeartbeatRequest(HeartbeatRequestChunk chunk);

  // Called on received HeartbeatRequestChunk chunks.
  void HandleHeartbeatAck(HeartbeatAckChunk chunk);

 private:
  TimeDelta OnIntervalTimerExpiry();
  TimeDelta OnTimeoutTimerExpiry();

  const std::string_view log_prefix_;
  Context* ctx_;
  TimerManager* timer_manager_;
  // The time for a connection to be idle before a heartbeat is sent.
  const TimeDelta interval_duration_;
  // Adding RTT to the duration will add some jitter, which is good in
  // production, but less good in unit tests, which is why it can be disabled.
  const bool interval_duration_should_include_rtt_;
  const std::unique_ptr<Timer> interval_timer_;
  const std::unique_ptr<Timer> timeout_timer_;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_SOCKET_HEARTBEAT_HANDLER_H_
