/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/crc32c.h (compare/webrtc/reference/net/dcsctp/packet/crc32c.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_PACKET_CRC32C_H_
#define SM_DCSCTP_PACKET_CRC32C_H_

#include <cstdint>
#include <span>

namespace sm::dcsctp {

// Generates the CRC32C checksum of `data`.
uint32_t GenerateCrc32C(std::span<const uint8_t> data);

}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_PACKET_CRC32C_H_
