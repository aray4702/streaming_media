/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/state_cookie_parameter.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/state_cookie_parameter.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/parameter/state_cookie_parameter.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.3.1

std::optional<StateCookieParameter> StateCookieParameter::Parse(
    std::span<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }
  return StateCookieParameter(reader->variable_data());
}

void StateCookieParameter::SerializeTo(std::vector<uint8_t>& out) const {
  BoundedByteWriter<kHeaderSize> writer = AllocateTLV(out, data_.size());
  writer.CopyToVariableData(data_);
}

std::string StateCookieParameter::ToString() const {
  StringBuilder sb;
  sb << "State Cookie parameter (cookie_length=" << data_.size() << ")";
  return sb.Release();
}

}  // namespace sm::dcsctp
