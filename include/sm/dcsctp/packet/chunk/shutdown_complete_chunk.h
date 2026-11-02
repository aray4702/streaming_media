/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/shutdown_complete_chunk.h (compare/webrtc/reference/net/dcsctp/packet/chunk/shutdown_complete_chunk.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CHUNK_SHUTDOWN_COMPLETE_CHUNK_H_
#define SM_DCSCTP_PACKET_CHUNK_SHUTDOWN_COMPLETE_CHUNK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.13
struct ShutdownCompleteChunkConfig : ChunkConfig {
  static constexpr int kType = 14;
  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kVariableLengthAlignment = 0;
};

class ShutdownCompleteChunk : public Chunk,
                              public TLVTrait<ShutdownCompleteChunkConfig> {
 public:
  static constexpr int kType = ShutdownCompleteChunkConfig::kType;

  explicit ShutdownCompleteChunk(bool tag_reflected)
      : tag_reflected_(tag_reflected) {}

  static std::optional<ShutdownCompleteChunk> Parse(
      std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;
  bool tag_reflected() const { return tag_reflected_; }

 private:
  static constexpr int kFlagsBitT = 0;
  bool tag_reflected_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CHUNK_SHUTDOWN_COMPLETE_CHUNK_H_
