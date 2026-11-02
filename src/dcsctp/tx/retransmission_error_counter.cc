/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/retransmission_error_counter.cc (compare/webrtc/reference/net/dcsctp/tx/retransmission_error_counter.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/tx/retransmission_error_counter.h"

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {
bool RetransmissionErrorCounter::Increment(std::string_view reason) {
  ++counter_;
  if (limit_.has_value() && counter_ > limit_.value()) {
    SM_DCSCTP_DLOG << log_prefix_ << reason
                      << ", too many retransmissions, counter=" << counter_;
    return false;
  }

  SM_DCSCTP_DLOG << log_prefix_ << reason << ", new counter=" << counter_
                       << ", max=" << limit_.value_or(-1);
  return true;
}

void RetransmissionErrorCounter::Clear() {
  if (counter_ > 0) {
    SM_DCSCTP_DLOG << log_prefix_
                         << "recovered from counter=" << counter_;
    counter_ = 0;
  }
}

}  // namespace sm::dcsctp
