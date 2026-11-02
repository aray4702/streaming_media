/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/invalid_stream_identifier_cause.cc (compare/webrtc/reference/net/dcsctp/packet/error_cause/invalid_stream_identifier_cause.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/error_cause/invalid_stream_identifier_cause.h"

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

// https://tools.ietf.org/html/rfc4960#section-3.3.10.1

//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |     Cause Code=1              |      Cause Length=8           |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |        Stream Identifier      |         (Reserved)            |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

std::optional<InvalidStreamIdentifierCause> InvalidStreamIdentifierCause::Parse(
    std::span<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }

  StreamID stream_id(reader->Load16<4>());
  return InvalidStreamIdentifierCause(stream_id);
}

void InvalidStreamIdentifierCause::SerializeTo(
    std::vector<uint8_t>& out) const {
  BoundedByteWriter<kHeaderSize> writer = AllocateTLV(out);

  writer.Store16<4>(*stream_id_);
}

std::string InvalidStreamIdentifierCause::ToString() const {
  StringBuilder sb;
  sb << "Invalid Stream Identifier, stream_id=" << *stream_id_;
  return sb.Release();
}

}  // namespace sm::dcsctp
