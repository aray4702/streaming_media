// 3.5 Keyframe requests (PLI / FIR, RFC 4585 / RFC 5104) and loss
// notification (LNTF), driven by frame-level decodability.
//
// The receiver tracks which frames are decodable from their references.
// A frame whose references are missing waits for NACK to repair them for up
// to `repair_window` (scaled by RTT); after that, or immediately on a decoder
// error, a PLI is sent. Requests are throttled so a slow keyframe does not
// trigger a request storm, and retried if no keyframe arrives in time.
// LNTF reports the first undecodable frame right away so a sender that
// supports it can react before the PLI.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::loss {

struct FrameInfo {
  int64_t frame_id = 0;      // Monotonic (unwrapped) frame id.
  bool keyframe = false;
  std::vector<int64_t> references;  // Frame ids this frame depends on.
};

enum class FeedbackType { kPli, kFir, kLntf };

struct FeedbackMessage {
  FeedbackType type;
  uint8_t fir_seq = 0;            // FIR command sequence number.
  int64_t last_decodable = -1;    // LNTF fields.
  int64_t last_received = -1;
};

struct KeyframeRequestConfig {
  TimeDelta repair_window = TimeDelta::Millis(200);  // Wait for NACK repair.
  double repair_rtt_factor = 2.0;                    // window = max(w, rtt*f).
  TimeDelta min_request_interval = TimeDelta::Millis(100);
  double request_rtt_factor = 1.5;                   // Throttle = max(min, rtt*f).
  bool send_lntf = true;
  size_t max_tracked_frames = 512;
  EventSink* events = nullptr;
};

struct KeyframeRequestStats {
  uint64_t frames_received = 0;
  uint64_t frames_decodable = 0;
  uint64_t frames_dropped = 0;      // Undecodable, discarded at the next keyframe.
  uint64_t pli_sent = 0;
  uint64_t fir_sent = 0;
  uint64_t lntf_sent = 0;
  uint64_t throttled = 0;
  Histogram time_to_keyframe_ms;    // First request -> keyframe received.
};

class KeyframeRequester : public MetricsSource {
 public:
  explicit KeyframeRequester(KeyframeRequestConfig cfg = {});

  // A complete frame (all packets present) was assembled.
  void OnFrame(const FrameInfo& f, Timestamp now);
  // Packets of a frame are permanently lost (NACK gave up).
  void OnFrameLost(int64_t frame_id, Timestamp now);
  void OnDecoderError(Timestamp now);
  // New receiver or stream switch: needs a FIR (full intra request).
  void OnStreamStart(Timestamp now);
  void UpdateRtt(TimeDelta rtt) { rtt_ = rtt; }

  // Feedback to send now.
  std::vector<FeedbackMessage> Process(Timestamp now);
  // Frames that became decodable, in order. Safe to pass to the decoder.
  std::vector<int64_t> TakeDecodableFrames();

  bool waiting_for_keyframe() const { return request_pending_; }
  const KeyframeRequestStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool IsDecodable(const FrameInfo& f) const;
  void Propagate();
  void RequestKeyframe(Timestamp now, bool fir, const char* reason);

  KeyframeRequestConfig cfg_;
  TimeDelta rtt_ = TimeDelta::Millis(100);
  std::set<int64_t> decodable_;  // Recently decodable frame ids.
  std::map<int64_t, std::pair<FrameInfo, Timestamp>> waiting_;  // Blocked frames.
  std::vector<int64_t> ready_;
  int64_t last_received_ = -1;
  int64_t last_decodable_ = -1;
  bool need_request_ = false;
  bool need_fir_ = false;
  bool request_pending_ = false;
  bool lntf_pending_ = false;
  Timestamp first_request_ = Timestamp::MinusInfinity();
  Timestamp last_request_ = Timestamp::MinusInfinity();
  uint8_t fir_seq_ = 0;
  KeyframeRequestStats stats_;
};

}  // namespace sm::loss
