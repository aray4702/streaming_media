// The browser's audio jitter buffer (`sm.AudioJitterBuffer`), kept free of
// Emscripten so the native tests run the same code (tests/unit/web).
//
// A page works at its AudioContext's rate, often 44.1 kHz, which NetEq
// (the default jitter buffer, version 2) doesn't run at: it discards every
// packet at a rate outside 8/16/32/48 kHz (NetEqSupportsSampleRate, #256).
// libwebrtc never hands NetEq the device rate: NetEq runs at the decoder's
// rate and AudioCodingModule / the mixer resample its 10 ms output to the
// device with PushResampler. The binding does the same (#579): NetEq runs
// at the page's rate when it supports it, at 48 kHz otherwise; GetAudio pulls
// 10 ms from NetEq and resamples it to the page's rate. The page also hands
// in frames decoded at its own rate, so a packet is resampled to NetEq's rate
// in 10 ms blocks on the way in, with its RTP timestamp scaled to match.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sm/audio/push_resampler.h"
#include "sm/core/seq_num.h"
#include "sm/timing/audio_jitter_buffer.h"

namespace sm::web {

class AudioJitterBufferBinding {
 public:
  explicit AudioJitterBufferBinding(int sample_rate)
      : rate_(sample_rate), neteq_rate_(NetEqRateFor(sample_rate)), jb_(Config(neteq_rate_)) {}

  // The rate NetEq runs at for a page at `sample_rate`.
  static int NetEqRateFor(int sample_rate) {
    return timing::NetEqSupportsSampleRate(sample_rate) ? sample_rate : 48000;
  }

  // `samples`: one decoded frame at the page's rate; `rtp_timestamp` in the
  // page's rate too.
  void InsertPacket(uint32_t rtp_timestamp, std::vector<float> samples, Timestamp arrival) {
    if (neteq_rate_ == rate_) {
      jb_.InsertPacket(rtp_timestamp, std::move(samples), arrival);
      return;
    }
    const int64_t ts = ts_unwrapper_.Unwrap(rtp_timestamp);
    const uint32_t neteq_ts = static_cast<uint32_t>(static_cast<uint64_t>(ts * neteq_rate_ / rate_));
    jb_.InsertPacket(neteq_ts, ToNetEqRate(samples), arrival);
  }

  // Fills `out` (10 ms at the page's rate) with playout.
  timing::AudioOperation GetAudio(std::span<float> out, Timestamp now) {
    if (neteq_rate_ == rate_) return jb_.GetAudio(out, now);
    neteq_out_.assign(Scaled(out.size()), 0.0f);
    const timing::AudioOperation op = jb_.GetAudio(neteq_out_, now);
    out_resampler_.Resample(std::span<const float>(neteq_out_), out);
    return op;
  }

  int neteq_rate() const { return neteq_rate_; }
  const timing::AudioJitterBuffer& jitter_buffer() const { return jb_; }

 private:
  static timing::AudioJitterBufferConfig Config(int sample_rate) {
    timing::AudioJitterBufferConfig c;
    c.sample_rate = sample_rate;
    return c;
  }

  size_t Scaled(size_t samples) const {
    return static_cast<size_t>((static_cast<int64_t>(samples) * neteq_rate_ + rate_ / 2) / rate_);
  }

  // 10 ms blocks through one resampler, as libwebrtc's paths; a packet that
  // isn't whole 10 ms blocks goes through in one call.
  std::vector<float> ToNetEqRate(const std::vector<float>& samples) {
    const size_t block = static_cast<size_t>(rate_ / 100);
    std::vector<float> out;
    if (block == 0 || samples.empty()) return out;
    if (samples.size() % block != 0) {
      out.assign(Scaled(samples.size()), 0.0f);
      in_resampler_.Resample(std::span<const float>(samples), std::span<float>(out));
      return out;
    }
    const size_t neteq_block = static_cast<size_t>(neteq_rate_ / 100);
    out.assign(samples.size() / block * neteq_block, 0.0f);
    for (size_t i = 0, o = 0; i < samples.size(); i += block, o += neteq_block) {
      in_resampler_.Resample(std::span<const float>(samples.data() + i, block),
                             std::span<float>(out.data() + o, neteq_block));
    }
    return out;
  }

  const int rate_;
  const int neteq_rate_;
  timing::AudioJitterBuffer jb_;
  SeqNumUnwrapper<uint32_t> ts_unwrapper_;
  audio::PushResampler<float> in_resampler_;
  audio::PushResampler<float> out_resampler_;
  std::vector<float> neteq_out_;
};

}  // namespace sm::web
