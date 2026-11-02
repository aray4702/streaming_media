/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/error_cause.h (compare/webrtc/reference/net/dcsctp/packet/error_cause/error_cause.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_ERROR_CAUSE_ERROR_CAUSE_H_
#define SM_DCSCTP_PACKET_ERROR_CAUSE_ERROR_CAUSE_H_


#include <string>

#include "sm/dcsctp/packet/parameter/parameter.h"

namespace sm::dcsctp {

// Converts the Error Causes in `parameters` to a human readable string,
// to be used in error reporting and logging.
std::string ErrorCausesToString(const Parameters& parameters);

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_ERROR_CAUSE_ERROR_CAUSE_H_
