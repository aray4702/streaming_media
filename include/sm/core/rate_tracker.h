// libwebrtc's RateTracker (rtc_base/rate_tracker.cc; not in
// compare/webrtc/reference/, read from the M150 release branch, unchanged in
// years): a rate over `bucket_count` buckets of `bucket_ms` each, kept in a
// ring of bucket_count + 1 so the bucket in progress is counted in full and
// the oldest one only for its part inside the interval. Until a full interval
// has passed since the first Update, the rate is over the time since then,
// and it is 0 for the first bucket. SendStatisticsProxy measures the sent
// frame rate with (100 ms, 10) and starts it with Update(0) at the first
// incoming frame (send_statistics_proxy.cc:67-68, 1191-1192, 1234-1256).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm {

class RateTracker {
 public:
  RateTracker(int64_t bucket_ms, size_t bucket_count)
      : bucket_ms_(bucket_ms), bucket_count_(bucket_count), buckets_(bucket_count + 1, 0) {}

  // Samples per second over the last bucket_ms * bucket_count.
  double Rate(int64_t now_ms) const { return ComputeRateForInterval(now_ms, bucket_ms_ * static_cast<int64_t>(bucket_count_)); }

  double ComputeRateForInterval(int64_t now_ms, int64_t interval_ms) const {
    if (bucket_start_ms_ == kTimeUnset) return 0.0;
    int64_t available_ms = std::min(interval_ms, bucket_ms_ * static_cast<int64_t>(bucket_count_));
    // Old buckets (after the current one in the ring) that are outside the interval, and the part of
    // the first counted bucket that is.
    size_t buckets_to_skip;
    int64_t ms_to_skip;
    if (now_ms > initialization_ms_ + available_ms) {
      const int64_t time_to_skip =
          now_ms - bucket_start_ms_ + static_cast<int64_t>(bucket_count_) * bucket_ms_ - available_ms;
      buckets_to_skip = static_cast<size_t>(time_to_skip / bucket_ms_);
      ms_to_skip = time_to_skip % bucket_ms_;
    } else {
      buckets_to_skip = bucket_count_ - current_bucket_;
      ms_to_skip = 0;
      available_ms = now_ms - initialization_ms_;
      // One bucket interval passes after initialization before a rate is reported.
      if (available_ms < bucket_ms_) return 0.0;
    }
    if (buckets_to_skip > bucket_count_ || available_ms == 0) return 0.0;
    const size_t start = NextBucketIndex(current_bucket_ + buckets_to_skip);
    int64_t total = (buckets_[start] * (bucket_ms_ - ms_to_skip) + (bucket_ms_ >> 1)) / bucket_ms_;
    for (size_t i = NextBucketIndex(start); i != NextBucketIndex(current_bucket_); i = NextBucketIndex(i)) total += buckets_[i];
    return static_cast<double>(total * 1000) / static_cast<double>(available_ms);
  }

  int64_t TotalSampleCount() const { return total_sample_count_; }

  void Update(int64_t sample_count, int64_t now_ms) {
    if (bucket_start_ms_ == kTimeUnset) {
      initialization_ms_ = now_ms;
      bucket_start_ms_ = now_ms;
      current_bucket_ = 0;
      buckets_[current_bucket_] = 0;
    }
    // Advance to the bucket of `now_ms`, clearing the buckets passed.
    for (size_t i = 0; i <= bucket_count_ && now_ms >= bucket_start_ms_ + bucket_ms_; ++i) {
      bucket_start_ms_ += bucket_ms_;
      current_bucket_ = NextBucketIndex(current_bucket_);
      buckets_[current_bucket_] = 0;
    }
    // After a gap longer than the whole ring, move the start in one step.
    bucket_start_ms_ += bucket_ms_ * ((now_ms - bucket_start_ms_) / bucket_ms_);
    buckets_[current_bucket_] += sample_count;
    total_sample_count_ += sample_count;
  }

 private:
  static constexpr int64_t kTimeUnset = -1;
  size_t NextBucketIndex(size_t i) const { return (i + 1u) % (bucket_count_ + 1u); }

  int64_t bucket_ms_;
  size_t bucket_count_;
  std::vector<int64_t> buckets_;
  int64_t total_sample_count_ = 0;
  size_t current_bucket_ = 0;
  int64_t bucket_start_ms_ = kTimeUnset;
  int64_t initialization_ms_ = 0;
};

}  // namespace sm
