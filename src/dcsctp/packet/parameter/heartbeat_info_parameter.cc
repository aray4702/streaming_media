/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/heartbeat_info_parameter.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/heartbeat_info_parameter.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/parameter/heartbeat_info_parameter.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.5

//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |   Type = 4    | Chunk  Flags  |      Heartbeat Length         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  \                                                               \
//  /            Heartbeat Information TLV (Variable-Length)        /
//  \                                                               \
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

std::optional<HeartbeatInfoParameter> HeartbeatInfoParameter::Parse(
    std::span<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }
  return HeartbeatInfoParameter(reader->variable_data());
}

void HeartbeatInfoParameter::SerializeTo(std::vector<uint8_t>& out) const {
  BoundedByteWriter<kHeaderSize> writer = AllocateTLV(out, info_.size());
  writer.CopyToVariableData(info_);
}

std::string HeartbeatInfoParameter::ToString() const {
  StringBuilder sb;
  sb << "Heartbeat Info parameter (info_length=" << info_.size() << ")";
  return sb.Release();
}

}  // namespace sm::dcsctp
