// Strongly typed time, size and rate units.
//
// Every algorithm in the library takes the current time as an explicit
// argument instead of reading a clock. That keeps the core deterministic,
// testable with a simulated clock and free of platform clock APIs.
#pragma once

#include <cstdint>
#include <limits>

namespace sm {

class TimeDelta {
 public:
  constexpr TimeDelta() = default;
  static constexpr TimeDelta Micros(int64_t us) { return TimeDelta(us); }
  static constexpr TimeDelta Millis(int64_t ms) { return TimeDelta(ms * 1000); }
  static constexpr TimeDelta Seconds(int64_t s) { return TimeDelta(s * 1000000); }
  static constexpr TimeDelta SecondsF(double s) { return TimeDelta(static_cast<int64_t>(s * 1e6)); }
  static constexpr TimeDelta MillisF(double ms) { return TimeDelta(static_cast<int64_t>(ms * 1e3)); }
  static constexpr TimeDelta Zero() { return TimeDelta(0); }
  static constexpr TimeDelta PlusInfinity() { return TimeDelta(std::numeric_limits<int64_t>::max()); }
  static constexpr TimeDelta MinusInfinity() { return TimeDelta(std::numeric_limits<int64_t>::min()); }

  constexpr int64_t us() const { return us_; }
  constexpr int64_t ms() const { return us_ / 1000; }
  constexpr double ms_f() const { return static_cast<double>(us_) / 1e3; }
  constexpr double seconds() const { return static_cast<double>(us_) / 1e6; }
  constexpr bool IsFinite() const {
    return us_ != std::numeric_limits<int64_t>::max() && us_ != std::numeric_limits<int64_t>::min();
  }
  constexpr bool IsZero() const { return us_ == 0; }

  constexpr bool IsPlusInfinity() const { return us_ == std::numeric_limits<int64_t>::max(); }
  constexpr bool IsMinusInfinity() const { return us_ == std::numeric_limits<int64_t>::min(); }

  // Infinite operands saturate, as libwebrtc's RelativeUnit (rtc_base/units/unit_base.h):
  // +inf wins over a finite value, and +inf combined with -inf is a caller bug there (RTC_DCHECK).
  constexpr TimeDelta operator+(TimeDelta o) const {
    if (IsPlusInfinity() || o.IsPlusInfinity()) return PlusInfinity();
    if (IsMinusInfinity() || o.IsMinusInfinity()) return MinusInfinity();
    return TimeDelta(us_ + o.us_);
  }
  constexpr TimeDelta operator-(TimeDelta o) const {
    if (IsPlusInfinity() || o.IsMinusInfinity()) return PlusInfinity();
    if (IsMinusInfinity() || o.IsPlusInfinity()) return MinusInfinity();
    return TimeDelta(us_ - o.us_);
  }
  constexpr TimeDelta operator-() const {
    if (IsPlusInfinity()) return MinusInfinity();
    if (IsMinusInfinity()) return PlusInfinity();
    return TimeDelta(-us_);
  }
  constexpr TimeDelta operator*(double f) const { return TimeDelta(static_cast<int64_t>(us_ * f)); }
  constexpr TimeDelta operator/(int64_t d) const { return TimeDelta(us_ / d); }
  constexpr double operator/(TimeDelta o) const { return static_cast<double>(us_) / o.us_; }
  constexpr TimeDelta& operator+=(TimeDelta o) { return *this = *this + o; }
  constexpr TimeDelta& operator-=(TimeDelta o) { return *this = *this - o; }
  // UnitBase::RoundTo: (value + resolution / 2) / resolution * resolution, for a resolution > 0.
  // libwebrtc requires a finite value (RTC_DCHECK); an infinite one is returned unchanged here.
  constexpr TimeDelta RoundTo(TimeDelta resolution) const {
    if (!IsFinite()) return *this;
    return TimeDelta((us_ + resolution.us_ / 2) / resolution.us_ * resolution.us_);
  }
  constexpr auto operator<=>(const TimeDelta&) const = default;

 private:
  constexpr explicit TimeDelta(int64_t us) : us_(us) {}
  int64_t us_ = 0;
};

class Timestamp {
 public:
  constexpr Timestamp() = default;
  static constexpr Timestamp Micros(int64_t us) { return Timestamp(us); }
  static constexpr Timestamp Millis(int64_t ms) { return Timestamp(ms * 1000); }
  static constexpr Timestamp Seconds(int64_t s) { return Timestamp(s * 1000000); }
  static constexpr Timestamp PlusInfinity() { return Timestamp(std::numeric_limits<int64_t>::max()); }
  static constexpr Timestamp MinusInfinity() { return Timestamp(std::numeric_limits<int64_t>::min()); }

  constexpr int64_t us() const { return us_; }
  constexpr int64_t ms() const { return us_ / 1000; }
  constexpr double seconds() const { return static_cast<double>(us_) / 1e6; }
  constexpr bool IsFinite() const {
    return us_ != std::numeric_limits<int64_t>::max() && us_ != std::numeric_limits<int64_t>::min();
  }

  constexpr bool IsPlusInfinity() const { return us_ == std::numeric_limits<int64_t>::max(); }
  constexpr bool IsMinusInfinity() const { return us_ == std::numeric_limits<int64_t>::min(); }

  // Infinite operands saturate, as libwebrtc's Timestamp (api/units/timestamp.h:78-114).
  constexpr Timestamp operator+(TimeDelta d) const {
    if (IsPlusInfinity() || d.IsPlusInfinity()) return PlusInfinity();
    if (IsMinusInfinity() || d.IsMinusInfinity()) return MinusInfinity();
    return Timestamp(us_ + d.us());
  }
  constexpr Timestamp operator-(TimeDelta d) const {
    if (IsPlusInfinity() || d.IsMinusInfinity()) return PlusInfinity();
    if (IsMinusInfinity() || d.IsPlusInfinity()) return MinusInfinity();
    return Timestamp(us_ - d.us());
  }
  constexpr TimeDelta operator-(Timestamp o) const {
    if (IsPlusInfinity() || o.IsMinusInfinity()) return TimeDelta::PlusInfinity();
    if (IsMinusInfinity() || o.IsPlusInfinity()) return TimeDelta::MinusInfinity();
    return TimeDelta::Micros(us_ - o.us_);
  }
  constexpr Timestamp& operator+=(TimeDelta d) { return *this = *this + d; }
  constexpr Timestamp& operator-=(TimeDelta d) { return *this = *this - d; }
  constexpr auto operator<=>(const Timestamp&) const = default;

 private:
  constexpr explicit Timestamp(int64_t us) : us_(us) {}
  int64_t us_ = std::numeric_limits<int64_t>::min();
};

class DataSize {
 public:
  constexpr DataSize() = default;
  static constexpr DataSize Bytes(int64_t b) { return DataSize(b); }
  static constexpr DataSize Zero() { return DataSize(0); }
  constexpr int64_t bytes() const { return bytes_; }
  constexpr int64_t bits() const { return bytes_ * 8; }
  constexpr DataSize operator+(DataSize o) const { return DataSize(bytes_ + o.bytes_); }
  constexpr DataSize operator-(DataSize o) const { return DataSize(bytes_ - o.bytes_); }
  constexpr DataSize& operator+=(DataSize o) { bytes_ += o.bytes_; return *this; }
  constexpr DataSize& operator-=(DataSize o) { bytes_ -= o.bytes_; return *this; }
  constexpr auto operator<=>(const DataSize&) const = default;

 private:
  constexpr explicit DataSize(int64_t b) : bytes_(b) {}
  int64_t bytes_ = 0;
};

class DataRate {
 public:
  constexpr DataRate() = default;
  static constexpr DataRate BitsPerSec(int64_t bps) { return DataRate(bps); }
  static constexpr DataRate KilobitsPerSec(int64_t kbps) { return DataRate(kbps * 1000); }
  static constexpr DataRate Zero() { return DataRate(0); }
  static constexpr DataRate PlusInfinity() { return DataRate(std::numeric_limits<int64_t>::max()); }
  constexpr int64_t bps() const { return bps_; }
  constexpr double kbps() const { return static_cast<double>(bps_) / 1e3; }
  constexpr bool IsFinite() const { return bps_ != std::numeric_limits<int64_t>::max(); }
  constexpr bool IsZero() const { return bps_ == 0; }

  constexpr DataRate operator+(DataRate o) const { return DataRate(bps_ + o.bps_); }
  constexpr DataRate operator-(DataRate o) const { return DataRate(bps_ - o.bps_); }
  constexpr DataRate operator*(double f) const { return DataRate(static_cast<int64_t>(bps_ * f)); }
  constexpr double operator/(DataRate o) const { return static_cast<double>(bps_) / o.bps_; }
  constexpr DataRate& operator+=(DataRate o) { bps_ += o.bps_; return *this; }
  constexpr auto operator<=>(const DataRate&) const = default;

  // size / time -> rate, rate * time -> size.
  static constexpr DataRate FromSizeAndTime(DataSize s, TimeDelta t) {
    return t.us() <= 0 ? DataRate::Zero() : DataRate(s.bits() * 1000000 / t.us());
  }
  constexpr DataSize operator*(TimeDelta t) const { return DataSize::Bytes(bps_ * t.us() / 8000000); }

 private:
  constexpr explicit DataRate(int64_t bps) : bps_(bps) {}
  int64_t bps_ = 0;
};

template <class T>
constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (hi < v ? hi : v); }

}  // namespace sm
