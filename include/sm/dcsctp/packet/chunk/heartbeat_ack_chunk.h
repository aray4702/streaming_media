/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/heartbeat_ack_chunk.h (compare/webrtc/reference/net/dcsctp/packet/chunk/heartbeat_ack_chunk.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CHUNK_HEARTBEAT_ACK_CHUNK_H_
#define SM_DCSCTP_PACKET_CHUNK_HEARTBEAT_ACK_CHUNK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/parameter/heartbeat_info_parameter.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.6
struct HeartbeatAckChunkConfig : ChunkConfig {
  static constexpr int kType = 5;
  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kVariableLengthAlignment = 1;
};

class HeartbeatAckChunk : public Chunk,
                          public TLVTrait<HeartbeatAckChunkConfig> {
 public:
  static constexpr int kType = HeartbeatAckChunkConfig::kType;

  explicit HeartbeatAckChunk(Parameters parameters)
      : parameters_(std::move(parameters)) {}

  HeartbeatAckChunk(HeartbeatAckChunk&& other) = default;
  HeartbeatAckChunk& operator=(HeartbeatAckChunk&& other) = default;

  static std::optional<HeartbeatAckChunk> Parse(std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  const Parameters& parameters() const { return parameters_; }

  std::optional<HeartbeatInfoParameter> info() const {
    return parameters_.get<HeartbeatInfoParameter>();
  }

 private:
  Parameters parameters_;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CHUNK_HEARTBEAT_ACK_CHUNK_H_
