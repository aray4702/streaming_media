// Big-endian byte reader and writer for the connectivity layer: the parts of
// libwebrtc's rtc_base/byte_buffer.h that api/transport/stun.cc uses. That file
// is not in compare/webrtc/reference/ (issue #986); the semantics kept here are
// the ones stun.cc relies on: network byte order, a reader whose Length() and
// DataView() are what is left to read, and reads that fail without consuming
// when too few bytes remain.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sm::p2p {

class ByteBufferWriter {
 public:
  void WriteUInt8(uint8_t v) { buf_.push_back(v); }
  void WriteUInt16(uint16_t v) {
    WriteUInt8(static_cast<uint8_t>(v >> 8));
    WriteUInt8(static_cast<uint8_t>(v));
  }
  void WriteUInt32(uint32_t v) {
    WriteUInt16(static_cast<uint16_t>(v >> 16));
    WriteUInt16(static_cast<uint16_t>(v));
  }
  void WriteUInt64(uint64_t v) {
    WriteUInt32(static_cast<uint32_t>(v >> 32));
    WriteUInt32(static_cast<uint32_t>(v));
  }
  void WriteString(std::string_view s) { buf_.insert(buf_.end(), s.begin(), s.end()); }
  void Write(std::span<const uint8_t> b) { buf_.insert(buf_.end(), b.begin(), b.end()); }

  const uint8_t* Data() const { return buf_.data(); }
  size_t Length() const { return buf_.size(); }
  std::span<const uint8_t> DataView() const { return buf_; }
  void Clear() { buf_.clear(); }

 private:
  std::vector<uint8_t> buf_;
};

class ByteBufferReader {
 public:
  explicit ByteBufferReader(std::span<const uint8_t> data) : data_(data) {}
  explicit ByteBufferReader(const ByteBufferWriter& w) : data_(w.DataView()) {}

  // Bytes left to read.
  size_t Length() const { return data_.size() - pos_; }
  std::span<const uint8_t> DataView() const { return data_.subspan(pos_); }

  bool ReadUInt8(uint8_t* v) {
    if (Length() < 1) return false;
    *v = data_[pos_++];
    return true;
  }
  bool ReadUInt16(uint16_t* v) {
    if (Length() < 2) return false;
    *v = static_cast<uint16_t>((data_[pos_] << 8) | data_[pos_ + 1]);
    pos_ += 2;
    return true;
  }
  bool ReadUInt32(uint32_t* v) {
    if (Length() < 4) return false;
    *v = (uint32_t{data_[pos_]} << 24) | (uint32_t{data_[pos_ + 1]} << 16) | (uint32_t{data_[pos_ + 2]} << 8) |
         data_[pos_ + 3];
    pos_ += 4;
    return true;
  }
  bool ReadUInt64(uint64_t* v) {
    uint32_t hi = 0, lo = 0;
    if (Length() < 8) return false;
    ReadUInt32(&hi);
    ReadUInt32(&lo);
    *v = (uint64_t{hi} << 32) | lo;
    return true;
  }
  bool ReadBytes(std::span<uint8_t> out) {
    if (Length() < out.size()) return false;
    if (!out.empty()) std::memcpy(out.data(), data_.data() + pos_, out.size());
    pos_ += out.size();
    return true;
  }
  bool ReadString(std::string* out, size_t len) {
    if (Length() < len) return false;
    out->assign(reinterpret_cast<const char*>(data_.data()) + pos_, len);
    pos_ += len;
    return true;
  }
  bool ReadStringView(std::string_view* out, size_t len) {
    if (Length() < len) return false;
    *out = std::string_view(reinterpret_cast<const char*>(data_.data()) + pos_, len);
    pos_ += len;
    return true;
  }
  bool Consume(size_t len) {
    if (Length() < len) return false;
    pos_ += len;
    return true;
  }

 private:
  std::span<const uint8_t> data_;
  size_t pos_ = 0;
};

}  // namespace sm::p2p
