/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/common/handover_testing.cc (compare/webrtc/reference/net/dcsctp/common/handover_testing.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "common/handover_testing.h"

#include "sm/dcsctp/public/dcsctp_handover_state.h"

namespace sm::dcsctp {
namespace {
// Default transformer function does nothing - dcSCTP does not implement
// state serialization that could be tested by setting
// `g_handover_state_transformer_for_test`.
void NoTransformation(DcSctpSocketHandoverState*) {}
}  // namespace

void (*g_handover_state_transformer_for_test)(DcSctpSocketHandoverState*) =
    NoTransformation;
}  // namespace sm::dcsctp
