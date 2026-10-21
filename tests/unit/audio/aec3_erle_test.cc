// Ports of libwebrtc's AEC3 ERL/ERLE unit tests (`apm v2`, #240 step 3):
// erl_estimator_unittest.cc, erle_estimator_unittest.cc and
// signal_dependent_erle_estimator_unittest.cc, with their signals, lengths,
// channel combinations and pass bounds. The parameterized suites
// (INSTANTIATE_TEST_SUITE_P) are loops over the same channel counts.
//
// The signal-dependent tests only check that the sweep of filter lengths,
// delay headrooms and section counts runs (libwebrtc's have no expectations
// either; here the ASan preset checks the indexing). They skip the
// configurations that EchoCanceller3Config::Validate rejects; the checks of
// Validate that the sweep can trip are in ValidConfig below.
#include "sm/audio/aec3_erle.h"

#include <algorithm>
#include <cmath>

#include "sm_test.h"

using namespace sm;
using namespace sm::audio::aec3;

namespace {

// --- erl_estimator_unittest.cc -----------------------------------------------

void VerifyErl(const std::array<float, kFftLengthBy2Plus1>& erl, float erl_time_domain, float reference) {
  for (float a : erl) SM_EXPECT_NEAR(reference, a, 0.001);
  SM_EXPECT_NEAR(reference, erl_time_domain, 0.001);
}

// --- erle_estimator_unittest.cc ----------------------------------------------

constexpr int kLowFrequencyLimit = kFftLengthBy2 / 2;
constexpr float kTrueErle = 10.f;
constexpr float kTrueErleOnsets = 1.0f;
constexpr float kEchoPathGain = 3.f;

void VerifyErleBands(std::span<const std::array<float, kFftLengthBy2Plus1>> erle, float reference_lf,
                     float reference_hf) {
  for (size_t ch = 0; ch < erle.size(); ++ch) {
    std::for_each(erle[ch].begin(), erle[ch].begin() + kLowFrequencyLimit,
                  [reference_lf](float a) { SM_EXPECT_NEAR(reference_lf, a, 0.001); });
    std::for_each(erle[ch].begin() + kLowFrequencyLimit, erle[ch].end(),
                  [reference_hf](float a) { SM_EXPECT_NEAR(reference_hf, a, 0.001); });
  }
}

void VerifyErle(std::span<const std::array<float, kFftLengthBy2Plus1>> erle, float erle_time_domain,
                float reference_lf, float reference_hf) {
  VerifyErleBands(erle, reference_lf, reference_hf);
  SM_EXPECT_NEAR(kTrueErle, erle_time_domain, 0.5);
}

void VerifyErleGreaterOrEqual(std::span<const std::array<float, kFftLengthBy2Plus1>> erle1,
                              std::span<const std::array<float, kFftLengthBy2Plus1>> erle2) {
  for (size_t ch = 0; ch < erle1.size(); ++ch) {
    for (size_t i = 0; i < kFftLengthBy2Plus1; ++i) {
      SM_EXPECT_GE(erle1[ch][i], erle2[ch][i]);
    }
  }
}

// erle_estimator_unittest.cc's FormFarendTimeFrame and
// signal_dependent_erle_estimator_unittest.cc's GetActiveFrame (the same
// frame).
void FormFarendTimeFrame(Block* x) {
  const std::array<float, kBlockSize> frame = {
      7459.88, 17209.6, 17383,  20768.9, 16816.7, 18386.3, 4492.83, 9675.85, 6665.52, 14808.6, 9342.3,
      7483.28, 19261.7, 4145.98, 1622.18, 13475.2, 7166.32, 6856.61, 21937,  7263.14, 9569.07, 14919,
      8413.32, 7551.89, 7848.65, 6011.27, 13080.6, 15865.2, 12656,  17459.6, 4263.93, 4503.03, 9311.79,
      21095.8, 12657.9, 13906.6, 19267.2, 11338.1, 16828.9, 11501.6, 11405,  15031.4, 14541.6, 19765.5,
      18346.3, 19350.2, 3157.47, 18095.8, 1743.68, 21328.2, 19727.5, 7295.16, 10332.4, 11055.5, 20107.4,
      14708.4, 12416.2, 16434,  2454.69, 9840.8,  6867.23, 1615.75, 6059.9,  8394.19};
  for (int band = 0; band < x->NumBands(); ++band) {
    for (int channel = 0; channel < x->NumChannels(); ++channel) {
      std::copy(frame.begin(), frame.end(), x->View(band, channel).begin());
    }
  }
}

void FormFarendFrame(const RenderBuffer& render_buffer, float erle, std::array<float, kFftLengthBy2Plus1>* X2,
                     std::span<std::array<float, kFftLengthBy2Plus1>> E2,
                     std::span<std::array<float, kFftLengthBy2Plus1>> Y2) {
  const auto& spectrum_buffer = render_buffer.GetSpectrumBuffer();
  const int num_render_channels = static_cast<int>(spectrum_buffer.buffer[0].size());
  const int num_capture_channels = static_cast<int>(Y2.size());

  X2->fill(0.f);
  for (int ch = 0; ch < num_render_channels; ++ch) {
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      (*X2)[k] += spectrum_buffer.buffer[static_cast<size_t>(spectrum_buffer.write)][static_cast<size_t>(ch)][k] /
                  static_cast<float>(num_render_channels);
    }
  }

  for (int ch = 0; ch < num_capture_channels; ++ch) {
    const size_t c = static_cast<size_t>(ch);
    std::transform(X2->begin(), X2->end(), Y2[c].begin(), [](float a) { return a * kEchoPathGain * kEchoPathGain; });
    std::transform(Y2[c].begin(), Y2[c].end(), E2[c].begin(), [erle](float a) { return a / erle; });
  }
}

void FormNearendFrame(Block* x, std::array<float, kFftLengthBy2Plus1>* X2,
                      std::span<std::array<float, kFftLengthBy2Plus1>> E2,
                      std::span<std::array<float, kFftLengthBy2Plus1>> Y2) {
  for (int band = 0; band < x->NumBands(); ++band) {
    for (int ch = 0; ch < x->NumChannels(); ++ch) {
      auto v = x->View(band, ch);
      std::fill(v.begin(), v.end(), 0.f);
    }
  }

  X2->fill(0.f);
  for (size_t ch = 0; ch < Y2.size(); ++ch) {
    Y2[ch].fill(500.f * 1000.f * 1000.f);
    E2[ch].fill(Y2[ch][0]);
  }
}

void GetFilterFreq(size_t delay_headroom_samples,
                   std::span<std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_response) {
  const size_t delay_headroom_blocks = delay_headroom_samples / kBlockSize;
  for (size_t ch = 0; ch < filter_frequency_response[0].size(); ++ch) {
    for (auto& block_freq_resp : filter_frequency_response) {
      block_freq_resp[ch].fill(0.f);
    }

    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      filter_frequency_response[delay_headroom_blocks][ch][k] = kEchoPathGain;
    }
  }
}

// --- signal_dependent_erle_estimator_unittest.cc -----------------------------

class TestInputs {
 public:
  TestInputs(const Aec3Config& cfg, size_t num_render_channels, size_t num_capture_channels)
      : render_delay_buffer_(std::make_unique<RenderDelayBuffer>(cfg, 16000, num_render_channels)),
        Y2_(num_capture_channels),
        E2_(num_capture_channels),
        H2_(num_capture_channels, std::vector<std::array<float, kFftLengthBy2Plus1>>(cfg.filter.refined.length_blocks)),
        x_(1, static_cast<int>(num_render_channels)),
        converged_filters_(num_capture_channels, true) {
    render_delay_buffer_->AlignFromDelay(4);
    render_buffer_ = render_delay_buffer_->GetRenderBuffer();
    for (auto& H2_ch : H2_) {
      for (auto& H2_p : H2_ch) {
        H2_p.fill(0.f);
      }
    }
    for (auto& H2_p : H2_[0]) {
      H2_p.fill(1.f);
    }
  }
  const RenderBuffer& GetRenderBuffer() { return *render_buffer_; }
  std::span<const float, kFftLengthBy2Plus1> GetX2() { return X2_; }
  std::span<const std::array<float, kFftLengthBy2Plus1>> GetY2() const { return Y2_; }
  std::span<const std::array<float, kFftLengthBy2Plus1>> GetE2() const { return E2_; }
  std::span<const std::vector<std::array<float, kFftLengthBy2Plus1>>> GetH2() const { return H2_; }
  const std::vector<bool>& GetConvergedFilters() const { return converged_filters_; }

  void Update() {
    if (n_ % 2 == 0) {
      auto v = x_.View(/*band=*/0, /*channel=*/0);
      std::fill(v.begin(), v.end(), 0.f);
    } else {
      FormFarendTimeFrame(&x_);
    }

    render_delay_buffer_->Insert(x_);
    render_delay_buffer_->PrepareCaptureProcessing();
    UpdateCurrentPowerSpectra();
    ++n_;
  }

 private:
  void UpdateCurrentPowerSpectra() {
    const SpectrumBuffer& spectrum_render_buffer = render_buffer_->GetSpectrumBuffer();
    size_t idx = render_buffer_->Position();
    size_t prev_idx = static_cast<size_t>(spectrum_render_buffer.OffsetIndex(static_cast<int>(idx), 1));
    auto& X2 = spectrum_render_buffer.buffer[idx][/*channel=*/0];
    auto& X2_prev = spectrum_render_buffer.buffer[prev_idx][/*channel=*/0];
    std::copy(X2.begin(), X2.end(), X2_.begin());
    for (size_t ch = 0; ch < Y2_.size(); ++ch) {
      for (size_t k = 0; k < X2.size(); ++k) {
        E2_[ch][k] = 0.01f * X2_prev[k];
        Y2_[ch][k] = X2[k] + E2_[ch][k];
      }
    }
  }

  int n_ = 0;
  std::unique_ptr<RenderDelayBuffer> render_delay_buffer_;
  RenderBuffer* render_buffer_;
  std::array<float, kFftLengthBy2Plus1> X2_{};
  std::vector<std::array<float, kFftLengthBy2Plus1>> Y2_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> E2_;
  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> H2_;
  Block x_;
  std::vector<bool> converged_filters_;
};

// The checks of EchoCanceller3Config::Validate (api/audio/
// echo_canceller3_config.cc) on the fields the sweep changes; like Validate,
// clamps the section count when it is out of range and returns false.
bool ValidConfig(Aec3Config* c) {
  bool res = true;
  if (c->delay.delay_headroom_samples > 5000) {
    c->delay.delay_headroom_samples = 5000;
    res = false;
  }
  if (c->filter.refined.length_blocks < 1) {
    c->filter.refined.length_blocks = 1;
    res = false;
  }
  if (c->filter.refined_initial.length_blocks < 1) {
    c->filter.refined_initial.length_blocks = 1;
    res = false;
  }
  if (c->filter.refined.length_blocks < c->filter.refined_initial.length_blocks) {
    c->filter.refined_initial.length_blocks = c->filter.refined.length_blocks;
    res = false;
  }
  if (c->erle.num_sections < 1) {
    c->erle.num_sections = 1;
    res = false;
  } else if (c->erle.num_sections > c->filter.refined.length_blocks) {
    c->erle.num_sections = c->filter.refined.length_blocks;
    res = false;
  }
  return res;
}

}  // namespace

// --- erl_estimator_unittest.cc -----------------------------------------------

SM_TEST(Aec3Erle, ErlEstimatorEstimates) {
  for (size_t num_render_channels : {1, 2, 8}) {
    for (size_t num_capture_channels : {1, 2, 8}) {
      std::vector<std::array<float, kFftLengthBy2Plus1>> X2(num_render_channels);
      for (auto& X2_ch : X2) {
        X2_ch.fill(0.f);
      }
      std::vector<std::array<float, kFftLengthBy2Plus1>> Y2(num_capture_channels);
      for (auto& Y2_ch : Y2) {
        Y2_ch.fill(0.f);
      }
      std::vector<bool> converged_filters(num_capture_channels, false);
      const size_t converged_idx = num_capture_channels - 1;
      converged_filters[converged_idx] = true;

      ErlEstimator estimator(0);

      // The ERL estimate is reduced to lower values.
      for (auto& X2_ch : X2) {
        X2_ch.fill(500 * 1000.f * 1000.f);
      }
      Y2[converged_idx].fill(10 * X2[0][0]);
      for (size_t k = 0; k < 200; ++k) {
        estimator.Update(converged_filters, X2, Y2);
      }
      VerifyErl(estimator.Erl(), estimator.ErlTimeDomain(), 10.f);

      // The ERL is not immediately increased when the ERL in the data
      // increases.
      Y2[converged_idx].fill(10000 * X2[0][0]);
      for (size_t k = 0; k < 998; ++k) {
        estimator.Update(converged_filters, X2, Y2);
      }
      VerifyErl(estimator.Erl(), estimator.ErlTimeDomain(), 10.f);

      // The rate of increase is 3 dB.
      estimator.Update(converged_filters, X2, Y2);
      VerifyErl(estimator.Erl(), estimator.ErlTimeDomain(), 20.f);

      // The maximum ERL is reached when there are no low ERL estimates.
      for (size_t k = 0; k < 1000; ++k) {
        estimator.Update(converged_filters, X2, Y2);
      }
      VerifyErl(estimator.Erl(), estimator.ErlTimeDomain(), 1000.f);

      // The ERL estimate is not updated for low-level signals.
      for (auto& X2_ch : X2) {
        X2_ch.fill(1000.f * 1000.f);
      }
      Y2[converged_idx].fill(10 * X2[0][0]);
      for (size_t k = 0; k < 200; ++k) {
        estimator.Update(converged_filters, X2, Y2);
      }
      VerifyErl(estimator.Erl(), estimator.ErlTimeDomain(), 1000.f);
    }
  }
}

// --- erle_estimator_unittest.cc ----------------------------------------------

SM_TEST(Aec3Erle, ErleEstimatorVerifyErleIncreaseAndHold) {
  for (size_t num_render_channels : {1, 2, 4, 8}) {
    for (size_t num_capture_channels : {1, 2, 8}) {
      constexpr int kSampleRateHz = 48000;
      constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

      std::array<float, kFftLengthBy2Plus1> X2;
      std::vector<std::array<float, kFftLengthBy2Plus1>> E2(num_capture_channels);
      std::vector<std::array<float, kFftLengthBy2Plus1>> Y2(num_capture_channels);
      std::vector<bool> converged_filters(num_capture_channels, true);

      Aec3Config config;
      config.erle.onset_detection = true;

      Block x(static_cast<int>(kNumBands), static_cast<int>(num_render_channels));
      std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_response(
          config.filter.refined.length_blocks, std::vector<std::array<float, kFftLengthBy2Plus1>>(num_capture_channels));
      auto render_delay_buffer = std::make_unique<RenderDelayBuffer>(config, kSampleRateHz, num_render_channels);

      GetFilterFreq(config.delay.delay_headroom_samples, filter_frequency_response);

      ErleEstimator estimator(0, config, num_capture_channels);

      FormFarendTimeFrame(&x);
      render_delay_buffer->Insert(x);
      render_delay_buffer->PrepareCaptureProcessing();
      // The ERLE estimate is increased to higher values.
      FormFarendFrame(*render_delay_buffer->GetRenderBuffer(), kTrueErle, &X2, E2, Y2);
      for (size_t k = 0; k < 1000; ++k) {
        render_delay_buffer->Insert(x);
        render_delay_buffer->PrepareCaptureProcessing();
        estimator.Update(*render_delay_buffer->GetRenderBuffer(), filter_frequency_response, X2, Y2, E2,
                         converged_filters);
      }
      VerifyErle(estimator.Erle(/*onset_compensated=*/true), std::pow(2.f, estimator.FullbandErleLog2()),
                 config.erle.max_l, config.erle.max_h);
      VerifyErleGreaterOrEqual(estimator.Erle(/*onset_compensated=*/false),
                               estimator.Erle(/*onset_compensated=*/true));
      VerifyErleGreaterOrEqual(estimator.ErleUnbounded(), estimator.Erle(/*onset_compensated=*/false));

      FormNearendFrame(&x, &X2, E2, Y2);
      // The ERLE is not immediately decreased during nearend activity.
      for (size_t k = 0; k < 50; ++k) {
        render_delay_buffer->Insert(x);
        render_delay_buffer->PrepareCaptureProcessing();
        estimator.Update(*render_delay_buffer->GetRenderBuffer(), filter_frequency_response, X2, Y2, E2,
                         converged_filters);
      }
      VerifyErle(estimator.Erle(/*onset_compensated=*/true), std::pow(2.f, estimator.FullbandErleLog2()),
                 config.erle.max_l, config.erle.max_h);
      VerifyErleGreaterOrEqual(estimator.Erle(/*onset_compensated=*/false),
                               estimator.Erle(/*onset_compensated=*/true));
      VerifyErleGreaterOrEqual(estimator.ErleUnbounded(), estimator.Erle(/*onset_compensated=*/false));
    }
  }
}

SM_TEST(Aec3Erle, ErleEstimatorVerifyErleTrackingOnOnsets) {
  for (size_t num_render_channels : {1, 2, 4, 8}) {
    for (size_t num_capture_channels : {1, 2, 8}) {
      constexpr int kSampleRateHz = 48000;
      constexpr size_t kNumBands = NumBandsForRate(kSampleRateHz);

      std::array<float, kFftLengthBy2Plus1> X2;
      std::vector<std::array<float, kFftLengthBy2Plus1>> E2(num_capture_channels);
      std::vector<std::array<float, kFftLengthBy2Plus1>> Y2(num_capture_channels);
      std::vector<bool> converged_filters(num_capture_channels, true);

      Aec3Config config;
      config.erle.onset_detection = true;

      Block x(static_cast<int>(kNumBands), static_cast<int>(num_render_channels));
      std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>> filter_frequency_response(
          config.filter.refined.length_blocks, std::vector<std::array<float, kFftLengthBy2Plus1>>(num_capture_channels));
      auto render_delay_buffer = std::make_unique<RenderDelayBuffer>(config, kSampleRateHz, num_render_channels);

      GetFilterFreq(config.delay.delay_headroom_samples, filter_frequency_response);

      ErleEstimator estimator(/*startup_phase_length_blocks=*/0, config, num_capture_channels);

      FormFarendTimeFrame(&x);
      render_delay_buffer->Insert(x);
      render_delay_buffer->PrepareCaptureProcessing();

      for (size_t burst = 0; burst < 20; ++burst) {
        FormFarendFrame(*render_delay_buffer->GetRenderBuffer(), kTrueErleOnsets, &X2, E2, Y2);
        for (size_t k = 0; k < 10; ++k) {
          render_delay_buffer->Insert(x);
          render_delay_buffer->PrepareCaptureProcessing();
          estimator.Update(*render_delay_buffer->GetRenderBuffer(), filter_frequency_response, X2, Y2, E2,
                           converged_filters);
        }
        FormFarendFrame(*render_delay_buffer->GetRenderBuffer(), kTrueErle, &X2, E2, Y2);
        for (size_t k = 0; k < 1000; ++k) {
          render_delay_buffer->Insert(x);
          render_delay_buffer->PrepareCaptureProcessing();
          estimator.Update(*render_delay_buffer->GetRenderBuffer(), filter_frequency_response, X2, Y2, E2,
                           converged_filters);
        }
        FormNearendFrame(&x, &X2, E2, Y2);
        for (size_t k = 0; k < 300; ++k) {
          render_delay_buffer->Insert(x);
          render_delay_buffer->PrepareCaptureProcessing();
          estimator.Update(*render_delay_buffer->GetRenderBuffer(), filter_frequency_response, X2, Y2, E2,
                           converged_filters);
        }
      }
      VerifyErleBands(estimator.ErleDuringOnsets(), config.erle.min, config.erle.min);
      FormNearendFrame(&x, &X2, E2, Y2);
      for (size_t k = 0; k < 1000; k++) {
        estimator.Update(*render_delay_buffer->GetRenderBuffer(), filter_frequency_response, X2, Y2, E2,
                         converged_filters);
      }
      // During nearend activity, the ERLE converges to the ERLE for onsets.
      VerifyErle(estimator.Erle(/*onset_compensated=*/true), std::pow(2.f, estimator.FullbandErleLog2()),
                 config.erle.min, config.erle.min);
    }
  }
}

// --- signal_dependent_erle_estimator_unittest.cc -----------------------------

SM_TEST(Aec3Erle, SignalDependentErleEstimatorSweepSettings) {
  for (size_t num_render_channels : {1, 2, 4}) {
    for (size_t num_capture_channels : {1, 2, 4}) {
      Aec3Config cfg;
      size_t max_length_blocks = 50;
      for (size_t blocks = 1; blocks < max_length_blocks; blocks = blocks + 10) {
        for (size_t delay_headroom = 0; delay_headroom < 5; ++delay_headroom) {
          for (size_t num_sections = 2; num_sections < max_length_blocks; ++num_sections) {
            cfg.filter.refined.length_blocks = blocks;
            cfg.filter.refined_initial.length_blocks = std::min(cfg.filter.refined_initial.length_blocks, blocks);
            cfg.delay.delay_headroom_samples = delay_headroom * kBlockSize;
            cfg.erle.num_sections = num_sections;
            if (ValidConfig(&cfg)) {
              SignalDependentErleEstimator s(cfg, num_capture_channels);
              std::vector<std::array<float, kFftLengthBy2Plus1>> average_erle(num_capture_channels);
              for (auto& e : average_erle) {
                e.fill(cfg.erle.max_l);
              }
              TestInputs inputs(cfg, num_render_channels, num_capture_channels);
              for (size_t n = 0; n < 10; ++n) {
                inputs.Update();
                s.Update(inputs.GetRenderBuffer(), inputs.GetH2(), inputs.GetX2(), inputs.GetY2(), inputs.GetE2(),
                         average_erle, average_erle, inputs.GetConvergedFilters());
              }
            }
          }
        }
      }
    }
  }
}

SM_TEST(Aec3Erle, SignalDependentErleEstimatorLongerRun) {
  for (size_t num_render_channels : {1, 2, 4}) {
    for (size_t num_capture_channels : {1, 2, 4}) {
      Aec3Config cfg;
      cfg.filter.refined.length_blocks = 2;
      cfg.filter.refined_initial.length_blocks = 1;
      cfg.delay.delay_headroom_samples = 0;
      cfg.delay.hysteresis_limit_blocks = 0;
      cfg.erle.num_sections = 2;
      SM_EXPECT_EQ(ValidConfig(&cfg), true);
      std::vector<std::array<float, kFftLengthBy2Plus1>> average_erle(num_capture_channels);
      for (auto& e : average_erle) {
        e.fill(cfg.erle.max_l);
      }
      SignalDependentErleEstimator s(cfg, num_capture_channels);
      TestInputs inputs(cfg, num_render_channels, num_capture_channels);
      for (size_t n = 0; n < 200; ++n) {
        inputs.Update();
        s.Update(inputs.GetRenderBuffer(), inputs.GetH2(), inputs.GetX2(), inputs.GetY2(), inputs.GetE2(),
                 average_erle, average_erle, inputs.GetConvergedFilters());
      }
    }
  }
}
