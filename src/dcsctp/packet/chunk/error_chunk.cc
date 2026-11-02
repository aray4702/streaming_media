/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/error_chunk.cc (compare/webrtc/reference/net/dcsctp/packet/chunk/error_chunk.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/chunk/error_chunk.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "sm/dcsctp/packet/parameter/parameter.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10

//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |   Type = 9    | Chunk  Flags  |           Length              |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  \                                                               \
//  /                    one or more Error Causes                   /
//  \                                                               \
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

std::optional<ErrorChunk> ErrorChunk::Parse(std::span<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }
  std::optional<Parameters> error_causes =
      Parameters::Parse(reader->variable_data());
  if (!error_causes.has_value()) {
    return std::nullopt;
  }
  return ErrorChunk(*std::move(error_causes));
}

void ErrorChunk::SerializeTo(std::vector<uint8_t>& out) const {
  std::span<const uint8_t> error_causes = error_causes_.data();
  BoundedByteWriter<kHeaderSize> writer = AllocateTLV(out, error_causes.size());
  writer.CopyToVariableData(error_causes);
}

std::string ErrorChunk::ToString() const {
  return "ERROR";
}
}  // namespace sm::dcsctp
