/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/retransmission_error_counter_test.cc (compare/webrtc/reference/net/dcsctp/tx/retransmission_error_counter_test.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/tx/retransmission_error_counter.h"

#include <optional>

#include "sm/dcsctp/public/dcsctp_options.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {

SM_TEST(DcsctpRetransmissionErrorCounterTest, HasInitialValue) {
  DcSctpOptions options;
  RetransmissionErrorCounter counter("log: ", options);
  SM_EXPECT_EQ(counter.value(), 0);
}

SM_TEST(DcsctpRetransmissionErrorCounterTest, ReturnsFalseAtMaximumValue) {
  DcSctpOptions options;
  options.max_retransmissions = 5;
  RetransmissionErrorCounter counter("log: ", options);
  SM_EXPECT_TRUE(counter.Increment("test"));   // 1
  SM_EXPECT_TRUE(counter.Increment("test"));   // 2
  SM_EXPECT_TRUE(counter.Increment("test"));   // 3
  SM_EXPECT_TRUE(counter.Increment("test"));   // 4
  SM_EXPECT_TRUE(counter.Increment("test"));   // 5
  SM_EXPECT_FALSE(counter.Increment("test"));  // Too many retransmissions
}

SM_TEST(DcsctpRetransmissionErrorCounterTest, CanHandleZeroRetransmission) {
  DcSctpOptions options;
  options.max_retransmissions = 0;
  RetransmissionErrorCounter counter("log: ", options);
  SM_EXPECT_FALSE(counter.Increment("test"));  // One is too many.
}

SM_TEST(DcsctpRetransmissionErrorCounterTest, IsExhaustedAtMaximum) {
  DcSctpOptions options;
  options.max_retransmissions = 3;
  RetransmissionErrorCounter counter("log: ", options);
  SM_EXPECT_TRUE(counter.Increment("test"));  // 1
  SM_EXPECT_FALSE(counter.IsExhausted());
  SM_EXPECT_TRUE(counter.Increment("test"));  // 2
  SM_EXPECT_FALSE(counter.IsExhausted());
  SM_EXPECT_TRUE(counter.Increment("test"));  // 3
  SM_EXPECT_FALSE(counter.IsExhausted());
  SM_EXPECT_FALSE(counter.Increment("test"));  // Too many retransmissions
  SM_EXPECT_TRUE(counter.IsExhausted());
  SM_EXPECT_FALSE(counter.Increment("test"));  // One after too many
  SM_EXPECT_TRUE(counter.IsExhausted());
}

SM_TEST(DcsctpRetransmissionErrorCounterTest, ClearingCounter) {
  DcSctpOptions options;
  options.max_retransmissions = 3;
  RetransmissionErrorCounter counter("log: ", options);
  SM_EXPECT_TRUE(counter.Increment("test"));  // 1
  SM_EXPECT_TRUE(counter.Increment("test"));  // 2
  counter.Clear();
  SM_EXPECT_TRUE(counter.Increment("test"));  // 1
  SM_EXPECT_TRUE(counter.Increment("test"));  // 2
  SM_EXPECT_TRUE(counter.Increment("test"));  // 3
  SM_EXPECT_FALSE(counter.IsExhausted());
  SM_EXPECT_FALSE(counter.Increment("test"));  // Too many retransmissions
  SM_EXPECT_TRUE(counter.IsExhausted());
}

SM_TEST(DcsctpRetransmissionErrorCounterTest, CanBeLimitless) {
  DcSctpOptions options;
  options.max_retransmissions = std::nullopt;
  RetransmissionErrorCounter counter("log: ", options);
  for (int i = 0; i < 100; ++i) {
    SM_EXPECT_TRUE(counter.Increment("test"));
    SM_EXPECT_FALSE(counter.IsExhausted());
  }
}

}  // namespace
}  // namespace sm::dcsctp
