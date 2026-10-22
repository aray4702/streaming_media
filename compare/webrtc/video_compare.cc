// sm_video_compare: this library's video receive and adaptation modules and
// libwebrtc's on the same inputs (#63, section 7).
//
// Three blocks, each feeding one synthetic input to both implementations and
// reading both with the same metric code:
//   - freeze detection: render times through video::FreezeDetector and
//     libwebrtc's VideoQualityObserver (video/video_quality_observer2.cc),
//     the class behind getStats' freezeCount/totalFreezesDuration. A gap of
//     5 s or more stands for a stream with no packets for 5 s, so libwebrtc
//     gets OnStreamInactive() 5 s into it, as VideoReceiveStream2 calls it
//     (video/video_receive_stream2.cc, kInactiveDuration);
//   - quality scaling: encoder QP and drops through video::QualityScaler
//     (version 2, the port of libwebrtc's, #84) and
//     libwebrtc's QualityScaler (modules/video_coding/utility/
//     quality_scaler.cc) with H.264's thresholds 24/37, on a simulated task
//     queue. Both clear their samples after a signal, as libwebrtc does;
//   - frame timing: frame arrivals through timing::VcmTiming and libwebrtc's
//     VCMTiming with its DefaultVideoJitterTiming (jitter estimator,
//     timestamp extrapolator), driven in the order of
//     video/video_stream_buffer_controller.cc: OnCompleteFrame on arrival;
//     the next frame is scheduled when it is at the front, released at its
//     latest decode time (render time - decode estimate - render delay) or at
//     once if that has passed, then OnDecodableTemporalUnit,
//     UpdateCurrentDelay unless retransmitted, and a 5 ms decode time.
//
//   out/compare/sm_video_compare            # Markdown table on stdout
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/environment/environment_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/timing/timing.h"
#include "modules/video_coding/utility/quality_scaler.h"
#include "rtc_base/logging.h"
#include "sim_time.h"
#include "sm/core/random.h"
#include "sm/timing/vcm_timing.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/quality_scaler.h"
#include "system_wrappers/include/clock.h"
#include "video/video_quality_observer2.h"
#include "video/video_receive_stream2.h"

namespace {

namespace w = webrtc;

int64_t Round(double x) { return static_cast<int64_t>(std::llround(x)); }

struct Row {
  std::string module, scenario, metric;
  double ours = 0, webrtc = 0;
  std::string note;
};
std::vector<Row> rows;

void Add(std::string module, std::string scenario, std::string metric, double ours, double webrtc,
         std::string note = "") {
  rows.push_back({std::move(module), std::move(scenario), std::move(metric), ours, webrtc, std::move(note)});
}

// --- Freeze detection ---------------------------------------------------------

constexpr int64_t kPauseMs = 5000;  // video_receive_stream2.cc kInactiveDuration.

struct FreezeResult {
  double freezes = 0, freeze_ms = 0, pauses = 0, pause_ms = 0, harmonic_fps = 0;
};

FreezeResult FreezeOurs(const std::vector<int64_t>& render_ms) {
  sm::video::FreezeDetector d;
  for (int64_t t : render_ms) d.OnFrameRendered(sm::Timestamp::Millis(t));
  const auto& s = d.stats();
  return {static_cast<double>(s.freeze_count), s.total_freezes_ms, static_cast<double>(s.pause_count),
          s.total_pauses_ms, d.HarmonicFps()};
}

FreezeResult FreezeWebrtc(const std::vector<int64_t>& render_ms) {
  w::internal::VideoQualityObserver o;
  auto buffer = w::I420Buffer::Create(640, 360);
  int64_t prev = -1;
  uint32_t rtp = 0;
  for (int64_t t : render_ms) {
    if (prev >= 0 && t - prev >= kPauseMs) o.OnStreamInactive();
    prev = t;
    rtp += 3000;
    w::VideoFrame frame =
        w::VideoFrame::Builder().set_video_frame_buffer(buffer).set_rtp_timestamp(rtp).set_timestamp_us(t * 1000).build();
    o.OnRenderedFrame(w::internal::VideoFrameMetaData(frame, w::Timestamp::Millis(t)));
  }
  const double dur_s = o.TotalFramesDurationMs() / 1000.0;
  const double sq = o.SumSquaredFrameDurationsSec();
  return {static_cast<double>(o.NumFreezes()), static_cast<double>(o.TotalFreezesDurationMs()),
          static_cast<double>(o.NumPauses()), static_cast<double>(o.TotalPausesDurationMs()), sq > 0 ? dur_s / sq : 0};
}

void FreezeCase(const std::string& scenario, const std::vector<int64_t>& render_ms, bool pauses = false) {
  auto a = FreezeOurs(render_ms);
  auto b = FreezeWebrtc(render_ms);
  Add("freeze detection", scenario, "freezes", a.freezes, b.freezes);
  Add("freeze detection", scenario, "total freeze duration (ms)", a.freeze_ms, b.freeze_ms);
  if (pauses) {
    Add("freeze detection", scenario, "pauses", a.pauses, b.pauses);
    Add("freeze detection", scenario, "total pause duration (ms)", a.pause_ms, b.pause_ms);
  }
  Add("freeze detection", scenario, "harmonic fps", a.harmonic_fps, b.harmonic_fps);
}

// Frames every `period` ms from `from` (exclusive of `to`), with Gaussian
// render jitter of `jitter_ms` (render times stay increasing).
void Frames(std::vector<int64_t>& v, double from, double to, double period, sm::Random* rng = nullptr,
            double jitter_ms = 0) {
  for (double t = from; t < to; t += period) {
    double j = rng ? rng->Gaussian(0, jitter_ms) : 0;
    int64_t ms = Round(t + j);
    if (!v.empty()) ms = std::max(ms, v.back() + 1);
    v.push_back(ms);
  }
}

void Freezes() {
  constexpr double k30 = 1000.0 / 30;
  {
    // One gap every 5 s, 180 to 230 ms long: both thresholds are near
    // max(3 x 33, 33 + 150) = 183 ms.
    std::vector<int64_t> v;
    double t = 0;
    for (int gap = 180; gap <= 230; gap += 5) {
      Frames(v, t, t + 5000, k30);
      t = static_cast<double>(v.back()) + gap;
    }
    Frames(v, t, t + 2000, k30);
    FreezeCase("30 fps, one gap of 180, 185, ..., 230 ms every 5 s", v);
  }
  {
    sm::Random rng(1);
    std::vector<int64_t> v;
    Frames(v, 0, 60000, k30, &rng, 8);
    FreezeCase("30 fps, render jitter 8 ms, 60 s", v);
  }
  {
    // A 400 ms gap every second (a flapping link).
    std::vector<int64_t> v;
    double t = 0;
    for (int i = 0; i < 30; ++i) {
      Frames(v, t, t + 600, k30);
      t = static_cast<double>(v.back()) + 400;
    }
    FreezeCase("30 fps, a 400 ms gap every 1 s, 30 s", v);
  }
  {
    // Frame rate drops to 3 fps for 3 s (the sender dropping frames), then back.
    std::vector<int64_t> v;
    Frames(v, 0, 3000, k30);
    Frames(v, 3000, 6000, 333);
    Frames(v, 6000, 9000, k30);
    FreezeCase("30 fps, 3 s at 3 fps, 30 fps", v);
  }
  {
    // 15 fps with gaps of 250 to 450 ms (thresholds near 66.7 + 150 = 217 ms).
    std::vector<int64_t> v;
    double t = 0;
    for (int gap = 250; gap <= 450; gap += 50) {
      Frames(v, t, t + 4000, 1000.0 / 15);
      t = static_cast<double>(v.back()) + gap;
    }
    Frames(v, t, t + 2000, 1000.0 / 15);
    FreezeCase("15 fps, one gap of 250, 300, ..., 450 ms every 4 s", v);
  }
  {
    // Outages: 2 s, then 6 s (a pause), then 1 s.
    std::vector<int64_t> v;
    Frames(v, 0, 5000, k30);
    Frames(v, 7000, 12000, k30);
    Frames(v, 18000, 23000, k30);
    Frames(v, 24000, 29000, k30);
    FreezeCase("30 fps, outages of 2 s, 6 s and 1 s", v, true);
  }
  {
    // Random gaps: 2% of frames are followed by a 100-800 ms gap, 120 s.
    sm::Random rng(7);
    std::vector<int64_t> v;
    double t = 0;
    while (t < 120000) {
      v.push_back(Round(t));
      t += rng.Bernoulli(0.02) ? rng.Uniform(100, 800) : k30;
    }
    FreezeCase("30 fps, 2% of frames followed by a 100-800 ms gap, 120 s", v);
  }
}

// --- Quality scaling ----------------------------------------------------------

struct ScalerInput {
  std::function<int(int64_t frame, double t_s)> qp;  // < 0: frame dropped.
  double seconds = 30;
};

struct ScalerResult {
  int downs = 0, ups = 0;
  double first_down_s = -1, first_up_s = -1;
};

constexpr double kFps = 30;
constexpr int kH264Low = 24, kH264High = 37;  // VideoEncoder H.264 defaults (openh264 encoder info).

ScalerResult ScalerOurs(const ScalerInput& in) {
  sm::video::QualityScalerConfig cfg;
  cfg.thresholds = {kH264Low, kH264High};
  cfg.version = 2;  // libwebrtc M157's QualityScaler (#84).
  sm::video::QualityScaler s(cfg);
  ScalerResult r;
  const int64_t n = Round(in.seconds * kFps);
  auto check = [&](double t) {
    auto sig = s.Check(sm::Timestamp::Micros(Round(t * 1e6)));
    if (sig == sm::video::QualityScaler::Signal::kAdaptDown) {
      if (r.downs++ == 0) r.first_down_s = t;
      s.OnResolutionChanged();
    } else if (sig == sm::video::QualityScaler::Signal::kAdaptUp) {
      if (r.ups++ == 0) r.first_up_s = t;
      s.OnResolutionChanged();
    }
  };
  for (int64_t f = 0; f < n; ++f) {
    const double t = static_cast<double>(f) / kFps;
    // libwebrtc's CheckQpTask due at `t` runs before the frame at `t` is
    // reported (ScalerWebrtc: RunUntil, then post the frame).
    check(t);
    const int qp = in.qp(f, t);
    if (qp < 0) {
      s.OnFrameDropped();
    } else {
      s.OnEncodedFrame(qp, sm::Timestamp::Micros(Round(t * 1e6)));
    }
  }
  check(in.seconds);
  return r;
}

class Handler final : public w::QualityScalerQpUsageHandlerInterface {
 public:
  Handler(std::function<double()> now_s, ScalerResult* r) : now_s_(std::move(now_s)), r_(r) {}
  void OnReportQpUsageHigh() override {
    if (r_->downs++ == 0) r_->first_down_s = now_s_();
  }
  void OnReportQpUsageLow() override {
    if (r_->ups++ == 0) r_->first_up_s = now_s_();
  }

 private:
  std::function<double()> now_s_;
  ScalerResult* r_;
};

ScalerResult ScalerWebrtc(const ScalerInput& in) {
  const w::Timestamp t0 = w::Timestamp::Seconds(100000);
  sm::compare::SimTime sim(t0);
  w::ScopedYieldPolicy yield_policy(&sim);
  const w::Environment env = w::CreateEnvironment(sim.clock());
  auto q = sim.CreateTaskQueue("encoder", w::TaskQueueFactory::Priority::kNormal);
  ScalerResult r;
  Handler handler([&] { return (sim.Now() - t0).us() / 1e6; }, &r);
  std::unique_ptr<w::QualityScaler> scaler;
  q->PostTask([&] {
    scaler = std::make_unique<w::QualityScaler>(&handler, w::VideoEncoder::QpThresholds(kH264Low, kH264High),
                                                env.field_trials());
  });
  sim.RunReady();
  const int64_t n = Round(in.seconds * kFps);
  for (int64_t f = 0; f < n; ++f) {
    const double t = static_cast<double>(f) / kFps;
    const w::Timestamp at = t0 + w::TimeDelta::Micros(Round(t * 1e6));
    sim.RunUntil(at);
    const int qp = in.qp(f, t);
    q->PostTask([&, qp] {
      if (qp < 0) {
        scaler->ReportDroppedFrameByMediaOpt();
      } else {
        scaler->ReportQp(qp, at.us());
      }
    });
    sim.RunReady();
  }
  sim.RunUntil(t0 + w::TimeDelta::Micros(Round(in.seconds * 1e6)));
  q->PostTask([&] { scaler.reset(); });
  sim.RunReady();
  return r;
}

void ScalerCase(const std::string& scenario, const ScalerInput& in) {
  auto a = ScalerOurs(in);
  auto b = ScalerWebrtc(in);
  Add("quality scaler", scenario, "adapt-down signals", a.downs, b.downs);
  Add("quality scaler", scenario, "first adapt-down (s)", a.first_down_s, b.first_down_s, "-1: none");
  Add("quality scaler", scenario, "adapt-up signals", a.ups, b.ups);
  Add("quality scaler", scenario, "first adapt-up (s)", a.first_up_s, b.first_up_s, "-1: none");
}

void Scaling() {
  ScalerCase("H.264, QP 40 (above 37), 30 fps, 30 s", {[](int64_t, double) { return 40; }});
  ScalerCase("H.264, QP 30 (between 24 and 37)", {[](int64_t, double) { return 30; }});
  ScalerCase("H.264, QP 18 (below 24)", {[](int64_t, double) { return 18; }});
  ScalerCase("H.264, QP 40 for 10 s, then 18", {[](int64_t, double t) { return t < 10 ? 40 : 18; }});
  ScalerCase("H.264, QP uniform 30-45 (mean 37.5)", {[](int64_t f, double) {
               // A fixed hash of the frame number, the same sequence for both.
               uint64_t h = static_cast<uint64_t>(f) * 0x9e3779b97f4a7c15ULL;
               return 30 + static_cast<int>((h >> 33) % 16);
             }});
  ScalerCase("H.264, QP 30, 70% of frames dropped", {[](int64_t f, double) { return f % 10 < 7 ? -1 : 30; }});
}

// --- Frame timing -------------------------------------------------------------

struct TimingInput {
  double fps = 30;
  double seconds = 20;
  double base_delay_ms = 50;
  double jitter_ms = 0;              // Gaussian network jitter per frame.
  double step_ms = 0;                // Extra delay from `step_at_s` on.
  double step_at_s = 10;
  double rtx_fraction = 0;           // Frames repaired by a retransmission.
  double rtt_ms = 100;
  uint64_t seed = 1;
};

struct TimingFrame {
  uint32_t rtp = 0;
  int64_t capture_us = 0, arrival_us = 0;
  int64_t size_bytes = 0;
  bool rtx = false;
};

std::vector<TimingFrame> MakeFrames(const TimingInput& in) {
  sm::Random rng(in.seed);
  std::vector<TimingFrame> v;
  const int64_t n = Round(in.seconds * in.fps);
  int64_t last_arrival = 0;
  for (int64_t i = 0; i < n; ++i) {
    TimingFrame f;
    f.capture_us = Round(static_cast<double>(i) * 1e6 / in.fps);
    f.rtp = static_cast<uint32_t>(f.capture_us * 90 / 1000);
    f.size_bytes = i == 0 ? 40000 : Round(rng.Uniform(4000, 6000));
    double d = in.base_delay_ms + std::max(-in.base_delay_ms, rng.Gaussian(0, in.jitter_ms));
    if (static_cast<double>(f.capture_us) >= in.step_at_s * 1e6) d += in.step_ms;
    f.rtx = i > 0 && rng.Bernoulli(in.rtx_fraction);
    if (f.rtx) d += in.rtt_ms;
    // Frames complete in order: a frame behind a late one waits for it.
    f.arrival_us = std::max(last_arrival, f.capture_us + Round(d * 1000));
    last_arrival = f.arrival_us;
    v.push_back(f);
  }
  return v;
}

struct TimingStats {
  std::vector<double> jitter_ms, target_ms, current_ms, buffer_ms, e2e_ms;
  int late = 0;
};

// The two VCMTiming implementations behind one interface; times in us
// relative to the call start.
class OurTiming {
 public:
  explicit OurTiming(double rtt_ms) { t_.OnNetworkUpdate(sm::TimeDelta::Millis(Round(rtt_ms))); }
  void OnComplete(const TimingFrame& f) {
    t_.OnCompleteFrame(f.rtp, T(f.arrival_us), f.rtx);
  }
  int64_t RenderTime(uint32_t rtp, int64_t now) { return t_.RenderTime(rtp, T(now)).us(); }
  int64_t DecodeAndRenderUs() const { return (t_.estimated_max_decode_time() + t_.render_delay()).us(); }
  void Release(const TimingFrame& f, int64_t render, int64_t now) {
    t_.OnDecodableTemporalUnit(f.rtp, sm::DataSize::Bytes(f.size_bytes), T(f.arrival_us), f.rtx, T(now));
    if (!f.rtx) t_.UpdateCurrentDelay(T(render), T(now));
    t_.UpdateDecodeTimeEstimate(sm::TimeDelta::Millis(5), T(now));
  }
  double JitterMs() const { return t_.minimum_delay().ms_f(); }
  double TargetMs() const { return t_.TargetDelay().ms_f(); }
  double CurrentMs() const { return t_.current_delay().ms_f(); }

 private:
  static sm::Timestamp T(int64_t us) { return sm::Timestamp::Micros(us); }
  sm::timing::VcmTiming t_;
};

class WebrtcTiming {
 public:
  explicit WebrtcTiming(double rtt_ms)
      : clock_(w::Timestamp::Seconds(100000)),
        env_(w::CreateEnvironment(&clock_)),
        t_(env_, w::VCMTiming::VideoDelayTimings::kDefaultRenderDelay) {
    t_.OnNetworkUpdate({.rtt = w::TimeDelta::Millis(Round(rtt_ms))});
  }
  void OnComplete(const TimingFrame& f) {
    At(f.arrival_us);
    t_.OnCompleteFrame({.rtp_timestamp = f.rtp, .time = T(f.arrival_us), .last_spatial_layer = true,
                        .was_retransmitted = f.rtx});
  }
  int64_t RenderTime(uint32_t rtp, int64_t now) {
    At(now);
    return (t_.RenderTime(rtp, T(now)) - kT0).us();
  }
  int64_t DecodeAndRenderUs() const {
    auto g = t_.GetTimings();
    return (g.estimated_max_decode_time + g.render_delay).us();
  }
  void Release(const TimingFrame& f, int64_t render, int64_t now) {
    At(now);
    t_.OnDecodableTemporalUnit({.rtp_timestamp = f.rtp, .size = w::DataSize::Bytes(f.size_bytes),
                                .time = T(f.arrival_us), .was_retransmitted = f.rtx});
    if (!f.rtx) t_.UpdateCurrentDelay(T(render), T(now));
    t_.UpdateDecodeTimeEstimate(w::TimeDelta::Millis(5), T(now));
  }
  double JitterMs() const { return t_.GetTimings().minimum_delay.us() / 1000.0; }
  double TargetMs() const { return t_.TargetVideoDelay().us() / 1000.0; }
  double CurrentMs() const { return t_.GetTimings().current_delay.us() / 1000.0; }

 private:
  static constexpr w::Timestamp kT0 = w::Timestamp::Seconds(100000);
  static w::Timestamp T(int64_t us) { return kT0 + w::TimeDelta::Micros(us); }
  void At(int64_t us) {
    const w::Timestamp t = T(us);
    if (t > clock_.CurrentTime()) clock_.AdvanceTime(t - clock_.CurrentTime());
  }
  w::SimulatedClock clock_;
  const w::Environment env_;
  w::VCMTiming t_;
};

template <typename Timing>
TimingStats RunTiming(const TimingInput& in, const std::vector<TimingFrame>& frames) {
  Timing timing(in.rtt_ms);
  TimingStats st;
  size_t arrived = 0, next = 0;
  int64_t last_release = 0;
  std::optional<int64_t> release_at, render_at;
  const int64_t measure_from = Round(in.seconds * 1e6 / 4);  // Skip the start-up quarter.
  while (next < frames.size()) {
    const int64_t arrival = arrived < frames.size() ? frames[arrived].arrival_us : INT64_MAX;
    const int64_t schedule =
        !release_at && arrived > next ? std::max(frames[next].arrival_us, last_release) : INT64_MAX;
    const int64_t release = release_at ? *release_at : INT64_MAX;
    if (arrival <= schedule && arrival <= release) {
      timing.OnComplete(frames[arrived++]);
    } else if (schedule <= release) {
      // FrameDecodeTiming::OnFrameBufferUpdated: wait until the latest decode time.
      const int64_t render = timing.RenderTime(frames[next].rtp, schedule);
      const int64_t latest = render - timing.DecodeAndRenderUs();
      render_at = render;
      release_at = std::max(schedule, latest);
    } else {
      const TimingFrame& f = frames[next];
      const int64_t now = *release_at;
      if (now > *render_at - timing.DecodeAndRenderUs() + 5000) ++st.late;  // kMaxAllowedFrameDelay.
      timing.Release(f, *render_at, now);
      if (f.capture_us >= measure_from) {
        st.jitter_ms.push_back(timing.JitterMs());
        st.target_ms.push_back(timing.TargetMs());
        st.current_ms.push_back(timing.CurrentMs());
        st.buffer_ms.push_back(static_cast<double>(*render_at - f.arrival_us) / 1000);
        st.e2e_ms.push_back(static_cast<double>(*render_at - f.capture_us) / 1000);
      }
      last_release = now;
      release_at.reset();
      render_at.reset();
      ++next;
    }
  }
  return st;
}

double MeanOf(const std::vector<double>& v) {
  double s = 0;
  for (double x : v) s += x;
  return v.empty() ? 0 : s / static_cast<double>(v.size());
}

double P95(std::vector<double> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(v.size())))];
}

void TimingCase(const std::string& scenario, const TimingInput& in) {
  const auto frames = MakeFrames(in);
  auto a = RunTiming<OurTiming>(in, frames);
  auto b = RunTiming<WebrtcTiming>(in, frames);
  Add("frame timing", scenario, "jitter delay, mean (ms)", MeanOf(a.jitter_ms), MeanOf(b.jitter_ms));
  Add("frame timing", scenario, "target delay, p95 (ms)", P95(a.target_ms), P95(b.target_ms));
  Add("frame timing", scenario, "current delay, mean (ms)", MeanOf(a.current_ms), MeanOf(b.current_ms));
  Add("frame timing", scenario, "arrival to render, mean (ms)", MeanOf(a.buffer_ms), MeanOf(b.buffer_ms));
  Add("frame timing", scenario, "capture to render, p95 (ms)", P95(a.e2e_ms), P95(b.e2e_ms));
  Add("frame timing", scenario, "frames released late", a.late, b.late, "> 5 ms past the latest decode time");
}

void Timing() {
  TimingCase("30 fps, 50 ms delay, no jitter", {});
  TimingCase("30 fps, jitter 10 ms", {.jitter_ms = 10});
  TimingCase("30 fps, jitter 30 ms", {.jitter_ms = 30});
  TimingCase("30 fps, jitter 5 ms, +150 ms delay step at 10 s", {.jitter_ms = 5, .step_ms = 150});
  TimingCase("30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms)", {.jitter_ms = 5, .rtx_fraction = 0.03});
  TimingCase("15 fps, jitter 10 ms", {.fps = 15, .jitter_ms = 10});
}

}  // namespace

int main() {
  w::LogMessage::SetLogToStderr(false);
  Freezes();
  Scaling();
  Timing();
  std::printf("| module | scenario | metric | ours | libwebrtc | note |\n|---|---|---|---|---|---|\n");
  for (const auto& r : rows) {
    std::printf("| %s | %s | %s | %.1f | %.1f | %s |\n", r.module.c_str(), r.scenario.c_str(), r.metric.c_str(), r.ours,
                r.webrtc, r.note.c_str());
  }
  return 0;
}
