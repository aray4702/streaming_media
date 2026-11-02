/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/common/math.h (compare/webrtc/reference/net/dcsctp/common/math.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_COMMON_MATH_H_
#define SM_DCSCTP_COMMON_MATH_H_

namespace sm::dcsctp {

// Rounds up `val` to the nearest value that is divisible by four. Frequently
// used to e.g. pad chunks or parameters to an even 32-bit offset.
template <typename IntType>
IntType RoundUpTo4(IntType val) {
  return (val + 3) & ~3;
}

// Similarly, rounds down `val` to the nearest value that is divisible by four.
template <typename IntType>
IntType RoundDownTo4(IntType val) {
  return val & ~3;
}

// Returns true if `val` is divisible by four.
template <typename IntType>
bool IsDivisibleBy4(IntType val) {
  return (val & 3) == 0;
}

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_COMMON_MATH_H_
