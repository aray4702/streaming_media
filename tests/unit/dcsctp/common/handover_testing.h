/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/common/handover_testing.h (compare/webrtc/reference/net/dcsctp/common/handover_testing.h) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#ifndef SM_DCSCTP_COMMON_HANDOVER_TESTING_H_
#define SM_DCSCTP_COMMON_HANDOVER_TESTING_H_

#include "sm/dcsctp/public/dcsctp_handover_state.h"

namespace sm::dcsctp {
// This global function is to facilitate testing of the socket handover state
// (`DcSctpSocketHandoverState`) serialization. dcSCTP library users have to
// implement state serialization if it's needed. To test the serialization one
// can set a custom `g_handover_state_transformer_for_test` at startup, link to
// the dcSCTP tests and run the resulting binary. Custom function can serialize
// and deserialize the passed state. All dcSCTP handover tests call
// `g_handover_state_transformer_for_test`. If some part of the state is
// serialized incorrectly or is forgotten, high chance that it will fail the
// tests.
extern void (*g_handover_state_transformer_for_test)(
    DcSctpSocketHandoverState*);
}  // namespace sm::dcsctp

#endif  // SM_DCSCTP_COMMON_HANDOVER_TESTING_H_
