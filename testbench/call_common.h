// Pieces of a test bench call that other call implementations share, so
// they apply the same link timeline and compute metrics the same way
// (compare/ runs other libraries through the test bench).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "scenario.h"
#include "sm/sim/network_link.h"

namespace sm::testbench {

// The reverse-link trace's `seq` for an RTCP compound: the first sequence number
// its first generic NACK (RTPFB, FMT 1) asks for, else -1 (issue #217).
inline int32_t FirstNackedSeq(std::span<const uint8_t> d) {
  size_t off = 0;
  while (off + 4 <= d.size()) {
    const size_t len = (static_cast<size_t>((d[off + 2] << 8) | d[off + 3]) + 1) * 4;
    if (d[off + 1] == 205 && (d[off] & 0x1f) == 1 && off + 14 <= d.size()) return (d[off + 12] << 8) | d[off + 13];
    off += len;
  }
  return -1;
}

// Applies scenario link changes to an emulated link.
struct LinkState {
  sim::LinkConfig cfg;
  TimeDelta queue = TimeDelta::Zero();
  DataRate cross = DataRate::Zero();
  double loss = 0, burst = 1;

  void Apply(const LinkChange& c) {
    if (c.capacity) cfg.capacity = *c.capacity;
    if (c.delay) cfg.base_delay = *c.delay;
    if (c.jitter) cfg.jitter_stddev = *c.jitter;
    if (c.loss) loss = *c.loss;
    if (c.burst_len) burst = *c.burst_len;
    if (c.duplicate) cfg.duplicate_rate = *c.duplicate;
    if (c.reorder) cfg.allow_reordering = *c.reorder;
    if (c.queue) queue = *c.queue;
    if (c.cross) cross = *c.cross;
    if (c.down) cfg.down = *c.down;
    // Loss: Bernoulli, or Gilbert-Elliott with the requested mean burst
    // length and the same average loss rate.
    if (burst > 1 && loss > 0 && loss < 1) {
      cfg.gilbert = true;
      cfg.p_bad_to_good = 1.0 / burst;
      cfg.p_good_to_bad = loss * cfg.p_bad_to_good / (1 - loss);
      cfg.loss_in_bad = 1.0;
      cfg.loss_in_good = 0.0;
    } else {
      cfg.gilbert = false;
      cfg.loss_rate = loss;
    }
    cfg.queue_limit = (cfg.capacity.IsFinite() && queue.us() > 0)
                          ? std::max(DataSize::Bytes(1500), cfg.capacity * queue)
                          : DataSize::Zero();
  }
};

inline LinkState DefaultForward() {
  LinkState l;
  l.cfg.base_delay = TimeDelta::Millis(20);
  return l;
}

// Steps the forward and reverse links through the scenario's timeline.
class LinkTimeline {
 public:
  explicit LinkTimeline(const Scenario& s) : s_(s), fwd_(DefaultForward()), rev_(DefaultForward()) {
    // An unset reverse link mirrors the forward link's initial delay.
    if (s.reverse.empty() && !s.forward.empty() && s.forward[0].at.IsZero() && s.forward[0].delay) {
      rev_.cfg.base_delay = *s.forward[0].delay;
    }
  }
  const LinkState& forward() const { return fwd_; }
  const LinkState& reverse() const { return rev_; }

  // Applies every change due by `t` (time since the call started). Returns
  // {forward changed, reverse changed}.
  std::pair<bool, bool> Advance(TimeDelta t) {
    bool f = false, r = false;
    while (next_fwd_ < s_.forward.size() && s_.forward[next_fwd_].at <= t) {
      fwd_.Apply(s_.forward[next_fwd_++]);
      f = true;
    }
    while (next_rev_ < s_.reverse.size() && s_.reverse[next_rev_].at <= t) {
      rev_.Apply(s_.reverse[next_rev_++]);
      r = true;
    }
    return {f, r};
  }

 private:
  const Scenario& s_;
  LinkState fwd_, rev_;
  size_t next_fwd_ = 0, next_rev_ = 0;
};

// Bottleneck queue delay of a link, in ms.
template <class T>
double QueueDelayMs(const sim::NetworkLink<T>& link, const LinkState& st, Timestamp now) {
  if (!st.cfg.capacity.IsFinite() || st.cfg.capacity.bps() <= 0) return 0;
  return static_cast<double>(link.QueuedBytes(now).bits()) * 1000.0 / static_cast<double>(st.cfg.capacity.bps());
}

inline double Percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  double idx = p / 100.0 * static_cast<double>(v.size() - 1);
  size_t lo = static_cast<size_t>(std::floor(idx));
  size_t hi = std::min(v.size() - 1, lo + 1);
  return v[lo] + (v[hi] - v[lo]) * (idx - static_cast<double>(lo));
}

inline double Mean(const std::vector<double>& v) {
  if (v.empty()) return 0;
  double s = 0;
  for (double x : v) s += x;
  return s / static_cast<double>(v.size());
}

// Audio/video synchronization as the user sees it (issue #63), measured the
// same way for every call implementation. At each rendered video frame, the
// audio playing at that moment is compared with the frame in capture time:
//   offset = capture time of the audio being played - capture time of the frame
// so a positive offset means the video plays later than the audio it was
// captured with. This is libwebrtc's RtpStreamsSynchronizer::
// GetStreamSyncOffsetInMs (video/rtp_streams_synchronizer2.cc), the
// VideoReceiveStream stat sync_offset_ms, with the capture times known
// exactly instead of estimated from RTCP sender reports: the audio's comes
// from the receiver's playout RTP timestamp (Syncable::GetPlayoutRtpTimestamp,
// NetEq::GetPlayoutTimestamp) moved on by the time since it was read, and the
// video's from the frame being rendered. Frames rendered before audio playout
// starts are not counted.
class AvSyncMeter {
 public:
  // The audio last played out was captured at `audio_capture`; it played at `now`.
  void OnAudioPlayout(Timestamp now, Timestamp audio_capture) {
    audio_at_ = now;
    audio_capture_ = audio_capture;
  }
  void OnVideoRendered(Timestamp now, Timestamp video_capture) {
    if (!audio_at_.IsFinite()) return;
    const Timestamp audio_now = audio_capture_ + (now - audio_at_);
    offsets_ms_.push_back(static_cast<double>((audio_now - video_capture).us()) / 1000.0);
  }
  // sync.av_offset_p50_ms (signed median), sync.av_offset_p95_ms (95th
  // percentile of the absolute offset) and sync.av_offset_max_ms (largest
  // absolute offset). Adds nothing without a measured frame.
  template <class Metrics>
  void AddMetrics(Metrics& m) const {
    if (offsets_ms_.empty()) return;
    std::vector<double> abs_ms;
    abs_ms.reserve(offsets_ms_.size());
    for (double o : offsets_ms_) abs_ms.push_back(std::fabs(o));
    m["sync.av_offset_p50_ms"] = Percentile(offsets_ms_, 50);
    m["sync.av_offset_p95_ms"] = Percentile(abs_ms, 95);
    m["sync.av_offset_max_ms"] = *std::max_element(abs_ms.begin(), abs_ms.end());
  }

 private:
  Timestamp audio_at_ = Timestamp::MinusInfinity();
  Timestamp audio_capture_ = Timestamp::MinusInfinity();
  std::vector<double> offsets_ms_;
};

}  // namespace sm::testbench
