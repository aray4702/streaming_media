/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/iforward_tsn_chunk.h (compare/webrtc/reference/net/dcsctp/packet/chunk/iforward_tsn_chunk.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CHUNK_IFORWARD_TSN_CHUNK_H_
#define SM_DCSCTP_PACKET_CHUNK_IFORWARD_TSN_CHUNK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/chunk/forward_tsn_common.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc8260#section-2.3.1
struct IForwardTsnChunkConfig : ChunkConfig {
  static constexpr int kType = 194;
  static constexpr size_t kHeaderSize = 8;
  static constexpr size_t kVariableLengthAlignment = 8;
};

class IForwardTsnChunk : public AnyForwardTsnChunk,
                         public TLVTrait<IForwardTsnChunkConfig> {
 public:
  static constexpr int kType = IForwardTsnChunkConfig::kType;

  IForwardTsnChunk(TSN new_cumulative_tsn,
                   std::vector<SkippedStream> skipped_streams)
      : AnyForwardTsnChunk(new_cumulative_tsn, std::move(skipped_streams)) {}

  static std::optional<IForwardTsnChunk> Parse(std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

 private:
  static constexpr size_t kSkippedStreamBufferSize = 8;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CHUNK_IFORWARD_TSN_CHUNK_H_
