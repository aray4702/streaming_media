// 3.7 Protection selection: NACK vs. FEC vs. hybrid, driven by RTT and loss.
//
// - Low RTT: retransmission arrives well inside the playout budget, so NACK
//   alone repairs loss at near-zero overhead.
// - High RTT: a retransmission would arrive too late; FEC must carry the
//   protection.
// - In between: hybrid. NACK stays on and FEC covers only part of the loss.
//
// The FEC rate is chosen analytically: for a frame of k media packets the
// controller picks the smallest m parity packets such that the probability
// of more than m losses among k+m packets (binomial, filtered loss p) is
// below a residual-loss target. The target is strict (1%) at high RTT and
// relaxed toward NACK-only (10%) at low RTT. Overhead is capped. The result
// is ULPFEC's protection parameters (loss/ulpfec.h): the rate m/k in Q8,
// one frame per FEC block and random masks, which libwebrtc's
// FecControllerDefault always picks (fec_controller_default.cc:138-143).
#pragma once

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/loss/ulpfec.h"

namespace sm::loss {

struct ProtectionConfig {
  TimeDelta low_rtt = TimeDelta::Millis(30);    // <= : NACK only.
  TimeDelta high_rtt = TimeDelta::Millis(150);  // >= : FEC carries protection.
  double target_residual_fec_only = 0.01;
  double target_residual_hybrid = 0.10;
  double max_overhead = 0.5;       // Max parity / media packets.
  double min_loss_for_fec = 0.005;
  double loss_smoothing = 0.3;     // EMA weight of each new loss report.
  EventSink* events = nullptr;
};

struct ProtectionInput {
  TimeDelta rtt;
  double loss_rate = 0;            // Fraction lost since last report.
  double packets_per_frame = 5;
};

struct ProtectionDecision {
  bool nack = true;
  ulpfec::FecProtectionParams fec;  // fec_rate 0: no FEC.
  double filtered_loss = 0;
  double expected_residual = 0;    // Predicted frame loss after FEC.
};

// P(X > m), X ~ Binomial(n, p).
double BinomialTail(int n, int m, double p);

class ProtectionController : public MetricsSource {
 public:
  explicit ProtectionController(ProtectionConfig cfg = {}) : cfg_(cfg) {}
  ProtectionDecision Update(const ProtectionInput& in, Timestamp now = Timestamp::Micros(0));
  const ProtectionDecision& last() const { return last_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  ProtectionConfig cfg_;
  double filtered_loss_ = 0;
  bool has_loss_ = false;
  ProtectionDecision last_;
  uint64_t updates_ = 0, fec_on_ = 0;
};

}  // namespace sm::loss
