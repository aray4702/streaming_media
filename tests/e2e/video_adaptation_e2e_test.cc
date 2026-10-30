// E2E_VideoAdaptation: CPU overuse and the bitrate limits through
// libwebrtc's adaptation chain (issue #819; formerly in cc_e2e_test.cc,
// driven by the library's own bits-per-pixel ladder).
#include <cstdint>
#include <utility>
#include <vector>

#include "sm/cc/video_adaptation.h"
#include "sm/video/resource_adaptation_processor.h"
#include "sm/video/video_stream_adapter.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
// A camera through libwebrtc's adaptation chain as rtp::VideoSendController
// runs it: OveruseFrameDetector (cc::CpuOveruseDetector) -> the CPU resource
// of ResourceAdaptationProcessor -> VideoStreamAdapter (MAINTAIN_FRAMERATE,
// BitrateConstraint) -> VideoAdapter scaling the 720p input. Encoding takes
// `encode_ms_per_megapixel(t)` per frame.
struct AdaptationSim {
  static constexpr int kCpu = 1;
  video::VideoStreamAdapter adapter;
  video::ResourceAdaptationProcessor processor{adapter};
  video::VideoAdapter scaler;
  CpuOveruseDetector cpu;
  int width = 1280, height = 720, changes = 0;

  explicit AdaptationSim(std::vector<video::ResolutionBitrateLimits> limits = {}) {
    adapter.SetResolutionBitrateLimits(std::move(limits));
    adapter.OnFrameRateObserved(30);
    processor.AddResource(kCpu);
  }
  // One frame at `t_ms`.
  void Frame(int64_t t_ms, double encode_ms_per_megapixel) {
    const Timestamp now = Timestamp::Millis(t_ms);
    int w = 0, h = 0;
    scaler.AdaptFrameResolution(1280, 720, &w, &h);
    if (w != width || h != height) ++changes;
    width = w;
    height = h;
    adapter.OnFrameSizeObserved(w * h);
    const uint32_t ts = static_cast<uint32_t>(t_ms * 90);
    cpu.OnFrameCaptured(w * h, ts, now);
    cpu.OnFrameSent(ts, now + TimeDelta::MillisF(w * h / 1e6 * encode_ms_per_megapixel));
    const auto sig = cpu.Check(now);
    if (sig == CpuOveruseDetector::Signal::kOveruse) {
      processor.OnResourceUsageStateMeasured(kCpu, video::ResourceUsageState::kOveruse);
    } else if (sig == CpuOveruseDetector::Signal::kUnderuse) {
      processor.OnResourceUsageStateMeasured(kCpu, video::ResourceUsageState::kUnderuse);
    }
    scaler.OnSinkWants(adapter.filtered_restrictions());
  }
};
}  // namespace

SM_TEST(E2E_VideoAdaptation, CpuOveruseConvergesToSustainableResolution) {
  // A slow device: 40 ms of encode per megapixel per frame, so 720p30 needs
  // 37 ms of every 33 ms; 960x540 (21 ms, 63%) sits between the thresholds.
  AdaptationSim sim;
  for (int64_t t = 0; t < 120000; t += 33) sim.Frame(t, 40.0);
  SM_RECORD("cpu_final_width", sim.width);
  SM_RECORD("cpu_final_usage", sim.cpu.usage());
  SM_RECORD("cpu_format_changes", sim.changes);
  SM_EXPECT_LT(sim.width, 1280);
  SM_EXPECT_GE(sim.width, 640);
  SM_EXPECT_LT(sim.cpu.usage(), 0.85);
  SM_EXPECT_LE(sim.changes, 6);
}

SM_TEST(E2E_VideoAdaptation, BitrateLimitsHoldResolutionUntilTheTargetAllows) {
  // Issue #819, BitrateConstraint: the CPU load falls after the device adapted
  // down, but the step back to 720p waits until the encoder target reaches the
  // encoder's min_start_bitrate for 720p (1 Mbps).
  AdaptationSim sim({{640 * 360, 500000, 0, 5000000}, {1280 * 720, 1000000, 0, 5000000}});
  sim.adapter.OnEncoderTargetBitrateUpdated(900000);
  int width_at_90s = 0;
  for (int64_t t = 0; t < 180000; t += 33) {
    if (t >= 90000) sim.adapter.OnEncoderTargetBitrateUpdated(1200000);
    sim.Frame(t, t < 20000 ? 40.0 : 10.0);
    if (t < 90000) width_at_90s = sim.width;
  }
  // Control: without limits (libwebrtc's default) the same load goes back to
  // 720p before 90 s.
  AdaptationSim free;
  free.adapter.OnEncoderTargetBitrateUpdated(900000);
  for (int64_t t = 0; t < 90000; t += 33) free.Frame(t, t < 20000 ? 40.0 : 10.0);
  SM_RECORD("width_at_90s", width_at_90s);
  SM_RECORD("final_width", sim.width);
  SM_RECORD("width_at_90s_without_limits", free.width);
  SM_EXPECT_LT(width_at_90s, 1280);
  SM_EXPECT_EQ(sim.width, 1280);
  SM_EXPECT_EQ(free.width, 1280);
}
