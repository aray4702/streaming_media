// libwebrtc M157 audio/nack_tracker.cc, line for line (#971).
#include "sm/loss/audio_nack_tracker.h"

namespace sm::loss {
namespace {

constexpr int kDefaultSampleRateHz = 48000;
constexpr int64_t kMaxPacketDurationMs = 120;
constexpr TimeDelta kDefaultRtt = TimeDelta::Millis(100);
constexpr TimeDelta kMaxNackDelay = TimeDelta::Seconds(1);

}  // namespace

AudioNackTracker::AudioNackTracker(size_t max_nack_list_size)
    : max_nack_list_size_(max_nack_list_size), sample_rate_hz_(kDefaultSampleRateHz) {}

void AudioNackTracker::UpdateSampleRate(int sample_rate_hz) {
  if (sample_rate_hz <= 0) return;  // RTC_DCHECK_GT(sample_rate_hz, 0).
  if (sample_rate_hz != sample_rate_hz_) {
    Reset();
    sample_rate_hz_ = sample_rate_hz;
  }
}

void AudioNackTracker::UpdateLastReceivedPacket(uint16_t sequence_number, uint32_t timestamp) {
  if (!sequence_num_last_received_rtp_) {
    sequence_num_last_received_rtp_ = sequence_number;
    timestamp_last_received_rtp_ = timestamp;
    return;
  }
  nack_list_.erase(sequence_number);
  if (!AheadOf<uint16_t>(sequence_number, *sequence_num_last_received_rtp_)) return;
  UpdateList(sequence_number, timestamp);
  sequence_num_last_received_rtp_ = sequence_number;
  timestamp_last_received_rtp_ = timestamp;
  LimitNackListSize();
}

std::optional<int> AudioNackTracker::GetSamplesPerPacket(uint16_t seq, uint32_t timestamp) const {
  const uint32_t timestamp_increase = timestamp - *timestamp_last_received_rtp_;
  const uint16_t sequence_num_increase = static_cast<uint16_t>(seq - *sequence_num_last_received_rtp_);
  // `int samples_per_packet = timestamp_increase / sequence_num_increase` (unsigned division).
  const int samples_per_packet = static_cast<int>(timestamp_increase / sequence_num_increase);
  // samples_per_packet > kMaxPacketDuration * sample_rate_ (a double: seconds x hertz).
  if (samples_per_packet == 0 ||
      static_cast<double>(samples_per_packet) >
          static_cast<double>(kMaxPacketDurationMs) / 1000.0 * static_cast<double>(sample_rate_hz_)) {
    return std::nullopt;
  }
  return samples_per_packet;
}

void AudioNackTracker::UpdateList(uint16_t seq, uint32_t timestamp) {
  if (!AheadOf<uint16_t>(seq, static_cast<uint16_t>(*sequence_num_last_received_rtp_ + 1))) return;
  const std::optional<int> samples_per_packet = GetSamplesPerPacket(seq, timestamp);
  if (!samples_per_packet) return;
  for (uint16_t s = static_cast<uint16_t>(*sequence_num_last_received_rtp_ + 1); AheadOf<uint16_t>(seq, s); ++s) {
    nack_list_[s] = EstimateTimestamp(s, *samples_per_packet);
  }
}

uint32_t AudioNackTracker::EstimateTimestamp(uint16_t seq, int samples_per_packet) const {
  const uint16_t sequence_num_diff = static_cast<uint16_t>(seq - *sequence_num_last_received_rtp_);
  return static_cast<uint32_t>(sequence_num_diff) * static_cast<uint32_t>(samples_per_packet) +
         *timestamp_last_received_rtp_;
}

void AudioNackTracker::Reset() {
  nack_list_.clear();
  sequence_num_last_received_rtp_.reset();
  timestamp_last_received_rtp_.reset();
  sample_rate_hz_ = kDefaultSampleRateHz;
}

void AudioNackTracker::LimitNackListSize() {
  const uint16_t limit = static_cast<uint16_t>(*sequence_num_last_received_rtp_ -
                                               static_cast<uint16_t>(max_nack_list_size_) - 1);
  nack_list_.erase(nack_list_.begin(), nack_list_.upper_bound(limit));
}

std::vector<uint16_t> AudioNackTracker::GetNackList(std::optional<TimeDelta> round_trip_time) const {
  std::vector<uint16_t> sequence_numbers;
  const TimeDelta rtt = round_trip_time.value_or(kDefaultRtt);
  for (const auto& [sequence_number, timestamp] : nack_list_) {
    if (Nack(timestamp, rtt)) sequence_numbers.push_back(sequence_number);
  }
  return sequence_numbers;
}

bool AudioNackTracker::Nack(uint32_t timestamp, TimeDelta round_trip_time) const {
  // (*timestamp_last_received_rtp_ - timestamp) / sample_rate_: int64 / Frequency, in microseconds,
  // nominator * 1e9 / millihertz.
  const int64_t samples = static_cast<int64_t>(static_cast<uint32_t>(*timestamp_last_received_rtp_ - timestamp));
  const TimeDelta time_since_packet =
      TimeDelta::Micros(samples * 1000000000 / (static_cast<int64_t>(sample_rate_hz_) * 1000));
  return time_since_packet + round_trip_time < kMaxNackDelay;
}

}  // namespace sm::loss
