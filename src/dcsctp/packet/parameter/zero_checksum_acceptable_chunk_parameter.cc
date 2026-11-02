/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.cc (compare/webrtc/reference/net/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {

// https://www.ietf.org/archive/id/draft-tuexen-tsvwg-sctp-zero-checksum-00.html#section-3

//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   Type = 0x8001 (suggested)   |          Length = 8           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           Error Detection Method Identifier (EDMID)           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

std::optional<ZeroChecksumAcceptableChunkParameter>
ZeroChecksumAcceptableChunkParameter::Parse(std::span<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }

  ZeroChecksumAlternateErrorDetectionMethod method(reader->Load32<4>());
  if (method == ZeroChecksumAlternateErrorDetectionMethod::None()) {
    return std::nullopt;
  }
  return ZeroChecksumAcceptableChunkParameter(method);
}

void ZeroChecksumAcceptableChunkParameter::SerializeTo(
    std::vector<uint8_t>& out) const {
  BoundedByteWriter<kHeaderSize> writer = AllocateTLV(out);
  writer.Store32<4>(*error_detection_method_);
}

std::string ZeroChecksumAcceptableChunkParameter::ToString() const {
  StringBuilder sb;
  sb << "Zero Checksum Acceptable (" << *error_detection_method_ << ")";
  return sb.Release();
}
}  // namespace sm::dcsctp
