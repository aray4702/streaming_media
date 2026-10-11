// 4.7 Clock drift between sender and receiver.
//
// RlsLineFit: recursive least squares fit y = a*x + b with forgetting; used
// by the timestamp extrapolator, the drift estimator and RTP->NTP mapping.
//
// ClockDriftEstimator: the sender's media clock and the receiver's clock
// tick at slightly different rates (crystals are typically within +-50-100
// ppm). Transit time = arrival - rtp_ts/rate then grows linearly with slope
// = drift. Jitter only ever adds delay, so the estimator takes the minimum
// transit in each window (the lower envelope) and fits a line through those
// minima. That is far less noisy than fitting raw arrivals.
//
// DriftCompensator: an asynchronous sample-rate converter with a
// continuously variable ratio (cubic Hermite interpolation with phase kept
// across calls) plus a PI controller that steers the ratio so a playout
// buffer stays at its target level.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"

namespace sm::timing {

class RlsLineFit {
 public:
  explicit RlsLineFit(double forgetting = 1.0) : lambda_(forgetting) { Reset(); }
  void Reset();
  void Update(double x, double y);
  double slope() const { return w_[0]; }
  double intercept() const { return w_[1]; }
  double Predict(double x) const { return w_[0] * x + w_[1]; }
  uint64_t samples() const { return n_; }

 private:
  double lambda_;
  double w_[2];
  double p_[2][2];
  double x0_ = 0;  // x offset for conditioning.
  bool has_x0_ = false;
  uint64_t n_ = 0;
};

struct ClockDriftConfig {
  TimeDelta window = TimeDelta::Seconds(1);  // Minimum-transit window.
  int min_windows = 5;
};

class ClockDriftEstimator : public MetricsSource {
 public:
  explicit ClockDriftEstimator(ClockDriftConfig cfg = {}) : cfg_(cfg) {}
  void OnPacket(uint32_t rtp_ts, int clock_rate, Timestamp arrival);
  // Receiver clock rate relative to sender, in parts per million.
  // Positive: the sender's clock runs slow (media arrives "late" over time).
  double drift_ppm() const { return drift_ppm_; }
  bool converged() const { return fit_.samples() >= static_cast<uint64_t>(cfg_.min_windows); }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  ClockDriftConfig cfg_;
  SeqNumUnwrapper<uint32_t> unwrapper_;
  RlsLineFit fit_;
  Timestamp window_start_ = Timestamp::MinusInfinity();
  double window_min_transit_ms_ = 0;
  double window_media_s_ = 0;
  bool window_has_ = false;
  double drift_ppm_ = 0;
};

// PI gains are per call of UpdateFromLevel (designed for one call per
// 10 ms). With the buffer acting as an integrator of gain 10 ms per update,
// kp = 3e-4 and ki = 2.5e-7 give a ~3 s, near critically damped loop.
struct DriftCompensatorConfig {
  double kp = 3e-4;              // Ratio change per ms of level error.
  double ki = 2.5e-7;            // Integral gain per ms of accumulated error.
  double level_smoothing = 0.05; // EMA on the level (packet sawtooth).
  double max_ratio_dev = 0.005;  // +-0.5%: inaudible pitch change.
};

class DriftCompensator : public MetricsSource {
 public:
  explicit DriftCompensator(DriftCompensatorConfig cfg = {}) : cfg_(cfg) {}
  // ratio = output samples per input sample.
  void SetRatio(double r) { ratio_ = r; }
  double ratio() const { return ratio_; }
  // Updates the ratio from the playout buffer level error (ms).
  void UpdateFromLevel(double level_ms, double target_ms);
  // Resamples `in`, appending to `out`. Phase is preserved across calls.
  void Process(std::span<const float> in, std::vector<float>& out);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  DriftCompensatorConfig cfg_;
  double ratio_ = 1.0;
  double integral_ = 0;
  double level_ms_ = -1;
  double pos_ = 0;           // Fractional read position into history_.
  std::vector<float> hist_{0, 0, 0};  // Last 3 input samples (for cubic).
  uint64_t in_samples_ = 0, out_samples_ = 0;
};

}  // namespace sm::timing
