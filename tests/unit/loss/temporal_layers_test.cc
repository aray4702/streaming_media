#include "sm/loss/temporal_layers.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

SM_TEST(TemporalLayers, L1T3Pattern) {
  TemporalLayerScheduler s(3);
  int expect[] = {0, 2, 1, 2, 0, 2, 1, 2};
  std::vector<LayerFrame> f;
  for (int i = 0; i < 8; ++i) f.push_back(s.NextFrame());
  for (int i = 0; i < 8; ++i) SM_EXPECT_EQ(f[i].temporal_id, expect[i]);
  SM_EXPECT_TRUE(f[0].keyframe);
  SM_EXPECT_EQ(f[4].info.references[0], 0);  // T0 refs previous T0.
  SM_EXPECT_EQ(f[2].info.references[0], 0);  // T1 refs T0.
  SM_EXPECT_EQ(f[3].info.references[0], 2);  // T2 refs T1.
  SM_EXPECT_EQ(f[5].info.references[0], 4);  // T2 refs new T0.
}

SM_TEST(TemporalLayers, NoFrameReferencesSameOrHigherLayer) {
  for (int n = 1; n <= 3; ++n) {
    TemporalLayerScheduler s(n);
    std::vector<int> tid_of;
    for (int i = 0; i < 100; ++i) {
      auto f = s.NextFrame(i == 50);
      tid_of.push_back(f.temporal_id);
      for (int64_t r : f.info.references) {
        int rt = tid_of[static_cast<size_t>(r)];
        SM_EXPECT_TRUE(rt < f.temporal_id || (rt == 0 && f.temporal_id == 0));
      }
    }
  }
}

SM_TEST(TemporalLayers, BitrateAndFramerateShares) {
  SM_EXPECT_EQ(TemporalLayerScheduler::CumulativeBitrateShare(3).back(), 1.0);
  SM_EXPECT_EQ(TemporalLayerScheduler::FramerateFraction(3, 0), 0.25);
  SM_EXPECT_EQ(TemporalLayerScheduler::FramerateFraction(2, 0), 0.5);
}

SM_TEST(TemporalLayers, FilterSwitchesUpOnlyAtSync) {
  TemporalLayerScheduler s(3);
  TemporalLayerFilter filt;
  filt.SetTargetLayer(0);
  int fwd = 0;
  for (int i = 0; i < 8; ++i) fwd += filt.ShouldForward(s.NextFrame());
  SM_EXPECT_EQ(fwd, 2);  // Only T0 at 1/4 rate.
  filt.SetTargetLayer(2);
  // Next frames: T0(8) T2(9, sync) T1(10) T2(11, refs T1)...
  auto f8 = s.NextFrame();
  SM_EXPECT_TRUE(filt.ShouldForward(f8));
  auto f9 = s.NextFrame();
  SM_EXPECT_TRUE(f9.layer_sync);
  SM_EXPECT_TRUE(filt.ShouldForward(f9));
  SM_EXPECT_EQ(filt.current_layer(), 2);
  filt.SetTargetLayer(1);
  SM_EXPECT_TRUE(filt.ShouldForward(s.NextFrame()));   // T1
  SM_EXPECT_FALSE(filt.ShouldForward(s.NextFrame()));  // T2 dropped
}
