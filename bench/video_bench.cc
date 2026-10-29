// Section 7 benchmarks. Per-frame budgets are relative to a 30 fps frame
// interval (33 ms); pixel operations are reported per frame at 720p.
#include "sm/video/codec_fallback.h"
#include "sm/video/content_policy.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/orientation.h"
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

SM_BENCH(video, FrameChangeDetector_1080p) {
  I420Frame f = MakeTestFrame(1920, 1080, 0);
  FrameChangeDetector cd;
  int64_t t = 0;
  for (auto _ : st) {
    f.Y(static_cast<int>(t % 1920), 500) ^= 1;
    bench::DoNotOptimize(cd.ShouldEncode(f, Timestamp::Millis(t)));
    t += 66;
  }
  st.RequireRealtimeFactor(1.0 / 15, 5);
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
  FrameChangeDetector cd;
  int encoded = 0, frames = 0;
  for (int i = 0; i < 900; ++i) {  // 60 s at 15 fps, slide change every 10 s.
    I420Frame f = MakeTestFrame(320, 180, (i / 150) * 50);
    encoded += cd.ShouldEncode(f, Timestamp::Millis(i * 66));
    ++frames;
  }
  double r = static_cast<double>(encoded) / frames;
  st.Counter("encode_ratio", r);
  st.Require("encode_ratio", r, "<", 0.1);
}
