// Section 7 unit tests.
#include <cmath>
#include <functional>
#include <vector>

#include "sm/core/random.h"
#include "sm/video/codec_fallback.h"
#include "sm/video/codec_negotiation.h"
#include "sm/video/content_policy.h"
#include "sm/video/frame_scaler.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/orientation.h"
#include "sm/video/quality_scaler.h"
#include "sm/video/render_frames.h"
#include "sm/video/video_stream_adapter.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::video;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
}

SM_TEST(FreezeDetector, DetectsFreezesAndPauses) {
  FreezeDetector fd;
  int64_t t = 0;
  for (int i = 0; i < 60; ++i, t += 33) fd.OnFrameRendered(Ms(t));
  t += 300;  // 333 ms gap: > max(99, 183) -> freeze.
  fd.OnFrameRendered(Ms(t));
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  t += 120;  // 153 ms: below threshold.
  fd.OnFrameRendered(Ms(t));
  t += 6000;  // Pause.
  fd.OnFrameRendered(Ms(t));
  SM_EXPECT_EQ(fd.stats().freeze_count, 1u);
  SM_EXPECT_NEAR(fd.stats().total_freezes_ms, 333, 1);
  SM_EXPECT_EQ(fd.stats().pause_count, 1u);
  SM_EXPECT_LT(fd.HarmonicFps(), 30.0);
}

// Issue #283: the longest freeze must not come from the `freeze_ms` histogram,
// which records nothing when metrics are compiled out.
SM_TEST(FreezeDetector, TracksLongestFreeze) {
  FreezeDetector fd;
  int64_t t = 0;
  SM_EXPECT_EQ(fd.stats().longest_freeze_ms, 0.0);
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  fd.OnFrameRendered(Ms(t += 1500));  // Freeze.
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  fd.OnFrameRendered(Ms(t += 400));  // Shorter freeze.
  for (int i = 0; i < 30; ++i) fd.OnFrameRendered(Ms(t += 33));
  fd.OnFrameRendered(Ms(t += 7000));  // Pause: not a freeze.
  SM_EXPECT_EQ(fd.stats().freeze_count, 2u);
  SM_EXPECT_NEAR(fd.stats().longest_freeze_ms, 1500, 1e-6);
}

SM_TEST(FreezeDetector, SmoothPlaybackHasNoFreezes) {
  FreezeDetector fd;
  for (int i = 0; i < 300; ++i) fd.OnFrameRendered(Ms(i * 33 + (i % 3) * 5));
  SM_EXPECT_EQ(fd.stats().freeze_count, 0u);
  // Intervals of 38, 38, 23 ms: sum(d) / sum(d^2) = 99 / 3.417 = 28.97 fps,
  // below the arithmetic 30.3 fps because irregular timing looks worse.
  SM_EXPECT_NEAR(fd.HarmonicFps(), 28.97, 0.05);
}

// Issue #268: freezes as libwebrtc's VideoQualityObserver counts them. The
// render times are those of sm_video_compare's freeze block
// (compare/webrtc/video_compare.cc), and the expected values are libwebrtc's
// on them (docs/results/video-webrtc.md).
namespace {
int64_t RoundMs(double x) { return static_cast<int64_t>(std::llround(x)); }
void AddFrames(std::vector<int64_t>& v, double from, double to, double period) {
  for (double t = from; t < to; t += period) {
    int64_t ms = RoundMs(t);
    if (!v.empty()) ms = std::max(ms, v.back() + 1);
    v.push_back(ms);
  }
}
FreezeStats RunFreezes(const std::vector<int64_t>& render_ms) {
  FreezeDetector fd;
  for (int64_t t : render_ms) fd.OnFrameRendered(Ms(t));
  return fd.stats();
}
constexpr double k30Fps = 1000.0 / 30;
}  // namespace

SM_TEST(FreezeDetector, CurrentIntervalEntersAverageBeforeTest) {
  // libwebrtc: a gap enters the 30-frame average before it is tested, so at
  // 30 fps a 185 ms gap is not a freeze (threshold about 188 ms) and 190 ms is.
  std::vector<int64_t> v;
  double t = 0;
  for (int gap = 180; gap <= 230; gap += 5) {
    AddFrames(v, t, t + 5000, k30Fps);
    t = static_cast<double>(v.back()) + gap;
  }
  AddFrames(v, t, t + 2000, k30Fps);
  FreezeStats s = RunFreezes(v);
  SM_EXPECT_EQ(s.freeze_count, 9u);  // libwebrtc 9 (was 10).
  SM_EXPECT_NEAR(s.total_freezes_ms, 1890, 1e-6);  // libwebrtc 1890 (was 2075).
}

SM_TEST(FreezeDetector, FreezesStayInAverage) {
  // 3 s at 3 fps: libwebrtc keeps the long intervals in the average, which
  // rises within a few frames and stops counting them as freezes.
  std::vector<int64_t> v;
  AddFrames(v, 0, 3000, k30Fps);
  AddFrames(v, 3000, 6000, 333);
  AddFrames(v, 6000, 9000, k30Fps);
  FreezeStats s = RunFreezes(v);
  SM_EXPECT_EQ(s.freeze_count, 7u);  // libwebrtc 7 (was 9).
  SM_EXPECT_NEAR(s.total_freezes_ms, 2331, 1e-6);  // libwebrtc 2331 (was 2997).
}

SM_TEST(FreezeDetector, RandomGapsAsLibwebrtc) {
  Random rng(7);
  std::vector<int64_t> v;
  double t = 0;
  while (t < 120000) {
    v.push_back(RoundMs(t));
    t += rng.Bernoulli(0.02) ? rng.Uniform(100, 800) : k30Fps;
  }
  FreezeStats s = RunFreezes(v);
  SM_EXPECT_EQ(s.freeze_count, 57u);  // libwebrtc 57 (was 59).
  SM_EXPECT_NEAR(s.total_freezes_ms, 26580, 1e-6);  // libwebrtc 26580 (was 26974).
}

SM_TEST(FreezeDetector, TestStartsAtFiveSamplesIncludingCurrent) {
  // Four 33 ms intervals, then a 300 ms gap: the window holds 5 samples
  // counting the gap (average 86 ms, threshold 258 ms), so it is a freeze.
  // Three intervals and the gap make only 4 samples: not tested.
  FreezeDetector a;
  int64_t t = 0;
  a.OnFrameRendered(Ms(t));
  for (int i = 0; i < 4; ++i) a.OnFrameRendered(Ms(t += 33));
  a.OnFrameRendered(Ms(t += 300));
  SM_EXPECT_EQ(a.stats().freeze_count, 1u);
  FreezeDetector b;
  t = 0;
  b.OnFrameRendered(Ms(t));
  for (int i = 0; i < 3; ++i) b.OnFrameRendered(Ms(t += 33));
  b.OnFrameRendered(Ms(t += 300));
  SM_EXPECT_EQ(b.stats().freeze_count, 0u);
}

SM_TEST(FreezeDetector, AverageRoundedDownToWholeMs) {
  // 29 intervals of 50 ms and a gap: with 205 ms the average is 1655 / 30 =
  // 55.17 ms, rounded down to 55, so the threshold is 205 ms and the gap is a
  // freeze (unrounded it would need 205.17 ms). 204 ms is not.
  for (int gap : {204, 205}) {
    FreezeDetector fd;
    int64_t t = 0;
    fd.OnFrameRendered(Ms(t));
    for (int i = 0; i < 29; ++i) fd.OnFrameRendered(Ms(t += 50));
    fd.OnFrameRendered(Ms(t += gap));
    SM_EXPECT_EQ(fd.stats().freeze_count, gap == 204 ? 0u : 1u);
  }
}

namespace {
// A scripted encoder, as libwebrtc's CountingFakeEncoder in
// video_encoder_software_fallback_wrapper_unittest.cc.
struct FakeEncoder : FallbackEncoder {
  EncoderStatus init_result = EncoderStatus::kOk;
  FallbackEncoderInfo info;
  int init_count = 0, release_count = 0, set_rates_count = 0, rtt_count = 0, loss_count = 0;
  EncoderRates last_rates;
  EncoderCodecSettings last_settings;
  EncoderStatus InitEncode(const EncoderCodecSettings& s) override {
    ++init_count;
    last_settings = s;
    return init_result;
  }
  void Release() override { ++release_count; }
  void SetRates(const EncoderRates& r) override {
    ++set_rates_count;
    last_rates = r;
  }
  void OnRttUpdate(TimeDelta) override { ++rtt_count; }
  void OnPacketLossRateUpdate(float) override { ++loss_count; }
  FallbackEncoderInfo GetEncoderInfo() const override { return info; }
};
EncoderCodecSettings Vga(bool vp8 = true) { return {640, 480, vp8, 1}; }
}  // namespace

SM_TEST(CodecFallback, InitializesMainEncoder) {
  FakeEncoder sw, hw;
  CodecFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.current() == nullptr);  // Encode before InitEncode is an error.
  SM_EXPECT_TRUE(w.InitEncode(Vga(), Ms(0)) == EncoderStatus::kOk);
  SM_EXPECT_TRUE(w.state() == EncoderState::kMainEncoderUsed);
  SM_EXPECT_TRUE(w.current() == &hw);
  SM_EXPECT_EQ(sw.init_count, 0);
  SM_EXPECT_FALSE(w.forced_params().has_value());  // Both field trials off by default.
}

SM_TEST(CodecFallback, InitFailureUsesSoftware) {
  FakeEncoder sw, hw;
  hw.init_result = EncoderStatus::kError;
  CodecFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.InitEncode(Vga(), Ms(0)) == EncoderStatus::kOk);
  SM_EXPECT_TRUE(w.state() == EncoderState::kFallbackDueToFailure);
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(sw.last_settings.width, 640);
  SM_EXPECT_EQ(hw.release_count, 0);  // The main encoder was never initialized.
  // Both fail: the main encoder's error, and nothing to encode with.
  FakeEncoder sw2, hw2;
  hw2.init_result = EncoderStatus::kError;
  sw2.init_result = EncoderStatus::kError;
  CodecFallbackController w2({}, sw2, hw2);
  SM_EXPECT_TRUE(w2.InitEncode(Vga(), Ms(0)) == EncoderStatus::kError);
  SM_EXPECT_TRUE(w2.current() == nullptr);
  SM_EXPECT_EQ(sw2.release_count, 1);
}

SM_TEST(CodecFallback, SimulcastNotSupportedDoesNotFallBack) {
  FakeEncoder sw, hw;
  hw.init_result = EncoderStatus::kSimulcastParametersNotSupported;
  CodecFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.InitEncode(Vga(), Ms(0)) == EncoderStatus::kSimulcastParametersNotSupported);
  SM_EXPECT_EQ(sw.init_count, 0);
  SM_EXPECT_TRUE(w.current() == nullptr);
}

SM_TEST(CodecFallback, EncodeRequestsFallbackOnlyWithFallbackSoftware) {
  FakeEncoder sw, hw;
  CodecFallbackController w({}, sw, hw);
  w.InitEncode(Vga(), Ms(0));
  w.SetRates({DataRate::KilobitsPerSec(300), 30});
  w.OnRttUpdate(TimeDelta::Millis(100));
  w.OnPacketLossRateUpdate(0.05f);
  // Plain errors are returned as they are, however many.
  for (int i = 0; i < 20; ++i) SM_EXPECT_FALSE(w.OnEncodeResult(EncoderStatus::kError, Ms(33 * i)));
  SM_EXPECT_TRUE(w.state() == EncoderState::kMainEncoderUsed);
  SM_EXPECT_TRUE(w.OnEncodeResult(EncoderStatus::kFallbackSoftware, Ms(700)));
  SM_EXPECT_TRUE(w.state() == EncoderState::kFallbackDueToFailure);
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(hw.release_count, 1);  // InternalEncoderReleasedDuringFallback.
  // PrimeEncoder: the fallback gets the last rates and channel parameters.
  SM_EXPECT_EQ(sw.set_rates_count, 1);
  SM_EXPECT_EQ(sw.last_rates.bitrate.bps(), 300000);
  SM_EXPECT_EQ(sw.rtt_count, 1);
  SM_EXPECT_EQ(sw.loss_count, 1);
  // Further calls go to the fallback only; the fallback's own errors don't move.
  w.SetRates({DataRate::KilobitsPerSec(200), 30});
  SM_EXPECT_EQ(sw.set_rates_count, 2);
  SM_EXPECT_EQ(hw.set_rates_count, 1);
  SM_EXPECT_FALSE(w.OnEncodeResult(EncoderStatus::kFallbackSoftware, Ms(733)));
  SM_EXPECT_EQ(w.stats().fallbacks, 1u);
}

SM_TEST(CodecFallback, FailedFallbackInitKeepsMainEncoder) {
  FakeEncoder sw, hw;
  sw.init_result = EncoderStatus::kError;
  CodecFallbackController w({}, sw, hw);
  w.InitEncode(Vga(), Ms(0));
  SM_EXPECT_FALSE(w.OnEncodeResult(EncoderStatus::kFallbackSoftware, Ms(33)));
  SM_EXPECT_TRUE(w.state() == EncoderState::kMainEncoderUsed);
  SM_EXPECT_EQ(hw.release_count, 0);
}

SM_TEST(CodecFallback, ReleaseAndReinitTriesMainAgain) {
  FakeEncoder sw, hw;
  CodecFallbackController w({}, sw, hw);
  w.InitEncode(Vga(), Ms(0));
  w.SetRates({DataRate::KilobitsPerSec(300), 30});
  w.OnEncodeResult(EncoderStatus::kFallbackSoftware, Ms(33));
  w.Release();
  SM_EXPECT_EQ(sw.release_count, 1);
  SM_EXPECT_TRUE(w.current() == nullptr);
  SM_EXPECT_TRUE(w.InitEncode(Vga(), Ms(1000)) == EncoderStatus::kOk);
  SM_EXPECT_TRUE(w.state() == EncoderState::kMainEncoderUsed);
  // InitEncode clears the stored rates: none replayed on this init.
  SM_EXPECT_EQ(hw.set_rates_count, 1);
}

SM_TEST(CodecFallback, ForcedFallbackByResolution) {
  // WebRTC-Video-EncoderFallbackSettings/resolution_threshold_px:76800/.
  CodecFallbackConfig cfg;
  cfg.resolution_threshold_px = 320 * 240;
  FakeEncoder sw, hw;
  CodecFallbackController w(cfg, sw, hw);
  SM_EXPECT_TRUE(w.InitEncode({320, 240, false, 1}, Ms(0)) == EncoderStatus::kOk);
  SM_EXPECT_TRUE(w.state() == EncoderState::kForcedFallback);
  SM_EXPECT_EQ(hw.init_count, 0);
  w.Release();
  SM_EXPECT_TRUE(w.InitEncode({321, 240, false, 1}, Ms(0)) == EncoderStatus::kOk);
  SM_EXPECT_TRUE(w.state() == EncoderState::kMainEncoderUsed);
  SM_EXPECT_EQ(w.stats().forced_fallbacks, 1u);
}

SM_TEST(CodecFallback, Vp8ForcedFallbackTrial) {
  FallbackEncoderInfo main;  // min_pixels_per_frame 320x180.
  CodecFallbackConfig cfg;
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Disabled-1,76800,30000";
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-1,76800";  // Too few values.
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-1,57598,30000";  // max below min_pixels_per_frame - 1.
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-1,57599,30000";  // At the bound: valid.
  SM_EXPECT_TRUE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-1,57599,0";  // min_bps must be positive.
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-0,76800,30000";  // min_pixels must be positive.
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-80000,76800,30000";  // max below min.
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main).has_value());
  cfg.vp8_forced_fallback = "Enabled-57600,76800,30000";
  auto p = ParseForcedFallbackParams(cfg, main);
  SM_ASSERT_TRUE(p.has_value());
  SM_EXPECT_TRUE(p->vp8_specific_resolution_switch);
  // VP8, one stream, at most max_pixels only.
  SM_EXPECT_TRUE(p->SupportsResolutionBasedSwitch({320, 240, true, 1}));
  SM_EXPECT_FALSE(p->SupportsResolutionBasedSwitch({320, 240, false, 1}));
  SM_EXPECT_FALSE(p->SupportsResolutionBasedSwitch({320, 240, true, 2}));
  SM_EXPECT_FALSE(p->SupportsResolutionBasedSwitch({640, 480, true, 1}));
  // The settings trial wins over the VP8 one.
  cfg.resolution_threshold_px = 100;
  SM_EXPECT_FALSE(ParseForcedFallbackParams(cfg, main)->vp8_specific_resolution_switch);
  // GetEncoderInfo reports the trial's min_pixels as the scaling floor.
  cfg.resolution_threshold_px.reset();
  FakeEncoder sw, hw;
  CodecFallbackController w(cfg, sw, hw);
  SM_EXPECT_EQ(w.GetEncoderInfo().min_pixels_per_frame, 57600);
}

SM_TEST(CodecFallback, EncoderInfoMerge) {
  FakeEncoder sw, hw;
  sw.info.requested_resolution_alignment = 4;
  sw.info.max_pixels_per_frame = 1920 * 1080;
  sw.info.min_pixels_per_frame = 1000;
  hw.info.requested_resolution_alignment = 6;
  hw.info.apply_alignment_to_all_simulcast_layers = true;
  hw.info.max_pixels_per_frame = 1280 * 720;
  hw.info.min_pixels_per_frame = 2000;
  CodecFallbackController w({}, sw, hw);
  w.InitEncode(Vga(), Ms(0));
  FallbackEncoderInfo info = w.GetEncoderInfo();
  SM_EXPECT_EQ(info.requested_resolution_alignment, 12);  // lcm(4, 6).
  SM_EXPECT_TRUE(info.apply_alignment_to_all_simulcast_layers);
  SM_EXPECT_EQ(*info.max_pixels_per_frame, size_t{1920 * 1080});  // Always the fallback's.
  SM_EXPECT_EQ(info.min_pixels_per_frame, 2000);  // The active encoder's.
  w.OnEncodeResult(EncoderStatus::kFallbackSoftware, Ms(33));
  SM_EXPECT_EQ(w.GetEncoderInfo().min_pixels_per_frame, 1000);
}

namespace {
// A scripted decoder, as CountingFakeDecoder in libwebrtc's
// video_decoder_software_fallback_wrapper_unittest.cc.
struct FakeDecoder : FallbackDecoder {
  bool configure_result = true;
  DecoderStatus release_result = DecoderStatus::kOk;
  FallbackDecoderInfo info;
  int configure_count = 0, release_count = 0;
  DecoderSettings last_settings;
  explicit FakeDecoder(std::string name = "fake") { info.implementation_name = std::move(name); }
  bool Configure(const DecoderSettings& s) override {
    ++configure_count;
    last_settings = s;
    return configure_result;
  }
  DecoderStatus Release() override {
    ++release_count;
    return release_result;
  }
  FallbackDecoderInfo GetDecoderInfo() const override { return info; }
};
DecoderSettings H264Decoder() { return {DecoderCodecType::kH264}; }
using DecoderType = DecoderFallbackController::DecoderType;
}  // namespace

SM_TEST(DecoderFallback, InitializesHardwareDecoder) {
  FakeDecoder sw, hw;
  DecoderFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.current() == nullptr);  // Decode before Configure: UNINITIALIZED.
  SM_EXPECT_TRUE(w.Configure(H264Decoder(), Ms(0)));
  SM_EXPECT_TRUE(w.decoder_type() == DecoderType::kHardware);
  SM_EXPECT_TRUE(w.current() == &hw);
  SM_EXPECT_EQ(hw.configure_count, 1);
  SM_EXPECT_EQ(sw.configure_count, 0);
  SM_EXPECT_TRUE(hw.last_settings.codec_type == DecoderCodecType::kH264);
}

SM_TEST(DecoderFallback, ConfigureFailureUsesSoftware) {
  FakeDecoder sw, hw;
  hw.configure_result = false;
  DecoderFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.Configure(H264Decoder(), Ms(0)));
  SM_EXPECT_TRUE(w.decoder_type() == DecoderType::kFallback);
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(hw.release_count, 0);  // The hardware decoder never configured.
  SM_EXPECT_EQ(w.stats().fallbacks, 1u);
  // Both fail: Configure fails and there is nothing to decode with.
  FakeDecoder sw2, hw2;
  sw2.configure_result = hw2.configure_result = false;
  DecoderFallbackController w2({}, sw2, hw2);
  SM_EXPECT_FALSE(w2.Configure(H264Decoder(), Ms(0)));
  SM_EXPECT_TRUE(w2.current() == nullptr);
  SM_EXPECT_EQ(w2.stats().fallbacks, 0u);
}

SM_TEST(DecoderFallback, FallbackRequestOnDecodeIsSticky) {
  FakeDecoder sw, hw;
  DecoderFallbackController w({}, sw, hw);
  w.Configure(H264Decoder(), Ms(0));
  for (int i = 0; i < 10; ++i) SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kOk, i == 0, Ms(33 * i)));
  // The same frame is decoded again by the software decoder.
  SM_EXPECT_TRUE(w.OnDecodeResult(DecoderStatus::kFallbackSoftware, false, Ms(330)));
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(hw.release_count, 1);
  SM_EXPECT_EQ(sw.configure_count, 1);
  SM_EXPECT_EQ(w.stats().hw_decoded_frames_at_last_fallback, 10);
  // IsSoftwareFallbackSticky: the fallback's own results never move back.
  for (auto st : {DecoderStatus::kOk, DecoderStatus::kError, DecoderStatus::kFallbackSoftware})
    SM_EXPECT_FALSE(w.OnDecodeResult(st, true, Ms(363)));
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(sw.configure_count, 1);
  SM_EXPECT_EQ(w.stats().fallbacks, 1u);
}

SM_TEST(DecoderFallback, FailedFallbackInitKeepsHardware) {
  FakeDecoder sw, hw;
  sw.configure_result = false;
  DecoderFallbackController w({}, sw, hw);
  w.Configure(H264Decoder(), Ms(0));
  // The hardware decoder's result is kept as the frame's result.
  SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kFallbackSoftware, true, Ms(33)));
  SM_EXPECT_TRUE(w.current() == &hw);
  SM_EXPECT_EQ(hw.release_count, 0);
  SM_EXPECT_EQ(w.stats().fallbacks, 0u);
}

SM_TEST(DecoderFallback, GenericErrorsCountOnlyOnKeyframes) {
  FakeDecoder sw, hw;
  DecoderFallbackController w({}, sw, hw);
  w.Configure(H264Decoder(), Ms(0));
  // DoesNotFallbackOnEveryError / on delta frames: any number of delta-frame errors.
  for (int i = 0; i < 50; ++i) SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kError, false, Ms(i)));
  // Three keyframe errors stay on the hardware decoder.
  for (int i = 0; i < 3; ++i) SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kError, true, Ms(100 + i)));
  // A decoded frame (or any non-error code) resets the count.
  SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kOther, false, Ms(200)));
  for (int i = 0; i < 3; ++i) SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kError, true, Ms(300 + i)));
  SM_EXPECT_TRUE(w.current() == &hw);
  // The 4th consecutive keyframe error (kMaxConsequtiveHwErrors) falls back,
  // with delta-frame errors in between not breaking the run.
  SM_EXPECT_FALSE(w.OnDecodeResult(DecoderStatus::kError, false, Ms(400)));
  SM_EXPECT_TRUE(w.OnDecodeResult(DecoderStatus::kError, true, Ms(401)));
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(w.stats().hw_decoded_frames_at_last_fallback, 1);  // Only the kOther frame.
}

SM_TEST(DecoderFallback, ReleaseAndReconfigureTriesHardwareAgain) {
  FakeDecoder sw, hw;
  DecoderFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.Release() == DecoderStatus::kOk);  // Nothing configured: OK, no call.
  SM_EXPECT_EQ(hw.release_count + sw.release_count, 0);
  w.Configure(H264Decoder(), Ms(0));
  hw.release_result = DecoderStatus::kError;
  SM_EXPECT_TRUE(w.Release() == DecoderStatus::kError);  // ForwardsReleaseCall.
  SM_EXPECT_EQ(hw.release_count, 1);
  SM_EXPECT_TRUE(w.current() == nullptr);
  w.Configure(H264Decoder(), Ms(10));
  w.OnDecodeResult(DecoderStatus::kOk, true, Ms(20));
  w.OnDecodeResult(DecoderStatus::kFallbackSoftware, false, Ms(53));
  SM_EXPECT_TRUE(w.Release() == DecoderStatus::kOk);
  SM_EXPECT_EQ(sw.release_count, 1);
  // UsesHwDecoderAfterReinit.
  SM_EXPECT_TRUE(w.Configure(H264Decoder(), Ms(100)));
  SM_EXPECT_TRUE(w.current() == &hw);
  SM_EXPECT_EQ(hw.configure_count, 3);
  // The histogram count is never reset, as in libwebrtc.
  w.OnDecodeResult(DecoderStatus::kOk, true, Ms(133));
  w.OnDecodeResult(DecoderStatus::kFallbackSoftware, false, Ms(166));
  SM_EXPECT_EQ(w.stats().hw_decoded_frames_at_last_fallback, 2);
  SM_EXPECT_EQ(w.stats().fallbacks, 2u);
}

SM_TEST(DecoderFallback, ForcedSoftwareDecoderFallback) {
  // WebRTC-Video-ForcedSwDecoderFallback/Enabled/.
  DecoderFallbackConfig cfg;
  cfg.force_sw_decoder_fallback = true;
  FakeDecoder sw, hw;
  DecoderFallbackController w(cfg, sw, hw);
  SM_EXPECT_TRUE(w.Configure(H264Decoder(), Ms(0)));
  SM_EXPECT_TRUE(w.current() == &sw);
  SM_EXPECT_EQ(hw.configure_count, 0);
  // Forced and the software decoder fails: no hardware attempt.
  FakeDecoder sw2, hw2;
  sw2.configure_result = false;
  DecoderFallbackController w2(cfg, sw2, hw2);
  SM_EXPECT_FALSE(w2.Configure(H264Decoder(), Ms(0)));
  SM_EXPECT_EQ(hw2.configure_count, 0);
}

SM_TEST(DecoderFallback, ReportsFallbackImplementationName) {
  FakeDecoder sw("libavcodec"), hw("VideoToolbox");
  hw.info.is_hardware_accelerated = true;
  DecoderFallbackController w({}, sw, hw);
  SM_EXPECT_TRUE(w.ImplementationName() == "VideoToolbox");  // Before Configure: the main decoder's.
  w.Configure(H264Decoder(), Ms(0));
  SM_EXPECT_TRUE(w.GetDecoderInfo().is_hardware_accelerated);
  w.OnDecodeResult(DecoderStatus::kFallbackSoftware, true, Ms(33));
  SM_EXPECT_TRUE(w.ImplementationName() == "libavcodec (fallback from: VideoToolbox)");
  FallbackDecoderInfo info = w.GetDecoderInfo();
  SM_EXPECT_TRUE(info.implementation_name == "libavcodec (fallback from: VideoToolbox)");
  SM_EXPECT_FALSE(info.is_hardware_accelerated);
}

SM_TEST(Orientation, CvoByteAndRotation) {
  VideoOrientation o{Rotation::k90, true, true};
  uint8_t b = EncodeCvo(o);
  SM_EXPECT_EQ(b, 0x0D);
  auto d = DecodeCvo(b);
  SM_EXPECT_TRUE(d.rotation == Rotation::k90);
  SM_EXPECT_TRUE(d.flip && d.back_camera);
  I420Frame f(4, 2);
  for (int i = 0; i < 8; ++i) f.y[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
  auto r = ApplyOrientation(f, {Rotation::k90, false, false});
  SM_EXPECT_EQ(r.width, 2);
  SM_EXPECT_EQ(r.height, 4);
  // Clockwise: the bottom-left source pixel (4) becomes the top-left.
  SM_EXPECT_EQ(r.Y(0, 0), 4);
  SM_EXPECT_EQ(r.Y(1, 0), 0);
}

SM_TEST(Orientation, InverseRestoresAllEightCases) {
  I420Frame f = MakeTestFrame(64, 48, 3);
  for (int rot = 0; rot < 4; ++rot) {
    for (bool flip : {false, true}) {
      VideoOrientation o{static_cast<Rotation>(rot), flip, false};
      I420Frame back = ApplyOrientation(ApplyOrientation(f, o), Inverse(o));
      SM_EXPECT_TRUE(back == f);
    }
  }
}

SM_TEST(ContentPolicy, HintsMapToPolicies) {
  auto text = SettingsFor(ContentHint::kText, true);
  SM_EXPECT_TRUE(text.degradation == Degradation::kMaintainResolution);
  SM_EXPECT_TRUE(text.variable_framerate);
  SM_EXPECT_LE(text.max_qp_h264, 32);
  auto motion = SettingsFor(ContentHint::kMotion, false);
  SM_EXPECT_TRUE(motion.degradation == Degradation::kMaintainFramerate);
  SM_EXPECT_FALSE(motion.variable_framerate);
}

SM_TEST(ContentPolicy, ChangeDetectorSkipsStaticFrames) {
  FrameChangeDetector cd(TimeDelta::Seconds(1));
  I420Frame slide = MakeTestFrame(320, 240, 0);
  SM_EXPECT_TRUE(cd.ShouldEncode(slide, Ms(0)));
  SM_EXPECT_FALSE(cd.ShouldEncode(slide, Ms(100)));
  slide.Y(100, 100) ^= 0xff;  // One pixel changes (cursor blink).
  auto r = cd.Analyze(slide);
  SM_EXPECT_TRUE(r.changed);
  SM_EXPECT_EQ(r.dirty_blocks, 1);
  SM_EXPECT_FALSE(cd.ShouldEncode(slide, Ms(200)));  // Already absorbed by Analyze.
  SM_EXPECT_TRUE(cd.ShouldEncode(slide, Ms(1300)));  // Keep-alive.
}

SM_TEST(CodecNegotiation, H264ProfileLevelParsing) {
  auto cb = ParseH264ProfileLevelId("42e01f");
  SM_ASSERT_TRUE(cb.has_value());
  SM_EXPECT_TRUE(cb->profile == H264Profile::kConstrainedBaseline);
  SM_EXPECT_EQ(cb->level_idc, 31);
  auto high = ParseH264ProfileLevelId("640c34");
  SM_ASSERT_TRUE(high.has_value());
  SM_EXPECT_TRUE(high->profile == H264Profile::kConstrainedHigh);
  SM_EXPECT_EQ(ParseH264ProfileLevelId("4d001f")->profile, H264Profile::kMain);
  SM_EXPECT_FALSE(ParseH264ProfileLevelId("zz0000").has_value());
  SM_EXPECT_EQ(H264ProfileLevelIdString(*cb), "42e01f");
}

SM_TEST(CodecNegotiation, AnswerKeepsSupportedInLocalOrder) {
  std::vector<CodecCapability> offer = {
      {"VP8", 96, {}, {"nack", "nack pli", "transport-cc"}},
      {"rtx", 97, {{"apt", "96"}}, {}},
      {"VP9", 98, {{"profile-id", "2"}}, {"nack"}},
      {"H264", 102, {{"profile-level-id", "42e034"}, {"packetization-mode", "1"}}, {"nack", "ccm fir", "goog-remb"}},
      {"rtx", 103, {{"apt", "102"}}, {}},
      {"H264", 104, {{"profile-level-id", "640034"}, {"packetization-mode", "1"}}, {"nack"}},
  };
  std::vector<CodecCapability> local = {
      {"VP8", 0, {}, {"nack", "nack pli", "transport-cc"}, false},
      {"VP9", 0, {{"profile-id", "0"}}, {"nack"}, false},
      {"H264", 0, {{"profile-level-id", "42e01f"}, {"packetization-mode", "1"}}, {"nack", "ccm fir"}, true},
  };
  auto ans = NegotiateCodecs(offer, local);
  SM_ASSERT_EQ(ans.size(), 4u);  // H264 CB, VP8, rtx(96), rtx(102). VP9 p2 != p0.
  SM_EXPECT_EQ(ans[0].name, "H264");  // Hardware first.
  SM_EXPECT_EQ(ans[0].payload_type, 102);
  SM_EXPECT_EQ(ans[0].params["profile-level-id"], "42e01f");  // min(5.2, 3.1)
  SM_EXPECT_EQ(ans[0].feedback.size(), 2u);
  SM_EXPECT_EQ(ans[1].name, "VP8");
  SM_EXPECT_EQ(ans[1].payload_type, 96);
  SM_EXPECT_EQ(ans[2].payload_type, 97);
  SM_EXPECT_EQ(ans[3].payload_type, 103);
}

SM_TEST(FrameScaler, DownUpRoundTripQuality) {
  I420Frame f = MakeTestFrame(640, 360, 1);
  for (auto [w, h] : {std::pair{480, 270}, {320, 180}, {160, 90}}) {
    I420Frame back = Scale(Scale(f, w, h), 640, 360);
    double psnr = PsnrY(f, back);
    SM_EXPECT_GT(psnr, w >= 320 ? 26.0 : 20.0);
  }
  I420Frame same = Scale(f, 640, 360);
  SM_EXPECT_TRUE(same == f);
}

SM_TEST(FrameScaler, BoxFilterPreservesMeanAndAspectModes) {
  I420Frame f(64, 64);
  for (size_t i = 0; i < f.y.size(); ++i) f.y[i] = (i % 2) ? 200 : 100;  // Fine detail.
  I420Frame s = Scale(f, 16, 16);
  for (uint8_t v : s.y) SM_EXPECT_NEAR(v, 150, 1);  // Averaged, no aliasing.
  I420Frame wide = MakeTestFrame(640, 360, 0);
  I420Frame crop = ScaleToFit(wide, 320, 320, AspectMode::kCrop);
  SM_EXPECT_EQ(crop.width, 320);
  I420Frame box = ScaleToFit(wide, 320, 320, AspectMode::kLetterbox);
  SM_EXPECT_EQ(box.Y(160, 0), 16);    // Black bar on top.
  SM_EXPECT_NE(box.Y(160, 160), 16);  // Picture in the middle.
}

SM_TEST(FrameScheduler, RendersOnTimeDropsLateWhenNewerWaiting) {
  FrameScheduler s;
  s.Insert({1, Ms(100)});
  s.Insert({2, Ms(133)});
  s.Insert({3, Ms(166)});
  SM_EXPECT_TRUE(s.Process(Ms(50)).empty());
  auto a = s.Process(Ms(96));
  SM_ASSERT_EQ(a.size(), 1u);
  SM_EXPECT_TRUE(a[0].render);
  // Stall until 200: frame 2 is 72 ms late with frame 3 waiting -> skipped.
  auto b = s.Process(Ms(200));
  SM_ASSERT_EQ(b.size(), 2u);
  SM_EXPECT_FALSE(b[0].render);
  SM_EXPECT_TRUE(b[1].render);  // Newest is shown even though late.
  SM_EXPECT_EQ(s.stats().dropped_late, 1u);
  SM_EXPECT_EQ(s.stats().decoded, 3u);
}

SM_TEST(FrameScheduler, FastForwardOnHugeBacklog) {
  FrameSchedulerConfig cfg;
  cfg.max_queue = 10;
  FrameScheduler s(cfg);
  for (int i = 0; i < 20; ++i) s.Insert({i, Ms(1000 + i * 33)});
  auto a = s.Process(Ms(0));
  SM_EXPECT_EQ(a.size(), 19u);
  SM_EXPECT_EQ(s.queued(), 1u);
  SM_EXPECT_EQ(s.stats().fast_forwards, 1u);
}

// RenderFrames ports libwebrtc M157's IncomingVideoStream + VideoRenderFrames
// (video/render/incoming_video_stream.cc, video/render/video_render_frames.cc; issue #437).
SM_TEST(RenderFrames, FrameIntoEmptyQueueRendersAtOnceWhenDue) {
  RenderFrames r;
  r.OnFrame(1, Ms(100), Ms(90));  // Released render_delay (10 ms) before its render time.
  auto d = r.TakeDecisions();
  SM_ASSERT_EQ(d.size(), 1u);
  SM_EXPECT_EQ(d[0].id, 1);
  SM_EXPECT_TRUE(d[0].render);
  SM_EXPECT_EQ(r.queued(), 0u);
  SM_EXPECT_FALSE(r.NextWakeup().IsFinite());
}

SM_TEST(RenderFrames, WaitsUntilRenderTimeMinusRenderDelay) {
  RenderFrames r;
  r.OnFrame(1, Ms(100), Ms(50));
  SM_EXPECT_TRUE(r.TakeDecisions().empty());
  SM_EXPECT_EQ(r.NextWakeup().ms(), 90);  // Dequeue's PostDelayedHighPrecisionTask.
  r.Process(Ms(89));
  SM_EXPECT_TRUE(r.TakeDecisions().empty());
  r.Process(Ms(90));
  auto d = r.TakeDecisions();
  SM_ASSERT_EQ(d.size(), 1u);
  SM_EXPECT_TRUE(d[0].render);
}

SM_TEST(RenderFrames, OnlyNewestOfSeveralDueFramesRenders) {
  RenderFrames r;
  r.OnFrame(1, Ms(100), Ms(0));
  r.OnFrame(2, Ms(110), Ms(0));
  r.OnFrame(3, Ms(120), Ms(0));
  SM_EXPECT_EQ(r.queued(), 3u);
  r.Process(Ms(200));  // The dequeue ran late: all three are due (FrameToRender).
  auto d = r.TakeDecisions();
  SM_ASSERT_EQ(d.size(), 3u);
  SM_EXPECT_FALSE(d[0].render);
  SM_EXPECT_FALSE(d[1].render);
  SM_EXPECT_TRUE(d[2].render);
  SM_EXPECT_EQ(d[2].id, 3);
  SM_EXPECT_EQ(r.stats().dropped_skipped, 2u);
  SM_EXPECT_EQ(r.stats().rendered, 1u);
}

SM_TEST(RenderFrames, OldFrameDroppedOnlyWhenOthersWait) {
  RenderFrames alone;
  alone.OnFrame(1, Ms(100), Ms(700));  // 600 ms old, but the queue is empty: rendered.
  auto d = alone.TakeDecisions();
  SM_ASSERT_EQ(d.size(), 1u);
  SM_EXPECT_TRUE(d[0].render);

  RenderFrames r;
  r.OnFrame(1, Ms(100), Ms(0));    // Waits until 90.
  r.OnFrame(2, Ms(105), Ms(606));  // 501 ms old (kOldRenderTimestampMS) with frame 1 waiting.
  d = r.TakeDecisions();
  SM_ASSERT_EQ(d.size(), 1u);
  SM_EXPECT_EQ(d[0].id, 2);
  SM_EXPECT_FALSE(d[0].render);
  SM_EXPECT_EQ(r.stats().dropped_old, 1u);
}

SM_TEST(RenderFrames, OutOfOrderAndFarFutureFramesDropped) {
  RenderFrames r;
  r.OnFrame(1, Ms(500), Ms(0));
  r.OnFrame(2, Ms(400), Ms(0));    // Earlier render time than the last queued frame.
  r.OnFrame(3, Ms(10001), Ms(0));  // More than 10 s ahead (kFutureRenderTimestampMS).
  auto d = r.TakeDecisions();
  SM_ASSERT_EQ(d.size(), 2u);
  SM_EXPECT_EQ(d[0].id, 2);
  SM_EXPECT_EQ(d[1].id, 3);
  SM_EXPECT_EQ(r.stats().dropped_out_of_order, 1u);
  SM_EXPECT_EQ(r.stats().dropped_future, 1u);
  SM_EXPECT_EQ(r.queued(), 1u);
}

SM_TEST(RenderFrames, InvalidRenderDelayBecomesTenMs) {
  SM_EXPECT_EQ(RenderFrames({TimeDelta::Millis(600)}).render_delay().ms(), 10);  // EnsureValidRenderDelay.
  SM_EXPECT_EQ(RenderFrames({TimeDelta::Millis(5)}).render_delay().ms(), 10);
  SM_EXPECT_EQ(RenderFrames({TimeDelta::Millis(40)}).render_delay().ms(), 40);
}

SM_TEST(QualityScaler, ThresholdsAndSignals) {
  SM_EXPECT_EQ(QpThresholds::ForCodec("vp8").high, 95);
  SM_EXPECT_EQ(QpThresholds::ForCodec("H264").low, 24);
  QualityScaler q;  // H.264 thresholds.
  for (int i = 0; i < 30; ++i) q.OnEncodedFrame(45);
  SM_EXPECT_TRUE(q.Check(Ms(0)) == QualityScaler::Signal::kAdaptDown);
  q.OnResolutionChanged();
  for (int i = 0; i < 30; ++i) q.OnEncodedFrame(30);
  SM_EXPECT_TRUE(q.Check(Ms(2000)) == QualityScaler::Signal::kNone);
  for (int i = 0; i < 30; ++i) q.OnEncodedFrame(18);
  SM_EXPECT_TRUE(q.Check(Ms(2500)) == QualityScaler::Signal::kNone);  // Rate limited.
  SM_EXPECT_TRUE(q.Check(Ms(4000)) == QualityScaler::Signal::kAdaptUp);
  QualityScaler d;
  for (int i = 0; i < 20; ++i) d.OnFrameDropped();
  for (int i = 0; i < 5; ++i) d.OnEncodedFrame(20);
  SM_EXPECT_TRUE(d.Check(Ms(0)) == QualityScaler::Signal::kAdaptDown);  // 80% dropped.
}

// Version 2: libwebrtc M157's QualityScaler. These are the cases of
// modules/video_coding/utility/quality_scaler_unittest.cc (thresholds 15/40,
// 150 frames per TriggerScale, every sample at time 0), run on M157's real
// cadence instead of the test's 5 ms period: the first check comes 2 s after
// the first Check() call.
namespace {

QualityScaler V2Scaler() {
  QualityScalerConfig cfg;
  cfg.thresholds = {15, 40};
  cfg.version = 2;
  return QualityScaler(cfg);
}

// Starts the check timer at 0 and runs `feed`, then the first check at 2 s.
QualityScaler::Signal FirstCheck(QualityScaler& q, const std::function<void()>& feed) {
  SM_EXPECT_TRUE(q.Check(Ms(0)) == QualityScaler::Signal::kNone);
  feed();
  SM_EXPECT_TRUE(q.Check(Ms(1999)) == QualityScaler::Signal::kNone);
  return q.Check(Ms(2000));
}

}  // namespace

SM_TEST(QualityScaler, V2DownscalesAfterContinuousFramedrop) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnFrameDropped(); }) ==
                 QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2KeepsScaleAtHighQp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnEncodedFrame(40, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
}

SM_TEST(QualityScaler, V2DownscalesAboveHighQp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnEncodedFrame(41, Ms(0)); }) ==
                 QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2DownscalesAfterTwoThirdsFramedrop) {
  // Also DownscalesAfterTwoThirdsIfFieldTrialEnabled: QualityScalingExperiment
  // is on by default and counts drops by the encoder too.
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] {
                   for (int i = 0; i < 150; ++i) {
                     q.OnFrameDropped();
                     q.OnFrameDropped();
                     q.OnEncodedFrame(40, Ms(0));
                   }
                 }) == QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2DoesNotDownscaleAfterHalfFramedrop) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] {
                   for (int i = 0; i < 150; ++i) {
                     q.OnFrameDropped();
                     q.OnEncodedFrame(40, Ms(0));
                   }
                 }) == QualityScaler::Signal::kNone);
}

SM_TEST(QualityScaler, V2KeepsScaleOnNormalQp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnEncodedFrame(16, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
}

SM_TEST(QualityScaler, V2ScalesDownAndBackUp) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 150; ++i) q.OnFrameDropped(); }) ==
                 QualityScaler::Signal::kAdaptDown);
  for (int i = 0; i < 150; ++i) q.OnEncodedFrame(15, Ms(2000));
  // After the first adapt-down the period is 2 s x 2.5.
  SM_EXPECT_TRUE(q.Check(Ms(6999)) == QualityScaler::Signal::kNone);
  SM_EXPECT_TRUE(q.Check(Ms(7000)) == QualityScaler::Signal::kAdaptUp);
}

SM_TEST(QualityScaler, V2DoesNotScaleUntilEnoughFramesObserved) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 59; ++i) q.OnEncodedFrame(15, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
  q.OnEncodedFrame(15, Ms(0));  // The 60th frame.
  // Still in fast rampup: the next check is 2 s later.
  SM_EXPECT_TRUE(q.Check(Ms(3999)) == QualityScaler::Signal::kNone);
  SM_EXPECT_TRUE(q.Check(Ms(4000)) == QualityScaler::Signal::kAdaptUp);
  // Samples are cleared after a report.
  q.OnEncodedFrame(15, Ms(4000));
  SM_EXPECT_TRUE(q.Check(Ms(6000)) == QualityScaler::Signal::kNone);
  SM_EXPECT_EQ(q.average_qp(), 15.0);
}

SM_TEST(QualityScaler, V2ScalesDownAndBackUpWithMinFramesNeeded) {
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 60; ++i) q.OnEncodedFrame(41, Ms(0)); }) ==
                 QualityScaler::Signal::kAdaptDown);
  for (int i = 0; i < 60; ++i) q.OnEncodedFrame(15, Ms(2000));
  SM_EXPECT_TRUE(q.Check(Ms(7000)) == QualityScaler::Signal::kAdaptUp);
}

SM_TEST(QualityScaler, V2HalfPeriodWhileTooFewFramesAfterRampup) {
  // CheckQpTask::GetCheckingQpDelayMs: with QualityScalingExperiment on, a
  // check that saw too few frames is followed by one after 1 s, not 5 s.
  auto q = V2Scaler();
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 60; ++i) q.OnEncodedFrame(41, Ms(0)); }) ==
                 QualityScaler::Signal::kAdaptDown);
  SM_EXPECT_TRUE(q.Check(Ms(7000)) == QualityScaler::Signal::kNone);  // No frames since.
  for (int i = 0; i < 60; ++i) q.OnEncodedFrame(41, Ms(7000));
  SM_EXPECT_TRUE(q.Check(Ms(7999)) == QualityScaler::Signal::kNone);
  SM_EXPECT_TRUE(q.Check(Ms(8000)) == QualityScaler::Signal::kAdaptDown);
}

SM_TEST(QualityScaler, V2SmoothsQpOverTime) {
  // QpSmoother: alpha 0.9995 per ms for the high check. 40 for 10 s at 30 fps,
  // then 18: the high filter is still above 37 a few frames into the 18s.
  QualityScalerConfig cfg;
  cfg.version = 2;  // H.264 24/37.
  QualityScaler q(cfg);
  q.Check(Ms(0));
  int64_t t = 0;
  for (; t < 10000; t += 33) q.OnEncodedFrame(40, Ms(t));
  SM_EXPECT_NEAR(q.average_qp(), 40.0, 1e-3);
  q.OnEncodedFrame(18, Ms(t));
  // 0.9995^33 = 0.98363: 40 - 22 x (1 - 0.98363) = 39.64.
  SM_EXPECT_NEAR(q.average_qp(), 39.64, 0.01);
}

SM_TEST(QualityScaler, V2RecordsTheChecksLibwebrtcLogs) {
  // Issue #347: the checks QualityScaler::CheckQp logs, with the values it
  // logs ("Checking average QP H (L)." or "Reporting high QP, framedrop
  // percent N"); a check with too few frames logs nothing.
  auto q = V2Scaler();
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(0));
  SM_EXPECT_TRUE(FirstCheck(q, [&] { for (int i = 0; i < 59; ++i) q.OnEncodedFrame(20, Ms(0)); }) ==
                 QualityScaler::Signal::kNone);
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(0));
  SM_EXPECT_EQ(q.last_check().qp_high, -1);
  q.OnEncodedFrame(20, Ms(2000));
  SM_EXPECT_TRUE(q.Check(Ms(4000)) == QualityScaler::Signal::kNone);  // 20 is between 15 and 40.
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(1));
  SM_EXPECT_EQ(q.last_check().qp_high, 20);
  SM_EXPECT_EQ(q.last_check().qp_low, 20);
  SM_EXPECT_EQ(q.last_check().drop_pct, -1);
  SM_EXPECT_TRUE(q.last_check().signal == QualityScaler::Signal::kNone);
  for (int i = 0; i < 150; ++i) q.OnFrameDropped();
  SM_EXPECT_TRUE(q.Check(Ms(6000)) == QualityScaler::Signal::kAdaptDown);
  SM_EXPECT_EQ(q.checks(), static_cast<uint64_t>(2));
  SM_EXPECT_EQ(q.last_check().qp_high, -1);
  SM_EXPECT_EQ(q.last_check().drop_pct, 100);
  SM_EXPECT_TRUE(q.last_check().signal == QualityScaler::Signal::kAdaptDown);
  for (int i = 0; i < 60; ++i) q.OnEncodedFrame(15, Ms(6000));
  SM_EXPECT_TRUE(q.Check(Ms(11000)) == QualityScaler::Signal::kAdaptUp);
  SM_EXPECT_EQ(q.last_check().qp_low, 15);
  SM_EXPECT_TRUE(q.last_check().signal == QualityScaler::Signal::kAdaptUp);
}

// Ports of call/adaptation/video_stream_adapter_unittest.cc (MAINTAIN_FRAMERATE)
// and the VideoAdapter scale series (api/video/video_adapter.cc), issue #84.
namespace {
// FakeVideoStream::ApplyAdaptation: the input follows the target, else the maximum.
void FollowRestrictions(VideoStreamAdapter& a, int& input_pixels) {
  const auto& r = a.restrictions();
  if (r.target_pixels_per_frame) {
    input_pixels = *r.target_pixels_per_frame;
  } else if (r.max_pixels_per_frame) {
    input_pixels = *r.max_pixels_per_frame;
  }
  a.OnFrameSizeObserved(input_pixels);
}
}  // namespace

SM_TEST(VideoStreamAdapter, DecreasesPixelsToThreeFifthsAndStopsAtTheMinimum) {
  VideoStreamAdapter a;
  SM_EXPECT_TRUE(a.restrictions() == VideoSourceRestrictions{});
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kInsufficientInput);
  a.OnFrameSizeObserved(1280 * 720);
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_EQ(a.restrictions().max_pixels_per_frame.value_or(0), 1280 * 720 * 3 / 5);
  SM_EXPECT_FALSE(a.restrictions().target_pixels_per_frame.has_value());
  SM_EXPECT_EQ(a.resolution_adaptations(), 1);
  // MaintainFramerate_DecreasesPixelsToLimitReached: 3/5 would go below the minimum.
  VideoStreamAdapter limited(640 * 480);
  limited.OnFrameSizeObserved(640 * 480 + 1);
  SM_EXPECT_TRUE(limited.AdaptDown() == VideoStreamAdapter::Status::kLimitReached);
}

SM_TEST(VideoStreamAdapter, IncreasesPixelsToFiveThirdsThenUnrestricted) {
  VideoStreamAdapter a;
  int input = 1280 * 720;
  a.OnFrameSizeObserved(input);
  // Unrestricted: nothing to undo.
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kLimitReached);
  a.AdaptDown();
  FollowRestrictions(a, input);
  a.AdaptDown();
  FollowRestrictions(a, input);
  SM_EXPECT_EQ(a.resolution_adaptations(), 2);
  const int target = input * 5 / 3;
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_EQ(a.restrictions().max_pixels_per_frame.value_or(0), target * 12 / 5);
  SM_EXPECT_EQ(a.restrictions().target_pixels_per_frame.value_or(0), target);
  SM_EXPECT_EQ(a.resolution_adaptations(), 1);
  FollowRestrictions(a, input);
  // Undoing the last step removes the restrictions.
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_TRUE(a.restrictions() == VideoSourceRestrictions{});
  SM_EXPECT_EQ(a.resolution_adaptations(), 0);
}

SM_TEST(VideoStreamAdapter, AwaitsThePreviousAdaptation) {
  VideoStreamAdapter a;
  a.OnFrameSizeObserved(1280 * 720);
  a.AdaptDown();
  // The input has not changed size yet: no second step down.
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kAwaitingPreviousAdaptation);
  a.OnFrameSizeObserved(960 * 540);
  SM_EXPECT_TRUE(a.AdaptDown() == VideoStreamAdapter::Status::kValid);
  a.OnFrameSizeObserved(640 * 360);
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kValid);
  SM_EXPECT_TRUE(a.AdaptUp() == VideoStreamAdapter::Status::kAwaitingPreviousAdaptation);
}

SM_TEST(VideoAdapter, ScalesThroughLibwebrtcsSeries) {
  // 1280x720: 3/4 -> 960x540, 1/2 -> 640x360, 3/8 -> 480x270, 1/4 -> 320x180.
  VideoAdapter v;
  int w = 0, h = 0;
  SM_EXPECT_TRUE(v.AdaptFrameResolution(1280, 720, &w, &h));
  SM_EXPECT_EQ(w, 1280);
  SM_EXPECT_EQ(h, 720);
  const int expected[][2] = {{960, 540}, {640, 360}, {480, 270}, {320, 180}};
  int pixels = 1280 * 720;
  for (const auto& e : expected) {
    pixels = GetLowerResolutionThan(pixels);
    v.OnSinkWants({pixels, std::nullopt});
    v.AdaptFrameResolution(1280, 720, &w, &h);
    SM_EXPECT_EQ(w, e[0]);
    SM_EXPECT_EQ(h, e[1]);
    pixels = w * h;
  }
  // Going up: target 5/3 of 320x180, maximum 12/5 of that -> 480x270 is closest.
  const int target = GetHigherResolutionThan(320 * 180);
  v.OnSinkWants({target * 12 / 5, target});
  v.AdaptFrameResolution(1280, 720, &w, &h);
  SM_EXPECT_EQ(w, 480);
  SM_EXPECT_EQ(h, 270);
  // 960x540 divides by 3, so the series starts with 2/3: 3/5 of it -> 640x360.
  v.OnSinkWants({GetLowerResolutionThan(960 * 540), std::nullopt});
  v.AdaptFrameResolution(960, 540, &w, &h);
  SM_EXPECT_EQ(w, 640);
  SM_EXPECT_EQ(h, 360);
}
