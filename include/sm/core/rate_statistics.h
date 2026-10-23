// libwebrtc's RateStatistics (rtc_base/rate_statistics.cc in
// compare/webrtc/reference/): a sliding-window rate over 1 ms buckets. The
// rate divides by the *active* window, which starts at the first sample and
// restarts when a sample comes after the tracker emptied and its last sample
// is older than 1.5 windows. So a stream that starts (or restarts after a
// pause) is reported at its rate since it started, not averaged over the whole
// window. Rate() is empty with no samples, an active window of 1 ms or less,
// or a single sample in a window not yet full.
//
// BitrateTracker (rtc_base/bitrate_tracker.cc, not in reference/) is this class
// with scale 8000: bytes counted, bits per second returned.
#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <optional>

namespace sm {

class RateStatistics {
 public:
  RateStatistics(int64_t window_size_ms, float scale) : scale_(scale), window_size_ms_(window_size_ms) {}

  void Update(int64_t count, int64_t now_ms) {
    const int64_t recent_sample_time_margin = static_cast<int64_t>(1.5 * static_cast<double>(window_size_ms_));
    const bool last_sample_is_recent = !buckets_.empty() && buckets_.back().timestamp > now_ms - recent_sample_time_margin;
    EraseOld(now_ms);
    if (first_timestamp_ == -1 || (num_samples_ == 0 && !last_sample_is_recent)) first_timestamp_ = now_ms;
    if (buckets_.empty() || now_ms != buckets_.back().timestamp) {
      // A timestamp before the last bucket is aligned to it.
      if (!buckets_.empty() && now_ms < buckets_.back().timestamp) now_ms = buckets_.back().timestamp;
      buckets_.push_back({now_ms, 0, 0});
    }
    Bucket& last = buckets_.back();
    last.sum += count;
    ++last.num_samples;
    if (std::numeric_limits<int64_t>::max() - accumulated_count_ > count) {
      accumulated_count_ += count;
    } else {
      overflow_ = true;
    }
    ++num_samples_;
  }

  std::optional<int64_t> Rate(int64_t now_ms) const {
    EraseOld(now_ms);
    int64_t active_window_size = 0;
    if (first_timestamp_ != -1) {
      active_window_size =
          first_timestamp_ <= now_ms - window_size_ms_ ? window_size_ms_ : now_ms - first_timestamp_ + 1;
    }
    if (num_samples_ == 0 || active_window_size <= 1 || (num_samples_ <= 1 && active_window_size < window_size_ms_) ||
        overflow_) {
      return std::nullopt;
    }
    const float scale = scale_ / static_cast<float>(active_window_size);
    const float result = static_cast<float>(accumulated_count_) * scale + 0.5f;
    if (result > static_cast<float>(std::numeric_limits<int64_t>::max())) return std::nullopt;
    return static_cast<int64_t>(result);
  }

 private:
  struct Bucket {
    int64_t timestamp;
    int64_t sum;
    int64_t num_samples;
  };
  void EraseOld(int64_t now_ms) const {
    const int64_t new_oldest_time = now_ms - window_size_ms_ + 1;
    while (!buckets_.empty() && buckets_.front().timestamp < new_oldest_time) {
      accumulated_count_ -= buckets_.front().sum;
      num_samples_ -= buckets_.front().num_samples;
      buckets_.pop_front();
    }
  }

  // Rate() erases expired buckets, as libwebrtc's const_cast does.
  mutable std::deque<Bucket> buckets_;
  mutable int64_t accumulated_count_ = 0;
  mutable int64_t num_samples_ = 0;
  int64_t first_timestamp_ = -1;
  bool overflow_ = false;
  float scale_;
  int64_t window_size_ms_;
};

}  // namespace sm
