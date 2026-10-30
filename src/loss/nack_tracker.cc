#include "sm/loss/nack_tracker.h"

#include <algorithm>

namespace sm::loss {
namespace {
constexpr TimeDelta kProcessInterval = TimeDelta::Millis(20);  // NackPeriodicProcessor::kUpdateInterval
}  // namespace

NackTracker::NackTracker(NackConfig cfg) : cfg_(cfg), rtt_(cfg.initial_rtt) {}

// `is_keyframe` is unused: M157's NackRequester keeps no keyframe list (nack_requester.cc AddPacketsToNack
// clears a full list instead of dropping to a keyframe).
int NackTracker::OnReceivedPacket(uint16_t seq, bool /*is_keyframe*/, Timestamp now, bool is_recovered) {
  int64_t s = unwrapper_.Unwrap(seq);
  ++stats_.packets_received;
  if (!initialized_) {
    initialized_ = true;
    newest_ = s;
    return 0;
  }
  if (s == newest_) {
    ++stats_.duplicates;
    return 0;
  }
  if (s < newest_) {
    auto it = missing_.find(s);
    if (it == missing_.end()) {
      ++stats_.duplicates;  // Older than newest and not missing: a duplicate.
      return 0;
    }
    if (it->second.retries == 0) {
      ++stats_.reordered;
    } else {
      ++stats_.recovered;
      stats_.recovery_time_ms.Add((now - it->second.detected).ms_f());
      stats_.retries_per_recovery.Add(it->second.retries);
      SM_EVENT(cfg_.events, now, "nack", "recovered", (now - it->second.detected).ms_f(), seq);
    }
    const int times_nacked = it->second.retries;
    missing_.erase(it);
    return times_nacked;
  }
  // NackRequester::OnReceivedPacket: a recovered packet ahead of the newest is remembered, so that
  // AddPacketsToNack skips it, and the list keeps only the last kMaxPacketAge sequence numbers.
  if (is_recovered) {
    recovered_.insert(s);
    recovered_.erase(recovered_.begin(), recovered_.lower_bound(s - cfg_.max_packet_age));
    ++stats_.recovered_ahead;
    SM_EVENT(cfg_.events, now, "nack", "recovered_ahead", static_cast<double>(s - newest_), seq);
    return 0;
  }
  // Newer packet: everything between newest_ and s is missing (NackRequester::AddPacketsToNack).
  if (s > newest_ + 1) AddMissing(newest_ + 1, s, now);
  newest_ = s;
  return 0;
}

void NackTracker::ClearUpTo(uint16_t seq) {
  if (!initialized_) return;
  const int64_t s = unwrapper_.PeekUnwrap(seq);
  const auto end = missing_.lower_bound(s);
  for (auto it = missing_.begin(); it != end; it = missing_.erase(it)) ++stats_.cleared;
  recovered_.erase(recovered_.begin(), recovered_.lower_bound(s));
}

// NackRequester::AddPacketsToNack: forget gaps older than the age limit; if the
// list would pass the limit, clear it all and ask for a keyframe.
void NackTracker::AddMissing(int64_t from, int64_t to, Timestamp now) {
  const int64_t oldest = to - cfg_.max_packet_age;
  while (!missing_.empty() && missing_.begin()->first < oldest) {
    missing_.erase(missing_.begin());
    ++stats_.abandoned;
  }
  if (missing_.size() + static_cast<size_t>(to - from) > cfg_.max_nack_list) {
    stats_.abandoned += missing_.size();
    missing_.clear();
    keyframe_needed_ = true;
    SM_EVENT(cfg_.events, now, "nack", "list_full", static_cast<double>(to - from), from);
    return;
  }
  for (int64_t i = from; i < to; ++i) {
    if (recovered_.count(i)) continue;  // Recovered ahead of the gap: nothing to ask for.
    missing_.emplace(i, Info{now});
    ++stats_.missing_detected;
  }
  SM_EVENT(cfg_.events, now, "nack", "gap", static_cast<double>(to - from), from);
}

std::vector<uint16_t> NackTracker::GetNackBatch(Timestamp now) {
  std::vector<uint16_t> out;
  // nack_on_rtt_passed: `now - sent_at_time >= rtt_`, with no floor below the RTT.
  const TimeDelta retry = rtt_ * cfg_.retry_rtt_factor;
  // A retry is decided by NackPeriodicProcessor, every kUpdateInterval (20 ms), not at every call
  // (nack_requester.h). A retry therefore leaves up to 20 ms after the RTT has passed, which is what
  // keeps it from arriving just short of the sender's one-RTT repeat guard.
  // The ticks keep the grid of the first one, as RepeatingTask adds the delay to its last due time
  // (rtc_base/task_utils/repeating_task.cc:87), so a call between ticks does not move them (issue #518).
  const bool tick = !last_process_.IsFinite() || now - last_process_ >= kProcessInterval;
  if (tick) {
    last_process_ = !last_process_.IsFinite()
                        ? now
                        : last_process_ + kProcessInterval * ((now - last_process_).us() / kProcessInterval.us());
  }
  for (auto it = missing_.begin(); it != missing_.end();) {
    Info& info = it->second;
    bool due = info.retries == 0 ? (now - info.detected >= cfg_.reorder_wait)
                                 : (tick && now - info.last_sent >= retry);
    if (due && info.retries >= cfg_.max_retries) {
      ++stats_.abandoned;
      SM_EVENT(cfg_.events, now, "nack", "abandon", info.retries, it->first);
      it = missing_.erase(it);  // libwebrtc gives up without a keyframe request.
      continue;
    }
    if (due) Send(it->first, info, now, out);
    ++it;
  }
  if (!out.empty()) SM_EVENT(cfg_.events, now, "nack", "request", out.size(), out.front());
  return out;
}

// nack_requester.cc GetNackBatch(kSeqNumOnly): every request not sent yet whose `send_at_seq_num` the newest
// packet has reached. That is the gap's own sequence number, because the reordering histogram stays empty
// (OnReceivedPacket treats every packet as retransmitted, so WaitNumberOfPackets is 0).
std::vector<uint16_t> NackTracker::GetSeqNumNackBatch(Timestamp now) {
  std::vector<uint16_t> out;
  for (auto& [seq, info] : missing_) {
    if (info.retries == 0 && now - info.detected >= cfg_.reorder_wait) Send(seq, info, now, out);
  }
  if (!out.empty()) SM_EVENT(cfg_.events, now, "nack", "request", out.size(), out.front());
  return out;
}

Timestamp NackTracker::NextProcessTime() const {
  if (missing_.empty()) return Timestamp::PlusInfinity();
  Timestamp next = last_process_.IsFinite() ? last_process_ + kProcessInterval : Timestamp::MinusInfinity();
  for (const auto& [seq, info] : missing_) {
    if (info.retries == 0) next = std::min(next, info.detected + cfg_.reorder_wait);
  }
  return next;
}

void NackTracker::Send(int64_t seq, Info& info, Timestamp now, std::vector<uint16_t>& out) {
  out.push_back(static_cast<uint16_t>(seq));
  info.last_sent = now;
  ++info.retries;
  ++stats_.nack_requests;
}

bool NackTracker::TakeKeyframeRequest() {
  bool r = keyframe_needed_;
  if (r) ++stats_.keyframe_requests;
  keyframe_needed_ = false;
  return r;
}

void NackTracker::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets_received", stats_.packets_received);
  w.Counter("duplicates", stats_.duplicates);
  w.Counter("reordered", stats_.reordered);
  w.Counter("missing_detected", stats_.missing_detected);
  w.Counter("nack_requests", stats_.nack_requests);
  w.Counter("recovered", stats_.recovered);
  w.Counter("abandoned", stats_.abandoned);
  w.Counter("cleared", stats_.cleared);
  w.Counter("keyframe_requests", stats_.keyframe_requests);
  w.Counter("recovered_ahead", stats_.recovered_ahead);
  w.Gauge("pending", static_cast<double>(missing_.size()));
  w.Gauge("rtt_ms", rtt_.ms_f());
  w.Hist("recovery_time_ms", stats_.recovery_time_ms);
  w.Hist("retries_per_recovery", stats_.retries_per_recovery);
}

}  // namespace sm::loss
