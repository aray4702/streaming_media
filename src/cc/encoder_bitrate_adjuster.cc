#include "sm/cc/encoder_bitrate_adjuster.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
// The buffer level for media-rate utilization is allowed to go below zero,
// down to -(kMaxMediaUnderrunFrames / target_framerate_fps) * target_bitrate.
constexpr double kMaxMediaUnderrunFrames = 5.0;
// VideoEncoder::EncoderInfo::kMaxFramerateFraction, and the one temporal layer's
// fps_allocation (100%).
constexpr double kMaxFramerateFraction = 255;
}  // namespace

// ---- EncoderOvershootDetector (video/encoder_overshoot_detector.cc) -------

void EncoderOvershootDetector::SetTargetRate(DataRate target_bitrate, double target_framerate_fps,
                                             int64_t time_ms) {
  // First leak bits according to the previous target rate.
  if (target_bitrate_.bps() != 0) {
    LeakBits(time_ms);
  } else if (target_bitrate.bps() != 0) {
    // Stream was just enabled, reset state.
    time_last_update_ms_ = time_ms;
    utilization_factors_.clear();
    sum_network_utilization_factors_ = 0.0;
    sum_media_utilization_factors_ = 0.0;
    network_buffer_level_bits_ = 0;
    media_buffer_level_bits_ = 0;
  }
  target_bitrate_ = target_bitrate;
  target_framerate_fps_ = target_framerate_fps;
}

void EncoderOvershootDetector::OnEncodedFrame(size_t bytes, int64_t time_ms) {
  if (bytes == 0) {
    if (time_last_update_ms_ != -1) time_last_update_ms_ = std::max(time_last_update_ms_, time_ms);
    return;
  }
  // Leak bits from the virtual pacer buffer, according to the current target bitrate.
  LeakBits(time_ms);

  const int64_t frame_size_bits = static_cast<int64_t>(bytes) * 8;
  // Ideal size of a frame given the current rates.
  const int64_t ideal_frame_size_bits = IdealFrameSizeBits();
  if (ideal_frame_size_bits == 0) return;  // No updated bitrate and/or framerate yet.

  const double network_utilization_factor =
      HandleEncodedFrame(static_cast<size_t>(frame_size_bits), ideal_frame_size_bits, time_ms,
                         &network_buffer_level_bits_);
  const double media_utilization_factor = HandleEncodedFrame(
      static_cast<size_t>(frame_size_bits), ideal_frame_size_bits, time_ms, &media_buffer_level_bits_);

  sum_network_utilization_factors_ += network_utilization_factor;
  sum_media_utilization_factors_ += media_utilization_factor;
  utilization_factors_.push_back({network_utilization_factor, media_utilization_factor, time_ms});
}

double EncoderOvershootDetector::HandleEncodedFrame(size_t frame_size_bits, int64_t ideal_frame_size_bits,
                                                    int64_t /*time_ms*/, int64_t* buffer_level_bits) const {
  // Add the frame to the buffer level. If that exceeds the ideal buffer size,
  // penalize this frame but cap the overshoot to the current buffer level
  // rather than the size of this frame, so a single large frame is not
  // penalized if the encoder afterwards compensates by dropping frames and/or
  // reducing frame size.
  const int64_t bitsum = static_cast<int64_t>(frame_size_bits) + *buffer_level_bits;
  int64_t overshoot_bits = 0;
  if (bitsum > ideal_frame_size_bits) {
    overshoot_bits = std::min(*buffer_level_bits, bitsum - ideal_frame_size_bits);
  }

  // The (over) utilization of this frame; never below 1.0.
  double utilization_factor;
  if (utilization_factors_.empty()) {
    // First frame: nothing to compare with, so size against ideal size.
    utilization_factor =
        std::max(1.0, static_cast<double>(frame_size_bits) / static_cast<double>(ideal_frame_size_bits));
  } else {
    utilization_factor = 1.0 + (static_cast<double>(overshoot_bits) / static_cast<double>(ideal_frame_size_bits));
  }

  // Remove the overshot bits from the virtual buffer so they are not penalized twice.
  *buffer_level_bits -= overshoot_bits;
  *buffer_level_bits += static_cast<int64_t>(frame_size_bits);
  return utilization_factor;
}

std::optional<double> EncoderOvershootDetector::GetNetworkRateUtilizationFactor(int64_t time_ms) {
  CullOldUpdates(time_ms);
  if (utilization_factors_.empty()) return std::nullopt;
  return sum_network_utilization_factors_ / static_cast<double>(utilization_factors_.size());
}

std::optional<double> EncoderOvershootDetector::GetMediaRateUtilizationFactor(int64_t time_ms) {
  CullOldUpdates(time_ms);
  if (utilization_factors_.empty()) return std::nullopt;
  return sum_media_utilization_factors_ / static_cast<double>(utilization_factors_.size());
}

void EncoderOvershootDetector::Reset() {
  time_last_update_ms_ = -1;
  utilization_factors_.clear();
  target_bitrate_ = DataRate::BitsPerSec(0);
  sum_network_utilization_factors_ = 0.0;
  sum_media_utilization_factors_ = 0.0;
  target_framerate_fps_ = 0.0;
  network_buffer_level_bits_ = 0;
  media_buffer_level_bits_ = 0;
}

int64_t EncoderOvershootDetector::IdealFrameSizeBits() const {
  if (target_framerate_fps_ <= 0 || target_bitrate_.bps() == 0) return 0;
  // Current ideal frame size, based on the current target bitrate.
  return static_cast<int64_t>((static_cast<double>(target_bitrate_.bps()) + target_framerate_fps_ / 2) /
                              target_framerate_fps_);
}

void EncoderOvershootDetector::LeakBits(int64_t time_ms) {
  if (time_last_update_ms_ != -1 && target_bitrate_.bps() > 0) {
    const int64_t time_delta_ms = time_ms - time_last_update_ms_;
    // Leak bits according to the current target bitrate.
    const int64_t leaked_bits = (target_bitrate_.bps() * time_delta_ms) / 1000;
    // The network buffer may not go below zero.
    network_buffer_level_bits_ = std::max<int64_t>(0, network_buffer_level_bits_ - leaked_bits);
    // The media buffer may go down to minus kMaxMediaUnderrunFrames frames' worth of data.
    const double max_underrun_seconds =
        std::min(kMaxMediaUnderrunFrames, target_framerate_fps_) / target_framerate_fps_;
    media_buffer_level_bits_ = std::max<int64_t>(
        static_cast<int64_t>(-max_underrun_seconds * static_cast<double>(target_bitrate_.bps())),
        media_buffer_level_bits_ - leaked_bits);
  }
  time_last_update_ms_ = time_ms;
}

void EncoderOvershootDetector::CullOldUpdates(int64_t time_ms) {
  const int64_t cutoff_time_ms = time_ms - window_size_ms_;
  while (!utilization_factors_.empty() && utilization_factors_.front().update_time_ms < cutoff_time_ms) {
    // The sums never go negative through rounding.
    sum_network_utilization_factors_ =
        std::max(0.0, sum_network_utilization_factors_ - utilization_factors_.front().network_utilization_factor);
    sum_media_utilization_factors_ =
        std::max(0.0, sum_media_utilization_factors_ - utilization_factors_.front().media_utilization_factor);
    utilization_factors_.pop_front();
  }
}

// ---- RateUtilizationTracker (video/rate_utilization_tracker.cc) ------------

namespace {
// api/units/data_rate.h operator*(DataRate, TimeDelta): rounded to the byte.
DataSize RateTimesDuration(DataRate rate, TimeDelta duration) {
  const int64_t microbits = rate.bps() * duration.us();
  return DataSize::Bytes((microbits + 4000000) / 8000000);
}
}  // namespace

void RateUtilizationTracker::OnDataRateChanged(DataRate rate, Timestamp time) {
  current_rate_ = rate;
  // The first entry holds the first produced data, so only the rate is kept.
  if (data_points_.empty()) return;
  RateUsageUpdate& last_data_point = data_points_.back();
  time = std::max(time, last_data_point.time);
  if (last_data_point.time == time) {
    last_data_point.target_rate = rate;
  } else {
    data_points_.push_back({time, rate, DataSize::Zero()});
  }
  CullOldData(time);
}

void RateUtilizationTracker::OnDataProduced(DataSize size, Timestamp time) {
  if (data_points_.empty()) {
    data_points_.push_back({time, current_rate_, size});
  } else {
    RateUsageUpdate& last_data_point = data_points_.back();
    time = std::max(time, last_data_point.time);
    if (last_data_point.time == time) {
      last_data_point.produced_data += size;
    } else {
      data_points_.push_back({time, current_rate_, size});
    }
  }
  CullOldData(time);
}

std::optional<double> RateUtilizationTracker::GetRateUtilizationFactor(Timestamp time) const {
  if (data_points_.empty()) return std::nullopt;
  time = std::max(time, data_points_.back().time);
  DataSize allocated_send_data_size = DataSize::Zero();
  DataSize total_produced_data = DataSize::Zero();
  // The last time data was produced: how much it was, and how much rate budget
  // has been allocated since then.
  DataSize data_allocated_for_last_data = DataSize::Zero();
  DataSize size_of_last_data = DataSize::Zero();
  for (size_t i = 0; i < data_points_.size(); ++i) {
    const RateUsageUpdate& update = data_points_[i];
    total_produced_data += update.produced_data;
    const DataSize allocated_since_previous_data_point =
        i == 0 ? DataSize::Zero()
               : RateTimesDuration(data_points_[i - 1].target_rate, update.time - data_points_[i - 1].time);
    allocated_send_data_size += allocated_since_previous_data_point;
    if (update.produced_data.bytes() == 0) {
      // Just a rate update past the last seen produced data.
      data_allocated_for_last_data =
          std::min(size_of_last_data, data_allocated_for_last_data + allocated_since_previous_data_point);
    } else {
      // A newer data point with produced data: reset the accumulator.
      size_of_last_data = update.produced_data;
      data_allocated_for_last_data = DataSize::Zero();
    }
  }
  // No allocated rate across all of the data points: ignore.
  if (allocated_send_data_size.bytes() == 0 && current_rate_.IsZero()) return std::nullopt;
  // The budget past the very last data point until the polling time, or the
  // last produced data if that is larger (less the budget of rate updates after
  // it was produced).
  const RateUsageUpdate& last_update = data_points_.back();
  const DataSize allocated_since_last_data_point = RateTimesDuration(last_update.target_rate, time - last_update.time);
  allocated_send_data_size += std::max(allocated_since_last_data_point, size_of_last_data - data_allocated_for_last_data);
  return static_cast<double>(total_produced_data.bytes()) / static_cast<double>(allocated_send_data_size.bytes());
}

void RateUtilizationTracker::CullOldData(Timestamp time) {
  // Drop data points that are too old or over the count limit, and keep the
  // first entry one with produced data, since the budget runs from its time.
  const Timestamp oldest_included_time =
      time.ms() > max_duration_.ms() ? time - max_duration_ : Timestamp::Micros(0);
  while (!data_points_.empty() &&
         (data_points_.front().time < oldest_included_time || data_points_.size() > max_data_points_ ||
          data_points_.front().produced_data.bytes() == 0)) {
    data_points_.pop_front();
  }
}

// ---- EncoderBitrateAdjuster (video/encoder_bitrate_adjuster.cc) ------------

namespace {
// LayerRateInfo::WantedOvershoot.
DataRate WantedOvershoot(double link_utilization_factor, double media_utilization_factor, DataRate target_rate) {
  // If there is headroom, allow bitrate to go up to the media rate limit. Still
  // limit media utilization to 1.0, so we don't overshoot over long runs.
  const double max_media_utilization = std::max(1.0, media_utilization_factor);
  if (link_utilization_factor > max_media_utilization) {
    return DataRate::BitsPerSec(std::llround((link_utilization_factor - max_media_utilization) * static_cast<double>(target_rate.bps())));
  }
  return DataRate::BitsPerSec(0);
}
}  // namespace

DataRate EncoderBitrateAdjuster::AdjustRate(DataRate target, DataRate bandwidth_allocation, double framerate_fps,
                                            Timestamp now) {
  last_target_ = target;
  last_bandwidth_ = bandwidth_allocation;
  last_fps_ = framerate_fps;

  // The layer is enabled iff it has both a positive bitrate and framerate target.
  const bool active = target.bps() > 0 && framerate_fps > 0;
  if (active) {
    if (!detector_active_) {
      detector_.Reset();
      detector_active_ = true;
      frames_since_layout_change_ = 0;
    }
  } else if (detector_active_) {
    detector_.Reset();
    detector_active_ = false;
    frames_since_layout_change_ = 0;
  }
  if (cfg_.newfangled_headroom_adjustment) {
    // One media rate tracker for the stream; it takes the unadjusted target.
    if (target.IsZero()) {
      media_rate_tracker_.reset();
    } else {
      if (!media_rate_tracker_) {
        media_rate_tracker_.emplace(kMaxDataPointsInUtilizationTrackers, TimeDelta::Millis(kWindowSizeMs));
      }
      media_rate_tracker_->OnDataRateChanged(target, now);
    }
  }

  double link_utilization_factor, media_utilization_factor;
  if (frames_since_layout_change_ < kMinFramesSinceLayoutChange) {
    link_utilization_factor = kDefaultUtilizationFactor;
    media_utilization_factor = kDefaultUtilizationFactor;
  } else if (!active) {
    // No bitrate set: pass the bitrate through without any change.
    link_utilization_factor = 1.0;
    media_utilization_factor = 1.0;
  } else {
    link_utilization_factor = detector_.GetNetworkRateUtilizationFactor(now.ms()).value_or(kDefaultUtilizationFactor);
    media_utilization_factor =
        cfg_.newfangled_headroom_adjustment
            ? media_rate_tracker_->GetRateUtilizationFactor(now).value_or(kDefaultUtilizationFactor)
            : detector_.GetMediaRateUtilizationFactor(now.ms()).value_or(kDefaultUtilizationFactor);
  }

  DataRate wanted_overshoot_sum = DataRate::BitsPerSec(0);
  if (link_utilization_factor < 1.0) {
    // Don't boost the target bitrate if the encoder is under-using.
    link_utilization_factor = 1.0;
  } else {
    // Don't reduce the encoder target below 50%, in which case the frame
    // dropper should kick in instead.
    link_utilization_factor = std::min(link_utilization_factor, 2.0);
    wanted_overshoot_sum += WantedOvershoot(link_utilization_factor, media_utilization_factor, target);
  }
  link_factor_ = link_utilization_factor;
  media_factor_ = media_utilization_factor;

  // Available link headroom that can be used to fill the wanted overshoot.
  DataRate available_headroom = DataRate::BitsPerSec(0);
  if (cfg_.utilize_bandwidth_headroom) available_headroom = bandwidth_allocation - target;

  // All wanted overshoots are satisfied in the same proportion based on available headroom.
  const double granted_overshoot_ratio =
      wanted_overshoot_sum.bps() == 0
          ? 0.0
          : std::min(1.0, static_cast<double>(available_headroom.bps()) / static_cast<double>(wanted_overshoot_sum.bps()));

  double utilization_factor = link_utilization_factor;
  const DataRate allowed_overshoot = DataRate::BitsPerSec(std::llround(
      granted_overshoot_ratio *
      static_cast<double>(WantedOvershoot(link_utilization_factor, media_utilization_factor, target).bps())));
  if (allowed_overshoot.bps() > 0) {
    // Pretend the target bitrate is higher by the allowed overshoot.
    utilization_factor *= static_cast<double>(target.bps()) /
                          static_cast<double>(allowed_overshoot.bps() + target.bps());
  }
  const int64_t min_bps = cfg_.min_bitrate.bps();
  if (min_bps > 0 && target.bps() > 0 && min_bps < target.bps()) {
    // Make sure the adjuster doesn't push the target bitrate below the minimum.
    utilization_factor = std::min(utilization_factor, static_cast<double>(target.bps()) / static_cast<double>(min_bps));
  }

  int64_t adjusted_bps = 0;
  if (target.bps() > 0) {
    adjusted_bps = std::min(static_cast<int64_t>(static_cast<uint32_t>(static_cast<double>(target.bps()) / utilization_factor + 0.5)),
                            target.bps());
    // In case of rounding errors, add bitrate until the min bitrate constraint is met.
    if (adjusted_bps < min_bps) adjusted_bps = min_bps;
  }
  adjusted_ = DataRate::BitsPerSec(adjusted_bps);

  // Update the detector with the new adjusted bitrate target.
  if (adjusted_bps > 0 && detector_active_) {
    const double fps_fraction = kMaxFramerateFraction / kMaxFramerateFraction;
    detector_.SetTargetRate(adjusted_, fps_fraction * framerate_fps, now.ms());
  }
  return adjusted_;
}

void EncoderBitrateAdjuster::OnEncodedFrame(DataSize size, Timestamp now) {
  ++frames_since_layout_change_;
  if (detector_active_) detector_.OnEncodedFrame(static_cast<size_t>(size.bytes()), now.ms());
  if (media_rate_tracker_) media_rate_tracker_->OnDataProduced(size, now);
}

void EncoderBitrateAdjuster::OnFrameDropped(Timestamp now) {
  if (detector_active_) detector_.OnEncodedFrame(0, now.ms());
  if (media_rate_tracker_) media_rate_tracker_->OnDataProduced(DataSize::Zero(), now);
}

void EncoderBitrateAdjuster::Reset(Timestamp now) {
  detector_.Reset();
  detector_active_ = false;
  media_rate_tracker_.reset();
  // Re-create the detector at once with the last known rates.
  AdjustRate(last_target_, last_bandwidth_, last_fps_, now);
}

void EncoderBitrateAdjuster::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("link_utilization_factor", link_factor_);
  w.Gauge("media_utilization_factor", media_factor_);
  w.Gauge("adjusted_kbps", adjusted_.kbps());
}

}  // namespace sm::cc
