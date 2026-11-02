// A reader over a fuzzer's input, modelled on libwebrtc's
// test/fuzzers/fuzz_data_helper.h (FuzzDataHelper): reads past the end yield
// zeros, so every input is a valid program and the targets never index out
// of the buffer themselves.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sm::fuzz {

class FuzzDataReader {
 public:
  explicit FuzzDataReader(std::span<const uint8_t> data) : data_(data) {}

  bool CanRead(size_t n) const { return data_.size() - pos_ >= n; }
  size_t BytesLeft() const { return data_.size() - pos_; }

  uint8_t ReadU8() { return CanRead(1) ? data_[pos_++] : 0; }
  uint16_t ReadU16() { return static_cast<uint16_t>((ReadU8() << 8) | ReadU8()); }
  uint32_t ReadU32() {
    uint32_t hi = ReadU16();
    return (hi << 16) | ReadU16();
  }
  // Up to `n` bytes (fewer at the end of the input).
  std::span<const uint8_t> ReadBytes(size_t n) {
    if (n > BytesLeft()) n = BytesLeft();
    auto out = data_.subspan(pos_, n);
    pos_ += n;
    return out;
  }
  // A big-endian 16-bit length, then that many bytes: the record format of
  // the multi-packet targets (and of tools/fuzz_corpus.py's seeds).
  std::span<const uint8_t> ReadLengthPrefixed() { return ReadBytes(ReadU16()); }

 private:
  std::span<const uint8_t> data_;
  size_t pos_ = 0;
};

}  // namespace sm::fuzz
