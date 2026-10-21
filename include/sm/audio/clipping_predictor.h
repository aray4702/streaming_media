// 6.4 AGC1's clipping predictor as libwebrtc M157's, which
// `agc1::AgcManagerDirect` (agc_manager.h) asks for a mic volume step before
// the capture clips. `apm v2` (#242). Off by default, as in libwebrtc
// (`ClippingPredictor::enabled = false`), so a call never creates it.
//
// Ported from compare/webrtc/reference/modules/audio_processing/agc2/ with
// libwebrtc's constants and defaults:
//  - clipping_predictor_level_buffer.{cc,h}: a circular buffer of per-frame
//    mean square and peak, with partial metrics over a delayed window.
//  - clipping_predictor.{cc,h}: `ClippingEventPredictor` (clipping predicted
//    when the window's peak is above the threshold and its crest factor
//    dropped by more than the margin against the reference window; fixed
//    step) and `ClippingPeakPredictor` (the window's RMS plus the reference
//    crest factor projects a peak above the threshold; fixed step, or an
//    adaptive one through agc2/gain_map_internal.h's `kGainMap`, with a gain
//    change of at most 15 dB).
//  - api/audio/audio_processing.h: `AnalogGainController::ClippingPredictor`
//    defaults (event prediction, windows 5 / 5 / delay 5, threshold -1 dBFS,
//    crest factor margin 3 dB, predicted step used).
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace sm::audio::agc1 {

// AudioProcessing::Config::GainController1::AnalogGainController::
// ClippingPredictor.
struct ClippingPredictorConfig {
  bool enabled = false;
  enum class Mode {
    kClippingEventPrediction,              // Fixed step.
    kAdaptiveStepClippingPeakPrediction,   // Clipped peak, adaptive step.
    kFixedStepClippingPeakPrediction,      // Clipped peak, fixed step.
  };
  Mode mode = Mode::kClippingEventPrediction;
  int window_length = 5;            // Frames in the analysis window.
  int reference_window_length = 5;  // Frames in the reference window.
  int reference_window_delay = 5;   // Frames.
  float clipping_threshold = -1.0f;  // dBFS.
  float crest_factor_margin = 3.0f;  // dB.
  // Whether the predicted step lowers the volume; otherwise the predictor
  // runs without effect.
  bool use_predicted_step = true;
};

// agc2/clipping_predictor_level_buffer.h.
class ClippingPredictorLevelBuffer {
 public:
  struct Level {
    float average;  // Mean square (FloatS16).
    float max;      // Peak (FloatS16).
    bool operator==(const Level& level) const;  // Within 1e-6.
  };
  static constexpr int kMaxCapacity = 100;  // Recommended, not enforced.

  explicit ClippingPredictorLevelBuffer(int capacity);  // max(1, capacity).
  void Reset();
  int Size() const { return size_; }
  int Capacity() const { return static_cast<int>(data_.size()); }
  // Replaces the oldest item once full.
  void Push(Level level);
  // Mean of the averages and max of the peaks over the `num_items` items
  // pushed from `delay` back (0 is the newest), if that many were pushed.
  std::optional<Level> ComputePartialMetrics(int delay, int num_items) const;

 private:
  int tail_ = -1;
  int size_ = 0;
  std::vector<Level> data_;
};

// agc2/clipping_predictor.h.
class ClippingPredictor {
 public:
  virtual ~ClippingPredictor() = default;
  virtual void Reset() = 0;
  // A 10 ms FloatS16 frame, one span per channel.
  virtual void Analyze(std::span<const std::span<const float>> channels) = 0;
  // The recommended volume decrease if clipping is predicted on `channel`.
  virtual std::optional<int> EstimateClippedLevelStep(int channel, int level, int default_step, int min_mic_level,
                                                      int max_mic_level) const = 0;
};

// Null when `config.enabled` is false. When enabled,
// `window_length < reference_window_length + reference_window_delay`.
std::unique_ptr<ClippingPredictor> CreateClippingPredictor(int num_channels, const ClippingPredictorConfig& config);

}  // namespace sm::audio::agc1
