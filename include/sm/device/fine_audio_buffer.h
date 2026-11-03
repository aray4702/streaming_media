// FineAudioBuffer (#1105): libwebrtc's modules/audio_device/fine_audio_buffer
// (M157), which audio_device_ios uses between the Audio Unit's callbacks (any
// number of frames, e.g. 470 or 512) and the 10 ms frames of AudioTransport.
//
// Playout: GetPlayoutData asks the transport for 10 ms chunks until it holds
// enough for the request, hands out the request and keeps the rest. Record:
// DeliverRecordedData appends and delivers every complete 10 ms chunk.
// Storage is reserved up front so the audio thread does not allocate for
// callbacks up to `max_callback_frames`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sm/device/audio_device.h"

namespace sm::device {

class FineAudioBuffer {
 public:
  FineAudioBuffer(int sample_rate_hz, int channels, size_t max_callback_frames = 4096);

  void SetTransport(AudioTransport* transport) { transport_ = transport; }
  void ResetPlayout() { playout_buffer_.clear(); }
  void ResetRecord() { record_buffer_.clear(); }

  // Fills `out` (`num_samples` interleaved) with playout; silence when the
  // transport is missing or returns less than 10 ms, as libwebrtc does.
  void GetPlayoutData(int16_t* out, size_t num_samples, int playout_delay_ms);
  // Appends `num_samples` interleaved samples and delivers each full 10 ms.
  void DeliverRecordedData(const int16_t* in, size_t num_samples, int record_delay_ms);

  size_t samples_per_channel_10ms() const { return samples_per_channel_10ms_; }
  size_t playout_buffered() const { return playout_buffer_.size(); }
  size_t record_buffered() const { return record_buffer_.size(); }

 private:
  int sample_rate_hz_;
  int channels_;
  size_t samples_per_channel_10ms_;
  AudioTransport* transport_ = nullptr;
  std::vector<int16_t> playout_buffer_;
  std::vector<int16_t> record_buffer_;
  int playout_delay_ms_ = 0;
};

}  // namespace sm::device
