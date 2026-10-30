// 3.1 (part 1) Receiver-side NACK generation (RFC 4585 generic NACK).
//
// Detects sequence-number gaps, schedules retransmission requests with
// RTT-based retry spacing and retires requests that exceed their retry budget,
// as libwebrtc's NackRequester (issue #23): the request that reaches
// `max_retries` is the last, and its entry goes with it (issue #867); giving
// up asks for no keyframe, and a list that would pass `max_nack_list` is
// cleared and asks for one. A gap is NACKed as soon as it is found: M157
// never fills the reordering histogram (OnReceivedPacket's
// `is_retransmitted = true`), so WaitNumberOfPackets is 0 (issue #867).
// A packet recovered (by FEC or RTX) ahead of the newest received one is kept
// in a recovered list and never NACKed, as NackRequester's recovered_list_ (issue #846).
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
  int max_packet_age = 10000;       // Forget gaps older than newest - age.
  size_t max_nack_list = 1000;      // Overflow -> clear the list and ask for a keyframe.
  int max_retries = 100;            // nack_requester.cc kMaxNackRetries.
  double retry_rtt_factor = 1.0;    // Retry after rtt * factor (GetNackBatch's nack_on_rtt_passed, no floor).
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
  uint64_t abandoned = 0;           // Gave up at the max_retries-th request, or by age.
  uint64_t cleared = 0;             // Requests dropped by ClearUpTo.
  uint64_t keyframe_requests = 0;
  uint64_t recovered_ahead = 0;     // Recovered (FEC/RTX) ahead of the newest packet: never NACKed.
  Histogram recovery_time_ms;       // Gap detected -> packet arrived.
  Histogram retries_per_recovery;
};

class NackTracker : public MetricsSource {
 public:
  explicit NackTracker(NackConfig cfg = {});

  // Returns how many times the packet was NACKed before it arrived (0 if never), as NackRequester::OnReceivedPacket.
  // `is_recovered` is RtpPacketReceived::recovered() (restored from RTX or recovered by FEC): such a packet
  // ahead of the newest one goes to the recovered list and does not move the newest, so the gap before it is
  // found by the next packet received as sent, which skips it.
  int OnReceivedPacket(uint16_t seq, bool is_keyframe, Timestamp now, bool is_recovered = false);
  void UpdateRtt(TimeDelta rtt) { rtt_ = rtt; }
  TimeDelta rtt() const { return rtt_; }
  // Sequence numbers to put in a NACK now (NackRequester::ProcessNacks, kTimeOnly): first requests and retries.
  // Each call counts as one attempt.
  std::vector<uint16_t> GetNackBatch(Timestamp now);
  // NackRequester::OnReceivedPacket's GetNackBatch(kSeqNumOnly): the first requests only, which the receiver
  // sends right after the packet that revealed the gap rather than at the next process call. A packet that is
  // only reordered, and arrives a moment after the one that revealed the gap, has then been NACKed already, as in
  // libwebrtc (issue #411).
  std::vector<uint16_t> GetSeqNumNackBatch(Timestamp now);
  // When GetNackBatch has work next: the next 20 ms tick while requests are outstanding, or a first request's
  // reorder wait. PlusInfinity when nothing is missing.
  Timestamp NextProcessTime() const;
  // NackRequester::ClearUpTo: forgets the requests (and recovered packets) for every sequence number before `seq`. The
  // receiver calls it for each continuous frame (RtpVideoStreamReceiver2::FrameContinuous), so a keyframe
  // that follows an outage ends the repair of the packets lost during it.
  void ClearUpTo(uint16_t seq);
  // True once if the tracker gave up and a keyframe is needed.
  bool TakeKeyframeRequest();

  size_t pending() const { return missing_.size(); }
  const NackStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Timestamp last_process_ = Timestamp::MinusInfinity();  // The last 20 ms retry tick, on the grid of the first.
  struct Info {
    Timestamp detected;
    Timestamp last_sent = Timestamp::MinusInfinity();
    int retries = 0;
  };
  using MissingMap = std::map<int64_t, Info>;
  void AddMissing(int64_t from, int64_t to, Timestamp now);
  // Requests the entry and returns the next one; the request that reaches max_retries erases the entry.
  MissingMap::iterator Send(MissingMap::iterator it, Timestamp now, std::vector<uint16_t>& out);

  NackConfig cfg_;
  TimeDelta rtt_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  bool initialized_ = false;
  int64_t newest_ = 0;
  MissingMap missing_;
  std::set<int64_t> recovered_;  // NackRequester::recovered_list_.
  bool keyframe_needed_ = false;
  NackStats stats_;
};

}  // namespace sm::loss
