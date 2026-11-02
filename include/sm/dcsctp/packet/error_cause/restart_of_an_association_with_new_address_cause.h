/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause.h (compare/webrtc/reference/net/dcsctp/packet/error_cause/restart_of_an_association_with_new_address_cause.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_ERROR_CAUSE_RESTART_OF_AN_ASSOCIATION_WITH_NEW_ADDRESS_CAUSE_H_
#define SM_DCSCTP_PACKET_ERROR_CAUSE_RESTART_OF_AN_ASSOCIATION_WITH_NEW_ADDRESS_CAUSE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10.11
struct RestartOfAnAssociationWithNewAddressesCauseConfig
    : public ParameterConfig {
  static constexpr int kType = 11;
  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kVariableLengthAlignment = 1;
};

class RestartOfAnAssociationWithNewAddressesCause
    : public Parameter,
      public TLVTrait<RestartOfAnAssociationWithNewAddressesCauseConfig> {
 public:
  static constexpr int kType =
      RestartOfAnAssociationWithNewAddressesCauseConfig::kType;

  explicit RestartOfAnAssociationWithNewAddressesCause(
      std::span<const uint8_t> new_address_tlvs)
      : new_address_tlvs_(new_address_tlvs.begin(), new_address_tlvs.end()) {}

  static std::optional<RestartOfAnAssociationWithNewAddressesCause> Parse(
      std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  std::span<const uint8_t> new_address_tlvs() const {
    return new_address_tlvs_;
  }

 private:
  std::vector<uint8_t> new_address_tlvs_;
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_ERROR_CAUSE_RESTART_OF_AN_ASSOCIATION_WITH_NEW_ADDRESS_CAUSE_H_
