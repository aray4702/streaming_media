// The rtc_base/ pieces that libwebrtc M157's net/dcsctp/ uses (#1032, C7):
// StrongAlias (rtc_base/strong_alias.h), StringBuilder
// (rtc_base/strings/string_builder.h), StrJoin (rtc_base/strings/str_join.h),
// StringFormat (rtc_base/strings/string_format.h), RTC_CHECK / RTC_DCHECK
// (rtc_base/checks.h) and RTC_DLOG (rtc_base/logging.h).
//
// These rtc_base files are not in compare/webrtc/reference/ (only the
// dcsctp tree is), so this header gives the behaviour the dcsctp code relies
// on, and nothing more:
// - StrongAlias<Tag, T>: an explicit wrapper with value(), operator*,
//   operator-> and the comparisons of T, and `UnderlyingType`.
// - StringBuilder: appends strings, characters and numbers (numbers in
//   decimal, as std::to_string; an unsigned char promotes to int, so it
//   prints as a number, as in libwebrtc). str() and Release().
// - StrJoin(container, delimiter[, formatter]).
// - StringFormat(printf format, ...).
// - SM_DCSCTP_CHECK(cond) aborts when cond is false (RTC_CHECK);
//   SM_DCSCTP_DCHECK(cond) does the same only in debug builds (RTC_DCHECK);
//   both take `<< message`. SM_DCSCTP_DLOG is RTC_DLOG: a debug-build log
//   line, compiled out in release builds as libwebrtc's is (NDEBUG). Here the
//   debug build writes it to the library's text log at WARNING.
#ifndef SM_DCSCTP_RTC_COMPAT_H_
#define SM_DCSCTP_RTC_COMPAT_H_

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "sm/core/logging.h"

namespace sm::dcsctp {

template <typename TagType, typename TValue>
class StrongAlias {
 public:
  using UnderlyingType = TValue;

  constexpr StrongAlias() = default;
  constexpr explicit StrongAlias(const TValue& v) : value_(v) {}
  constexpr explicit StrongAlias(TValue&& v) : value_(std::move(v)) {}

  constexpr TValue* operator->() { return &value_; }
  constexpr const TValue* operator->() const { return &value_; }
  constexpr TValue& operator*() & { return value_; }
  constexpr const TValue& operator*() const& { return value_; }
  constexpr TValue& value() & { return value_; }
  constexpr const TValue& value() const& { return value_; }
  constexpr explicit operator const TValue&() const& { return value_; }

  constexpr bool operator==(const StrongAlias& other) const { return value_ == other.value_; }
  constexpr bool operator!=(const StrongAlias& other) const { return value_ != other.value_; }
  constexpr bool operator<(const StrongAlias& other) const { return value_ < other.value_; }
  constexpr bool operator<=(const StrongAlias& other) const { return value_ <= other.value_; }
  constexpr bool operator>(const StrongAlias& other) const { return value_ > other.value_; }
  constexpr bool operator>=(const StrongAlias& other) const { return value_ >= other.value_; }

 protected:
  TValue value_{};
};

class StringBuilder {
 public:
  StringBuilder() = default;
  explicit StringBuilder(std::string_view s) : str_(s) {}

  StringBuilder& operator<<(std::string_view s) {
    str_.append(s);
    return *this;
  }
  StringBuilder& operator<<(const char* s) {
    str_.append(s);
    return *this;
  }
  StringBuilder& operator<<(const std::string& s) {
    str_.append(s);
    return *this;
  }
  StringBuilder& operator<<(char c) {
    str_.push_back(c);
    return *this;
  }
  StringBuilder& operator<<(int i) { return Append(std::to_string(i)); }
  StringBuilder& operator<<(unsigned i) { return Append(std::to_string(i)); }
  StringBuilder& operator<<(long i) { return Append(std::to_string(i)); }
  StringBuilder& operator<<(long long i) { return Append(std::to_string(i)); }
  StringBuilder& operator<<(unsigned long i) { return Append(std::to_string(i)); }
  StringBuilder& operator<<(unsigned long long i) { return Append(std::to_string(i)); }
  StringBuilder& operator<<(float f) { return Append(std::to_string(f)); }
  StringBuilder& operator<<(double f) { return Append(std::to_string(f)); }

  const std::string& str() const { return str_; }
  std::string Release() {
    std::string ret = std::move(str_);
    str_.clear();
    return ret;
  }

 private:
  StringBuilder& Append(const std::string& s) {
    str_.append(s);
    return *this;
  }
  std::string str_;
};

template <typename Range>
std::string StrJoin(const Range& seq, std::string_view delimiter) {
  StringBuilder sb;
  int idx = 0;
  for (const auto& elem : seq) {
    if (idx > 0) sb << delimiter;
    sb << elem;
    ++idx;
  }
  return sb.Release();
}

template <typename Range, typename Formatter>
std::string StrJoin(const Range& seq, std::string_view delimiter, const Formatter& fmt) {
  StringBuilder sb;
  int idx = 0;
  for (const auto& elem : seq) {
    if (idx > 0) sb << delimiter;
    fmt(sb, elem);
    ++idx;
  }
  return sb.Release();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string StringFormat(const char* fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n < 0) return std::string();
  return std::string(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
}

namespace compat_impl {

// The message of a failed check: printed with the condition, then abort().
class FatalMessage {
 public:
  FatalMessage(const char* file, int line, const char* condition) {
    sb_ << file << ":" << line << ": check failed: " << condition << " ";
  }
  [[noreturn]] ~FatalMessage() {
    std::fprintf(stderr, "%s\n", sb_.str().c_str());
    std::abort();
  }
  template <typename T>
  FatalMessage& operator<<(const T& v) {
    sb_ << v;
    return *this;
  }

 private:
  StringBuilder sb_;
};

// A debug log line, written at WARNING when it goes out of scope.
class DebugLogMessage {
 public:
  DebugLogMessage(const char* file, int line) : file_(file), line_(line) {}
  ~DebugLogMessage() { ::sm::LogPrintf(::sm::LogSeverity::kWarning, file_, line_, "%s", sb_.str().c_str()); }
  template <typename T>
  DebugLogMessage& operator<<(const T& v) {
    sb_ << v;
    return *this;
  }

 private:
  const char* file_;
  int line_;
  StringBuilder sb_;
};

// Swallows the message of a check or log that is compiled out.
class NullStream {
 public:
  template <typename T>
  NullStream& operator<<(const T&) {
    return *this;
  }
};

// Lowers `cond ? (void)0 : Voidify() & stream << ...` to void.
struct Voidify {
  template <typename T>
  void operator&(T&&) {}
};

}  // namespace compat_impl
}  // namespace sm::dcsctp

#define SM_DCSCTP_CHECK(condition) \
  (condition) ? (void)0            \
              : ::sm::dcsctp::compat_impl::Voidify() & \
                    ::sm::dcsctp::compat_impl::FatalMessage(__FILE__, __LINE__, #condition)

#if defined(NDEBUG)
#define SM_DCSCTP_DCHECK(condition) \
  (true || (condition)) ? (void)0 : ::sm::dcsctp::compat_impl::Voidify() & ::sm::dcsctp::compat_impl::NullStream()
#define SM_DCSCTP_DLOG \
  true ? (void)0 : ::sm::dcsctp::compat_impl::Voidify() & ::sm::dcsctp::compat_impl::NullStream()
#else
#define SM_DCSCTP_DCHECK(condition) SM_DCSCTP_CHECK(condition)
#define SM_DCSCTP_DLOG \
  ::sm::dcsctp::compat_impl::Voidify() & ::sm::dcsctp::compat_impl::DebugLogMessage(__FILE__, __LINE__)
#endif
#define SM_DCSCTP_DCHECK_NOTREACHED() SM_DCSCTP_DCHECK(false)

#endif  // SM_DCSCTP_RTC_COMPAT_H_
