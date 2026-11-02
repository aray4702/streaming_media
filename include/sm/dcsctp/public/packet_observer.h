/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/public/packet_observer.h (compare/webrtc/reference/net/dcsctp/public/packet_observer.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PUBLIC_PACKET_OBSERVER_H_
#define SM_DCSCTP_PUBLIC_PACKET_OBSERVER_H_

#include <cstdint>
#include <span>

#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {

// A PacketObserver can be attached to a socket and will be called for
// all sent and received packets.
class PacketObserver {
 public:
  virtual ~PacketObserver() = default;
  // Called when a packet is sent, with the current time (in milliseconds) as
  // `now`, and the packet payload as `payload`.
  virtual void OnSentPacket(TimeMs now, std::span<const uint8_t> payload) = 0;

  // Called when a packet is received, with the current time (in milliseconds)
  // as `now`, and the packet payload as `payload`.
  virtual void OnReceivedPacket(TimeMs now,
                                std::span<const uint8_t> payload) = 0;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PUBLIC_PACKET_OBSERVER_H_
