// Section 7 end-to-end tests: each algorithm inside a sender -> network ->
// receiver loop (with the loss and timing sections where relevant).
#include <cmath>
#include <map>

#include "sm/core/random.h"
#include "sm/core/rtp_packet.h"
#include "sm/loss/keyframe_request.h"
#include "sm/sim/network_link.h"
#include "sm/timing/video_jitter_estimator.h"
#include "sm/video/codec_fallback.h"
#include "sm/video/content_policy.h"
#include "sm/video/frame_cadence_adapter.h"
#include "sm/video/frame_scheduler.h"
#include "sm/video/freeze_detector.h"
#include "sm/video/orientation.h"
#include "sm/video/quality_convergence.h"
#include "sm/video/quality_scaler.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::video;

SM_TEST(E2E_FreezeDetector, MatchesGroundTruthGapsOverLossyLink) {
  // 30 fps stream, 1% frame loss, two 600 ms outages; the receiver renders
  // decodable frames and requests keyframes (PLI) when stuck.
  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(40);
  lc.jitter_stddev = TimeDelta::Millis(4);
  lc.allow_reordering = false;
  lc.loss_rate = 0.01;
  sim::NetworkLink<loss::FrameInfo> fwd(lc, 5);
  sim::NetworkLink<int> rev(sim::LinkConfig{}, 6);
  loss::KeyframeRequester req;
  FreezeDetector fd;
  std::vector<int64_t> render_ms;
  bool force_key = true;
  int64_t id = 0;
  for (int64_t t = 0; t < 60000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    bool outage = (t >= 20000 && t < 20600) || (t >= 40000 && t < 40600);
    if (outage != lc.down) {
      lc.down = outage;
      fwd.SetConfig(lc);
    }
    if (t % 33 == 0) {
      loss::FrameInfo f{id, force_key, {}};
      if (!force_key) f.references = {id - 1};
      force_key = false;
      ++id;
      fwd.Send(now, f, DataSize::Bytes(5000));
    }
    fwd.ReceiveEach(now, [&](loss::FrameInfo f, Timestamp at) {
      req.OnMediaPacket(at, f.keyframe);
      req.OnFrame(f, at);
    });
    for (auto fid : req.TakeDecodableFrames()) {
      (void)fid;
      req.OnFrameReleased(now);
      fd.OnFrameRendered(now);
      render_ms.push_back(t);
    }
    for (auto& m : req.Process(now)) {
      if (m.type != loss::FeedbackType::kLntf) rev.Send(now, 1, DataSize::Bytes(20));
    }
    rev.ReceiveEach(now, [&](int, Timestamp) { force_key = true; });
  }
  // Ground truth: gaps > 183 ms (= max(3 x 33, 33 + 150)) in the render log.
  int truth = 0;
  for (size_t i = 1; i < render_ms.size(); ++i) truth += render_ms[i] - render_ms[i - 1] > 183;
  SM_RECORD("freezes_detected", fd.stats().freeze_count);
  SM_RECORD("freezes_ground_truth", truth);
  SM_RECORD("total_freeze_ms", fd.stats().total_freezes_ms);
  SM_RECORD("harmonic_fps", fd.HarmonicFps());
  SM_EXPECT_GE(fd.stats().freeze_count, 2u);  // At least the two outages.
  SM_EXPECT_NEAR(static_cast<double>(fd.stats().freeze_count), truth, 1);
  SM_EXPECT_LT(fd.HarmonicFps(), 30.0);
}

namespace {
// A hardware encoder that asks for the software fallback (libwebrtc's
// WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE) once it starts failing, and a
// software encoder whose first frame after InitEncode is a keyframe.
struct ScriptedEncoder : FallbackEncoder {
  bool initialized = false, next_is_keyframe = false;
  DataRate rate = DataRate::Zero();
  EncoderStatus InitEncode(const EncoderCodecSettings&) override {
    initialized = next_is_keyframe = true;
    return EncoderStatus::kOk;
  }
  void Release() override { initialized = false; }
  void SetRates(const EncoderRates& r) override { rate = r.bitrate; }
  void OnRttUpdate(TimeDelta) override {}
  void OnPacketLossRateUpdate(float) override {}
  FallbackEncoderInfo GetEncoderInfo() const override { return {}; }
};
}  // namespace

SM_TEST(E2E_CodecFallback, HardwareFailureMidCallKeepsVideoFlowing) {
  // The hardware encoder fails at 10 s and errs on 30% of frames from then
  // on; every 10th of its errors is the fallback request, as a MediaCodec
  // wrapper reports a dead codec after a few transient errors.
  ScriptedEncoder sw, hw;
  CodecFallbackController ctl({}, sw, hw);
  SM_ASSERT_TRUE(ctl.InitEncode({1280, 720, false, 1}, Timestamp::Millis(0)) == EncoderStatus::kOk);
  ctl.SetRates({DataRate::KilobitsPerSec(1500), 30});
  Random rng(2);
  int64_t last_ok = 0, max_gap = 0;
  int hw_errors = 0, sw_keyframes = 0, frames_on_sw = 0;
  for (int64_t t = 0; t < 30000; t += 33) {
    Timestamp now = Timestamp::Millis(t);
    FallbackEncoder* enc = ctl.current();
    SM_ASSERT_TRUE(enc != nullptr);
    EncoderStatus st = EncoderStatus::kOk;
    if (enc == &hw && t >= 10000 && rng.Bernoulli(0.3))
      st = (++hw_errors % 10 == 0) ? EncoderStatus::kFallbackSoftware : EncoderStatus::kError;
    if (ctl.OnEncodeResult(st, now)) {
      enc = ctl.current();  // The same frame, encoded by the fallback.
      st = EncoderStatus::kOk;
    }
    if (enc == &sw && st == EncoderStatus::kOk) {
      ++frames_on_sw;
      if (sw.next_is_keyframe) ++sw_keyframes;
      sw.next_is_keyframe = false;
    }
    if (st == EncoderStatus::kOk) {
      max_gap = std::max(max_gap, t - last_ok);
      last_ok = t;
    }
  }
  SM_RECORD("fallbacks", ctl.stats().fallbacks);
  SM_RECORD("hardware_errors", hw_errors);
  SM_RECORD("max_encoded_gap_ms", max_gap);
  SM_EXPECT_EQ(ctl.stats().fallbacks, 1u);
  SM_EXPECT_TRUE(ctl.state() == EncoderState::kFallbackDueToFailure);
  SM_EXPECT_EQ(hw_errors, 10);  // Plain errors don't switch; the request does.
  SM_EXPECT_FALSE(hw.initialized);  // Released on the switch.
  SM_EXPECT_EQ(sw.rate.kbps(), 1500.0);  // Primed with the last rate.
  SM_EXPECT_EQ(sw_keyframes, 1);
  SM_EXPECT_GT(frames_on_sw, 0);
  SM_EXPECT_LE(max_gap, 200);
}

SM_TEST(E2E_Orientation, RotatingPhoneRendersUpright) {
  // The scene is upright; the sensor captures it rotated by the phone's
  // orientation. The sender signals CVO in a one-byte RTP header extension
  // (RFC 8285, id 3) and the receiver rotates for display.
  const uint8_t kCvoId = 3;
  I420Frame scene = MakeTestFrame(96, 64, 7);
  int frames = 0, exact = 0;
  for (int step = 0; step < 16; ++step) {
    VideoOrientation phone{static_cast<Rotation>(step % 4), (step / 4) % 2 == 1, step % 3 == 0};
    I420Frame sensor = ApplyOrientation(scene, Inverse(phone));
    RtpPacket p;
    p.extension_profile = 0xBEDE;
    p.extension = {static_cast<uint8_t>(kCvoId << 4), EncodeCvo(phone), 0, 0};
    p.payload = sensor.y;  // Stand-in for the encoded frame.
    auto wire = p.Serialize();
    auto rx = RtpPacket::Parse(wire);
    SM_ASSERT_TRUE(rx.has_value());
    SM_ASSERT_EQ(rx->extension[0] >> 4, kCvoId);
    VideoOrientation got = DecodeCvo(rx->extension[1]);
    I420Frame shown = ApplyOrientation(sensor, got);
    ++frames;
    exact += shown == scene;
  }
  SM_RECORD("upright_frames_ratio", static_cast<double>(exact) / frames);
  SM_EXPECT_EQ(exact, frames);
}

SM_TEST(E2E_ContentPolicy, ScreenShareZeroHertzRepeatsAsLibwebrtc) {
  // 60 s of screen sharing, max 15 fps, VP8:
  // a new slide every 10 s and a cursor blinking at 3.75 Hz for 2 s of every
  // 10 s. The capturer delivers a frame only when the screen changes (min fps
  // 0), as a zero-hertz source; FrameCadenceAdapter repeats the last frame
  // while the screen is static, and the encoder's QP (a model: 40 on a new
  // frame, 4 lower on each refresh) feeds QualityConvergenceController as
  // VideoStreamEncoder does (video_stream_encoder.cc:2316-2325).
  const double fps = 15;
  FrameCadenceAdapter a;
  QualityConvergenceController quality;
  quality.Initialize(1, std::nullopt, VideoCodecType::kVP8);
  a.SetZeroHertzModeEnabled(FrameCadenceAdapter::ZeroHertzModeParams{1}, Timestamp::Millis(0));
  a.OnConstraintsChanged({0.0, fps}, Timestamp::Millis(0));
  SM_ASSERT_TRUE(a.zero_hertz());

  const int64_t period_us = 66667;  // 1 s / 15, rounded as unit_base.h.
  std::vector<int64_t> change_us, encode_us;
  std::vector<bool> encode_repeat;
  int qp = 0;
  auto run_until = [&](Timestamp until) {
    for (Timestamp next = a.NextProcessTime(); next.IsFinite() && next <= until; next = a.NextProcessTime()) {
      for (const auto& o : a.Process(next)) {
        if (o.kind != FrameCadenceAdapter::Output::Kind::kEncode) continue;
        encode_us.push_back(next.us());
        encode_repeat.push_back(o.is_repeat);
        qp = o.is_repeat ? std::max(qp - 4, 0) : 40;
        a.UpdateLayerQualityConvergence(0, quality.AddSampleAndCheckTargetQuality(0, qp, o.is_repeat));
      }
    }
  };
  I420Frame prev;
  for (int i = 0; i < static_cast<int>(60 * fps); ++i) {
    int64_t t_ms = static_cast<int64_t>(i) * 1000 / static_cast<int64_t>(fps);
    I420Frame f = MakeTestFrame(320, 180, static_cast<int>(t_ms / 10000) * 50);
    bool blink = (t_ms % 10000) > 3000 && (t_ms % 10000) < 5000 && (i % 4 < 2);
    if (blink) {
      for (int y = 40; y < 52; ++y) f.Y(200, y) = 255;
    }
    run_until(Timestamp::Millis(t_ms));
    if (!(f == prev)) {  // The capturer only delivers changes.
      a.OnFrame(i, Timestamp::Millis(t_ms), Timestamp::Millis(t_ms));
      change_us.push_back(t_ms * 1000);
      run_until(Timestamp::Millis(t_ms));
    }
    prev = f;
  }
  run_until(Timestamp::Seconds(60));

  // Every change is encoded one frame period after it arrives.
  size_t originals = 0;
  for (size_t k = 0; k < encode_us.size(); ++k) {
    if (encode_repeat[k]) continue;
    SM_ASSERT_TRUE(originals < change_us.size());
    SM_EXPECT_EQ(encode_us[k] - change_us[originals], period_us);
    ++originals;
  }
  SM_EXPECT_EQ(originals, change_us.size());
  // Static screen: repeats at the frame period until QP reaches VP8's static
  // threshold (40 -> 16 -> 12 after 7 refreshes), then one per second.
  // A gap is a frame period (66 667 us) or the idle period, never longer.
  int64_t max_gap = 0, idle_gaps = 0;
  for (size_t k = 1; k < encode_us.size(); ++k) {
    int64_t gap = encode_us[k] - encode_us[k - 1];
    max_gap = std::max(max_gap, gap);
    idle_gaps += gap == 1000000;
  }
  SM_EXPECT_EQ(max_gap, 1000000);  // kZeroHertzIdleRepeatRatePeriod.
  // Per 10 s: 2 idle repeats before the cursor blinks (from 0.53 s) and 4
  // after it stops (from 5.53 s).
  SM_EXPECT_EQ(idle_gaps, 36);
  // While the cursor blinks every 133 ms, a repeat fills each gap: 15 fps.
  const double encode_ratio = static_cast<double>(encode_us.size()) / (60 * fps);
  SM_RECORD("screen_encode_ratio", encode_ratio);
  SM_RECORD("content_changes", static_cast<double>(change_us.size()));
}

SM_TEST(E2E_FrameScheduler, SmoothPlayoutOverJitteryNetwork) {
  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(40);
  lc.jitter_stddev = TimeDelta::Millis(15);
  lc.allow_reordering = false;
  sim::NetworkLink<std::pair<int64_t, uint32_t>> link(lc, 9);
  timing::VideoJitterEstimator jit;
  timing::VideoTiming vt;
  FrameScheduler sched;
  FreezeDetector smooth, naive;
  std::vector<double> sched_iv, naive_iv;
  int64_t last_s = -1, last_n = -1;
  for (int64_t t = 0; t < 60000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t % 33 == 0) {
      int64_t id = t / 33;
      link.Send(now, {id, static_cast<uint32_t>(id * 3000)}, DataSize::Bytes(4000));
    }
    link.ReceiveEach(now, [&](std::pair<int64_t, uint32_t> f, Timestamp at) {
      jit.OnFrame(f.second, at, DataSize::Bytes(4000));
      vt.IncomingTimestamp(f.second, at);
      vt.SetJitterDelay(jit.JitterDelay());
      vt.UpdateCurrentDelay(at);
      sched.Insert({f.first, vt.RenderTime(f.second, at)});
      naive.OnFrameRendered(at);  // Render on arrival.
      if (last_n >= 0 && t > 5000) naive_iv.push_back(static_cast<double>(t - last_n));
      last_n = t;
    });
    for (const auto& a : sched.Process(now)) {
      if (!a.render) continue;
      smooth.OnFrameRendered(now);
      if (last_s >= 0 && t > 5000) sched_iv.push_back(static_cast<double>(t - last_s));
      last_s = t;
    }
  }
  auto stdev = [](const std::vector<double>& v) {
    double m = 0, s = 0;
    for (double x : v) m += x;
    m /= static_cast<double>(v.size());
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size()));
  };
  double drop = static_cast<double>(sched.stats().dropped_late) / static_cast<double>(sched.stats().frames_in);
  SM_RECORD("render_interval_stdev_scheduled_ms", stdev(sched_iv));
  SM_RECORD("render_interval_stdev_on_arrival_ms", stdev(naive_iv));
  SM_RECORD("dropped_late_ratio", drop);
  SM_RECORD("freezes_scheduled", smooth.stats().freeze_count);
  SM_RECORD("freezes_on_arrival", naive.stats().freeze_count);
  SM_EXPECT_LT(stdev(sched_iv), 3.0);
  SM_EXPECT_GT(stdev(naive_iv), 10.0);
  SM_EXPECT_LT(drop, 0.02);
  SM_EXPECT_LE(smooth.stats().freeze_count, naive.stats().freeze_count);
}

namespace {

// Encoder model: QP rises ~6 per halving of bits per pixel (H.264-like).
// Bandwidth 2500 -> 300 -> 2500 kbps for 30 s each; resolution follows the
// scaler's signals down and up a 5-step ladder.
void QualityScalerLadder(int version) {
  const std::vector<std::pair<int, int>> ladder = {{1280, 720}, {960, 540}, {640, 360}, {480, 270}, {320, 180}};
  QualityScalerConfig cfg;
  cfg.version = version;
  QualityScaler qs(cfg);
  size_t step = 0;
  std::map<int, int> width_at;  // Phase -> width at end.
  std::map<int, int> qp_at;     // Phase -> QP at end.
  for (int64_t t = 0; t < 90000; t += 33) {
    double kbps = t < 30000 ? 2500 : (t < 60000 ? 300 : 2500);
    auto [w, h] = ladder[step];
    double bpp = kbps * 1000 / (static_cast<double>(w) * h * 30);
    int qp = static_cast<int>(std::lround(std::clamp(26 - 6 * std::log2(bpp / 0.1), 10.0, 51.0)));
    // Version 2 checks before the frame at `t`, as libwebrtc's CheckQpTask
    // due at `t` runs before that frame's ReportQp.
    if (version == 1) qs.OnEncodedFrame(qp);
    auto sig = qs.Check(Timestamp::Millis(t));
    if (version >= 2) qs.OnEncodedFrame(qp, Timestamp::Millis(t));
    if (sig == QualityScaler::Signal::kAdaptDown && step + 1 < ladder.size()) {
      ++step;
      qs.OnResolutionChanged();
    } else if (sig == QualityScaler::Signal::kAdaptUp && step > 0) {
      --step;
      qs.OnResolutionChanged();
    }
    width_at[static_cast<int>(t / 30000)] = ladder[step].first;
    qp_at[static_cast<int>(t / 30000)] = qp;
  }
  SM_RECORD("width_high_bw", width_at[0]);
  SM_RECORD("width_low_bw", width_at[1]);
  SM_RECORD("width_recovered", width_at[2]);
  SM_RECORD("qp_low_bw_end", qp_at[1]);
  auto th = QpThresholds::ForCodec("H264");
  SM_EXPECT_EQ(width_at[0], 1280);
  // At 300 kbps the resolution drops until QP is back inside the band.
  SM_EXPECT_LT(width_at[1], 1280);
  SM_EXPECT_LE(qp_at[1], th.high);
  SM_EXPECT_EQ(width_at[2], 1280);
}

}  // namespace

SM_TEST(E2E_QualityScaler, ResolutionTracksBitrateViaQp) { QualityScalerLadder(1); }

// Version 2, libwebrtc M157's QualityScaler (#84): the same ladder with
// libwebrtc's cadence (2 s, then 5 s after the first adapt-down), 60 frames
// and smoothed QP.
SM_TEST(E2E_QualityScaler, ResolutionTracksBitrateViaQpV2) { QualityScalerLadder(2); }
