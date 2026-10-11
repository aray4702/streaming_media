#include "sm/audio/echo_canceller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>

#include "sm/core/dsp.h"

namespace sm::audio {

namespace {
size_t NextPow2(size_t n) {
  size_t p = 1;
  while (p < n) p <<= 1;
  return p;
}
using Spectrum = std::vector<Complex>;
}  // namespace

struct EchoCanceller::Impl {
  EchoCancellerConfig cfg;
  size_t B, N, P, bins;
  RealFft fft;
  // Far-end history (aligned reads), counted in samples.
  std::vector<float> far_hist;
  int64_t far_written = 0;
  int64_t near_processed = 0;
  int bulk_delay = 0;
  std::unique_ptr<EchoDelayEstimator> delay_est;
  // Far spectra, newest first.
  std::deque<Spectrum> X;
  std::vector<float> psum;  // Sum over partitions of |X_p|^2.
  std::vector<Spectrum> Wfg, Wbg;
  // Foreground/background decision statistics (Speex MDF): mean and
  // variance of the error-energy improvement at two time constants.
  double davg1 = 0, dvar1 = 0, davg2 = 0, dvar2 = 0;
  // Adaptive step size (Valin 2007): mu ~ residual echo / error energy,
  // residual echo ~ leak_t * echo-estimate energy. leak_t is estimated from
  // the correlation of block-energy fluctuations of error and echo estimate.
  double eb_s = 0, yb_s = 0, pey_t = 0, pyy_t = 0;
  double leak_t = 1.0;
  bool adapted = false;
  uint64_t far_blocks = 0;
  // Scratch.
  std::vector<float> tbuf;
  Spectrum spec, ybg, yfg;
  // Residual echo suppressor (STFT, hop B, window 2B).
  std::vector<float> res_win, e_hist, y_hist, ola;
  Spectrum E2, Y2;
  std::vector<float> ee_s, yy_s, gain;
  double pey = 0, pyy = 0;
  float leak = 0.5f;
  // ERLE bookkeeping.
  double erle_d = 0, erle_lin = 0, erle_out = 0;

  explicit Impl(const EchoCancellerConfig& c)
      : cfg(c), B(static_cast<size_t>(c.sample_rate / 200)), N(NextPow2(2 * B)),
        P(static_cast<size_t>((c.filter_length_ms * c.sample_rate / 1000 + static_cast<int>(B) - 1) / static_cast<int>(B))),
        bins(N / 2 + 1), fft(N) {
    far_hist.assign(static_cast<size_t>(c.max_delay_ms * c.sample_rate / 1000) + 4 * N, 0.0f);
    if (c.delay_estimation) {
      EchoDelayConfig dc;
      dc.sample_rate = c.sample_rate;
      dc.max_delay_ms = c.max_delay_ms;
      dc.events = c.events;
      delay_est = std::make_unique<EchoDelayEstimator>(dc);
    } else {
      bulk_delay = c.fixed_delay_ms * c.sample_rate / 1000;
    }
    for (size_t p = 0; p < P; ++p) X.emplace_back(bins, Complex(0, 0));
    psum.assign(bins, 0.0f);
    Wfg.assign(P, Spectrum(bins, Complex(0, 0)));
    Wbg = Wfg;
    tbuf.assign(N, 0.0f);
    spec.assign(bins, Complex(0, 0));
    ybg.assign(bins, Complex(0, 0));
    yfg.assign(bins, Complex(0, 0));
    res_win = dsp::SqrtHannWindow(2 * B);
    e_hist.assign(2 * B, 0.0f);
    y_hist.assign(2 * B, 0.0f);
    ola.assign(B, 0.0f);
    E2.assign(bins, Complex(0, 0));
    Y2.assign(bins, Complex(0, 0));
    ee_s.assign(bins, 0.0f);
    yy_s.assign(bins, 0.0f);
    gain.assign(bins, 1.0f);
  }

  float FarAt(int64_t idx) const {
    // Far sample with absolute index idx (0 = first ever written).
    int64_t oldest = far_written - static_cast<int64_t>(far_hist.size());
    if (idx < oldest || idx >= far_written || idx < 0) return 0.0f;
    return far_hist[static_cast<size_t>(idx - oldest)];
  }

  void PushFar(std::span<const float> far) {
    size_t n = std::min(far.size(), far_hist.size());
    std::copy(far_hist.begin() + static_cast<std::ptrdiff_t>(n), far_hist.end(), far_hist.begin());
    std::copy(far.end() - static_cast<std::ptrdiff_t>(n), far.end(), far_hist.end() - static_cast<std::ptrdiff_t>(n));
    far_written += static_cast<int64_t>(far.size());
  }

  // Filters with W, returns the time-domain echo estimate (B samples) in out.
  void Filter(const std::vector<Spectrum>& W, Spectrum& Yspec, float* out) {
    std::fill(Yspec.begin(), Yspec.end(), Complex(0, 0));
    for (size_t p = 0; p < P; ++p) {
      const Spectrum& x = X[p];
      const Spectrum& w = W[p];
      for (size_t k = 0; k < bins; ++k) Yspec[k] += w[k] * x[k];
    }
    spec = Yspec;
    fft.Inverse(spec.data(), tbuf.data());
    for (size_t i = 0; i < B; ++i) out[i] = tbuf[N - B + i];
  }

  void Adapt(const float* e, float mu) {
    // E = FFT([0 ... 0, e]).
    std::fill(tbuf.begin(), tbuf.end(), 0.0f);
    for (size_t i = 0; i < B; ++i) tbuf[N - B + i] = e[i];
    Spectrum Ek(bins);
    fft.Forward(tbuf.data(), Ek.data());
    const float delta = 1e-6f * static_cast<float>(N);
    for (size_t p = 0; p < P; ++p) {
      Spectrum& w = Wbg[p];
      for (size_t k = 0; k < bins; ++k) spec[k] = mu * std::conj(X[p][k]) * Ek[k] / (psum[k] + delta);
      // Gradient constraint: keep the first B taps only (overlap-save).
      fft.Inverse(spec.data(), tbuf.data());
      std::fill(tbuf.begin() + static_cast<std::ptrdiff_t>(B), tbuf.end(), 0.0f);
      fft.Forward(tbuf.data(), spec.data());
      for (size_t k = 0; k < bins; ++k) w[k] += spec[k];
    }
  }

  // Residual echo suppression on one block; returns output (delayed by B).
  void Suppress(const float* e, const float* y, float* out) {
    std::copy(e_hist.begin() + static_cast<std::ptrdiff_t>(B), e_hist.end(), e_hist.begin());
    std::copy(e, e + B, e_hist.begin() + static_cast<std::ptrdiff_t>(B));
    std::copy(y_hist.begin() + static_cast<std::ptrdiff_t>(B), y_hist.end(), y_hist.begin());
    std::copy(y, y + B, y_hist.begin() + static_cast<std::ptrdiff_t>(B));
    std::fill(tbuf.begin(), tbuf.end(), 0.0f);
    for (size_t i = 0; i < 2 * B; ++i) tbuf[i] = e_hist[i] * res_win[i];
    fft.Forward(tbuf.data(), E2.data());
    std::fill(tbuf.begin(), tbuf.end(), 0.0f);
    for (size_t i = 0; i < 2 * B; ++i) tbuf[i] = y_hist[i] * res_win[i];
    fft.Forward(tbuf.data(), Y2.data());
    // Leak: correlation of power-spectrum fluctuations (Valin 2007).
    double cey = 0, cyy = 0;
    for (size_t k = 0; k < bins; ++k) {
      float ee = std::norm(E2[k]), yy = std::norm(Y2[k]);
      float de = ee - ee_s[k], dy = yy - yy_s[k];
      ee_s[k] = 0.9f * ee_s[k] + 0.1f * ee;
      yy_s[k] = 0.9f * yy_s[k] + 0.1f * yy;
      cey += static_cast<double>(de) * dy;
      cyy += static_cast<double>(dy) * dy;
    }
    pey = 0.98 * pey + 0.02 * cey;
    pyy = 0.98 * pyy + 0.02 * cyy;
    if (pyy > 1e-9) leak = static_cast<float>(std::clamp(pey / pyy, 0.005, 1.0));
    const float gmin = dsp::DbToLinear(cfg.min_gain_db);
    for (size_t k = 0; k < bins; ++k) {
      float ee = std::norm(E2[k]) + 1e-12f;
      float res = leak * std::norm(Y2[k]);
      float g = std::clamp(1.0f - cfg.over_suppression * res / ee, gmin, 1.0f);
      // Fast attack, slower release.
      gain[k] = g < gain[k] ? g : 0.7f * gain[k] + 0.3f * g;
      E2[k] *= gain[k];
    }
    fft.Inverse(E2.data(), tbuf.data());
    for (size_t i = 0; i < 2 * B; ++i) tbuf[i] *= res_win[i];
    for (size_t i = 0; i < B; ++i) out[i] = ola[i] + tbuf[i];
    for (size_t i = 0; i < B; ++i) ola[i] = tbuf[B + i];
  }

  void ProcessBlock(float* d, EchoCancellerStats& st) {
    // Aligned far block: ends `bulk_delay` samples before the capture block end.
    int64_t end = near_processed + static_cast<int64_t>(B) - bulk_delay;
    for (size_t i = 0; i < N; ++i) tbuf[i] = FarAt(end - static_cast<int64_t>(N) + static_cast<int64_t>(i));
    // Shift far spectra; maintain the power sum.
    Spectrum oldest = std::move(X.back());
    X.pop_back();
    for (size_t k = 0; k < bins; ++k) psum[k] -= std::norm(oldest[k]);
    fft.Forward(tbuf.data(), oldest.data());
    for (size_t k = 0; k < bins; ++k) psum[k] = std::max(0.0f, psum[k] + std::norm(oldest[k]));
    X.push_front(std::move(oldest));

    std::vector<float> ybg_t(B), yfg_t(B), ebg(B), efg(B);
    Filter(Wbg, ybg, ybg_t.data());
    Filter(Wfg, yfg, yfg_t.data());
    double Ed = 0, Ebg = 0, Efg = 0;
    for (size_t i = 0; i < B; ++i) {
      ebg[i] = d[i] - ybg_t[i];
      efg[i] = d[i] - yfg_t[i];
      Ed += static_cast<double>(d[i]) * d[i];
      Ebg += static_cast<double>(ebg[i]) * ebg[i];
      Efg += static_cast<double>(efg[i]) * efg[i];
    }
    double Ybg = 0;
    for (size_t i = 0; i < B; ++i) Ybg += static_cast<double>(ybg_t[i]) * ybg_t[i];
    double de = Ebg - eb_s, dy = Ybg - yb_s;
    eb_s = 0.9 * eb_s + 0.1 * Ebg;
    yb_s = 0.9 * yb_s + 0.1 * Ybg;
    pey_t = 0.97 * pey_t + 0.03 * de * dy;
    pyy_t = 0.97 * pyy_t + 0.03 * dy * dy;
    if (pyy_t > 1e-12) leak_t = std::clamp(pey_t / pyy_t, 1e-4, 1.0);
    double far_energy = 0;
    for (size_t k = 0; k < bins; ++k) far_energy += std::norm(X[0][k]);
    bool far_active = far_energy > 1e-6 * static_cast<double>(N * N);
    if (far_active) ++far_blocks;
    // Fixed step until the filter has seen ~2 s of far end and removes
    // ~15 dB of echo; afterwards the step follows the residual-to-error ratio,
    // which collapses when near-end speech is present (double talk).
    if (!adapted && far_blocks > 400 && st.erle_linear_db > 15.0f) adapted = true;  // Smoothed ERLE.
    float mu = cfg.mu;
    if (adapted) mu = static_cast<float>(cfg.mu * std::min(1.0, leak_t * Ybg / (Ebg + 1e-10)));
    st.step_size = mu;
    if (far_active) Adapt(ebg.data(), mu);
    // Copy background -> foreground only when its improvement is
    // statistically significant; near-end speech makes the improvement
    // noisy (large variance), so double talk rarely triggers a copy.
    double dbf = 0;
    for (size_t i = 0; i < B; ++i) {
      double d_ = static_cast<double>(ebg[i]) - efg[i];
      dbf += d_ * d_;
    }
    double diff = Efg - Ebg;
    davg1 = 0.6 * davg1 + 0.4 * diff;
    dvar1 = 0.36 * dvar1 + 0.16 * Efg * dbf;
    davg2 = 0.85 * davg2 + 0.15 * diff;
    dvar2 = 0.7225 * dvar2 + 0.0225 * Efg * dbf;
    bool update = (davg1 > 0 && davg1 * davg1 > 0.5 * dvar1) || (davg2 > 0 && davg2 * davg2 > 0.25 * dvar2);
    bool reset = (davg1 < 0 && davg1 * davg1 > 4.0 * dvar1) || (davg2 < 0 && davg2 * davg2 > 4.0 * dvar2);
    if (update) {
      Wfg = Wbg;
      davg1 = dvar1 = davg2 = dvar2 = 0;
      ++st.foreground_updates;
    } else if (reset) {
      Wbg = Wfg;
      davg1 = dvar1 = davg2 = dvar2 = 0;
      ++st.background_resets;
    }
    std::vector<float> out(B);
    if (cfg.residual_suppression) {
      Suppress(efg.data(), yfg_t.data(), out.data());
    } else {
      out = efg;
    }
    double Eout = 0;
    for (size_t i = 0; i < B; ++i) Eout += static_cast<double>(out[i]) * out[i];
    // ERLE (smoothed energies) only while the far end is active.
    double far_e = 0;
    for (size_t k = 0; k < bins; ++k) far_e += std::norm(X[0][k]);
    if (far_e > 1e-6 * static_cast<double>(N * N)) {
      erle_d = 0.95 * erle_d + 0.05 * Ed;
      erle_lin = 0.95 * erle_lin + 0.05 * Efg;
      erle_out = 0.95 * erle_out + 0.05 * Eout;
      st.erle_linear_db = static_cast<float>(10 * std::log10((erle_d + 1e-12) / (erle_lin + 1e-12)));
      st.erle_db = static_cast<float>(10 * std::log10((erle_d + 1e-12) / (erle_out + 1e-12)));
    }
    std::copy(out.begin(), out.end(), d);
    near_processed += static_cast<int64_t>(B);
    st.leak = leak;
    ++st.blocks;
  }
};

EchoCanceller::EchoCanceller(EchoCancellerConfig cfg) : impl_(std::make_unique<Impl>(cfg)) {}
EchoCanceller::~EchoCanceller() = default;

size_t EchoCanceller::latency_samples() const { return impl_->cfg.residual_suppression ? impl_->B : 0; }

void EchoCanceller::AnalyzeRender(std::span<const float> far) {
  impl_->PushFar(far);
  if (impl_->delay_est) impl_->delay_est->AnalyzeRender(far);
}

void EchoCanceller::ProcessCapture(std::span<float> near) {
  Impl& m = *impl_;
  if (m.delay_est) {
    m.delay_est->AnalyzeCapture(near);
    if (auto d = m.delay_est->delay_samples()) {
      // Keep two blocks of margin so the filter also sees a little pre-delay.
      int bulk = std::max(0, *d - 2 * static_cast<int>(m.B));
      if (std::abs(bulk - m.bulk_delay) > static_cast<int>(m.B)) {
        m.bulk_delay = bulk;
        // The echo path moved: restart adaptation from scratch.
        for (auto& w : m.Wbg) std::fill(w.begin(), w.end(), Complex(0, 0));
        m.Wfg = m.Wbg;
        m.davg1 = m.dvar1 = m.davg2 = m.dvar2 = 0;
        // Restart convergence: fixed step until the new path is learned.
        m.adapted = false;
        m.far_blocks = 0;
        m.leak_t = 1.0;
        m.pey_t = m.pyy_t = 0;
        m.erle_d = m.erle_lin = m.erle_out = 0;
        stats_.erle_linear_db = stats_.erle_db = 0;
        ++stats_.delay_changes;
        SM_EVENT(m.cfg.events, Timestamp::Micros(0), "aec", "delay_change", bulk, *d);
      }
    }
  }
  stats_.delay_samples = m.bulk_delay;
  for (size_t off = 0; off + m.B <= near.size(); off += m.B) m.ProcessBlock(near.data() + off, stats_);
}

void EchoCanceller::ExportMetrics(MetricsWriter& w) const {
  w.Counter("blocks", stats_.blocks);
  w.Counter("foreground_updates", stats_.foreground_updates);
  w.Counter("background_resets", stats_.background_resets);
  w.Counter("delay_changes", stats_.delay_changes);
  w.Gauge("erle_linear_db", stats_.erle_linear_db);
  w.Gauge("erle_db", stats_.erle_db);
  w.Gauge("leak", stats_.leak);
  w.Gauge("step_size", stats_.step_size);
  w.Gauge("bulk_delay_ms", stats_.delay_samples * 1000.0 / impl_->cfg.sample_rate);
}

}  // namespace sm::audio
