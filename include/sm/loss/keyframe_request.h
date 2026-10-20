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
  // 1 = the library's original policy (repair window, retry timer, throttle).
  // 2 = libwebrtc's RtpVideoStreamReceiver2 triggers (issue #23): the first
  // assembled frame is not a keyframe, a decoder error, the triggers other
  // modules report (OnExternalRequest) and the decode time-out below. No
  // request at stream start, no repair window, no retry timer, no throttle.
  // Default 1 until the final PR makes 2 the default.
  int version = 1;
  TimeDelta repair_window = TimeDelta::Millis(200);  // Wait for NACK repair.
  double repair_rtt_factor = 2.0;                    // window = max(w, rtt*f).
  TimeDelta min_request_interval = TimeDelta::Millis(100);
  double request_rtt_factor = 1.5;                   // Throttle = max(min, rtt*f).
  bool send_lntf = true;
  // Version 2 only. The decode time-out, from compare/webrtc/reference/video/
  // (added by PR #95):
  //  - video_receive_stream2.h kMaxWaitForKeyFrame 200 ms and kMaxWaitForFrame
  //    3 s (DetermineMaxWaitForFrame keeps them while the NACK history is the
  //    default 1000 ms: 3 x 1000 ms is not below kMaxWaitForFrame);
  //  - video_receive_stream_timeout_tracker.cc: the wait is the keyframe one
  //    until the first frame is released, then the frame one, and it re-arms
  //    for another wait after each firing (HandleTimeoutTask);
  //  - video_receive_stream2.cc OnDecodableFrameTimeout: no request when no
  //    packet came for kInactiveDuration (5 s) or while a keyframe packet
  //    arrived within max_wait_for_keyframe (IsReceivingKeyFrame).
  TimeDelta max_wait_for_keyframe = TimeDelta::Millis(200);
  TimeDelta max_wait_for_frame = TimeDelta::Seconds(3);
  TimeDelta stream_inactive_after = TimeDelta::Seconds(5);
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
  // Version 2: a media packet arrived (`keyframe_packet`: it belongs to a
  // keyframe). Feeds the decode time-out, which does not fire while a keyframe
  // is arriving or when the stream is idle.
  void OnMediaPacket(Timestamp now, bool keyframe_packet);
  // Version 2: a trigger decided elsewhere (the NACK list overflowed). Merged
  // with the other triggers of the same Process(). `reason` is an event name.
  void OnExternalRequest(Timestamp now, const char* reason);
  // New receiver or stream switch: needs a FIR (full intra request).
  void OnStreamStart(Timestamp now);
  void UpdateRtt(TimeDelta rtt) { rtt_ = rtt; }

  // Feedback to send now.
  std::vector<FeedbackMessage> Process(Timestamp now);
  // Frames that became decodable, in order. Safe to pass to the decoder.
  std::vector<int64_t> TakeDecodableFrames();
  // Version 2: a frame left the frame scheduler for the decoder. The decode time-out runs from here,
  // as libwebrtc's does from VideoStreamBufferController::OnFrameReady (OnEncodedFrameReleased), not
  // from the frame becoming decodable: the scheduler's wait for the render time comes first.
  void OnFrameReleased(Timestamp now);

  bool waiting_for_keyframe() const { return request_pending_; }
  const KeyframeRequestStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  bool IsDecodable(const FrameInfo& f) const;
  void Propagate(Timestamp now);
  void RequestKeyframe(Timestamp now, bool fir, const char* reason);

  KeyframeRequestConfig cfg_;
  TimeDelta rtt_ = TimeDelta::Millis(100);
  std::set<int64_t> decodable_;  // Recently decodable frame ids.
  std::map<int64_t, std::pair<FrameInfo, Timestamp>> waiting_;  // Blocked frames.
  std::vector<int64_t> ready_;
  int64_t last_received_ = -1;
  int64_t last_decodable_ = -1;
  bool has_received_frame_ = false;
  bool decoded_any_ = false;
  Timestamp timeout_base_ = Timestamp::MinusInfinity();  // Last frame released, or the first Process().
  Timestamp last_packet_ = Timestamp::MinusInfinity();
  Timestamp last_keyframe_packet_ = Timestamp::MinusInfinity();
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
