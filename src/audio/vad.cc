#include "sm/audio/vad.h"

#include <algorithm>
#include <cmath>

namespace sm::audio {

Vad::Vad(Aggressiveness aggressiveness) : aggressiveness_(aggressiveness) { Reset(); }

Vad::Activity Vad::VoiceActivity(const int16_t* audio, size_t num_samples, int sample_rate_hz) {
  ++stats_.frames;
  switch (vad_.Process(sample_rate_hz, audio, num_samples)) {
    case 0:
      return kPassive;
    case 1:
      ++stats_.speech_frames;
      return kActive;
    default:
      ++stats_.errors;
      return kError;
  }
}

Vad::Activity Vad::VoiceActivity(std::span<const float> frame, int sample_rate_hz) {
  pcm_.resize(frame.size());
  for (size_t i = 0; i < frame.size(); ++i) {
    pcm_[i] = static_cast<int16_t>(std::clamp(std::lround(frame[i] * 32768.0f), -32768L, 32767L));
  }
  return VoiceActivity(pcm_.data(), pcm_.size(), sample_rate_hz);
}

void Vad::Reset() {
  vad_ = agc1::detail::WebRtcVad();
  vad_.set_mode(aggressiveness_);
}

void Vad::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", stats_.frames);
  w.Counter("speech_frames", stats_.speech_frames);
  w.Counter("errors", stats_.errors);
}

}  // namespace sm::audio
