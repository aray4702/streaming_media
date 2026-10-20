#include "sm/timing/neteq_delay.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace sm::timing {

namespace {
// underrun_optimizer.cc, reorder_optimizer.cc.
constexpr int kDelayBuckets = 100;
constexpr int kBucketSizeMs = 20;
// delay_constraints.cc.
constexpr int kMinBaseMinimumDelayMs = 0;
constexpr int kMaxBaseMinimumDelayMs = 10000;
}  // namespace

// ---------------------------------------------------------------- Histogram

NetEqHistogram::NetEqHistogram(size_t num_buckets, int forget_factor, std::optional<double> start_forget_weight)
    : buckets_(num_buckets, 0), base_forget_factor_(forget_factor), start_forget_weight_(start_forget_weight) {}

void NetEqHistogram::Add(int value) {
  int vector_sum = 0;
  for (int& bucket : buckets_) {
    bucket = static_cast<int>((static_cast<int64_t>(bucket) * forget_factor_) >> 15);
    vector_sum += bucket;
  }
  buckets_[static_cast<size_t>(value)] += (32768 - forget_factor_) << 15;
  vector_sum += (32768 - forget_factor_) << 15;
  // The buckets should sum to 1 in Q30; correct the fixed-point rounding.
  vector_sum -= 1 << 30;
  if (vector_sum != 0) {
    int flip_sign = vector_sum > 0 ? -1 : 1;
    for (int& bucket : buckets_) {
      int correction = flip_sign * std::min(std::abs(vector_sum), bucket >> 4);
      bucket += correction;
      vector_sum += correction;
      if (std::abs(vector_sum) == 0) break;
    }
  }
  ++add_count_;
  if (start_forget_weight_) {
    if (forget_factor_ != base_forget_factor_) {
      int forget_factor = static_cast<int>((1 << 15) * (1 - start_forget_weight_.value() / (add_count_ + 1)));
      forget_factor_ = std::max(0, std::min(base_forget_factor_, forget_factor));
    }
  } else {
    forget_factor_ += (base_forget_factor_ - forget_factor_ + 3) >> 2;
  }
}

int NetEqHistogram::Quantile(int probability) const {
  int inverse_probability = (1 << 30) - probability;
  size_t index = 0;
  int sum = 1 << 30;
  sum -= buckets_[index];
  while (sum > inverse_probability && index < buckets_.size() - 1) {
    ++index;
    sum -= buckets_[index];
  }
  return static_cast<int>(index);
}

void NetEqHistogram::Reset() {
  // buckets_[i] = 0.5^(i+1) in Q30, summing to (slightly more than) 1.
  uint16_t temp_prob = 0x4002;
  for (int& bucket : buckets_) {
    temp_prob >>= 1;
    bucket = temp_prob << 16;
  }
  forget_factor_ = 0;
  add_count_ = 0;
}

// -------------------------------------------------------- UnderrunOptimizer

UnderrunOptimizer::UnderrunOptimizer(int histogram_quantile, int forget_factor, std::optional<int> start_forget_weight,
                                     std::optional<int> resample_interval_ms)
    : histogram_(kDelayBuckets, forget_factor, start_forget_weight),
      histogram_quantile_(histogram_quantile),
      resample_interval_ms_(resample_interval_ms) {}

void UnderrunOptimizer::Update(int relative_delay_ms, int64_t now_ms) {
  std::optional<int> histogram_update;
  if (resample_interval_ms_) {
    if (!resample_start_ms_) resample_start_ms_ = now_ms;
    if (now_ms - *resample_start_ms_ > *resample_interval_ms_) {
      histogram_update = max_delay_in_interval_ms_;
      resample_start_ms_ = now_ms;
      max_delay_in_interval_ms_ = 0;
    }
    max_delay_in_interval_ms_ = std::max(max_delay_in_interval_ms_, relative_delay_ms);
  } else {
    histogram_update = relative_delay_ms;
  }
  if (!histogram_update) return;
  const int index = *histogram_update / kBucketSizeMs;
  if (index < histogram_.NumBuckets()) histogram_.Add(index);  // At most 2000 ms.
  int bucket_index = histogram_.Quantile(histogram_quantile_);
  optimal_delay_ms_ = (1 + bucket_index) * kBucketSizeMs;
}

void UnderrunOptimizer::Reset() {
  histogram_.Reset();
  resample_start_ms_.reset();
  max_delay_in_interval_ms_ = 0;
  optimal_delay_ms_.reset();
}

// --------------------------------------------------------- ReorderOptimizer

ReorderOptimizer::ReorderOptimizer(int forget_factor, int ms_per_loss_percent, std::optional<int> start_forget_weight)
    : histogram_(kDelayBuckets, forget_factor, start_forget_weight), ms_per_loss_percent_(ms_per_loss_percent) {}

void ReorderOptimizer::Update(int relative_delay_ms, bool reordered, int base_delay_ms) {
  const int index = reordered ? relative_delay_ms / kBucketSizeMs : 0;
  if (index < histogram_.NumBuckets()) histogram_.Add(index);  // At most 2000 ms.
  int bucket_index = MinimizeCostFunction(base_delay_ms);
  optimal_delay_ms_ = (1 + bucket_index) * kBucketSizeMs;
}

void ReorderOptimizer::Reset() {
  histogram_.Reset();
  optimal_delay_ms_.reset();
}

int ReorderOptimizer::MinimizeCostFunction(int base_delay_ms) const {
  const std::vector<int>& buckets = histogram_.buckets();
  // Q30.
  int64_t loss_probability = 1 << 30;
  int64_t min_cost = std::numeric_limits<int64_t>::max();
  int min_bucket = 0;
  for (int i = 0; i < static_cast<int>(buckets.size()); ++i) {
    loss_probability -= buckets[static_cast<size_t>(i)];
    int64_t delay_ms = static_cast<int64_t>(std::max(0, i * kBucketSizeMs - base_delay_ms)) << 30;
    int64_t cost = delay_ms + 100 * ms_per_loss_percent_ * loss_probability;
    if (cost < min_cost) {
      min_cost = cost;
      min_bucket = i;
    }
    if (loss_probability == 0) break;
  }
  return min_bucket;
}

// ----------------------------------------------------- PacketArrivalHistory

bool PacketArrivalHistory::Insert(uint32_t rtp_timestamp, int packet_length_samples, int64_t now_ms) {
  PacketArrival packet{unwrapper_.Unwrap(rtp_timestamp), now_ms * sample_rate_khz_, packet_length_samples};
  if (IsObsolete(packet)) return false;
  if (Contains(packet)) return false;
  history_.emplace(packet.rtp_timestamp, packet);
  if (!(packet == history_.rbegin()->second)) return true;  // Reordered.
  while (IsObsolete(history_.begin()->second)) {
    if (history_.begin()->second == min_packet_arrivals_.front()) min_packet_arrivals_.pop_front();
    if (history_.begin()->second == max_packet_arrivals_.front()) max_packet_arrivals_.pop_front();
    history_.erase(history_.begin());
  }
  while (!min_packet_arrivals_.empty() && packet <= min_packet_arrivals_.back()) min_packet_arrivals_.pop_back();
  while (!max_packet_arrivals_.empty() && packet >= max_packet_arrivals_.back()) max_packet_arrivals_.pop_back();
  min_packet_arrivals_.push_back(packet);
  max_packet_arrivals_.push_back(packet);
  return true;
}

void PacketArrivalHistory::Reset() {
  history_.clear();
  min_packet_arrivals_.clear();
  max_packet_arrivals_.clear();
  unwrapper_.Reset();
}

int PacketArrivalHistory::GetDelayMs(uint32_t rtp_timestamp, int64_t now_ms) const {
  PacketArrival packet{unwrapper_.PeekUnwrap(rtp_timestamp), now_ms * sample_rate_khz_, 0};
  return GetPacketArrivalDelayMs(packet);
}

int PacketArrivalHistory::GetMaxDelayMs() const {
  if (max_packet_arrivals_.empty()) return 0;
  return GetPacketArrivalDelayMs(max_packet_arrivals_.front());
}

bool PacketArrivalHistory::IsNewestRtpTimestamp(uint32_t rtp_timestamp) const {
  if (history_.empty()) return true;
  return unwrapper_.PeekUnwrap(rtp_timestamp) == history_.rbegin()->second.rtp_timestamp;
}

int PacketArrivalHistory::GetPacketArrivalDelayMs(const PacketArrival& p) const {
  if (min_packet_arrivals_.empty() || sample_rate_khz_ == 0) return 0;
  // Converted to ms first, as libwebrtc does for bit-exactness.
  const PacketArrival& m = min_packet_arrivals_.front();
  return static_cast<int>(std::max<int64_t>(p.arrival_timestamp / sample_rate_khz_ - m.arrival_timestamp / sample_rate_khz_ -
                                                (p.rtp_timestamp / sample_rate_khz_ - m.rtp_timestamp / sample_rate_khz_),
                                            0));
}

bool PacketArrivalHistory::IsObsolete(const PacketArrival& p) const {
  if (history_.empty()) return false;
  return p.rtp_timestamp + window_size_ms_ * sample_rate_khz_ < history_.rbegin()->second.rtp_timestamp;
}

bool PacketArrivalHistory::Contains(const PacketArrival& p) const {
  auto it = history_.upper_bound(p.rtp_timestamp);
  if (it == history_.begin()) return false;
  --it;
  return it->second.contains(p);
}

// --------------------------------------------------------- DelayConstraints

DelayConstraints::DelayConstraints(int max_packets_in_buffer, int base_minimum_delay_ms)
    : max_packets_in_buffer_(max_packets_in_buffer),
      base_minimum_delay_ms_(base_minimum_delay_ms),
      effective_minimum_delay_ms_(base_minimum_delay_ms) {}

int DelayConstraints::Clamp(int delay_ms) const {
  delay_ms = std::max(delay_ms, effective_minimum_delay_ms_);
  if (maximum_delay_ms_ > 0) delay_ms = std::min(delay_ms, maximum_delay_ms_);
  // At most 75% of the packet buffer.
  if (packet_len_ms_ > 0) delay_ms = std::min(delay_ms, 3 * max_packets_in_buffer_ * packet_len_ms_ / 4);
  return delay_ms;
}

bool DelayConstraints::SetPacketAudioLength(int length_ms) {
  if (length_ms <= 0) return false;
  packet_len_ms_ = length_ms;
  return true;
}

bool DelayConstraints::SetMinimumDelay(int delay_ms) {
  if (delay_ms < 0 || delay_ms > MinimumDelayUpperBound()) return false;
  minimum_delay_ms_ = delay_ms;
  UpdateEffectiveMinimumDelay();
  return true;
}

bool DelayConstraints::SetMaximumDelay(int delay_ms) {
  if (delay_ms != 0 && delay_ms < minimum_delay_ms_) return false;
  maximum_delay_ms_ = delay_ms;
  UpdateEffectiveMinimumDelay();
  return true;
}

bool DelayConstraints::SetBaseMinimumDelay(int delay_ms) {
  if (delay_ms < kMinBaseMinimumDelayMs || delay_ms > kMaxBaseMinimumDelayMs) return false;
  base_minimum_delay_ms_ = delay_ms;
  UpdateEffectiveMinimumDelay();
  return true;
}

void DelayConstraints::UpdateEffectiveMinimumDelay() {
  const int base = std::clamp(base_minimum_delay_ms_, 0, MinimumDelayUpperBound());
  effective_minimum_delay_ms_ = std::max(minimum_delay_ms_, base);
}

int DelayConstraints::MinimumDelayUpperBound() const {
  int q75 = max_packets_in_buffer_ * packet_len_ms_ * 3 / 4;
  q75 = q75 > 0 ? q75 : kMaxBaseMinimumDelayMs;
  const int maximum_delay_ms = maximum_delay_ms_ > 0 ? maximum_delay_ms_ : kMaxBaseMinimumDelayMs;
  return std::min(maximum_delay_ms, q75);
}

// -------------------------------------------------------- NetEqDelayManager

NetEqDelayManager::NetEqDelayManager(NetEqDelayConfig cfg)
    : cfg_(cfg),
      underrun_(static_cast<int>((1 << 30) * cfg.quantile), static_cast<int>((1 << 15) * cfg.forget_factor),
                cfg.start_forget_weight ? std::optional<int>(static_cast<int>(*cfg.start_forget_weight)) : std::nullopt,
                cfg.resample_interval_ms),
      constraints_(cfg.max_packets_in_buffer, cfg.base_min_delay_ms),
      arrival_history_(kPacketHistorySizeMs) {
  if (cfg_.use_reorder_optimizer) {
    reorder_.emplace(static_cast<int>((1 << 15) * cfg.reorder_forget_factor), cfg.ms_per_loss_percent,
                     cfg.start_forget_weight ? std::optional<int>(static_cast<int>(*cfg.start_forget_weight))
                                             : std::nullopt);
  }
  SoftReset();
}

void NetEqDelayManager::SoftReset() {
  // DelayManager::Reset.
  underrun_.Reset();
  target_level_ms_ = kStartDelayMs;
  if (reorder_) reorder_->Reset();
  arrival_history_.Reset();
  packet_length_samples_ = 0;
}

std::optional<int> NetEqDelayManager::PacketArrived(uint32_t rtp_timestamp, int packet_length_samples, int sample_rate,
                                                    int64_t now_ms) {
  if (packet_length_samples > 0 && sample_rate > 0 && packet_length_samples != packet_length_samples_) {
    packet_length_samples_ = packet_length_samples;
    constraints_.SetPacketAudioLength(packet_length_samples_ * 1000 / sample_rate);
  }
  bool inserted = arrival_history_.Insert(rtp_timestamp, packet_length_samples, now_ms);
  // No meaningful delay estimate unless at least 2 packets have arrived.
  if (!inserted || arrival_history_.size() < 2) return std::nullopt;
  int arrival_delay_ms = arrival_history_.GetDelayMs(rtp_timestamp, now_ms);
  bool reordered = !arrival_history_.IsNewestRtpTimestamp(rtp_timestamp);
  // DelayManager::Update.
  if (!reorder_ || !reordered) underrun_.Update(arrival_delay_ms, now_ms);
  target_level_ms_ = underrun_.GetOptimalDelayMs().value_or(kStartDelayMs);
  if (reorder_) {
    reorder_->Update(arrival_delay_ms, reordered, target_level_ms_);
    target_level_ms_ = std::max(target_level_ms_, reorder_->GetOptimalDelayMs().value_or(0));
  }
  ++updates_;
  if (reordered) ++reordered_;
  relative_delay_ms_.Add(arrival_delay_ms);
  return arrival_delay_ms;
}

void NetEqDelayManager::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("target_delay_ms", TargetLevelMs());
  w.Counter("updates", updates_);
  w.Counter("reordered", reordered_);
  w.Hist("relative_delay_ms", relative_delay_ms_);
}

}  // namespace sm::timing
