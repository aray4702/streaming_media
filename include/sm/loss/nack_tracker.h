// 3.1 (part 1) Receiver-side NACK generation (RFC 4585 generic NACK).
//
// Detects sequence-number gaps, schedules retransmission requests with
// RTT-based retry spacing, retires requests that exceed their retry budget,
// and asks for a keyframe when the missing list can no longer be repaired.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::loss {

struct NackConfig {
  // 1 = the library's original policy. 2 = libwebrtc's NackRequester: giving up
  // after kMaxNackRetries (100) asks for no keyframe, and a full list is
  // cleared and asks for one (issue #23). Default 1 until the final PR.
  int version = 1;
  int max_packet_age = 10000;       // Forget gaps older than newest - age.
  size_t max_nack_list = 1000;      // Overflow -> drop to next keyframe.
  int max_retries = 10;             // Version 2 uses libwebrtc's kMaxNackRetries (100) when left at 10.
  TimeDelta min_retry_interval = TimeDelta::Millis(20);
  double retry_rtt_factor = 1.0;    // Retry after max(rtt * factor, min).
  TimeDelta reorder_wait = TimeDelta::Zero();  // Delay before the first NACK.
  TimeDelta initial_rtt = TimeDelta::Millis(100);
  EventSink* events = nullptr;
};

struct NackStats {
  uint64_t packets_received = 0;
  uint64_t duplicates = 0;
  uint64_t reordered = 0;           // Late arrivals that were not yet NACKed.
  uint64_t missing_detected = 0;
  uint64_t nack_requests = 0;       // Sequence numbers sent in NACKs (incl. retries).
  uint64_t recovered = 0;           // Gaps later filled.
  uint64_t abandoned = 0;           // Gave up after max_retries or age.
  uint64_t keyframe_requests = 0;
  Histogram recovery_time_ms;       // Gap detected -> packet arrived.
  Histogram retries_per_recovery;
};

class NackTracker : public MetricsSource {
 public:
  explicit NackTracker(NackConfig cfg = {});

  void OnReceivedPacket(uint16_t seq, bool is_keyframe, Timestamp now);
  void UpdateRtt(TimeDelta rtt) { rtt_ = rtt; }
  // Sequence numbers to put in a NACK now. Each call counts as one attempt.
  std::vector<uint16_t> GetNackBatch(Timestamp now);
  // NackRequester::ClearUpTo: forgets the requests for every sequence number before `seq` (version 2; the
  // receiver calls it for each continuous frame, RtpVideoStreamReceiver2::FrameContinuous).
  void ClearUpTo(uint16_t seq);
  // True once if the tracker gave up and a keyframe is needed.
  bool TakeKeyframeRequest();

  size_t pending() const { return missing_.size(); }
  const NackStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Timestamp last_process_ = Timestamp::MinusInfinity();  // Version 2: the last 20 ms retry tick.
  struct Info {
    Timestamp detected;
    Timestamp last_sent = Timestamp::MinusInfinity();
    int retries = 0;
  };
  void AddMissing(int64_t from, int64_t to, Timestamp now);
  void AddMissingV2(int64_t from, int64_t to, Timestamp now);
  void TrimOld(Timestamp now);
  bool RecoverKeyframeSpace(Timestamp now);

  NackConfig cfg_;
  TimeDelta rtt_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  bool initialized_ = false;
  int64_t newest_ = 0;
  std::map<int64_t, Info> missing_;
  std::set<int64_t> keyframes_;
  bool keyframe_needed_ = false;
  NackStats stats_;
};

}  // namespace sm::loss
