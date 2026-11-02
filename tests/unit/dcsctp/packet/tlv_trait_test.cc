/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/tlv_trait_test.cc (compare/webrtc/reference/net/dcsctp/packet/tlv_trait_test.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "sm/dcsctp/packet/tlv_trait.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "gmock_lite.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::SizeIs;

struct OneByteTypeConfig {
  static constexpr int kTypeSizeInBytes = 1;
  static constexpr int kType = 0x49;
  static constexpr size_t kHeaderSize = 12;
  static constexpr int kVariableLengthAlignment = 4;
};

class OneByteChunk : public TLVTrait<OneByteTypeConfig> {
 public:
  static constexpr size_t kVariableSize = 4;

  void SerializeTo(std::vector<uint8_t>& out) {
    BoundedByteWriter<OneByteTypeConfig::kHeaderSize> writer =
        AllocateTLV(out, kVariableSize);
    writer.Store32<4>(0x01020304);
    writer.Store16<8>(0x0506);
    writer.Store16<10>(0x0708);

    uint8_t variable_data[kVariableSize] = {0xDE, 0xAD, 0xBE, 0xEF};
    writer.CopyToVariableData(std::span<const uint8_t>(variable_data));
  }

  static std::optional<BoundedByteReader<OneByteTypeConfig::kHeaderSize>> Parse(
      std::span<const uint8_t> data) {
    return ParseTLV(data);
  }
};

SM_TEST(DcsctpTlvDataTest, CanWriteOneByteTypeTlvs) {
  std::vector<uint8_t> out;
  OneByteChunk().SerializeTo(out);

  SM_EXPECT_THAT(out, SizeIs(OneByteTypeConfig::kHeaderSize +
                          OneByteChunk::kVariableSize));
  SM_EXPECT_THAT(out, ElementsAre(0x49, 0x00, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                               0x05, 0x06, 0x07, 0x08, 0xDE, 0xAD, 0xBE, 0xEF));
}

SM_TEST(DcsctpTlvDataTest, CanReadOneByteTypeTlvs) {
  uint8_t data[] = {0x49, 0x00, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                    0x05, 0x06, 0x07, 0x08, 0xDE, 0xAD, 0xBE, 0xEF};

  std::optional<BoundedByteReader<OneByteTypeConfig::kHeaderSize>> reader =
      OneByteChunk::Parse(data);
  SM_ASSERT_TRUE(reader.has_value());
  SM_EXPECT_EQ(reader->Load32<4>(), 0x01020304U);
  SM_EXPECT_EQ(reader->Load16<8>(), 0x0506U);
  SM_EXPECT_EQ(reader->Load16<10>(), 0x0708U);
  SM_EXPECT_THAT(reader->variable_data(), ElementsAre(0xDE, 0xAD, 0xBE, 0xEF));
}

struct TwoByteTypeConfig {
  static constexpr int kTypeSizeInBytes = 2;
  static constexpr int kType = 31337;
  static constexpr size_t kHeaderSize = 8;
  static constexpr int kVariableLengthAlignment = 2;
};

class TwoByteChunk : public TLVTrait<TwoByteTypeConfig> {
 public:
  static constexpr size_t kVariableSize = 8;

  void SerializeTo(std::vector<uint8_t>& out) {
    BoundedByteWriter<TwoByteTypeConfig::kHeaderSize> writer =
        AllocateTLV(out, kVariableSize);
    writer.Store32<4>(0x01020304U);

    uint8_t variable_data[] = {0x05, 0x06, 0x07, 0x08, 0xDE, 0xAD, 0xBE, 0xEF};
    writer.CopyToVariableData(std::span<const uint8_t>(variable_data));
  }

  static std::optional<BoundedByteReader<TwoByteTypeConfig::kHeaderSize>> Parse(
      std::span<const uint8_t> data) {
    return ParseTLV(data);
  }
};

SM_TEST(DcsctpTlvDataTest, CanWriteTwoByteTypeTlvs) {
  std::vector<uint8_t> out;

  TwoByteChunk().SerializeTo(out);

  SM_EXPECT_THAT(out, SizeIs(TwoByteTypeConfig::kHeaderSize +
                          TwoByteChunk::kVariableSize));
  SM_EXPECT_THAT(out, ElementsAre(0x7A, 0x69, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                               0x05, 0x06, 0x07, 0x08, 0xDE, 0xAD, 0xBE, 0xEF));
}

SM_TEST(DcsctpTlvDataTest, CanReadTwoByteTypeTlvs) {
  uint8_t data[] = {0x7A, 0x69, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                    0x05, 0x06, 0x07, 0x08, 0xDE, 0xAD, 0xBE, 0xEF};

  std::optional<BoundedByteReader<TwoByteTypeConfig::kHeaderSize>> reader =
      TwoByteChunk::Parse(data);
  SM_EXPECT_TRUE(reader.has_value());
  SM_EXPECT_EQ(reader->Load32<4>(), 0x01020304U);
  SM_EXPECT_THAT(reader->variable_data(),
              ElementsAre(0x05, 0x06, 0x07, 0x08, 0xDE, 0xAD, 0xBE, 0xEF));
}

SM_TEST(DcsctpTlvDataTest, CanHandleInvalidLengthSmallerThanFixedSize) {
  // Has 'length=6', which is below the kHeaderSize of 8.
  uint8_t data[] = {0x7A, 0x69, 0x00, 0x06, 0x01, 0x02, 0x03, 0x04};

  SM_EXPECT_FALSE(TwoByteChunk::Parse(data).has_value());
}

}  // namespace
}  // namespace sm::dcsctp
