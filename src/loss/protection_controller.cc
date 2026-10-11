#include "sm/loss/protection_controller.h"

#include <algorithm>
#include <cmath>

namespace sm::loss {

double BinomialTail(int n, int m, double p) {
  if (m >= n) return 0.0;
  if (p <= 0) return 0.0;
  if (p >= 1) return 1.0;
  // Sum P(X = i) for i <= m, then complement. Log-space for stability.
  double cdf = 0;
  double log_p = std::log(p), log_q = std::log1p(-p);
  for (int i = 0; i <= m; ++i) {
    double log_c = std::lgamma(n + 1.0) - std::lgamma(i + 1.0) - std::lgamma(n - i + 1.0);
    cdf += std::exp(log_c + i * log_p + (n - i) * log_q);
  }
  return std::clamp(1.0 - cdf, 0.0, 1.0);
}

ProtectionDecision ProtectionController::Update(const ProtectionInput& in, Timestamp now) {
  ++updates_;
  if (!has_loss_) {
    filtered_loss_ = in.loss_rate;
    has_loss_ = true;
  } else {
    filtered_loss_ += cfg_.loss_smoothing * (in.loss_rate - filtered_loss_);
  }
  ProtectionDecision d;
  d.filtered_loss = filtered_loss_;
  d.nack = true;  // Cheap; still useful for packets FEC cannot repair.
  const int k = std::clamp(static_cast<int>(std::lround(in.packets_per_frame)), 1, 48);
  const double p = filtered_loss_;
  d.expected_residual = BinomialTail(k, 0, p);

  double target = 1.0;  // 1.0 = no FEC needed.
  if (p >= cfg_.min_loss_for_fec && in.rtt > cfg_.low_rtt) {
    if (in.rtt >= cfg_.high_rtt) {
      target = cfg_.target_residual_fec_only;
    } else {
      double t = (in.rtt - cfg_.low_rtt) / (cfg_.high_rtt - cfg_.low_rtt);
      target = std::exp(std::log(cfg_.target_residual_hybrid) * (1 - t) +
                        std::log(cfg_.target_residual_fec_only) * t);
    }
  }
  if (target < 1.0) {
    const int max_m = std::max(1, static_cast<int>(std::floor(k * cfg_.max_overhead)));
    int m = 0;
    double residual = BinomialTail(k, 0, p);
    while (residual > target && m < max_m) {
      ++m;
      residual = BinomialTail(k + m, m, p);
    }
    d.fec.protection = static_cast<double>(m) / k;
    d.expected_residual = residual;
  }
  d.fec.mask = in.mean_burst_length >= cfg_.burst_threshold ? FecMaskType::kInterleaved : FecMaskType::kBlock;
  if (d.fec.protection > 0) ++fec_on_;
  if (d.fec.protection != last_.fec.protection) {
    SM_EVENT(cfg_.events, now, "protection", "fec_rate", d.fec.protection, in.rtt.ms());
  }
  last_ = d;
  return d;
}

void ProtectionController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("updates", updates_);
  w.Counter("updates_with_fec", fec_on_);
  w.Gauge("filtered_loss", filtered_loss_);
  w.Gauge("fec_protection", last_.fec.protection);
  w.Gauge("expected_residual", last_.expected_residual);
}

}  // namespace sm::loss
