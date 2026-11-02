/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/parameter/add_incoming_streams_request_parameter.h (compare/webrtc/reference/net/dcsctp/packet/parameter/add_incoming_streams_request_parameter.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_PARAMETER_ADD_INCOMING_STREAMS_REQUEST_PARAMETER_H_
#define SM_DCSCTP_PACKET_PARAMETER_ADD_INCOMING_STREAMS_REQUEST_PARAMETER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/tlv_trait.h"

namespace sm::dcsctp {

// https://tools.ietf.org/html/rfc6525#section-4.6
struct AddIncomingStreamsRequestParameterConfig : ParameterConfig {
  static constexpr int kType = 18;
  static constexpr size_t kHeaderSize = 12;
  static constexpr size_t kVariableLengthAlignment = 0;
};

class AddIncomingStreamsRequestParameter
    : public Parameter,
      public TLVTrait<AddIncomingStreamsRequestParameterConfig> {
 public:
  static constexpr int kType = AddIncomingStreamsRequestParameterConfig::kType;

  explicit AddIncomingStreamsRequestParameter(
      ReconfigRequestSN request_sequence_number,
      uint16_t nbr_of_new_streams)
      : request_sequence_number_(request_sequence_number),
        nbr_of_new_streams_(nbr_of_new_streams) {}

  static std::optional<AddIncomingStreamsRequestParameter> Parse(
      std::span<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;

  ReconfigRequestSN request_sequence_number() const {
    return request_sequence_number_;
  }
  uint16_t nbr_of_new_streams() const { return nbr_of_new_streams_; }

 private:
  ReconfigRequestSN request_sequence_number_;
  uint16_t nbr_of_new_streams_;
};

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_PARAMETER_ADD_INCOMING_STREAMS_REQUEST_PARAMETER_H_
