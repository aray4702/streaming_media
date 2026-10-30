#include "sm/core/audio_frame_operations.h"

#include <algorithm>

namespace sm::dsp {
namespace {

// audio_frame_operations.cc:28-29.
constexpr size_t kMuteFadeFrames = 128;
constexpr float kMuteFadeInc = 1.0f / kMuteFadeFrames;

}  // namespace

void MuteAudioFrame(std::span<int16_t> interleaved, size_t num_channels, bool previous_frame_muted,
                    bool current_frame_muted) {
  if (!previous_frame_muted && !current_frame_muted) return;  // Not muted, don't touch.
  if (previous_frame_muted && current_frame_muted) {
    std::fill(interleaved.begin(), interleaved.end(), int16_t{0});  // AudioFrame::Mute().
    return;
  }
  if (num_channels == 0) return;
  const size_t samples_per_channel = interleaved.size() / num_channels;
  // Limit the number of samples to fade if the frame isn't long enough.
  size_t count = kMuteFadeFrames;
  float inc = kMuteFadeInc;
  if (samples_per_channel < kMuteFadeFrames) {
    count = samples_per_channel;
    if (count > 0) inc = 1.0f / static_cast<float>(count);
  }
  size_t start = 0;
  size_t end = count;
  float start_g = 0.0f;
  if (current_frame_muted) {
    // Fade out the last `count` samples of the frame.
    start = samples_per_channel - count;
    end = samples_per_channel;
    start_g = 1.0f;
    inc = -inc;
  }
  // Otherwise fade in the first `count` samples.
  for (size_t j = 0; j < num_channels; ++j) {
    float g = start_g;
    for (size_t i = start * num_channels; i < end * num_channels; i += num_channels) {
      g += inc;
      interleaved[i + j] = static_cast<int16_t>(static_cast<float>(interleaved[i + j]) * g);
    }
  }
}

}  // namespace sm::dsp
