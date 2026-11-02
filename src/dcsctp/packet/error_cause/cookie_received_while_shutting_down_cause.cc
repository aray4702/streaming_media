/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/cookie_received_while_shutting_down_cause.cc (compare/webrtc/reference/net/dcsctp/packet/error_cause/cookie_received_while_shutting_down_cause.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/error_cause/cookie_received_while_shutting_down_cause.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10.10

//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |     Cause Code=10              |      Cause Length=4          |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

std::optional<CookieReceivedWhileShuttingDownCause>
CookieReceivedWhileShuttingDownCause::Parse(std::span<const uint8_t> data) {
  if (!ParseTLV(data).has_value()) {
    return std::nullopt;
  }
  return CookieReceivedWhileShuttingDownCause();
}

void CookieReceivedWhileShuttingDownCause::SerializeTo(
    std::vector<uint8_t>& out) const {
  AllocateTLV(out);
}

std::string CookieReceivedWhileShuttingDownCause::ToString() const {
  return "Cookie Received While Shutting Down";
}
}  // namespace sm::dcsctp
