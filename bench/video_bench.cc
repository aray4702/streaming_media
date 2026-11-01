// Section 7 benchmarks. Per-frame budgets are relative to a 30 fps frame
// interval (33 ms); pixel operations are reported per frame at 720p.
#include "sm/video/codec_fallback.h"
#include "sm/video/content_policy.h"
#include "sm/video/frame_cadence_adapter.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/orientation.h"
#include "sm/video/quality_convergence.h"
#include "sm/video/quality_scaler.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::video;

SM_BENCH(video, FreezeDetector_frame) {
  FreezeDetector fd;
  int64_t t = 0;
  for (auto _ : st) {
    fd.OnFrameRendered(Timestamp::Millis(t));
    t += (t % 3000 == 0) ? 400 : 33;
  }
  st.BudgetNsPerItem(200);
}

namespace {
struct NullEncoder : FallbackEncoder {
  EncoderStatus InitEncode(const EncoderCodecSettings&) override { return EncoderStatus::kOk; }
  void Release() override {}
  void SetRates(const EncoderRates&) override {}
  void OnRttUpdate(TimeDelta) override {}
  void OnPacketLossRateUpdate(float) override {}
  FallbackEncoderInfo GetEncoderInfo() const override { return {}; }
};
}  // namespace

SM_BENCH(video, CodecFallback_frame) {
  NullEncoder sw, hw;
  CodecFallbackController c({}, sw, hw);
  c.InitEncode({1280, 720, false, 1}, Timestamp::Millis(0));
  int64_t t = 0;
  for (auto _ : st) {
    bench::DoNotOptimize(c.current());
    c.OnEncodeResult(t % 97 != 0 ? EncoderStatus::kOk : EncoderStatus::kError, Timestamp::Millis(t));
    t += 33;
  }
  st.BudgetNsPerItem(200);
}

SM_BENCH(video, Orientation_rotate90_720p) {
  I420Frame f = MakeTestFrame(1280, 720, 0);
  for (auto _ : st) bench::DoNotOptimize(ApplyOrientation(f, {Rotation::k90, false, false}));
  st.RequireRealtimeFactor(1.0 / 30, 5);
}

SM_BENCH(video, FrameCadenceAdapter_frame) {
  FrameCadenceAdapter a;
  a.SetZeroHertzModeEnabled(FrameCadenceAdapter::ZeroHertzModeParams{1}, Timestamp::Millis(0));
  a.OnConstraintsChanged({0.0, 15.0}, Timestamp::Millis(0));
  int64_t t = 0;
  for (auto _ : st) {
    a.OnFrame(t, Timestamp::Millis(t), Timestamp::Millis(t));
    bench::DoNotOptimize(a.Process(Timestamp::Millis(t)));
    t += 66;
    bench::DoNotOptimize(a.Process(Timestamp::Millis(t)));
  }
  st.BudgetNsPerItem(2000);
}

SM_BENCH(video, FrameScheduler_frame) {
  FrameScheduler s;
  int64_t id = 0;
  for (auto _ : st) {
    s.Insert({id, Timestamp::Millis(100 + id * 33)});
    bench::DoNotOptimize(s.Process(Timestamp::Millis(id * 33 + 100)));
    ++id;
  }
  st.BudgetNsPerItem(500);
}

SM_BENCH(video, QualityScaler_frame) {
  QualityScaler q;
  int64_t t = 0;
  for (auto _ : st) {
    q.OnEncodedFrame(static_cast<int>(20 + t % 25));
    bench::DoNotOptimize(q.Check(Timestamp::Millis(t)));
    t += 33;
  }
  st.BudgetNsPerItem(100);
}

// --- Quality ------------------------------------------------------------------

SM_BENCH_QUALITY(video, Q_ScreenShare_encode_ratio) {
  // 60 s of a static slide deck at max 15 fps, a slide change every 10 s,
  // VP8 QP 40 on a new frame and 4 lower per refresh: zero-hertz repeats
  // until the QP converges, then once per second.
  FrameCadenceAdapter a;
  QualityConvergenceController q;
  q.Initialize(1, std::nullopt, VideoCodecType::kVP8);
  a.SetZeroHertzModeEnabled(FrameCadenceAdapter::ZeroHertzModeParams{1}, Timestamp::Millis(0));
  a.OnConstraintsChanged({0.0, 15.0}, Timestamp::Millis(0));
  int encoded = 0, qp = 0;
  for (int64_t t = 0; t <= 60000; t += 1) {
    if (t % 10000 == 0 && t < 60000) a.OnFrame(t, Timestamp::Millis(t), Timestamp::Millis(t));
    if (a.NextProcessTime() > Timestamp::Millis(t)) continue;
    for (const auto& o : a.Process(Timestamp::Millis(t))) {
      if (o.kind != FrameCadenceAdapter::Output::Kind::kEncode) continue;
      ++encoded;
      qp = o.is_repeat ? std::max(qp - 4, 0) : 40;
      a.UpdateLayerQualityConvergence(0, q.AddSampleAndCheckTargetQuality(0, qp, o.is_repeat));
    }
  }
  // Per slide: the frame, 7 refreshes at the frame period (QP 36 .. 12,
  // under VP8's static threshold 15 at the 7th) and 9 idle repeats: 6 * 17
  // = 102, plus the last slide's idle repeats to 60 s. Was < 0.1 for
  // FrameChangeDetector's 1/s keep-alive, which libwebrtc does not have
  // (#878).
  double r = encoded / (60.0 * 15);
  st.Counter("encode_ratio", r);
  st.Require("encode_ratio", r, "<=", 0.12);
}
