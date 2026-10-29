// Strongly typed time, size and rate units.
//
// Every algorithm in the library takes the current time as an explicit
// argument instead of reading a clock. That keeps the core deterministic,
// testable with a simulated clock and free of platform clock APIs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace sm {

// The arithmetic of libwebrtc's RelativeUnit (rtc_base/units/unit_base.h), on the raw int64 values
// every unit stores, with INT64_MAX and INT64_MIN as plus and minus infinity. libwebrtc RTC_DCHECKs
// an infinite operand of `*` and `/`, an integer constructor out of range and a result that lands on
// or past an infinity; its release build then overflows (undefined behaviour). Here those cases
// saturate: an infinite operand keeps its infinity (sign flipped by a negative scalar, zero by a zero
// one) and a finite result out of range becomes the infinity it ran into.
namespace units_impl {
inline constexpr int64_t kPlusInf = std::numeric_limits<int64_t>::max();
inline constexpr int64_t kMinusInf = std::numeric_limits<int64_t>::min();
constexpr bool IsInf(int64_t v) { return v == kPlusInf || v == kMinusInf; }
constexpr int64_t Neg(int64_t v) { return v == kPlusInf ? kMinusInf : v == kMinusInf ? kPlusInf : -v; }

// UnitBase::FromFraction for an integer (unit_base.h:105-112): value * denominator.
constexpr int64_t FromFraction(int64_t denominator, int64_t v) {
  if (v >= kPlusInf / denominator) return kPlusInf;
  if (v <= kMinusInf / denominator) return kMinusInf;
  return v * denominator;
}
// UnitBase::FromValue for a floating point value (unit_base.h:94-103): +-inf map to the infinities,
// anything else truncates (dchecked_cast). NaN, a DCHECK there, is zero here.
constexpr int64_t FromDouble(double v) {
  if (v != v) return 0;
  if (v >= 9223372036854775808.0) return kPlusInf;
  if (v <= -9223372036854775808.0) return kMinusInf;
  return static_cast<int64_t>(v);
}
// UnitBase::ToValue<double> (unit_base.h:120-125): the infinities become +-inf.
constexpr double ToDouble(int64_t v) {
  return v == kPlusInf ? std::numeric_limits<double>::infinity()
       : v == kMinusInf ? -std::numeric_limits<double>::infinity()
                        : static_cast<double>(v);
}
// RelativeUnit::operator+ and operator- (unit_base.h:218-241).
constexpr int64_t Add(int64_t a, int64_t b) {
  if (a == kPlusInf || b == kPlusInf) return kPlusInf;
  if (a == kMinusInf || b == kMinusInf) return kMinusInf;
  if (b > 0 && a >= kPlusInf - b) return kPlusInf;
  if (b < 0 && a <= kMinusInf - b) return kMinusInf;
  return a + b;
}
constexpr int64_t Sub(int64_t a, int64_t b) {
  if (a == kPlusInf || b == kMinusInf) return kPlusInf;
  if (a == kMinusInf || b == kPlusInf) return kMinusInf;
  if (b < 0 && a >= kPlusInf + b) return kPlusInf;
  if (b > 0 && a <= kMinusInf + b) return kMinusInf;
  return a - b;
}
// RelativeUnit::operator*(int64_t / int32_t / size_t) (unit_base.h:268-276).
constexpr int64_t MulInt(int64_t a, int64_t s) {
  if (a == 0 || s == 0) return 0;
  const bool positive = (a > 0) == (s > 0);
  if (IsInf(a)) return positive ? kPlusInf : kMinusInf;
  const bool overflow = a > 0 ? (s > 0 ? a > kPlusInf / s : s < kMinusInf / a)
                              : (s > 0 ? a < kMinusInf / s : s < kPlusInf / a);
  if (overflow) return positive ? kPlusInf : kMinusInf;
  return a * s;
}
// std::llround, constexpr: the nearest integer, halves away from zero. The fractional part of a
// double is exact, so comparing it with 0.5 rounds as llround does. Out of range saturates, NaN is
// zero (both undefined for llround).
constexpr int64_t RoundDouble(double v) {
  if (v != v) return 0;
  if (v >= 9223372036854775808.0) return kPlusInf;
  if (v <= -9223372036854775808.0) return kMinusInf;
  int64_t t = static_cast<int64_t>(v);
  const double frac = v - static_cast<double>(t);
  if (frac >= 0.5) ++t;
  else if (frac <= -0.5) --t;
  return t;
}
// RelativeUnit::operator*(double) (unit_base.h:265-267): std::llround(value * scalar) (#461).
constexpr int64_t MulDouble(int64_t a, double s) {
  if (IsInf(a)) return s > 0 ? a : s < 0 ? Neg(a) : 0;
  return RoundDouble(static_cast<double>(a) * s);
}
// rtc_base/numerics/divide_round.h DivideRoundToNearest(v, d) for d > 0, used by UnitBase::ToFraction
// (unit_base.h:163-167) for the integer getters: the nearest integer, halves toward plus infinity
// (1.5 -> 2, -1.5 -> -1).
constexpr int64_t DivideRoundToNearest(int64_t v, int64_t d) {
  int64_t q = v / d;
  const int64_t r = v % d;
  if (v < 0) {
    if (-r > d / 2) --q;
  } else if (r > (d - 1) / 2) {
    ++q;
  }
  return q;
}
constexpr int64_t ToInt64(size_t v) {
  return static_cast<uint64_t>(v) > static_cast<uint64_t>(kPlusInf) ? kPlusInf : static_cast<int64_t>(v);
}
// RelativeUnit::operator/ by an integer (unit_base.h:259-263): truncating division.
constexpr int64_t Div(int64_t a, int64_t d) {
  if (IsInf(a)) return d < 0 ? Neg(a) : a;
  return a / d;
}
}  // namespace units_impl

class TimeDelta {
 public:
  constexpr TimeDelta() = default;
  static constexpr TimeDelta Micros(int64_t us) { return TimeDelta(us); }
  static constexpr TimeDelta Millis(int64_t ms) { return TimeDelta(units_impl::FromFraction(1000, ms)); }
  static constexpr TimeDelta Seconds(int64_t s) { return TimeDelta(units_impl::FromFraction(1000000, s)); }
  static constexpr TimeDelta SecondsF(double s) { return TimeDelta(units_impl::FromDouble(s * 1e6)); }
  static constexpr TimeDelta MillisF(double ms) { return TimeDelta(units_impl::FromDouble(ms * 1e3)); }
  static constexpr TimeDelta Zero() { return TimeDelta(0); }
  static constexpr TimeDelta PlusInfinity() { return TimeDelta(std::numeric_limits<int64_t>::max()); }
  static constexpr TimeDelta MinusInfinity() { return TimeDelta(std::numeric_limits<int64_t>::min()); }

  constexpr int64_t us() const { return us_; }
  // TimeDelta::ms() (api/units/time_delta.h:64-66): ToFraction<1000>, rounded to the nearest (#461).
  constexpr int64_t ms() const { return units_impl::DivideRoundToNearest(us_, 1000); }
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
  constexpr TimeDelta operator+(TimeDelta o) const { return TimeDelta(units_impl::Add(us_, o.us_)); }
  constexpr TimeDelta operator-(TimeDelta o) const { return TimeDelta(units_impl::Sub(us_, o.us_)); }
  constexpr TimeDelta operator-() const { return TimeDelta(units_impl::Neg(us_)); }
  constexpr TimeDelta operator*(double f) const { return TimeDelta(units_impl::MulDouble(us_, f)); }
  constexpr TimeDelta operator*(int64_t f) const { return TimeDelta(units_impl::MulInt(us_, f)); }
  constexpr TimeDelta operator*(int32_t f) const { return TimeDelta(units_impl::MulInt(us_, f)); }
  constexpr TimeDelta operator*(size_t f) const { return TimeDelta(units_impl::MulInt(us_, units_impl::ToInt64(f))); }
  constexpr TimeDelta operator/(int64_t d) const { return TimeDelta(units_impl::Div(us_, d)); }
  constexpr TimeDelta operator/(int32_t d) const { return TimeDelta(units_impl::Div(us_, d)); }
  constexpr double operator/(TimeDelta o) const { return units_impl::ToDouble(us_) / units_impl::ToDouble(o.us_); }
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
  static constexpr Timestamp Millis(int64_t ms) { return Timestamp(units_impl::FromFraction(1000, ms)); }
  static constexpr Timestamp Seconds(int64_t s) { return Timestamp(units_impl::FromFraction(1000000, s)); }
  static constexpr Timestamp PlusInfinity() { return Timestamp(std::numeric_limits<int64_t>::max()); }
  static constexpr Timestamp MinusInfinity() { return Timestamp(std::numeric_limits<int64_t>::min()); }

  constexpr int64_t us() const { return us_; }
  // Timestamp::ms() (api/units/timestamp.h:56-58): ToFraction<1000>, rounded to the nearest (#461).
  constexpr int64_t ms() const { return units_impl::DivideRoundToNearest(us_, 1000); }
  constexpr double seconds() const { return static_cast<double>(us_) / 1e6; }
  constexpr bool IsFinite() const {
    return us_ != std::numeric_limits<int64_t>::max() && us_ != std::numeric_limits<int64_t>::min();
  }

  constexpr bool IsPlusInfinity() const { return us_ == std::numeric_limits<int64_t>::max(); }
  constexpr bool IsMinusInfinity() const { return us_ == std::numeric_limits<int64_t>::min(); }

  // Infinite operands saturate, as libwebrtc's Timestamp (api/units/timestamp.h:78-114).
  constexpr Timestamp operator+(TimeDelta d) const { return Timestamp(units_impl::Add(us_, d.us())); }
  constexpr Timestamp operator-(TimeDelta d) const { return Timestamp(units_impl::Sub(us_, d.us())); }
  constexpr TimeDelta operator-(Timestamp o) const { return TimeDelta::Micros(units_impl::Sub(us_, o.us_)); }
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
  static constexpr DataSize PlusInfinity() { return DataSize(units_impl::kPlusInf); }
  static constexpr DataSize MinusInfinity() { return DataSize(units_impl::kMinusInf); }
  constexpr int64_t bytes() const { return bytes_; }
  // UnitBase::ToMultiple<8> (unit_base.h:175-181), saturating as MulInt.
  constexpr int64_t bits() const { return units_impl::MulInt(bytes_, 8); }
  constexpr bool IsFinite() const { return !units_impl::IsInf(bytes_); }
  constexpr bool IsPlusInfinity() const { return bytes_ == units_impl::kPlusInf; }
  constexpr bool IsMinusInfinity() const { return bytes_ == units_impl::kMinusInf; }
  // RelativeUnit operators (rtc_base/units/unit_base.h), saturating as TimeDelta's.
  constexpr DataSize operator+(DataSize o) const { return DataSize(units_impl::Add(bytes_, o.bytes_)); }
  constexpr DataSize operator-(DataSize o) const { return DataSize(units_impl::Sub(bytes_, o.bytes_)); }
  constexpr DataSize operator*(double f) const { return DataSize(units_impl::MulDouble(bytes_, f)); }
  constexpr DataSize operator*(int64_t f) const { return DataSize(units_impl::MulInt(bytes_, f)); }
  constexpr DataSize operator*(int32_t f) const { return DataSize(units_impl::MulInt(bytes_, f)); }
  constexpr DataSize operator*(size_t f) const { return DataSize(units_impl::MulInt(bytes_, units_impl::ToInt64(f))); }
  constexpr DataSize operator/(int64_t d) const { return DataSize(units_impl::Div(bytes_, d)); }
  constexpr DataSize operator/(int32_t d) const { return DataSize(units_impl::Div(bytes_, d)); }
  constexpr double operator/(DataSize o) const { return units_impl::ToDouble(bytes_) / units_impl::ToDouble(o.bytes_); }
  constexpr DataSize& operator+=(DataSize o) { return *this = *this + o; }
  constexpr DataSize& operator-=(DataSize o) { return *this = *this - o; }
  constexpr auto operator<=>(const DataSize&) const = default;

 private:
  constexpr explicit DataSize(int64_t b) : bytes_(b) {}
  int64_t bytes_ = 0;
};

class DataRate {
 public:
  constexpr DataRate() = default;
  static constexpr DataRate BitsPerSec(int64_t bps) { return DataRate(bps); }
  static constexpr DataRate KilobitsPerSec(int64_t kbps) { return DataRate(units_impl::FromFraction(1000, kbps)); }
  static constexpr DataRate Zero() { return DataRate(0); }
  static constexpr DataRate PlusInfinity() { return DataRate(units_impl::kPlusInf); }
  static constexpr DataRate MinusInfinity() { return DataRate(units_impl::kMinusInf); }
  constexpr int64_t bps() const { return bps_; }
  constexpr double kbps() const { return static_cast<double>(bps_) / 1e3; }
  constexpr bool IsFinite() const { return !units_impl::IsInf(bps_); }
  constexpr bool IsPlusInfinity() const { return bps_ == units_impl::kPlusInf; }
  constexpr bool IsMinusInfinity() const { return bps_ == units_impl::kMinusInf; }
  constexpr bool IsZero() const { return bps_ == 0; }

  // RelativeUnit operators (rtc_base/units/unit_base.h), saturating as TimeDelta's.
  constexpr DataRate operator+(DataRate o) const { return DataRate(units_impl::Add(bps_, o.bps_)); }
  constexpr DataRate operator-(DataRate o) const { return DataRate(units_impl::Sub(bps_, o.bps_)); }
  constexpr DataRate operator*(double f) const { return DataRate(units_impl::MulDouble(bps_, f)); }
  constexpr DataRate operator*(int64_t f) const { return DataRate(units_impl::MulInt(bps_, f)); }
  constexpr DataRate operator*(int32_t f) const { return DataRate(units_impl::MulInt(bps_, f)); }
  constexpr DataRate operator*(size_t f) const { return DataRate(units_impl::MulInt(bps_, units_impl::ToInt64(f))); }
  constexpr DataRate operator/(int64_t d) const { return DataRate(units_impl::Div(bps_, d)); }
  constexpr DataRate operator/(int32_t d) const { return DataRate(units_impl::Div(bps_, d)); }
  constexpr double operator/(DataRate o) const { return units_impl::ToDouble(bps_) / units_impl::ToDouble(o.bps_); }
  constexpr DataRate& operator+=(DataRate o) { return *this = *this + o; }
  constexpr DataRate& operator-=(DataRate o) { return *this = *this - o; }
  constexpr auto operator<=>(const DataRate&) const = default;

  // size / time -> rate, rate * time -> size.
  // operator/(DataSize, TimeDelta) (api/units/data_rate.h:99-102): data_rate_impl::Microbits(size) / us,
  // truncated; the microbits saturate as MulInt.
  static constexpr DataRate FromSizeAndTime(DataSize s, TimeDelta t) {
    return t.us() <= 0 ? DataRate::Zero() : DataRate(units_impl::Div(units_impl::MulInt(s.bytes(), 8000000), t.us()));
  }
  // operator*(DataRate, TimeDelta) (api/units/data_rate.h:106-110): the microbits rounded to the
  // nearest byte, (microbits + 4000000) / 8000000 (#461).
  constexpr DataSize operator*(TimeDelta t) const {
    const int64_t microbits = units_impl::MulInt(bps_, t.us());
    if (units_impl::IsInf(microbits)) return microbits > 0 ? DataSize::PlusInfinity() : DataSize::MinusInfinity();
    // The same value, without overflowing the + 4000000 for microbits near the maximum.
    if (microbits >= 0) return DataSize::Bytes(microbits / 8000000 + (microbits % 8000000 + 4000000) / 8000000);
    return DataSize::Bytes((microbits + 4000000) / 8000000);
  }

 private:
  constexpr explicit DataRate(int64_t bps) : bps_(bps) {}
  int64_t bps_ = 0;
};

constexpr DataSize operator*(TimeDelta t, DataRate r) { return r * t; }

// Scalar times unit, as unit_base.h's free operator* overloads.
constexpr TimeDelta operator*(double f, TimeDelta v) { return v * f; }
constexpr TimeDelta operator*(int64_t f, TimeDelta v) { return v * f; }
constexpr TimeDelta operator*(int32_t f, TimeDelta v) { return v * f; }
constexpr TimeDelta operator*(size_t f, TimeDelta v) { return v * f; }
constexpr DataSize operator*(double f, DataSize v) { return v * f; }
constexpr DataSize operator*(int64_t f, DataSize v) { return v * f; }
constexpr DataSize operator*(int32_t f, DataSize v) { return v * f; }
constexpr DataSize operator*(size_t f, DataSize v) { return v * f; }
constexpr DataRate operator*(double f, DataRate v) { return v * f; }
constexpr DataRate operator*(int64_t f, DataRate v) { return v * f; }
constexpr DataRate operator*(int32_t f, DataRate v) { return v * f; }
constexpr DataRate operator*(size_t f, DataRate v) { return v * f; }

template <class T>
constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (hi < v ? hi : v); }

}  // namespace sm
