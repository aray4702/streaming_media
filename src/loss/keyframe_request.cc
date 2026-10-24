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
  if (!has_received_frame_) {
    // RtpVideoStreamReceiver2::OnAssembledFrame: frames that arrive before a
    // keyframe cannot be decoded, so ask for one right away.
    has_received_frame_ = true;
    if (!f.keyframe) {
      need_request_ = true;
      SM_EVENT(cfg_.events, now, "keyframe", "trigger_first_frame_delta", 0, f.frame_id);
    }
  }
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
    Propagate(now);
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

void KeyframeRequester::Propagate(Timestamp now) {
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
  // libwebrtc does not turn a lost frame into a keyframe request: NACK keeps
  // repairing it, and the decode time-out is the backstop.
  if (cfg_.send_lntf) lntf_pending_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", "frame_lost", 0, frame_id);
}

void KeyframeRequester::OnDecoderError(Timestamp now) {
  need_request_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", "trigger_decoder_error", 0, last_received_);
}

void KeyframeRequester::OnMediaPacket(Timestamp now, bool keyframe_packet, std::optional<uint32_t> rtp_timestamp) {
  last_packet_ = now;
  // UpdatePacketReceiveTimestamps: a keyframe packet, or any packet of the
  // last keyframe's RTP timestamp (its parameter sets and fragments).
  if (keyframe_packet || (rtp_timestamp && rtp_timestamp == last_keyframe_rtp_timestamp_)) {
    last_keyframe_packet_ = now;
    if (rtp_timestamp) last_keyframe_rtp_timestamp_ = rtp_timestamp;
  }
}

void KeyframeRequester::OnExternalRequest(Timestamp now, const char* reason) {
  need_request_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", reason, 0, last_received_);
}

void KeyframeRequester::OnStreamStart(Timestamp now) {
  need_request_ = true;
  need_fir_ = true;
  SM_EVENT(cfg_.events, now, "keyframe", "stream_start", 0, 0);
}

void KeyframeRequester::RequestKeyframe(Timestamp now, bool fir, const char* reason) {
  if (!request_pending_) first_request_ = now;
  request_pending_ = true;
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

void KeyframeRequester::OnFrameReleased(Timestamp now) {
  // VideoReceiveStreamTimeoutTracker::OnEncodedFrameReleased: the next wait is the frame one, from now.
  timeout_base_ = now;
  decoded_any_ = true;
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
  // No repair window, no retry timer and no receiver-side throttle in
  // libwebrtc (RtcpFeedbackBuffer coalesces the triggers of one pass). A
  // blocked frame is repaired by NACK; no decodable frame for max_wait_for_*
  // on an active stream that is not receiving a keyframe asks for one.
  if (!timeout_base_.IsFinite()) timeout_base_ = now;
  const TimeDelta wait = decoded_any_ ? cfg_.max_wait_for_frame : cfg_.max_wait_for_keyframe;
  if (now - timeout_base_ >= wait) {
    const bool active = last_packet_.IsFinite() && now - last_packet_ < cfg_.stream_inactive_after;
    const bool receiving_key = last_keyframe_packet_.IsFinite() && now - last_keyframe_packet_ < cfg_.max_wait_for_keyframe;
    if (active && !receiving_key) {
      need_request_ = true;
      SM_EVENT(cfg_.events, now, "keyframe", "trigger_decode_timeout", wait.ms_f(), last_decodable_);
    }
    timeout_base_ = now;  // Re-arms after each firing.
  }
  if (need_request_) {
    const bool fir = need_fir_;
    RequestKeyframe(now, fir, fir ? "fir" : "pli");
    FeedbackMessage m{fir ? FeedbackType::kFir : FeedbackType::kPli};
    m.fir_seq = fir_seq_;
    out.push_back(m);
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
  w.Gauge("waiting_frames", static_cast<double>(waiting_.size()));
  w.Gauge("waiting_for_keyframe", request_pending_ ? 1 : 0);
  w.Hist("time_to_keyframe_ms", stats_.time_to_keyframe_ms);
}

}  // namespace sm::loss
