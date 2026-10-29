#include "sm/cc/bitrate_allocator.h"
#include "sm/cc/frame_dropper.h"
#include "sm/cc/outage_monitor.h"
#include "sm/cc/video_adaptation.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::cc;

namespace {
DataRate Kbps(int64_t k) { return DataRate::KilobitsPerSec(k); }
}

SM_TEST(BitrateAllocator, MinimumsPrioritiesAndCaps) {
  BitrateAllocator a;
  a.AddStream({"audio", Kbps(20), Kbps(40), 1.0, true});
  a.AddStream({"video", Kbps(100), Kbps(2000), 1.0, false});
  a.AddStream({"screen", Kbps(200), Kbps(500), 2.0, false});
  auto r = a.Allocate(Kbps(1000));
  SM_EXPECT_EQ(r["audio"].kbps(), 40.0);   // Capped at max.
  SM_EXPECT_EQ(r["screen"].kbps(), 500.0);  // Higher priority, capped.
  SM_EXPECT_NEAR(r["video"].kbps(), 460, 1);  // Gets the rest.
  double sum = r["audio"].kbps() + r["video"].kbps() + r["screen"].kbps();
  SM_EXPECT_NEAR(sum, 1000, 1);
}

SM_TEST(BitrateAllocator, PausesLowPriorityAndResumesWithHysteresis) {
  BitrateAllocator a(0.1);
  a.AddStream({"audio", Kbps(20), Kbps(40), 1.0, true});
  a.AddStream({"video", Kbps(100), Kbps(2000), 1.0, false});
  a.AddStream({"screen", Kbps(200), Kbps(500), 2.0, false});
  // 250: audio 20 (enforced) + screen 200 (priority 2) leave 30, too little
  // for video's 100 -> paused. The 30 is shared 1:2 by priority.
  auto r = a.Allocate(Kbps(250));
  SM_EXPECT_EQ(r["audio"].kbps(), 30.0);
  SM_EXPECT_EQ(r["screen"].kbps(), 220.0);
  SM_EXPECT_EQ(r["video"].kbps(), 0.0);
  // Enough for video's min, but not min + 10%: stays paused.
  r = a.Allocate(Kbps(325));
  SM_EXPECT_EQ(r["video"].kbps(), 0.0);
  r = a.Allocate(Kbps(340));
  SM_EXPECT_GE(r["video"].kbps(), 100.0);
  SM_EXPECT_EQ(CollectMetrics(a)["resumes"], 1.0);
  // Audio is enforced even with no bandwidth.
  r = a.Allocate(Kbps(5));
  SM_EXPECT_EQ(r["audio"].kbps(), 20.0);
}

SM_TEST(FrameDropper, OnTargetNoDropsOvershootDrops) {
  FrameDropper ok, hot;
  ok.SetRates(Kbps(1000), 30);
  hot.SetRates(Kbps(1000), 30);
  int ok_drops = 0, hot_drops = 0;
  for (int i = 0; i < 600; ++i) {
    if (ok.ShouldDrop(30)) ++ok_drops; else ok.OnEncodedFrame(DataSize::Bytes(4166), false);
    if (hot.ShouldDrop(30)) ++hot_drops; else hot.OnEncodedFrame(DataSize::Bytes(8333), false);
  }
  SM_EXPECT_EQ(ok_drops, 0);
  // 2x overshoot: about half the frames must go.
  SM_EXPECT_NEAR(hot_drops / 600.0, 0.5, 0.05);
}

SM_TEST(FrameDropper, KeyframeSpreadAvoidsDropBurst) {
  FrameDropper d;
  d.SetRates(Kbps(1000), 30);
  d.OnEncodedFrame(DataSize::Bytes(4166 * 8), true);  // Big keyframe.
  int drops = 0;
  for (int i = 0; i < 30; ++i) {
    if (d.ShouldDrop(30)) ++drops; else d.OnEncodedFrame(DataSize::Bytes(4000), false);
  }
  SM_EXPECT_LE(drops, 2);
}

// Keyframes skip ShouldDrop(30) but their bytes enter the bucket. A run of
// keyframe requests used to leave it far above its limit, so every delta frame
// was dropped for seconds (issue #53); the bucket is capped at 1.5 s of target.
SM_TEST(FrameDropper, KeyframeRunDoesNotStallDeltaFramesForSeconds) {
  FrameDropper d;
  const DataRate target = DataRate::KilobitsPerSec(400);
  d.SetRates(target, 30);
  const int64_t per_frame = 400000 / 8 / 30;
  for (int i = 0; i < 40; ++i) d.OnEncodedFrame(DataSize::Bytes(per_frame * 4), true);  // 40 forced keyframes.
  int dropped = 0;
  for (int i = 0; i < 300; ++i) {
    d.SetRates(target, 30);
    if (d.ShouldDrop(30)) {
      ++dropped;
    } else {
      d.OnEncodedFrame(DataSize::Bytes(per_frame), false);
    }
  }
  // Recovers within the 3 s cap (90 frames); uncapped it takes over 150 frames.
  SM_EXPECT_LT(dropped, 90);
}

SM_TEST(VideoAdaptation, LaddersFollowPreference) {
  DegradationConfig fr;
  fr.preference = DegradationPreference::kMaintainFramerate;
  DegradationController a(fr);
  for (const auto& f : a.ladder()) SM_EXPECT_EQ(f.fps, 30.0);
  SM_EXPECT_LT(a.ladder().back().pixels(), a.ladder().front().pixels());
  DegradationConfig rs;
  rs.preference = DegradationPreference::kMaintainResolution;
  DegradationController b(rs);
  for (const auto& f : b.ladder()) SM_EXPECT_EQ(f.width, 1280);
  SM_EXPECT_EQ(b.ladder().back().fps, 5.0);
  DegradationConfig bal;
  bal.preference = DegradationPreference::kBalanced;
  DegradationController c(bal);  // Balanced: fps first.
  SM_EXPECT_EQ(c.ladder()[1].width, 1280);
  SM_EXPECT_LT(c.ladder()[1].fps, 30.0);
}

// libwebrtc's camera default is MAINTAIN_FRAMERATE: BALANCED only with the
// WebRTC-Video-BalancedDegradation trial (webrtc_video_engine.cc:2032-2064).
SM_TEST(VideoAdaptation, DefaultPreferenceIsMaintainFramerate) {
  SM_EXPECT_TRUE(DegradationConfig{}.preference == DegradationPreference::kMaintainFramerate);
  DegradationController d;
  for (const auto& f : d.ladder()) SM_EXPECT_EQ(f.fps, 30.0);
  SM_EXPECT_LT(d.ladder().back().pixels(), d.ladder().front().pixels());
}

// Issue #643: an unknown (0) or negative input frame rate hung the frame-rate
// ladder. libwebrtc never adapts the frame rate of such input
// (VideoStreamAdapter::HasSufficientInputForAdaptation needs >= kMinFrameRateFps
// for MAINTAIN_RESOLUTION) and never steps below kMinFrameRateFps (2).
SM_TEST(VideoAdaptation, NonPositiveInputFpsBuildsLadderForEveryPreference) {
  for (double fps : {0.0, -1.0, -30.0, 1.0}) {
    for (auto pref : {DegradationPreference::kDisabled, DegradationPreference::kMaintainFramerate,
                      DegradationPreference::kMaintainResolution, DegradationPreference::kBalanced}) {
      for (double min_fps : {5.0, 0.0, -1.0}) {
        DegradationConfig c;
        c.input = {1280, 720, fps};
        c.preference = pref;
        c.min_fps = min_fps;
        DegradationController d(c);
        SM_EXPECT_GE(d.ladder().size(), size_t{1});
        SM_EXPECT_LT(d.ladder().size(), size_t{64});
        // No frame-rate step raises the input rate.
        for (const auto& f : d.ladder()) SM_EXPECT_LE(f.fps, fps);
        d.OnTargetBitrate(DataRate::KilobitsPerSec(50), Timestamp::Millis(0));
        d.OnResourceOveruse();
      }
    }
  }
  // A valid input keeps libwebrtc's 2 fps floor even when min_fps is lower.
  DegradationConfig c;
  c.preference = DegradationPreference::kMaintainResolution;
  c.min_fps = 0;
  DegradationController d(c);
  SM_EXPECT_GE(d.ladder().back().fps, 2.0);
}

SM_TEST(VideoAdaptation, BitrateDrivesFormatWithHysteresis) {
  DegradationConfig cfg;
  cfg.preference = DegradationPreference::kMaintainFramerate;
  DegradationController a(cfg);
  // 720p30 needs 1280*720*30*0.04 = 1.1 Mbps.
  SM_EXPECT_EQ(a.OnTargetBitrate(Kbps(2000)).width, 1280);
  int w = a.OnTargetBitrate(Kbps(500)).width;
  SM_EXPECT_LT(w, 1280);
  // Slightly more than the down threshold: no immediate bounce back up.
  SM_EXPECT_EQ(a.OnTargetBitrate(Kbps(1200)).width, a.ladder()[1].width);
  SM_EXPECT_EQ(a.OnTargetBitrate(Kbps(1700)).width, 1280);
}

SM_TEST(VideoAdaptation, CpuOveruseSignalsAndResourceCap) {
  CpuOveruseConfig v1;
  v1.version = 1;  // The library's own filter; the default is libwebrtc's (#579).
  CpuOveruseDetector cpu(v1);
  for (int i = 0; i < 100; ++i) cpu.OnFrameEncoded(TimeDelta::Millis(30), TimeDelta::Millis(33));
  SM_EXPECT_TRUE(cpu.Check(Timestamp::Seconds(5)) == CpuOveruseDetector::Signal::kNone);  // 1st check.
  SM_EXPECT_TRUE(cpu.Check(Timestamp::Seconds(10)) == CpuOveruseDetector::Signal::kOveruse);
  for (int i = 0; i < 100; ++i) cpu.OnFrameEncoded(TimeDelta::Millis(8), TimeDelta::Millis(33));
  SM_EXPECT_TRUE(cpu.Check(Timestamp::Seconds(15)) == CpuOveruseDetector::Signal::kUnderuse);
  DegradationController d;
  d.OnTargetBitrate(Kbps(5000));
  auto before = d.current();
  d.OnResourceOveruse();
  SM_EXPECT_FALSE(d.current() == before);
  d.OnResourceUnderuse();
  SM_EXPECT_TRUE(d.current() == before);
}

SM_TEST(Outage, StatesAndRecoveryActions) {
  OutageMonitor m;
  m.SetCurrentRate(Kbps(1000));
  m.OnFeedback(Timestamp::Millis(0));
  SM_EXPECT_FALSE(m.Process(Timestamp::Millis(500)).pause_encoder);
  m.Process(Timestamp::Millis(700));
  SM_EXPECT_TRUE(m.state() == LinkState::kDegraded);
  SM_EXPECT_TRUE(m.Process(Timestamp::Millis(2100)).pause_encoder);
  SM_EXPECT_TRUE(m.state() == LinkState::kDown);
  SM_EXPECT_TRUE(m.Process(Timestamp::Millis(5100)).ice_restart);
  SM_EXPECT_FALSE(m.Process(Timestamp::Millis(5200)).ice_restart);  // Once.
  m.OnFeedback(Timestamp::Millis(6000));
  auto a = m.Process(Timestamp::Millis(6010));
  SM_EXPECT_TRUE(a.resume_encoder);
  SM_EXPECT_TRUE(a.request_keyframe);
  SM_EXPECT_TRUE(a.reset_estimate);
  SM_EXPECT_EQ(a.reset_rate.kbps(), 500.0);
  SM_EXPECT_NEAR(CollectMetrics(m)["outage_ms.max"], 6000, 600);
}

// libwebrtc frame_dropper_unittest.cc: on a rate drop the accumulator is
// rescaled by new/old target, so a stale bucket does not drop frames for
// seconds (issue #5: the target fell 853 -> 187 kbps within 2 s and the
// pre-port dropper kept dropping every frame for 1.4 s).
SM_TEST(FrameDropper, RateDropRescalesAccumulatorSoDropsStayShort) {
  FrameDropper d;
  d.SetRates(Kbps(850), 30);
  // Run hot (about 1.4x target) for 4 s so the bucket is full.
  for (int i = 0; i < 120; ++i) {
    if (!d.ShouldDrop(30)) d.OnEncodedFrame(DataSize::Bytes(850000 / 8 / 30 * 1.4), false);
  }
  d.SetRates(Kbps(187), 30);
  int run = 0, longest = 0;
  for (int i = 0; i < 90; ++i) {
    if (d.ShouldDrop(30)) {
      longest = std::max(longest, ++run);
    } else {
      run = 0;
      d.OnEncodedFrame(DataSize::Bytes(187000 / 8 / 30), false);
    }
  }
  // Pre-port: every frame for ~42 frames. Ported: the rescaled bucket is back
  // under its limit within a few frames.
  SM_EXPECT_LT(longest, 15);
}

// Keyframes are spread over the next frames (large_frame_accumulation) rather
// than added at once, and the first keyframe of a call does not trigger drops.
SM_TEST(FrameDropper, DefaultsMatchLibwebrtc) {
  FrameDropper d;  // Target 300 kbps, 30 fps, bucket 150 kbit.
  int drops = 0;
  for (int i = 0; i < 90; ++i) {
    if (d.ShouldDrop(30)) ++drops; else d.OnEncodedFrame(DataSize::Bytes(300000 / 8 / 30), i == 0);
  }
  SM_EXPECT_EQ(drops, 0);
  SM_EXPECT_NEAR(d.bucket_bytes(), 0.0, 3000.0);
}
