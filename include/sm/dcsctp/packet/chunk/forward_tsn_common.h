/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/forward_tsn_common.h (compare/webrtc/reference/net/dcsctp/packet/chunk/forward_tsn_common.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CHUNK_FORWARD_TSN_COMMON_H_
#define SM_DCSCTP_PACKET_CHUNK_FORWARD_TSN_COMMON_H_

#include <span>
#include <utility>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {

// Base class for both ForwardTsnChunk and IForwardTsnChunk
class AnyForwardTsnChunk : public Chunk {
 public:
  struct SkippedStream {
    SkippedStream(StreamID stream_id, SSN ssn)
        : stream_id(stream_id), ssn(ssn), unordered(false), mid(0) {}
    SkippedStream(IsUnordered unordered, StreamID stream_id, MID mid)
        : stream_id(stream_id), ssn(0), unordered(unordered), mid(mid) {}

    StreamID stream_id;

    // Set for FORWARD_TSN
    SSN ssn;

    // Set for I-FORWARD_TSN
    IsUnordered unordered;
    MID mid;

    bool operator==(const SkippedStream& other) const {
      return stream_id == other.stream_id && ssn == other.ssn &&
             unordered == other.unordered && mid == other.mid;
    }
  };

  AnyForwardTsnChunk(TSN new_cumulative_tsn,
                     std::vector<SkippedStream> skipped_streams)
      : new_cumulative_tsn_(new_cumulative_tsn),
        skipped_streams_(std::move(skipped_streams)) {}

  TSN new_cumulative_tsn() const { return new_cumulative_tsn_; }

  std::span<const SkippedStream> skipped_streams() const {
    return skipped_streams_;
  }

 private:
  TSN new_cumulative_tsn_;
  std::vector<SkippedStream> skipped_streams_;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CHUNK_FORWARD_TSN_COMMON_H_
