// The parts of libwebrtc M157's rtc_base/string_encode.{h,cc} and string_to_number.h that the candidate and SDP
// parsers use (#1062): split, tokenize, tokenize_first and FromString. rtc_base/string_encode.cc is not in
// compare/webrtc/reference/; this follows its documented behaviour:
// - split() keeps empty fields ("a,,b" is three fields);
// - tokenize() drops them;
// - tokenize_first() splits at the first delimiter and skips any run of delimiters after it, and fails if there is
//   no delimiter;
// - FromString() takes the whole string as a decimal number of the type, with no leading '+' or white space and no
//   trailing characters, and fails outside the type's range (a '-' fails for an unsigned type).
#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sm::p2p {

inline std::vector<std::string_view> split(std::string_view source, char delimiter) {
  std::vector<std::string_view> fields;
  size_t last = 0;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == delimiter) {
      fields.push_back(source.substr(last, i - last));
      last = i + 1;
    }
  }
  fields.push_back(source.substr(last));
  return fields;
}

inline size_t tokenize(std::string_view source, char delimiter, std::vector<std::string>* fields) {
  fields->clear();
  size_t last = 0;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == delimiter) {
      if (i != last) fields->emplace_back(source.substr(last, i - last));
      last = i + 1;
    }
  }
  if (last != source.size()) fields->emplace_back(source.substr(last));
  return fields->size();
}

inline bool tokenize_first(std::string_view source, char delimiter, std::string* token, std::string* rest) {
  const size_t left_pos = source.find(delimiter);
  if (left_pos == std::string_view::npos) return false;
  size_t right_pos = left_pos + 1;
  while (right_pos < source.size() && source[right_pos] == delimiter) ++right_pos;
  *token = std::string(source.substr(0, left_pos));
  *rest = std::string(source.substr(right_pos));
  return true;
}

template <typename T>
bool FromString(std::string_view s, T* t) {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
  if (s.empty()) return false;
  if (s[0] == '-' && std::is_unsigned_v<T>) return false;
  if (s[0] != '-' && (s[0] < '0' || s[0] > '9')) return false;
  T value{};
  const char* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(s.data(), end, value);
  if (ec != std::errc() || ptr != end) return false;
  *t = value;
  return true;
}

}  // namespace sm::p2p
