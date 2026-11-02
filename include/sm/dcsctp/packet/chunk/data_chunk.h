/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/chunk/data_chunk.h (compare/webrtc/reference/net/dcsctp/packet/chunk/data_chunk.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CHUNK_DATA_CHUNK_H_
#define SM_DCSCTP_PACKET_CHUNK_DATA_CHUNK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/chunk/data_common.h"
#include "sm/dcsctp/packet/data.h"
#include "sm/dcsctp/packet/tlv_trait.h"
#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.1
struct DataChunkConfig : ChunkConfig {
  static constexpr int kType = 0;
  static constexpr size_t kHeaderSize = 16;
  static constexpr size_t kVariableLengthAlignment = 1;
};

class DataChunk : public AnyDataChunk, public TLVTrait<DataChunkConfig> {
 public:
  static constexpr int kType = DataChunkConfig::kType;

  // Exposed to allow the retransmission queue to make room for the correct
  // header size.
  static constexpr size_t kHeaderSize = DataChunkConfig::kHeaderSize;

  DataChunk(TSN tsn,
            StreamID stream_id,
            SSN ssn,
            PPID ppid,
            std::vector<uint8_t> payload,
            const Options& options)
      : AnyDataChunk(tsn,
                     stream_id,
                     ssn,
                     MID(0),
                     FSN(0),
                     ppid,
                     std::move(payload),
                     options) {}

  DataChunk(TSN tsn, Data&& data, bool immediate_ack)
      : AnyDataChunk(tsn, std::move(data), immediate_ack) {}

  static std::optional<DataChunk> Parse(std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CHUNK_DATA_CHUNK_H_
