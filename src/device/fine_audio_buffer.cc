#include "sm/device/fine_audio_buffer.h"

#include <algorithm>
#include <cstring>

namespace sm::device {

FineAudioBuffer::FineAudioBuffer(int sample_rate_hz, int channels, size_t max_callback_frames)
    : sample_rate_hz_(sample_rate_hz),
      channels_(channels),
      samples_per_channel_10ms_(static_cast<size_t>(sample_rate_hz / 100)) {
  const size_t chunk = samples_per_channel_10ms_ * static_cast<size_t>(channels);
  const size_t cap = max_callback_frames * static_cast<size_t>(channels) + 2 * chunk;
  playout_buffer_.reserve(cap);
  record_buffer_.reserve(cap);
}

void FineAudioBuffer::GetPlayoutData(int16_t* out, size_t num_samples, int playout_delay_ms) {
  const size_t num_elements_10ms = samples_per_channel_10ms_ * static_cast<size_t>(channels_);
  // Ask for 10 ms chunks until there is enough for the request
  // (fine_audio_buffer.cc, GetPlayoutData).
  while (playout_buffer_.size() < num_samples) {
    const size_t old_size = playout_buffer_.size();
    playout_buffer_.resize(old_size + num_elements_10ms);
    size_t got = 0;
    if (transport_ != nullptr) {
      got = transport_->NeedMorePlayData(playout_buffer_.data() + old_size, samples_per_channel_10ms_, channels_,
                                         sample_rate_hz_);
    }
    if (got != samples_per_channel_10ms_) {
      // libwebrtc: silence when RequestPlayoutData fails (no transport registered).
      playout_buffer_.resize(old_size);
      std::memset(out, 0, num_samples * sizeof(int16_t));
      return;
    }
  }
  std::memcpy(out, playout_buffer_.data(), num_samples * sizeof(int16_t));
  playout_buffer_.erase(playout_buffer_.begin(), playout_buffer_.begin() + static_cast<std::ptrdiff_t>(num_samples));
  // Cached for DeliverRecordedData (the echo canceller's total delay).
  playout_delay_ms_ = playout_delay_ms;
}

void FineAudioBuffer::DeliverRecordedData(const int16_t* in, size_t num_samples, int record_delay_ms) {
  record_buffer_.insert(record_buffer_.end(), in, in + num_samples);
  const size_t num_elements_10ms = samples_per_channel_10ms_ * static_cast<size_t>(channels_);
  size_t consumed = 0;
  while (record_buffer_.size() - consumed >= num_elements_10ms) {
    if (transport_ != nullptr) {
      transport_->RecordedDataIsAvailable(record_buffer_.data() + consumed, samples_per_channel_10ms_, channels_,
                                          sample_rate_hz_, playout_delay_ms_ + record_delay_ms);
    }
    consumed += num_elements_10ms;
  }
  record_buffer_.erase(record_buffer_.begin(), record_buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
}

}  // namespace sm::device
