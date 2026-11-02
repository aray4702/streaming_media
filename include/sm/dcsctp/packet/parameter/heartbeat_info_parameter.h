/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/heartbeat_info_parameter.h (compare/webrtc/reference/net/dcsctp/packet/parameter/heartbeat_info_parameter.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_PARAMETER_HEARTBEAT_INFO_PARAMETER_H_
#define SM_DCSCTP_PACKET_PARAMETER_HEARTBEAT_INFO_PARAMETER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.5
struct HeartbeatInfoParameterConfig : ParameterConfig {
  static constexpr int kType = 1;
  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kVariableLengthAlignment = 1;
};

class HeartbeatInfoParameter : public Parameter,
                               public TLVTrait<HeartbeatInfoParameterConfig> {
 public:
  static constexpr int kType = HeartbeatInfoParameterConfig::kType;

  explicit HeartbeatInfoParameter(std::span<const uint8_t> info)
      : info_(info.begin(), info.end()) {}

  static std::optional<HeartbeatInfoParameter> Parse(
      std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  std::span<const uint8_t> info() const { return info_; }

 private:
  std::vector<uint8_t> info_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_PARAMETER_HEARTBEAT_INFO_PARAMETER_H_
