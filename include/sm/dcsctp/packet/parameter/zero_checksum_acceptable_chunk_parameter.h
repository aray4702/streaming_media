/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.h (compare/webrtc/reference/net/dcsctp/packet/parameter/zero_checksum_acceptable_chunk_parameter.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_PARAMETER_ZERO_CHECKSUM_ACCEPTABLE_CHUNK_PARAMETER_H_
#define SM_DCSCTP_PACKET_PARAMETER_ZERO_CHECKSUM_ACCEPTABLE_CHUNK_PARAMETER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/tlv_trait.h"
#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {

// https://datatracker.ietf.org/doc/draft-ietf-tsvwg-sctp-zero-checksum/
struct ZeroChecksumAcceptableChunkParameterConfig : ParameterConfig {
  static constexpr int kType = 0x8001;
  static constexpr size_t kHeaderSize = 8;
  static constexpr size_t kVariableLengthAlignment = 0;
};

class ZeroChecksumAcceptableChunkParameter
    : public Parameter,
      public TLVTrait<ZeroChecksumAcceptableChunkParameterConfig> {
 public:
  static constexpr int kType =
      ZeroChecksumAcceptableChunkParameterConfig::kType;

  explicit ZeroChecksumAcceptableChunkParameter(
      ZeroChecksumAlternateErrorDetectionMethod error_detection_method)
      : error_detection_method_(error_detection_method) {}

  static std::optional<ZeroChecksumAcceptableChunkParameter> Parse(
      std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  ZeroChecksumAlternateErrorDetectionMethod error_detection_method() const {
    return error_detection_method_;
  }

 private:
  ZeroChecksumAlternateErrorDetectionMethod error_detection_method_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_PARAMETER_ZERO_CHECKSUM_ACCEPTABLE_CHUNK_PARAMETER_H_
