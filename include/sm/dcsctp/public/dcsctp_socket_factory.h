/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/public/dcsctp_socket_factory.h (compare/webrtc/reference/net/dcsctp/public/dcsctp_socket_factory.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PUBLIC_DCSCTP_SOCKET_FACTORY_H_
#define SM_DCSCTP_PUBLIC_DCSCTP_SOCKET_FACTORY_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/packet_observer.h"

namespace sm::dcsctp {
class DcSctpSocketFactory {
 public:
  virtual ~DcSctpSocketFactory();
  virtual std::unique_ptr<DcSctpSocketInterface> Create(
      std::string_view log_prefix,
      DcSctpSocketCallbacks& callbacks,
      std::unique_ptr<PacketObserver> packet_observer,
      const DcSctpOptions& options);
  std::vector<uint8_t> GenerateConnectionToken(
      const DcSctpOptions& options,
      std::function<uint32_t(uint32_t low, uint32_t high)> get_random_uint32);
};
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PUBLIC_DCSCTP_SOCKET_FACTORY_H_
