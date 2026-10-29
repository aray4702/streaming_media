// 4.3 (v2) Audio target delay as libwebrtc M157's NetEq (issue #62).
//
// A port of the part of NetEq that decides how much audio to buffer, used
// by timing::AudioJitterBuffer under `version = 2` (the test bench's
// `neteq v2`):
//   - Histogram (modules/audio_coding/neteq/histogram.cc): 100 buckets in
//     Q30 with a Q15 forgetting factor that starts at 0 and converges to its
//     base value with start_forget_weight 2;
//   - UnderrunOptimizer (underrun_optimizer.cc): the 95th percentile of the
//     largest relative arrival delay in each 500 ms interval, in 20 ms
//     buckets (forget factor 0.983);
//   - ReorderOptimizer (reorder_optimizer.cc): the delay that minimizes
//     delay + 20 ms per percent of reordered packets that would arrive too
//     late (forget factor 0.9993);
//   - PacketArrivalHistory (packet_arrival_history.cc): the relative arrival
//     delay of a packet against the fastest packet of the last 2 s of RTP
//     time, reordered packets excluded;
//   - DelayManager (delay_manager.cc): max of the two optimizers, 80 ms until
//     the first estimate;
//   - DelayConstraints (delay_constraints.cc) and DecisionLogic::PacketArrived
//     (decision_logic.cc): the minimum delay and the 75%-of-buffer cap.
// NetEq's clock is its TickTimer, which advances 10 ms per GetAudio call;
// here every call takes that tick time in ms. Field-trial overrides are not
// ported; every default is.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"

namespace sm::timing {

// histogram.cc. Probabilities in Q30, forgetting factor in Q15.
class NetEqHistogram {
 public:
  NetEqHistogram(size_t num_buckets, int forget_factor, std::optional<double> start_forget_weight = std::nullopt);
  void Reset();
  void Add(int index);
  int Quantile(int probability) const;
  int NumBuckets() const { return static_cast<int>(buckets_.size()); }
  const std::vector<int>& buckets() const { return buckets_; }
  int forget_factor() const { return forget_factor_; }

 private:
  std::vector<int> buckets_;
  int forget_factor_ = 0;
  const int base_forget_factor_;
  int add_count_ = 0;
  const std::optional<double> start_forget_weight_;
};

// underrun_optimizer.cc.
class UnderrunOptimizer {
 public:
  UnderrunOptimizer(int histogram_quantile, int forget_factor, std::optional<int> start_forget_weight,
                    std::optional<int> resample_interval_ms);
  void Update(int relative_delay_ms, int64_t now_ms);
  std::optional<int> GetOptimalDelayMs() const { return optimal_delay_ms_; }
  void Reset();

 private:
  NetEqHistogram histogram_;
  const int histogram_quantile_;  // Q30.
  const std::optional<int> resample_interval_ms_;
  std::optional<int64_t> resample_start_ms_;  // The stopwatch.
  int max_delay_in_interval_ms_ = 0;
  std::optional<int> optimal_delay_ms_;
};

// reorder_optimizer.cc.
class ReorderOptimizer {
 public:
  ReorderOptimizer(int forget_factor, int ms_per_loss_percent, std::optional<int> start_forget_weight);
  void Update(int relative_delay_ms, bool reordered, int base_delay_ms);
  std::optional<int> GetOptimalDelayMs() const { return optimal_delay_ms_; }
  void Reset();

 private:
  int MinimizeCostFunction(int base_delay_ms) const;
  NetEqHistogram histogram_;
  const int ms_per_loss_percent_;
  std::optional<int> optimal_delay_ms_;
};

// packet_arrival_history.cc.
class PacketArrivalHistory {
 public:
  explicit PacketArrivalHistory(int window_size_ms) : window_size_ms_(window_size_ms) {}
  // False if the packet is older than the window or already in the history.
  bool Insert(uint32_t rtp_timestamp, int packet_length_samples, int64_t now_ms);
  int GetDelayMs(uint32_t rtp_timestamp, int64_t now_ms) const;
  // Largest arrival delay in the history, reordered packets excluded.
  int GetMaxDelayMs() const;
  bool IsNewestRtpTimestamp(uint32_t rtp_timestamp) const;
  void Reset();
  void set_sample_rate(int sample_rate) { sample_rate_khz_ = sample_rate / 1000; }
  size_t size() const { return history_.size(); }

 private:
  struct PacketArrival {
    int64_t rtp_timestamp = 0;
    int64_t arrival_timestamp = 0;  // In samples.
    int length_samples = 0;
    bool operator==(const PacketArrival& o) const {
      return rtp_timestamp == o.rtp_timestamp && arrival_timestamp == o.arrival_timestamp &&
             length_samples == o.length_samples;
    }
    bool operator<=(const PacketArrival& o) const {
      return arrival_timestamp - rtp_timestamp <= o.arrival_timestamp - o.rtp_timestamp;
    }
    bool operator>=(const PacketArrival& o) const {
      return arrival_timestamp - rtp_timestamp >= o.arrival_timestamp - o.rtp_timestamp;
    }
    bool contains(const PacketArrival& o) const {
      return rtp_timestamp <= o.rtp_timestamp && rtp_timestamp + length_samples >= o.rtp_timestamp + o.length_samples;
    }
  };
  int GetPacketArrivalDelayMs(const PacketArrival& p) const;
  bool IsObsolete(const PacketArrival& p) const;
  bool Contains(const PacketArrival& p) const;
  const int window_size_ms_;
  int sample_rate_khz_ = 0;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  std::map<int64_t, PacketArrival> history_;
  std::deque<PacketArrival> min_packet_arrivals_;
  std::deque<PacketArrival> max_packet_arrivals_;
};

// delay_constraints.cc.
class DelayConstraints {
 public:
  DelayConstraints(int max_packets_in_buffer, int base_minimum_delay_ms);
  int Clamp(int delay_ms) const;
  bool SetPacketAudioLength(int length_ms);
  bool SetMinimumDelay(int delay_ms);
  bool SetMaximumDelay(int delay_ms);
  bool SetBaseMinimumDelay(int delay_ms);
  int effective_minimum_delay_ms() const { return effective_minimum_delay_ms_; }

 private:
  int MinimumDelayUpperBound() const;
  void UpdateEffectiveMinimumDelay();
  const int max_packets_in_buffer_;
  int base_minimum_delay_ms_;
  int effective_minimum_delay_ms_;
  int minimum_delay_ms_ = 0;
  int maximum_delay_ms_ = 0;
  int packet_len_ms_ = 0;
};

// One DelayManager::Update or Reset, for the libwebrtc comparison's NetEq
// trace (issue #721). The libwebrtc side writes the same rows from a
// DelayManager wrapper in compare/webrtc/webrtc_call.cc.
struct NetEqDelayTraceRow {
  int64_t now_ms = 0;  // TickTimer ticks * 10 ms.
  bool reset = false;
  uint32_t rtp_timestamp = 0;
  int delay_ms = 0;
  bool reordered = false;
  int underrun_ms = -1;  // UnderrunOptimizer::GetOptimalDelayMs(), -1 if none yet.
  int reorder_ms = -1;   // ReorderOptimizer::GetOptimalDelayMs(), -1 if none yet.
  int target_ms = 0;     // DelayManager::TargetDelayMs() after the call.
};

// DelayManager::Config defaults.
struct NetEqDelayConfig {
  double quantile = 0.95;
  double forget_factor = 0.983;
  std::optional<double> start_forget_weight = 2;
  std::optional<int> resample_interval_ms = 500;
  bool use_reorder_optimizer = true;
  double reorder_forget_factor = 0.9993;
  int ms_per_loss_percent = 20;
  int max_packets_in_buffer = 200;  // NetEq::Config::max_packets_in_buffer.
  int base_min_delay_ms = 0;        // NetEq::Config::min_delay_ms.
  // Test bench only: when set, every update and reset is appended (issue #721).
  std::vector<NetEqDelayTraceRow>* trace = nullptr;
};

// DelayManager with the packet-arrival half of DecisionLogic around it.
class NetEqDelayManager : public MetricsSource {
 public:
  explicit NetEqDelayManager(NetEqDelayConfig cfg = {});
  void SetSampleRate(int sample_rate) { arrival_history_.set_sample_rate(sample_rate); }
  // DecisionLogic::PacketArrived: returns the packet's relative arrival delay.
  std::optional<int> PacketArrived(uint32_t rtp_timestamp, int packet_length_samples, int sample_rate, int64_t now_ms);
  // DecisionLogic::TargetLevelMs: the delay manager's target, constrained.
  int TargetLevelMs() const { return constraints_.Clamp(target_level_ms_); }
  int UnlimitedTargetLevelMs() const { return target_level_ms_; }
  // DecisionLogic::GetPlayoutDelayMs and the accelerate limit's term.
  int GetDelayMs(uint32_t rtp_timestamp, int64_t now_ms) const { return arrival_history_.GetDelayMs(rtp_timestamp, now_ms); }
  int GetMaxDelayMs() const { return arrival_history_.GetMaxDelayMs(); }
  bool SetMinimumDelay(int delay_ms) { return constraints_.SetMinimumDelay(delay_ms); }
  // DecisionLogic::SoftReset (the delay part).
  void SoftReset(int64_t now_ms = 0);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  static constexpr int kStartDelayMs = 80;
  static constexpr int kPacketHistorySizeMs = 2000;
  NetEqDelayConfig cfg_;
  UnderrunOptimizer underrun_;
  std::optional<ReorderOptimizer> reorder_;
  DelayConstraints constraints_;
  PacketArrivalHistory arrival_history_;
  int target_level_ms_ = kStartDelayMs;
  int packet_length_samples_ = 0;
  uint64_t updates_ = 0;
  uint64_t reordered_ = 0;
  Histogram relative_delay_ms_;
};

// buffer_level_filter.cc: the packet buffer's level in samples, filtered in
// Q8 with a factor that follows the target level.
class BufferLevelFilter {
 public:
  BufferLevelFilter() { Reset(); }
  void Reset();
  // `time_stretched_samples` is subtracted from the filtered value directly.
  void Update(size_t buffer_size_samples, int time_stretched_samples);
  void SetFilteredBufferLevel(int buffer_size_samples);
  void SetTargetBufferLevel(int target_buffer_level_ms);
  // Rounded to the nearest whole sample.
  int filtered_current_level() const { return static_cast<int>((int64_t{filtered_current_level_} + (1 << 7)) >> 8); }
  int level_factor() const { return level_factor_; }

 private:
  int level_factor_ = 253;           // Q8.
  int filtered_current_level_ = 0;  // Q8.
};

// NetEq::Mode and NetEq::Operation, the values a receiver without DTMF,
// comfort noise or codec PLC reaches.
enum class NetEqMode {
  kNormal,
  kExpand,
  kMerge,
  kAccelerateSuccess,
  kAccelerateLowEnergy,
  kAccelerateFail,
  kPreemptiveExpandSuccess,
  kPreemptiveExpandLowEnergy,
  kPreemptiveExpandFail,
};
enum class NetEqOperation { kNormal, kMerge, kExpand, kAccelerate, kFastAccelerate, kPreemptiveExpand, kUndefined };
const char* ToString(NetEqOperation op);
bool IsTimestretch(NetEqMode mode);

// NetEqController::NetEqStatus. RTP timestamps are unwrapped. The last
// three fields are what DecisionLogic reads from its delay manager and
// packet-arrival history (NetEqDelayManager here).
struct NetEqStatus {
  NetEqMode last_mode = NetEqMode::kNormal;
  int64_t target_timestamp = 0;  // The sync buffer's end timestamp.
  std::optional<int64_t> next_packet_timestamp;
  int64_t span_samples = 0;            // PacketBuffer::GetSpanSamples(.., false).
  int64_t span_samples_wait_time = 0;  // PacketBuffer::GetSpanSamples(.., true).
  int64_t generated_noise_samples = 0;
  int64_t sync_buffer_samples = 0;  // SyncBuffer::FutureLength().
  int expand_mutefactor = 16384;    // Q14.
  int target_level_ms = 0;          // DecisionLogic::TargetLevelMs().
  int playout_delay_ms = 0;         // GetDelayMs(target_timestamp - sync_buffer_samples).
  int max_delay_ms = 0;             // PacketArrivalHistory::GetMaxDelayMs().
};

// decision_logic.cc, the GetDecision half (PacketArrived is in
// NetEqDelayManager). Time is NetEq's TickTimer: one tick per GetAudio.
class NetEqDecisionLogic {
 public:
  NetEqDecisionLogic() { SoftReset(0); }
  void SoftReset(int64_t tick);
  void SetSampleRate(int fs_hz) { sample_rate_khz_ = fs_hz / 1000; }
  NetEqOperation GetDecision(const NetEqStatus& status, int64_t tick);

  void AddSampleMemory(int value) { sample_memory_ += value; }
  void set_sample_memory(int value) { sample_memory_ = value; }
  void set_prev_time_scale(bool value) { prev_time_scale_ = value; }
  // PacketArrivedInfo::buffer_flush.
  void NotifyBufferFlush() { buffer_flush_ = true; }
  int GetFilteredBufferLevel() const { return buffer_level_filter_.filtered_current_level(); }
  const BufferLevelFilter& buffer_level_filter() const { return buffer_level_filter_; }

  // decision_logic.cc constants.
  static constexpr int kMinTimescaleInterval = 5;  // Ticks; about 100 ms/s of stretching at most.
  static constexpr int kPostponeDecodingLevel = 50;
  static constexpr int kTargetLevelWindowMs = 100;
  static constexpr int kDelayAdjustmentGranularityMs = 20;

 private:
  void FilterBufferLevel(int64_t buffer_size_samples, int target_level_ms);
  NetEqOperation NoPacket(const NetEqStatus& s) const;
  NetEqOperation ExpectedPacketAvailable(const NetEqStatus& s, int64_t tick) const;
  NetEqOperation FuturePacketAvailable(const NetEqStatus& s);
  bool PostponeDecode(const NetEqStatus& s) const;
  bool PacketTooEarly(const NetEqStatus& s) const;
  bool TimescaleAllowed(int64_t tick) const { return tick - countdown_start_ >= countdown_ticks_; }

  BufferLevelFilter buffer_level_filter_;
  int sample_rate_khz_ = 16;
  int sample_memory_ = 0;
  bool prev_time_scale_ = false;
  bool buffer_flush_ = false;
  // TickTimer::Countdown.
  int64_t countdown_start_ = 0;
  int64_t countdown_ticks_ = 0;
};

}  // namespace sm::timing
