// Wrap-around aware sequence number arithmetic (RFC 1982 serial numbers) and
// unwrapping to a monotonic 64-bit space.
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace sm {

// True when `a` is newer than `b` on the wrapping circle of T.
template <class T>
constexpr bool AheadOf(T a, T b) {
  static_assert(std::is_unsigned_v<T>);
  constexpr T kHalf = static_cast<T>(std::numeric_limits<T>::max() / 2 + 1);
  if (a - b == kHalf) return b < a;  // Break the tie deterministically.
  return a != b && static_cast<T>(a - b) < kHalf;
}

template <class T>
constexpr T ForwardDiff(T from, T to) { return static_cast<T>(to - from); }

// Maps a wrapping sequence (uint16 RTP seq, uint32 RTP timestamp) into int64.
template <class T>
class SeqNumUnwrapper {
 public:
  int64_t Unwrap(T value) {
    if (!last_) {
      last_unwrapped_ = value;
    } else {
      last_unwrapped_ += Delta(*last_, value);
    }
    last_ = value;
    return last_unwrapped_;
  }
  // Unwraps without updating state.
  int64_t PeekUnwrap(T value) const {
    if (!last_) return value;
    return last_unwrapped_ + Delta(*last_, value);
  }
  void Reset() { last_.reset(); }

 private:
  static int64_t Delta(T last, T value) {
    constexpr int64_t kRange = int64_t{std::numeric_limits<T>::max()} + 1;
    int64_t d = static_cast<int64_t>(static_cast<T>(value - last));
    if (!AheadOf<T>(value, last) && value != last) d -= kRange;
    return d;
  }
  std::optional<T> last_;
  int64_t last_unwrapped_ = 0;
};

}  // namespace sm
