// The scripted speech a send side encodes, made as the call reads it (issue #633).
//
// The signal is the one sm_compare gives libwebrtc's ADM (compare/webrtc/webrtc_call.cc): sim::SpeechLike at 48 kHz for
// the call's duration + 1 s, as 16-bit PCM (OpusSendEncoder::ToPcm). Building it whole took 2 bytes of PCM plus 4 of
// float per sample, about 15 MB per simulated minute for each send side. This makes it a block of 1 s at a time from
// sim::SpeechLikeGenerator, keeps the few blocks the call is reading, and remakes an older block from the generator's
// state at its start, so any frame reads the same samples as the whole signal would have and the memory does not grow
// with the call.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <utility>
#include <vector>

#include "opus_encoder.h"
#include "sm/sim/signals.h"

namespace sm::testbench {

class SpeechPcm {
 public:
  static constexpr int kSampleRateHz = OpusDefaults::kSampleRateHz;
  static constexpr size_t kFrameSamples = OpusSendEncoder::kFrameSamples;    // 20 ms.
  static constexpr size_t kBlockSamples = static_cast<size_t>(kSampleRateHz);  // 1 s: 50 whole frames.
  // Blocks kept: the call reads a frame at its capture (and send-ahead, #762, a little later), and a two-way call's
  // listeners read it again at its arrival (#960) and at the user's microphone (#961), seconds behind at most.
  static constexpr size_t kCachedBlocks = 4;
  // Generator states kept, one per block start: a block this far back is remade from its start state; an older one
  // (no reader lags that much) is remade from the signal's start.
  static constexpr size_t kCheckpoints = 128;

  // sim::SpeechLike(kSampleRateHz, seconds, seed, pauses) as PCM.
  SpeechPcm(double seconds, uint64_t seed, bool pauses)
      : size_(static_cast<size_t>(seconds * kSampleRateHz)), start_(kSampleRateHz, seed, pauses), next_(start_) {
    cache_.reserve(kCachedBlocks);
  }

  // Samples in the whole signal.
  size_t size() const { return size_; }

  // Frame k: samples [20k ms, 20(k+1) ms), fewer at the end of the signal and none past it. Valid until the next call.
  std::span<const int16_t> Frame(int64_t k) {
    if (k < 0) return {};
    const size_t off = static_cast<size_t>(k) * kFrameSamples;
    if (off >= size_) return {};
    const Block& b = BlockAt(off / kBlockSamples);
    return std::span<const int16_t>(b.pcm).subspan(off % kBlockSamples, std::min(kFrameSamples, size_ - off));
  }

  // The most PCM bytes held at once.
  size_t peak_bytes() const { return peak_bytes_; }

 private:
  struct Block {
    size_t index = 0;
    uint64_t used = 0;  // Last read, for eviction.
    std::vector<int16_t> pcm;
  };

  const Block& BlockAt(size_t index) {
    ++reads_;
    for (Block& b : cache_) {
      if (b.index == index) {
        b.used = reads_;
        return b;
      }
    }
    // The generator at the block's start: forward from the newest block made, or a kept start state, or the start.
    sim::SpeechLikeGenerator gen = start_;
    if (index >= next_index_) {
      for (; next_index_ < index; ++next_index_) {
        Checkpoint(next_index_);
        for (size_t i = 0; i < kBlockSamples; ++i) next_.Next();
      }
      Checkpoint(index);
      gen = next_;
    } else {
      const auto it = std::find_if(checkpoints_.begin(), checkpoints_.end(), [&](const auto& c) { return c.first == index; });
      if (it != checkpoints_.end()) {
        gen = it->second;
      } else {
        for (size_t i = 0; i < index * kBlockSamples; ++i) gen.Next();
      }
    }
    Block* slot;
    if (cache_.size() < kCachedBlocks) {
      slot = &cache_.emplace_back();
      slot->pcm.resize(kBlockSamples);
    } else {
      slot = &*std::min_element(cache_.begin(), cache_.end(), [](const Block& a, const Block& b) { return a.used < b.used; });
    }
    slot->index = index;
    slot->used = reads_;
    for (int16_t& v : slot->pcm) v = OpusSendEncoder::ToPcm(gen.Next());
    if (index >= next_index_) {
      next_ = gen;
      next_index_ = index + 1;
    }
    peak_bytes_ = std::max(peak_bytes_, cache_.size() * kBlockSamples * sizeof(int16_t));
    return *slot;
  }

  // Keeps the generator state at block `index`'s start (next_), dropping the oldest beyond kCheckpoints.
  void Checkpoint(size_t index) {
    checkpoints_.emplace_back(index, next_);
    if (checkpoints_.size() > kCheckpoints) checkpoints_.pop_front();
  }

  size_t size_;
  sim::SpeechLikeGenerator start_;  // At sample 0.
  sim::SpeechLikeGenerator next_;   // At block next_index_'s start: the first block not made yet.
  size_t next_index_ = 0;
  std::deque<std::pair<size_t, sim::SpeechLikeGenerator>> checkpoints_;
  std::vector<Block> cache_;
  uint64_t reads_ = 0;
  size_t peak_bytes_ = 0;
};

}  // namespace sm::testbench
