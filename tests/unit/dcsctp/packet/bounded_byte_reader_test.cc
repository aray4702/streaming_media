/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/bounded_byte_reader_test.cc (compare/webrtc/reference/net/dcsctp/packet/bounded_byte_reader_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.

#include "sm/dcsctp/packet/bounded_byte_reader.h"

#include <cstdint>

#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;

SM_TEST(DcsctpBoundedByteReaderTest, CanLoadData) {
  uint8_t data[14] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4};

  BoundedByteReader<8> reader(data);
  SM_EXPECT_EQ(reader.variable_data_size(), 6U);
  SM_EXPECT_EQ(reader.Load32<0>(), 0x01020304U);
  SM_EXPECT_EQ(reader.Load32<4>(), 0x05060708U);
  SM_EXPECT_EQ(reader.Load16<4>(), 0x0506U);
  SM_EXPECT_EQ(reader.Load8<4>(), 0x05U);
  SM_EXPECT_EQ(reader.Load8<5>(), 0x06U);

  BoundedByteReader<6> sub = reader.sub_reader<6>(0);
  SM_EXPECT_EQ(sub.Load16<0>(), 0x0900U);
  SM_EXPECT_EQ(sub.Load32<0>(), 0x09000102U);
  SM_EXPECT_EQ(sub.Load16<4>(), 0x0304U);

  SM_EXPECT_THAT(reader.variable_data(), ElementsAre(9, 0, 1, 2, 3, 4));
}

}  // namespace
}  // namespace sm::dcsctp
