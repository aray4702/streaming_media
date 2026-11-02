/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/tlv_trait.cc (compare/webrtc/reference/net/dcsctp/packet/tlv_trait.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/tlv_trait.h"

#include <cstddef>

#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {
namespace tlv_trait_impl {
void ReportInvalidSize(size_t actual_size, size_t expected_size) {
  SM_DCSCTP_DLOG << "Invalid size (" << actual_size
                       << ", expected minimum " << expected_size << " bytes)";
}

void ReportInvalidType(int actual_type, int expected_type) {
  SM_DCSCTP_DLOG << "Invalid type (" << actual_type << ", expected "
                       << expected_type << ")";
}

void ReportInvalidFixedLengthField(size_t value, size_t expected) {
  SM_DCSCTP_DLOG << "Invalid length field (" << value << ", expected "
                       << expected << " bytes)";
}

void ReportInvalidVariableLengthField(size_t value, size_t available) {
  SM_DCSCTP_DLOG << "Invalid length field (" << value << ", available "
                       << available << " bytes)";
}

void ReportInvalidPadding(size_t padding_bytes) {
  SM_DCSCTP_DLOG << "Invalid padding (" << padding_bytes << " bytes)";
}

void ReportInvalidLengthMultiple(size_t length, size_t alignment) {
  SM_DCSCTP_DLOG << "Invalid length field (" << length
                       << ", expected an even multiple of " << alignment
                       << " bytes)";
}
}  // namespace tlv_trait_impl
}  // namespace sm::dcsctp
