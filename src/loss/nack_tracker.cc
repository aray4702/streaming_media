#include "sm/loss/nack_tracker.h"

#include <algorithm>

namespace sm::loss {
namespace {
constexpr int kLibwebrtcMaxRetries = 100;  // nack_requester.cc kMaxNackRetries
constexpr TimeDelta kProcessInterval = TimeDelta::Millis(20);  // NackPeriodicProcessor::kUpdateInterval
}  // namespace

NackTracker::NackTracker(NackConfig cfg) : cfg_(cfg), rtt_(cfg.initial_rtt) {}

int NackTracker::OnReceivedPacket(uint16_t seq, bool is_keyframe, Timestamp now) {
  int64_t s = unwrapper_.Unwrap(seq);
  ++stats_.packets_received;
  if (!initialized_) {
    initialized_ = true;
    newest_ = s;
    if (is_keyframe) keyframes_.insert(s);
    return 0;
  }
  if (is_keyframe) keyframes_.insert(s);
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
  // Newer packet: everything between newest_ and s is missing.
  if (cfg_.version >= 2) {
    // NackRequester::AddPacketsToNack: no keyframe bookkeeping, no partial keep.
    if (s > newest_ + 1) AddMissingV2(newest_ + 1, s, now);
    newest_ = s;
    return 0;
  }
  if (s - newest_ > cfg_.max_packet_age) {
    // Huge jump: the old state is useless.
    stats_.abandoned += missing_.size();
    missing_.clear();
    keyframe_needed_ = true;
  } else if (s > newest_ + 1) {
    AddMissing(newest_ + 1, s, now);
  }
  newest_ = s;
  TrimOld(now);
  return 0;
}

void NackTracker::ClearUpTo(uint16_t seq) {
  if (cfg_.version < 2 || !initialized_) return;
  const auto end = missing_.lower_bound(unwrapper_.PeekUnwrap(seq));
  for (auto it = missing_.begin(); it != end; it = missing_.erase(it)) ++stats_.cleared;
}

void NackTracker::AddMissing(int64_t from, int64_t to, Timestamp now) {
  for (int64_t i = from; i < to; ++i) {
    missing_.emplace(i, Info{now});
    ++stats_.missing_detected;
  }
  SM_EVENT(cfg_.events, now, "nack", "gap", static_cast<double>(to - from), from);
  while (missing_.size() > cfg_.max_nack_list) {
    if (!RecoverKeyframeSpace(now)) {
      stats_.abandoned += missing_.size();
      missing_.clear();
      keyframe_needed_ = true;
      break;
    }
  }
}

// NackRequester::AddPacketsToNack: forget gaps older than the age limit; if the
// list would pass the limit, clear it all and ask for a keyframe.
void NackTracker::AddMissingV2(int64_t from, int64_t to, Timestamp now) {
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
    missing_.emplace(i, Info{now});
    ++stats_.missing_detected;
  }
  SM_EVENT(cfg_.events, now, "nack", "gap", static_cast<double>(to - from), from);
}

// Drops missing entries older than the newest keyframe after the first one.
bool NackTracker::RecoverKeyframeSpace(Timestamp now) {
  if (missing_.empty()) return true;
  auto kf = keyframes_.upper_bound(missing_.begin()->first);
  if (kf == keyframes_.end()) return false;
  auto end = missing_.lower_bound(*kf);
  for (auto it = missing_.begin(); it != end;) {
    ++stats_.abandoned;
    it = missing_.erase(it);
  }
  SM_EVENT(cfg_.events, now, "nack", "drop_to_keyframe", 0, *kf);
  return true;
}

void NackTracker::TrimOld(Timestamp now) {
  int64_t oldest = newest_ - cfg_.max_packet_age;
  while (!missing_.empty() && missing_.begin()->first < oldest) {
    missing_.erase(missing_.begin());
    ++stats_.abandoned;
  }
  while (!keyframes_.empty() && *keyframes_.begin() < oldest) keyframes_.erase(keyframes_.begin());
  (void)now;
}

std::vector<uint16_t> NackTracker::GetNackBatch(Timestamp now) {
  std::vector<uint16_t> out;
  const TimeDelta retry = std::max(rtt_ * cfg_.retry_rtt_factor, cfg_.min_retry_interval);
  const int max_retries = cfg_.version >= 2 && cfg_.max_retries == 10 ? kLibwebrtcMaxRetries : cfg_.max_retries;
  // Version 2: a retry is decided by NackPeriodicProcessor, every kUpdateInterval (20 ms), not at
  // every call (nack_requester.h). A retry therefore leaves up to 20 ms after the RTT has passed,
  // which is what keeps it from arriving just short of the sender's one-RTT repeat guard.
  bool tick = true;
  if (cfg_.version >= 2) {
    tick = !last_process_.IsFinite() || now - last_process_ >= kProcessInterval;
    if (tick) last_process_ = now;
  }
  for (auto it = missing_.begin(); it != missing_.end();) {
    Info& info = it->second;
    bool due = info.retries == 0 ? (now - info.detected >= cfg_.reorder_wait)
                                 : (tick && now - info.last_sent >= retry);
    if (due && info.retries >= max_retries) {
      ++stats_.abandoned;
      SM_EVENT(cfg_.events, now, "nack", "abandon", info.retries, it->first);
      it = missing_.erase(it);
      if (cfg_.version < 2) keyframe_needed_ = true;  // libwebrtc gives up without a request.
      continue;
    }
    if (due) {
      out.push_back(static_cast<uint16_t>(it->first));
      info.last_sent = now;
      ++info.retries;
      ++stats_.nack_requests;
    }
    ++it;
  }
  if (!out.empty()) SM_EVENT(cfg_.events, now, "nack", "request", out.size(), out.front());
  return out;
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
  w.Gauge("pending", static_cast<double>(missing_.size()));
  w.Gauge("rtt_ms", rtt_.ms_f());
  w.Hist("recovery_time_ms", stats_.recovery_time_ms);
  w.Hist("retries_per_recovery", stats_.retries_per_recovery);
}

}  // namespace sm::loss
