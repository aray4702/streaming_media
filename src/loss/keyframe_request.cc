#include "sm/loss/keyframe_request.h"

#include <algorithm>

namespace sm::loss {

KeyframeRequester::KeyframeRequester(KeyframeRequestConfig cfg) : cfg_(cfg) {}

bool KeyframeRequester::IsDecodable(const FrameInfo& f) const {
  if (f.keyframe) return true;
  return std::all_of(f.references.begin(), f.references.end(),
                     [&](int64_t r) { return decodable_.count(r) > 0; });
}

void KeyframeRequester::OnFrame(const FrameInfo& f, Timestamp now) {
  ++stats_.frames_received;
  last_received_ = std::max(last_received_, f.frame_id);
  if (f.keyframe) {
    // A keyframe resets the dependency chain; older blocked frames are dead.
    for (auto it = waiting_.begin(); it != waiting_.end() && it->first < f.frame_id;) {
      ++stats_.frames_dropped;
      it = waiting_.erase(it);
    }
    if (request_pending_ && first_request_.IsFinite()) {
      stats_.time_to_keyframe_ms.Add((now - first_request_).ms_f());
      SM_EVENT(cfg_.events, now, "keyframe", "received", (now - first_request_).ms_f(), f.frame_id);
    }
    request_pending_ = false;
    need_request_ = false;
    need_fir_ = false;
    first_request_ = Timestamp::MinusInfinity();
  }
  if (IsDecodable(f)) {
    decodable_.insert(f.frame_id);
    ready_.push_back(f.frame_id);
    ++stats_.frames_decodable;
    last_decodable_ = std::max(last_decodable_, f.frame_id);
    Propagate();
  } else {
    waiting_.emplace(f.frame_id, std::make_pair(f, now));
    if (cfg_.send_lntf && waiting_.size() == 1) lntf_pending_ = true;
  }
  while (decodable_.size() > cfg_.max_tracked_frames) decodable_.erase(decodable_.begin());
  while (waiting_.size() > cfg_.max_tracked_frames) {
    waiting_.erase(waiting_.begin());
    ++stats_.frames_dropped;
  }
}

void KeyframeRequester::Propagate() {
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
      if (IsDecodable(it->second.first)) {
        int64_t id = it->first;
        decodable_.insert(id);
        ready_.push_back(id);
        ++stats_.frames_decodable;
        last_decodable_ = std::max(last_decodable_, id);
        waiting_.erase(it);
        progress = true;
        break;
      }
    }
  }
}

void KeyframeRequester::OnFrameLost(int64_t frame_id, Timestamp now) {
  last_received_ = std::max(last_received_, frame_id);
  need_request_ = true;
  if (cfg_.send_lntf) lntf_pending_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", "frame_lost", 0, frame_id);
}

void KeyframeRequester::OnDecoderError(Timestamp now) {
  need_request_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", "decoder_error", 0, last_received_);
}

void KeyframeRequester::OnStreamStart(Timestamp now) {
  need_request_ = true;
  need_fir_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", "stream_start", 0, 0);
}

void KeyframeRequester::RequestKeyframe(Timestamp now, bool fir, const char* reason) {
  if (!request_pending_) first_request_ = now;
  request_pending_ = true;
  last_request_ = now;
  need_request_ = false;
  need_fir_ = false;
  if (fir) {
    ++stats_.fir_sent;
    ++fir_seq_;
  } else {
    ++stats_.pli_sent;
  }
  SM_EVENT(cfg_.events, now, "keyframe", reason, fir ? 1 : 0, last_decodable_);
}

std::vector<FeedbackMessage> KeyframeRequester::Process(Timestamp now) {
  std::vector<FeedbackMessage> out;
  if (lntf_pending_) {
    lntf_pending_ = false;
    ++stats_.lntf_sent;
    FeedbackMessage m{FeedbackType::kLntf};
    m.last_decodable = last_decodable_;
    m.last_received = last_received_;
    out.push_back(m);
  }
  // Frames blocked longer than the repair window are not going to be fixed by NACK.
  const TimeDelta window = std::max(cfg_.repair_window, rtt_ * cfg_.repair_rtt_factor);
  if (!waiting_.empty()) {
    Timestamp oldest = Timestamp::PlusInfinity();
    for (const auto& [id, fw] : waiting_) oldest = std::min(oldest, fw.second);
    if (now - oldest >= window) need_request_ = true;
  }
  // Retry when the requested keyframe does not show up.
  const TimeDelta throttle = std::max(cfg_.min_request_interval, rtt_ * cfg_.request_rtt_factor);
  if (request_pending_ && now - last_request_ >= throttle * 2) need_request_ = true;

  if (need_request_) {
    if (last_request_.IsFinite() && now - last_request_ < throttle) {
      ++stats_.throttled;
    } else {
      bool fir = need_fir_;
      RequestKeyframe(now, fir, fir ? "fir" : "pli");
      FeedbackMessage m{fir ? FeedbackType::kFir : FeedbackType::kPli};
      m.fir_seq = fir_seq_;
      out.push_back(m);
    }
  }
  return out;
}

std::vector<int64_t> KeyframeRequester::TakeDecodableFrames() {
  std::vector<int64_t> r;
  r.swap(ready_);
  return r;
}

void KeyframeRequester::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames_received", stats_.frames_received);
  w.Counter("frames_decodable", stats_.frames_decodable);
  w.Counter("frames_dropped", stats_.frames_dropped);
  w.Counter("pli_sent", stats_.pli_sent);
  w.Counter("fir_sent", stats_.fir_sent);
  w.Counter("lntf_sent", stats_.lntf_sent);
  w.Counter("throttled", stats_.throttled);
  w.Gauge("waiting_frames", static_cast<double>(waiting_.size()));
  w.Gauge("waiting_for_keyframe", request_pending_ ? 1 : 0);
  w.Hist("time_to_keyframe_ms", stats_.time_to_keyframe_ms);
}

}  // namespace sm::loss
