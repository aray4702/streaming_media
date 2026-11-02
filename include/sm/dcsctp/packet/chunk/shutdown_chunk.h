/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/shutdown_chunk.h (compare/webrtc/reference/net/dcsctp/packet/chunk/shutdown_chunk.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CHUNK_SHUTDOWN_CHUNK_H_
#define SM_DCSCTP_PACKET_CHUNK_SHUTDOWN_CHUNK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.8
struct ShutdownChunkConfig : ChunkConfig {
  static constexpr int kType = 7;
  static constexpr size_t kHeaderSize = 8;
  static constexpr size_t kVariableLengthAlignment = 0;
};

class ShutdownChunk : public Chunk, public TLVTrait<ShutdownChunkConfig> {
 public:
  static constexpr int kType = ShutdownChunkConfig::kType;

  explicit ShutdownChunk(TSN cumulative_tsn_ack)
      : cumulative_tsn_ack_(cumulative_tsn_ack) {}

  static std::optional<ShutdownChunk> Parse(std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  TSN cumulative_tsn_ack() const { return cumulative_tsn_ack_; }

 private:
  TSN cumulative_tsn_ack_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CHUNK_SHUTDOWN_CHUNK_H_
