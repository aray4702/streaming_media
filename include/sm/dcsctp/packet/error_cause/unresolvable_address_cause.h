/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/unresolvable_address_cause.h (compare/webrtc/reference/net/dcsctp/packet/error_cause/unresolvable_address_cause.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_ERROR_CAUSE_UNRESOLVABLE_ADDRESS_CAUSE_H_
#define SM_DCSCTP_PACKET_ERROR_CAUSE_UNRESOLVABLE_ADDRESS_CAUSE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10.5
struct UnresolvableAddressCauseConfig : public ParameterConfig {
  static constexpr int kType = 5;
  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kVariableLengthAlignment = 1;
};

class UnresolvableAddressCause
    : public Parameter,
      public TLVTrait<UnresolvableAddressCauseConfig> {
 public:
  static constexpr int kType = UnresolvableAddressCauseConfig::kType;

  explicit UnresolvableAddressCause(
      std::span<const uint8_t> unresolvable_address)
      : unresolvable_address_(unresolvable_address.begin(),
                              unresolvable_address.end()) {}

  static std::optional<UnresolvableAddressCause> Parse(
      std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  std::span<const uint8_t> unresolvable_address() const {
    return unresolvable_address_;
  }

 private:
  std::vector<uint8_t> unresolvable_address_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_ERROR_CAUSE_UNRESOLVABLE_ADDRESS_CAUSE_H_
