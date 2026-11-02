/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/common/internal_types.h (compare/webrtc/reference/net/dcsctp/common/internal_types.h) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#ifndef SM_DCSCTP_COMMON_INTERNAL_TYPES_H_
#define SM_DCSCTP_COMMON_INTERNAL_TYPES_H_

#include <cstdint>

#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {

// Stream Sequence Number (SSN)
using SSN = StrongAlias<class SSNTag, uint16_t>;

// Message Identifier (MID)
using MID = StrongAlias<class MIDTag, uint32_t>;

// Fragment Sequence Number (FSN)
using FSN = StrongAlias<class FSNTag, uint32_t>;

// Transmission Sequence Number (TSN)
using TSN = StrongAlias<class TSNTag, uint32_t>;

// Reconfiguration Request Sequence Number
using ReconfigRequestSN =
    StrongAlias<class ReconfigRequestSNTag, uint32_t>;

// Verification Tag, used for packet validation.
using VerificationTag = StrongAlias<class VerificationTagTag, uint32_t>;

// Tie Tag, used as a nonce when connecting.
using TieTag = StrongAlias<class TieTagTag, uint64_t>;

// An ID for every outgoing message, to correlate outgoing data chunks with the
// message it was carved from.
using OutgoingMessageId =
    StrongAlias<class OutgoingMessageIdTag, uint32_t>;

}  // namespace sm::dcsctp
#endif  // SM_DCSCTP_COMMON_INTERNAL_TYPES_H_
