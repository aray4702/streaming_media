// The browser's `sm.AudioJitterBuffer` (web/audio_jitter_buffer_binding.h):
// NetEq runs at a rate it supports and the audio is resampled at both ends,
// as libwebrtc's receive path (#579).
#include "audio_jitter_buffer_binding.h"

#include <cmath>
#include <vector>

#include "sm_test.h"

using namespace sm;
using namespace sm::web;

namespace {
struct Played {
  int expands = 0;
  double rms_tail = 0;  // Over the last 500 ms.
  uint64_t received = 0, discarded = 0;
};

// 2 s of 20 ms frames of a 440 Hz tone at `rate`, played 10 ms at a time,
// as web/test/smoke.mjs drives the binding.
Played Play(int rate) {
  AudioJitterBufferBinding jb(rate);
  const size_t frame = static_cast<size_t>(rate / 50), pull = static_cast<size_t>(rate / 100);
  std::vector<float> out(pull);
  Played p;
  double sum = 0;
  size_t n = 0;
  for (int t = 0; t < 2000; t += 10) {
    if (t % 20 == 0) {
      std::vector<float> x(frame);
      for (size_t i = 0; i < frame; ++i) {
        const double s = static_cast<double>(static_cast<int64_t>(t) * rate / 1000 + static_cast<int64_t>(i));
        x[i] = static_cast<float>(0.3 * std::sin(2 * M_PI * 440 * s / rate));
      }
      jb.InsertPacket(static_cast<uint32_t>(int64_t{t} * rate / 1000), std::move(x), Timestamp::Millis(t + 30));
    }
    if (jb.GetAudio(out, Timestamp::Millis(t + 30)) == timing::AudioOperation::kExpand) ++p.expands;
    if (t >= 1500) {
      for (float s : out) sum += double{s} * s;
      n += out.size();
    }
  }
  p.rms_tail = std::sqrt(sum / static_cast<double>(n));
  p.received = jb.jitter_buffer().stats().packets_received;
  p.discarded = jb.jitter_buffer().stats().packets_discarded_unsupported_rate;
  return p;
}
}  // namespace

SM_TEST(WebAudioJitterBuffer, NetEqRunsAtASupportedRate) {
  SM_EXPECT_EQ(AudioJitterBufferBinding::NetEqRateFor(16000), 16000);
  SM_EXPECT_EQ(AudioJitterBufferBinding::NetEqRateFor(48000), 48000);
  SM_EXPECT_EQ(AudioJitterBufferBinding::NetEqRateFor(44100), 48000);
  SM_EXPECT_EQ(AudioJitterBufferBinding::NetEqRateFor(22050), 48000);
}

// A page at 44.1 kHz (the usual AudioContext rate) hears the audio: every
// packet reaches NetEq at 48 kHz and the output is the tone, not silence.
SM_TEST(WebAudioJitterBuffer, PlaysAtRatesNetEqDoesNotRun) {
  for (int rate : {44100, 22050, 16000, 48000}) {
    const Played p = Play(rate);
    SM_EXPECT_EQ(p.discarded, 0u);
    SM_EXPECT_EQ(p.received, 100u);
    SM_EXPECT_LT(p.expands, 5);
    SM_EXPECT_NEAR(p.rms_tail, 0.3 / std::sqrt(2.0), 0.02);  // The tone's RMS.
  }
}
