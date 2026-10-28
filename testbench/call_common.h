// Pieces of a test bench call that other call implementations share, so
// they apply the same link timeline and compute metrics the same way
// (compare/ runs other libraries through the test bench).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "scenario.h"
#include "sm/core/seq_num.h"
#include "sm/sim/network_link.h"

namespace sm::testbench {

// Goodput counts each media packet once (#329): the first delivery of an RTP
// sequence number on a stream, as the original or as an RTX copy (RFC 4588
// original sequence number). A copy whose original had already arrived (a
// resend after reordering, RTX payload padding, a duplicate on the link) is
// repair traffic and is not counted again.
class FirstDelivery {
 public:
  enum Stream { kAudio = 0, kVideo = 1 };
  // True the first time `seq` arrives on `stream`.
  bool Deliver(Stream stream, uint16_t seq) {
    State& st = streams_[stream];
    const int64_t unwrapped = st.unwrap.Unwrap(seq);
    int64_t& slot = st.slots[seq];
    if (slot == unwrapped) return false;
    slot = unwrapped;
    return true;
  }

 private:
  struct State {
    SeqNumUnwrapper<uint16_t> unwrap;
    std::vector<int64_t> slots = std::vector<int64_t>(65536, std::numeric_limits<int64_t>::min());
  };
  State streams_[2];
};

// The repair metrics, computed by one rule from the same forward-link events for both stacks (#509):
// repair.overhead_pct, repair.nack_recovered and video.residual_loss_pct. Each call implementation feeds it the
// packets it puts on the forward link, classified as for net.<kind>_kbps (sim::LinkKind), the packets that arrive
// from it, and the media packets its stack's ULPFEC receiver recovered.
//   - Overhead: whole RTP packets (header, extensions, payload, padding) as they go on the wire, RTX and FEC
//     against audio and video media. An audio RED packet counts with its redundant block.
//   - NACK recovered: RTX arrivals with a payload whose original sequence number had not arrived yet, as
//     original or RTX. A late original after a NACK is not a recovery.
//   - Residual loss: video media packets sent up to a second before the end of the call that never arrived
//     (as media or as RTX) and were not recovered by FEC.
class RepairMeter {
 public:
  // A packet put on the forward link: its kind and the whole RTP packet's size. `seq` is a video media
  // packet's sequence number, sent at `send_ms`.
  void OnSent(sim::LinkKind kind, size_t rtp_bytes, uint16_t seq, int64_t send_ms) {
    const auto bytes = static_cast<int64_t>(rtp_bytes);
    if (kind == sim::LinkKind::kAudio || kind == sim::LinkKind::kVideo) media_bytes_ += bytes;
    if (kind == sim::LinkKind::kRtx || kind == sim::LinkKind::kFec) repair_bytes_ += bytes;
    if (kind == sim::LinkKind::kVideo) video_send_ms_.emplace(send_unwrap_.Unwrap(seq), send_ms);
  }
  // A video media packet (not FEC) that arrived from the forward link.
  void OnVideoArrived(uint16_t seq) { received_.insert(recv_unwrap_.Unwrap(seq)); }
  // An RTX packet with a payload that arrived from the forward link: `osn` is its original sequence number.
  void OnRtxArrived(uint16_t osn) {
    if (received_.insert(recv_unwrap_.Unwrap(osn)).second) ++nack_recovered_;
  }
  // A video media packet the stack's ULPFEC receiver recovered.
  void OnFecRecovered(uint16_t seq) { received_.insert(recv_unwrap_.Unwrap(seq)); }

  // repair.overhead_pct, and with video repair.nack_recovered and video.residual_loss_pct for a call of
  // `end_ms`.
  template <class Metrics>
  void AddMetrics(Metrics& m, bool video, int64_t end_ms) const {
    m["repair.overhead_pct"] =
        media_bytes_ ? 100.0 * static_cast<double>(repair_bytes_) / static_cast<double>(media_bytes_) : 0;
    if (!video) return;
    // Packets sent in the last second may still be in flight or under repair.
    int64_t counted = 0, lost = 0;
    for (const auto& [useq, sent_ms] : video_send_ms_) {
      if (sent_ms > end_ms - 1000) continue;
      ++counted;
      lost += !received_.count(useq);
    }
    m["video.residual_loss_pct"] = counted ? 100.0 * static_cast<double>(lost) / static_cast<double>(counted) : 0;
    m["repair.nack_recovered"] = static_cast<double>(nack_recovered_);
  }

 private:
  int64_t media_bytes_ = 0, repair_bytes_ = 0, nack_recovered_ = 0;
  SeqNumUnwrapper<uint16_t> send_unwrap_, recv_unwrap_;
  std::map<int64_t, int64_t> video_send_ms_;  // Unwrapped video media sequence number -> send time.
  std::set<int64_t> received_;
};

// Audio concealment from the jitter buffer's lifetime counters, read after
// every 10 ms pull and computed the same way for both stacks (#372):
// totalSamplesReceived, concealedSamples and concealmentEvents, from NetEq's
// GetStats on libwebrtc's side. A concealment event lasts the concealed
// samples added from its concealmentEvents increment to the next one, and is
// an interruption at 150 ms or more, as libwebrtc's
// StatisticsCalculator::EndExpandEvent counts interruptionCount
// (reference/modules/audio_coding/neteq/statistics_calculator.cc).
//
// The pull's operation, or the samples concealed within one pull, would miss
// part of an expansion: NetEqImpl::DoExpand adds whole Expand periods (12.6 ms
// each on #372's outage), so every few pulls play out the expanded samples
// left in the sync buffer with a Normal operation and add no concealed samples.
class ConcealmentMeter {
 public:
  static constexpr int64_t kInterruptionMs = 150;  // statistics_calculator.cc kInterruptionLenMs.

  explicit ConcealmentMeter(int sample_rate_hz) : sample_rate_hz_(sample_rate_hz) {}

  // The counters after a pull. Sets pull_total() and pull_concealed().
  void OnPull(uint64_t total_samples, uint64_t concealed_samples, uint64_t concealment_events) {
    pull_total_ = total_samples - prev_total_;
    pull_concealed_ = concealed_samples - prev_concealed_;
    prev_total_ = total_samples;
    prev_concealed_ = concealed_samples;
    total_ += pull_total_;
    concealed_ += pull_concealed_;
    if (concealment_events != prev_events_) {
      EndEvent();
      prev_events_ = concealment_events;
      in_event_ = true;
    }
    if (in_event_) event_samples_ += pull_concealed_;
  }
  // Ends the open event at the end of the call.
  void Finish() { EndEvent(); }

  uint64_t pull_total() const { return pull_total_; }
  uint64_t pull_concealed() const { return pull_concealed_; }
  // Concealed samples in percent of the samples played (100 if none were).
  double concealed_pct() const {
    return total_ ? 100.0 * static_cast<double>(concealed_) / static_cast<double>(total_) : 100.0;
  }
  int64_t interruptions() const { return interruptions_; }
  int64_t interruption_ms() const { return interruption_ms_; }
  int64_t longest_event_ms() const { return longest_event_ms_; }

 private:
  void EndEvent() {
    if (!in_event_) return;
    const int64_t ms = static_cast<int64_t>(1000 * event_samples_ / static_cast<uint64_t>(sample_rate_hz_));
    longest_event_ms_ = std::max(longest_event_ms_, ms);
    if (ms >= kInterruptionMs) {
      ++interruptions_;
      interruption_ms_ += ms;
    }
    in_event_ = false;
    event_samples_ = 0;
  }

  int sample_rate_hz_;
  uint64_t prev_total_ = 0, prev_concealed_ = 0, prev_events_ = 0;
  uint64_t pull_total_ = 0, pull_concealed_ = 0, total_ = 0, concealed_ = 0;
  bool in_event_ = false;
  uint64_t event_samples_ = 0;
  int64_t interruptions_ = 0, interruption_ms_ = 0, longest_event_ms_ = 0;
};

// audio.stretch_pct from the jitter buffer's lifetime counters after every
// 10 ms pull, the same way for both stacks (#510): a pull is stretched when
// insertedSamplesForDeceleration or removedSamplesForAcceleration grew, as
// sm_compare reads NetEq's GetStats (NetEqLifetimeStatistics). The pull's
// operation would also count an Accelerate or PreemptiveExpand that moved no
// samples. Playout counts from the first pull after which the jitter buffer
// has output samples (totalSamplesReceived > 0); OnPull returns false for the
// pulls before it, which no playout statistic counts.
class StretchMeter {
 public:
  bool OnPull(uint64_t total_samples, uint64_t inserted_for_deceleration, uint64_t removed_for_acceleration) {
    if (total_samples == 0) return false;
    const uint64_t stretched = inserted_for_deceleration + removed_for_acceleration;
    ++pulls_;
    if (stretched != prev_stretched_) ++stretched_pulls_;
    prev_stretched_ = stretched;
    return true;
  }
  int64_t played_ms() const { return 10 * pulls_; }
  // Stretched pulls in percent of the pulls played (0 if none were).
  double stretch_pct() const {
    return pulls_ ? 100.0 * static_cast<double>(stretched_pulls_) / static_cast<double>(pulls_) : 0;
  }

 private:
  uint64_t prev_stretched_ = 0;
  int64_t pulls_ = 0, stretched_pulls_ = 0;
};

// audio.time_to_first_ms, the same way for both stacks (#510): the arrival
// time of the first audio packet the stack's jitter buffer took, read right
// after the packet was handed to the stack. A stream the jitter buffer
// refuses (#256) never starts.
class FirstAudioMeter {
 public:
  void OnAudioPacket(Timestamp at, bool jitter_buffer_has_audio) {
    if (!first_.IsFinite() && jitter_buffer_has_audio) first_ = at;
  }
  bool started() const { return first_.IsFinite(); }
  // In ms since the call started; the call's length if no packet was taken.
  double ms(int64_t end_ms) const { return first_.IsFinite() ? static_cast<double>(first_.ms()) : static_cast<double>(end_ms); }

 private:
  Timestamp first_ = Timestamp::MinusInfinity();
};

// Times of one video frame on its way from capture to render (issue #60).
// An unset time is infinite or minus infinity and the frame is not counted.
struct VideoFrameTimes {
  Timestamp capture = Timestamp::MinusInfinity();
  Timestamp submitted = Timestamp::MinusInfinity();   // Handed to the sender (after encoding).
  Timestamp last_sent = Timestamp::MinusInfinity();   // Last media packet left the pacer.
  Timestamp complete = Timestamp::MinusInfinity();    // Last media or RTX packet of the frame arrived.
  Timestamp rendered = Timestamp::MinusInfinity();
};

// Adds video.delay_encode_ms (mean), video.delay_pacer_p95_ms,
// video.delay_network_p95_ms and video.delay_jitter_buffer_p95_ms from the
// frames that were rendered, for both stacks (#510). Adds nothing when no
// frame was.
template <class Metrics>
void AddVideoDelayMetrics(const std::vector<VideoFrameTimes>& frames, Metrics& m);

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

template <class Metrics>
void AddVideoDelayMetrics(const std::vector<VideoFrameTimes>& frames, Metrics& m) {
  std::vector<double> encode, pacer, network, jitter;
  auto ms = [](TimeDelta d) { return static_cast<double>(d.us()) / 1000.0; };
  for (const auto& f : frames) {
    if (!f.rendered.IsFinite() || !f.capture.IsFinite() || !f.submitted.IsFinite() || !f.last_sent.IsFinite() ||
        !f.complete.IsFinite()) {
      continue;
    }
    encode.push_back(ms(f.submitted - f.capture));
    pacer.push_back(ms(f.last_sent - f.submitted));
    // A frame completed by a retransmission arrives after its last original
    // packet was sent; a frame completed by FEC can look complete before it.
    network.push_back(std::max(0.0, ms(f.complete - f.last_sent)));
    jitter.push_back(ms(f.rendered - f.complete));
  }
  if (encode.empty()) return;
  m["video.delay_encode_ms"] = Mean(encode);
  m["video.delay_pacer_p95_ms"] = Percentile(pacer, 95);
  m["video.delay_network_p95_ms"] = Percentile(network, 95);
  m["video.delay_jitter_buffer_p95_ms"] = Percentile(jitter, 95);
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
