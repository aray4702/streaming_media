// End-to-end simulations for section 4 (shared by e2e tests and benchmarks).
#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "sm/core/random.h"
#include "sm/sim/network_link.h"
#include "sm/sim/signals.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/timing/packet_buffer.h"
#include "sm/timing/rtcp_stats.h"
#include "sm/timing/video_jitter_estimator.h"

namespace sm::e2e {

// --- Audio jitter buffer over a jittery, lossy link ---------------------------

struct AudioJbSimParams {
  TimeDelta base_delay = TimeDelta::Millis(50);
  TimeDelta jitter = TimeDelta::Millis(20);
  TimeDelta jitter_second_half = TimeDelta::MinusInfinity();  // Optional step.
  double loss = 0.02;
  bool time_stretch = true;
  int fixed_delay_ms = 0;  // > 0: fixed-size buffer (no adaptation).
  TimeDelta stall_at = TimeDelta::MinusInfinity();   // Optional network stall.
  TimeDelta stall_for = TimeDelta::Zero();
  double seconds = 60;
  uint64_t seed = 1;
};

struct AudioJbSimResult {
  double concealment_ratio = 0;   // Concealed / output samples after start.
  double mean_buffer_delay_ms = 0;
  double p95_buffer_delay_ms = 0;
  double target_first_half_ms = 0, target_second_half_ms = 0;
  double late_ratio = 0;
  double removed_ms = 0, inserted_ms = 0;
  double recover_s = -1;          // After a stall: time until level <= target + 40 ms.
  double final_level_ms = 0;
};

inline AudioJbSimResult RunAudioJbSim(const AudioJbSimParams& p) {
  using namespace sm::timing;
  const int fs = 16000, pkt = 320;
  auto x = sim::SpeechLike(fs, p.seconds + 1, p.seed, true);
  sim::LinkConfig lc;
  lc.base_delay = p.base_delay;
  lc.jitter_stddev = p.jitter;
  lc.loss_rate = p.loss;
  sim::NetworkLink<std::pair<uint32_t, std::vector<float>>> link(lc, p.seed + 3);
  AudioJitterBufferConfig cfg;
  cfg.version = 1;  // The library's own buffer and DelayManager; the default is NetEq (#579).
  cfg.sample_rate = fs;
  cfg.time_stretch = p.time_stretch && p.fixed_delay_ms == 0;
  if (p.fixed_delay_ms > 0) {
    cfg.delay.min_delay_ms = p.fixed_delay_ms;
    cfg.delay.max_delay_ms = p.fixed_delay_ms;
    cfg.delay.initial_target_ms = p.fixed_delay_ms;
  }
  AudioJitterBuffer jb(cfg);
  std::vector<float> out(160);
  AudioJbSimResult r;
  const int64_t total_ms = static_cast<int64_t>(p.seconds * 1000);
  int64_t half = total_ms / 2;
  std::deque<std::pair<uint32_t, std::vector<float>>> stalled;
  bool stalling = false;
  uint64_t concealed_at_start = 0, output_at_start = 0;
  bool started = false;
  Timestamp stall_end = Timestamp::PlusInfinity();
  for (int64_t t = 0; t < total_ms; t += 10) {
    Timestamp now = Timestamp::Millis(t);
    if (p.jitter_second_half.IsFinite() && t == half) {
      lc.jitter_stddev = p.jitter_second_half;
      link.SetConfig(lc);
      r.target_first_half_ms = jb.TargetDelayMs();
    }
    if (t % 20 == 0) {
      int i = static_cast<int>(t / 20);
      std::vector<float> pcm(x.begin() + i * pkt, x.begin() + (i + 1) * pkt);
      link.Send(now, {static_cast<uint32_t>(i * pkt), std::move(pcm)}, DataSize::Bytes(100));
    }
    stalling = p.stall_at.IsFinite() && t >= p.stall_at.ms() && t < (p.stall_at + p.stall_for).ms();
    link.ReceiveEach(now, [&](std::pair<uint32_t, std::vector<float>> pk, Timestamp at) {
      (void)at;
      stalled.push_back(std::move(pk));
    });
    if (!stalling) {
      if (p.stall_at.IsFinite() && t == (p.stall_at + p.stall_for).ms()) stall_end = now;
      for (auto& pk : stalled) jb.InsertPacket(pk.first, std::move(pk.second), now);
      stalled.clear();
    }
    auto op = jb.GetAudio(out, now);
    if (!started && op != AudioOperation::kSilence) {
      started = true;
      concealed_at_start = jb.stats().concealed_samples;
      output_at_start = jb.stats().total_samples_received;
    }
    if (stall_end.IsFinite() && r.recover_s < 0 && now - stall_end > TimeDelta::Millis(100) &&
        jb.CurrentLevelMs() <= jb.TargetDelayMs() + 40) {
      r.recover_s = (now - stall_end).seconds();
    }
  }
  const auto& s = jb.stats();
  r.concealment_ratio = static_cast<double>(s.concealed_samples - concealed_at_start) /
                        static_cast<double>(std::max<uint64_t>(1, s.total_samples_received - output_at_start));
  r.mean_buffer_delay_ms = s.jitter_buffer_delay_ms.mean();
  r.p95_buffer_delay_ms = s.jitter_buffer_delay_ms.Percentile(95);
  r.target_second_half_ms = jb.TargetDelayMs();
  r.late_ratio = static_cast<double>(s.packets_discarded_late) / static_cast<double>(std::max<uint64_t>(1, s.packets_received));
  r.removed_ms = s.removed_samples_for_acceleration / 16.0;
  r.inserted_ms = s.inserted_samples_for_deceleration / 16.0;
  r.final_level_ms = jb.CurrentLevelMs();
  return r;
}

// --- Video jitter estimation + render timing ----------------------------------

struct VideoJitterSimResult {
  double late_ratio = 0;       // Frames that missed their render time.
  double mean_delay_ms = 0;    // Render time - send time.
  double mean_jitter_ms = 0;
};

inline VideoJitterSimResult RunVideoJitterSim(bool use_estimator, TimeDelta jitter, uint64_t seed, double seconds = 60) {
  using namespace sm::timing;
  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(30);
  lc.jitter_stddev = jitter;
  lc.allow_reordering = false;
  lc.capacity = DataRate::KilobitsPerSec(2500);
  sim::NetworkLink<std::pair<uint32_t, int64_t>> link(lc, seed);
  Random rng(seed + 1);
  VideoJitterEstimator est;
  VideoTiming timing;
  VideoJitterSimResult r;
  uint64_t frames = 0, late = 0;
  double delay_sum = 0, jitter_sum = 0;
  const int64_t end = static_cast<int64_t>(seconds * 1000);
  for (int64_t t = 0; t < end + 500; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t < end && t % 33 == 0) {
      int64_t i = t / 33;
      int64_t size = (i % 60 == 0) ? 25000 : static_cast<int64_t>(rng.Uniform(3000, 6000));
      link.Send(now, {static_cast<uint32_t>(i * 3000), t}, DataSize::Bytes(size));
    }
    link.ReceiveEach(now, [&](std::pair<uint32_t, int64_t> f, Timestamp at) {
      int64_t i = f.first / 3000;
      int64_t size = (i % 60 == 0) ? 25000 : 4500;
      est.OnFrame(f.first, at, DataSize::Bytes(size));
      timing.IncomingTimestamp(f.first, at);
      timing.SetJitterDelay(use_estimator ? est.JitterDelay() : TimeDelta::Zero());
      timing.AddDecodeTime(TimeDelta::Millis(5));
      timing.UpdateCurrentDelay(at);
      Timestamp render = timing.RenderTime(f.first, at);
      ++frames;
      if (at + TimeDelta::Millis(5) > render) ++late;
      delay_sum += (render - Timestamp::Millis(f.second)).ms_f();
      jitter_sum += est.JitterDelay().ms_f();
    });
  }
  r.late_ratio = static_cast<double>(late) / static_cast<double>(frames);
  r.mean_delay_ms = delay_sum / static_cast<double>(frames);
  r.mean_jitter_ms = jitter_sum / static_cast<double>(frames);
  return r;
}

}  // namespace sm::e2e
