/*
 *  Copyright 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/public/dcsctp_socket_factory.cc (compare/webrtc/reference/net/dcsctp/public/dcsctp_socket_factory.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.

#include "sm/dcsctp/public/dcsctp_socket_factory.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/packet_observer.h"
#include "sm/dcsctp/socket/dcsctp_socket.h"

namespace sm::dcsctp {

DcSctpSocketFactory::~DcSctpSocketFactory() = default;

std::unique_ptr<DcSctpSocketInterface> DcSctpSocketFactory::Create(
    std::string_view log_prefix,
    DcSctpSocketCallbacks& callbacks,
    std::unique_ptr<PacketObserver> packet_observer,
    const DcSctpOptions& options) {
  return std::make_unique<DcSctpSocket>(log_prefix, callbacks,
                                        std::move(packet_observer), options);
}

std::vector<uint8_t> DcSctpSocketFactory::GenerateConnectionToken(
    const DcSctpOptions& options,
    std::function<uint32_t(uint32_t low, uint32_t high)> get_random_uint32) {
  return DcSctpSocket::GenerateConnectionToken(options, get_random_uint32);
}

}  // namespace sm::dcsctp
