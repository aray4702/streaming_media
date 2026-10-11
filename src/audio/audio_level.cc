#include "sm/audio/audio_level.h"

#include <algorithm>
#include <cmath>

#include "sm/core/dsp.h"

namespace sm::audio {

uint8_t EncodeAudioLevelExtension(const AudioLevel& l) {
  return static_cast<uint8_t>((l.voice ? 0x80 : 0) | (std::min<int>(l.level, 127) & 0x7f));
}

AudioLevel DecodeAudioLevelExtension(uint8_t b) { return AudioLevel{static_cast<uint8_t>(b & 0x7f), (b & 0x80) != 0}; }

uint8_t ComputeLevel(std::span<const float> frame) {
  float rms = dsp::Rms(frame);
  if (rms <= 0) return 127;
  return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(-20.0f * std::log10(rms))), 0, 127));
}

void ActiveSpeakerDetector::OnLevel(uint32_t stream, const AudioLevel& l, Timestamp now) {
  // Activity score: louder voiced frames score higher; silence decays.
  double inst = (l.voice && l.level < cfg_.silence_level) ? (cfg_.silence_level - l.level) : 0.0;
  double& s = score_[stream];
  s += cfg_.smoothing * (inst - s);
  Decide(now);
}

void ActiveSpeakerDetector::Decide(Timestamp now) {
  uint32_t best = 0;
  double best_score = -1;
  for (const auto& [id, sc] : score_) {
    if (sc > best_score) {
      best_score = sc;
      best = id;
    }
  }
  if (best_score <= 1.0) return;  // Nobody is talking.
  if (!dominant_) {
    dominant_ = best;
    ++switches_;
    return;
  }
  if (best == *dominant_) {
    challenger_.reset();
    return;
  }
  double cur = score_[*dominant_];
  if (best_score < cur * cfg_.switch_margin && cur > 1.0) {
    challenger_.reset();
    return;
  }
  if (challenger_ != best) {
    challenger_ = best;
    challenger_since_ = now;
  }
  if (now - challenger_since_ >= cfg_.switch_hold) {
    dominant_ = best;
    challenger_.reset();
    ++switches_;
  }
}

void ActiveSpeakerDetector::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("dominant", dominant_ ? static_cast<double>(*dominant_) : -1.0);
  w.Counter("switches", switches_);
  w.Gauge("streams", static_cast<double>(score_.size()));
}

}  // namespace sm::audio
