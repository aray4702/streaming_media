/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/unrecognized_chunk_type_cause.cc (compare/webrtc/reference/net/dcsctp/packet/error_cause/unrecognized_chunk_type_cause.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/error_cause/unrecognized_chunk_type_cause.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10.6

//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |     Cause Code=6              |      Cause Length             |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  /                  Unrecognized Chunk                           /
//  \                                                               \
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

std::optional<UnrecognizedChunkTypeCause> UnrecognizedChunkTypeCause::Parse(
    std::span<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }
  std::vector<uint8_t> unrecognized_chunk(reader->variable_data().begin(),
                                          reader->variable_data().end());
  return UnrecognizedChunkTypeCause(std::move(unrecognized_chunk));
}

void UnrecognizedChunkTypeCause::SerializeTo(std::vector<uint8_t>& out) const {
  BoundedByteWriter<kHeaderSize> writer =
      AllocateTLV(out, unrecognized_chunk_.size());
  writer.CopyToVariableData(unrecognized_chunk_);
}

std::string UnrecognizedChunkTypeCause::ToString() const {
  StringBuilder sb;
  sb << "Unrecognized Chunk Type, chunk_type=";
  if (!unrecognized_chunk_.empty()) {
    sb << static_cast<int>(unrecognized_chunk_[0]);
  } else {
    sb << "<missing>";
  }
  return sb.Release();
}

}  // namespace sm::dcsctp
