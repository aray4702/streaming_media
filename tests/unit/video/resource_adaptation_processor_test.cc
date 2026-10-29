// video::ResourceAdaptationProcessor: libwebrtc M157's
// call/adaptation/resource_adaptation_processor_unittest.cc, ported (issue
// #646). The thread and task-queue tests (TriggerOveruseNotOnAdaptationTaskQueue,
// DestroyProcessorWhileResourceListenerDelegateHasTaskInFlight) have no
// counterpart: the processor runs inline. ResourceOveruseIgnoredWhenSignalledDuringRemoval
// becomes RemovedResourceSignalsIgnoredByProcessor's case.
#include "sm/video/resource_adaptation_processor.h"

#include "sm_test.h"

using namespace sm;
using namespace sm::video;

namespace {
constexpr int kDefaultFrameRate = 30;
constexpr int kDefaultFrameSize = 1280 * 720;

// The fixture: two resources, and the restrictions listener's count of
// broadcasts (a change in the filtered restrictions).
struct Fixture {
  static constexpr int kResource = 0, kOther = 1;
  VideoStreamAdapter adapter;
  ResourceAdaptationProcessor processor{adapter};
  size_t updated = 0;

  Fixture() {
    // libwebrtc's adapter starts at MAINTAIN_FRAMERATE_AND_RESOLUTION (ours at
    // the camera's MAINTAIN_FRAMERATE).
    adapter.SetDegradationPreference(DegradationPreference::kMaintainFramerateAndResolution);
    processor.AddResource(kResource);
    processor.AddResource(kOther);
  }
  void SetInputStates(int fps, int frame_size) {
    adapter.OnFrameRateObserved(fps);
    adapter.OnFrameSizeObserved(frame_size);
  }
  void RestrictSource() {
    const VideoSourceRestrictions r = adapter.filtered_restrictions();
    SetInputStates(r.max_frame_rate ? static_cast<int>(*r.max_frame_rate) : kDefaultFrameRate,
                   r.target_pixels_per_frame ? *r.target_pixels_per_frame
                                             : r.max_pixels_per_frame.value_or(kDefaultFrameSize));
  }
  template <typename F>
  void Track(F f) {
    const VideoSourceRestrictions before = adapter.filtered_restrictions();
    f();
    if (!(adapter.filtered_restrictions() == before)) ++updated;
  }
  void Signal(int resource, ResourceUsageState s) {
    Track([&] { processor.OnResourceUsageStateMeasured(resource, s); });
  }
  void Overuse(int resource) { Signal(resource, ResourceUsageState::kOveruse); }
  void Underuse(int resource) { Signal(resource, ResourceUsageState::kUnderuse); }
  void Remove(int resource) {
    Track([&] { processor.RemoveResource(resource); });
  }
  void SetPreference(DegradationPreference p) { processor.SetDegradationPreference(p); }
  int Total() const { return adapter.counters().Total(); }
};
}  // namespace

SM_TEST(ResourceAdaptationProcessor, DisabledByDefault) {
  Fixture f;
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 0u);
}

SM_TEST(ResourceAdaptationProcessor, InsufficientInput) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  // No frame size observed yet.
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 0u);
}

SM_TEST(ResourceAdaptationProcessor, OveruseTriggersRestrictingResolutionInMaintainFrameRate) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions().max_pixels_per_frame.has_value());
}

SM_TEST(ResourceAdaptationProcessor, OveruseTriggersRestrictingFrameRateInMaintainResolution) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainResolution);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions().max_frame_rate.has_value());
}

SM_TEST(ResourceAdaptationProcessor, OveruseTriggersRestrictingFrameRateAndResolutionInBalanced) {
  Fixture f;
  f.SetPreference(DegradationPreference::kBalanced);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  for (size_t i = 0; i < 4; ++i) {
    f.Overuse(f.kResource);
    SM_EXPECT_EQ(f.updated, i + 1);
    f.RestrictSource();
  }
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions().max_pixels_per_frame.has_value());
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions().max_frame_rate.has_value());
}

SM_TEST(ResourceAdaptationProcessor, AwaitingPreviousAdaptation) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
  // Without restricting the source the next step waits ("double-adapt").
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
}

SM_TEST(ResourceAdaptationProcessor, CannotAdaptUpWhenUnrestricted) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 0u);
}

SM_TEST(ResourceAdaptationProcessor, UnderuseTakesUsBackToUnrestricted) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
  f.RestrictSource();
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 2u);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions() == VideoSourceRestrictions());
}

SM_TEST(ResourceAdaptationProcessor, ResourcesCanNotAdaptUpIfNeverAdaptedDown) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
  f.RestrictSource();
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.updated, 1u);
}

SM_TEST(ResourceAdaptationProcessor, ResourcesCanNotAdaptUpIfNotAdaptedDownAfterReset) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.updated, 1u);
  f.processor.ClearRestrictions();
  SM_EXPECT_EQ(f.Total(), 0);
  f.Overuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), 1);
  f.RestrictSource();
  // kResource did not overuse after the reset: no step up.
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
}

SM_TEST(ResourceAdaptationProcessor, OnlyMostLimitedResourceMayAdaptUp) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
  f.RestrictSource();
  f.Overuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), 2);
  f.RestrictSource();
  // kOther is most limited, kResource can't adapt up.
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 2);
  f.RestrictSource();
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), 1);
  f.RestrictSource();
  // Both are most limited now, so both must signal underuse.
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), 1);
  f.RestrictSource();
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 0);
}

SM_TEST(ResourceAdaptationProcessor, MultipleResourcesCanTriggerMultipleAdaptations) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  const int r = f.kResource, o = f.kOther;
  auto step = [&](int res, bool over, int total) {
    over ? f.Overuse(res) : f.Underuse(res);
    SM_EXPECT_EQ(f.Total(), total);
    f.RestrictSource();
  };
  step(r, true, 1);
  step(o, true, 2);
  step(o, true, 3);
  step(r, false, 3);  // kResource is not most limited.
  step(o, false, 2);
  step(r, false, 2);  // Still not.
  step(r, true, 3);   // But it is after an overuse.
  step(o, false, 3);  // Now kOther is not most limited.
  step(r, false, 2);
  step(o, false, 2);  // Both most limited.
  step(r, false, 1);
  step(r, false, 1);  // Again both.
  step(o, false, 0);
}

SM_TEST(ResourceAdaptationProcessor, MostLimitedResourceAdaptationWorksAfterChangingDegradataionPreference) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  for (int i = 0; i < 5; ++i) {
    f.Overuse(f.kResource);
    f.RestrictSource();
  }
  const int last_total = f.Total();
  f.SetPreference(DegradationPreference::kMaintainResolution);
  // kResource can not adapt up since the frame rate was never reduced.
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), last_total);
  f.Overuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), last_total + 1);
  f.RestrictSource();
  // kOther is most limited so may adapt up.
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), last_total);
}

SM_TEST(ResourceAdaptationProcessor, AdaptsDownWhenOtherResourceIsAlwaysUnderused) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), 0);
  f.RestrictSource();
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
  f.RestrictSource();
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), 1);
}

SM_TEST(ResourceAdaptationProcessor, RemovingOnlyAdaptedResourceResetsAdaptation) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
  f.RestrictSource();
  f.Remove(f.kResource);
  SM_EXPECT_EQ(f.Total(), 0);
  SM_EXPECT_TRUE(f.adapter.restrictions() == VideoSourceRestrictions());
}

SM_TEST(ResourceAdaptationProcessor, RemovingMostLimitedResourceSetsAdaptationToNextLimitedLevel) {
  Fixture f;
  f.SetPreference(DegradationPreference::kBalanced);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kOther);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 1);
  const VideoSourceRestrictions next_restrictions = f.adapter.filtered_restrictions();
  const VideoAdaptationCounters next_counters = f.adapter.counters();
  f.Overuse(f.kResource);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 2);
  f.Remove(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions() == next_restrictions);
  SM_EXPECT_TRUE(f.adapter.counters() == next_counters);
}

SM_TEST(ResourceAdaptationProcessor, RemovingMostLimitedResourceSetsAdaptationIfInputStateUnchanged) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kOther);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 1);
  const VideoSourceRestrictions next_restrictions = f.adapter.filtered_restrictions();
  const VideoAdaptationCounters next_counters = f.adapter.counters();
  // Overuse twice and underuse once; the source is not restricted after the
  // underuse, which would block the next step in MAINTAIN_FRAMERATE.
  f.Overuse(f.kResource);
  f.RestrictSource();
  f.Overuse(f.kResource);
  f.RestrictSource();
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), 2);
  // Removal goes back to kOther's level anyway.
  f.Remove(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions() == next_restrictions);
  SM_EXPECT_TRUE(f.adapter.counters() == next_counters);
}

SM_TEST(ResourceAdaptationProcessor, RemovingResourceNotMostLimitedHasNoEffectOnLimitations) {
  Fixture f;
  f.SetPreference(DegradationPreference::kBalanced);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kOther);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 1);
  f.Overuse(f.kResource);
  f.RestrictSource();
  const VideoSourceRestrictions current_restrictions = f.adapter.filtered_restrictions();
  const VideoAdaptationCounters current_counters = f.adapter.counters();
  SM_EXPECT_EQ(f.Total(), 2);
  f.Remove(f.kOther);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions() == current_restrictions);
  SM_EXPECT_TRUE(f.adapter.counters() == current_counters);
}

SM_TEST(ResourceAdaptationProcessor, RemovingMostLimitedResourceAfterSwitchingDegradationPreferences) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kOther);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 1);
  const VideoSourceRestrictions next_restrictions = f.adapter.filtered_restrictions();
  const VideoAdaptationCounters next_counters = f.adapter.counters();
  f.SetPreference(DegradationPreference::kMaintainResolution);
  f.Overuse(f.kResource);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 2);
  // Back to kOther's level even though the preference differed at the overuse.
  f.Remove(f.kResource);
  SM_EXPECT_TRUE(f.adapter.counters() == next_counters);
  // Back in MAINTAIN_FRAMERATE, kOther's restrictions are restored.
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions() == next_restrictions);
}

SM_TEST(ResourceAdaptationProcessor, RemovingMostLimitedResourceSetsNextLimitationsInDisabled) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kOther);
  f.RestrictSource();
  const VideoSourceRestrictions next_restrictions = f.adapter.filtered_restrictions();
  const VideoAdaptationCounters next_counters = f.adapter.counters();
  SM_EXPECT_EQ(f.Total(), 1);
  f.Overuse(f.kResource);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 2);
  f.SetPreference(DegradationPreference::kMaintainFramerateAndResolution);
  // Back to kOther's level even though adaptation is disabled.
  f.Remove(f.kResource);
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  SM_EXPECT_TRUE(f.adapter.filtered_restrictions() == next_restrictions);
  SM_EXPECT_TRUE(f.adapter.counters() == next_counters);
}

SM_TEST(ResourceAdaptationProcessor, RemovedResourceSignalsIgnoredByProcessor) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Remove(f.kResource);
  SM_EXPECT_FALSE(f.processor.HasResource(f.kResource));
  SM_EXPECT_TRUE(f.processor.OnResourceUsageStateMeasured(f.kResource, ResourceUsageState::kOveruse) ==
                 ResourceAdaptationProcessor::MitigationResult::kRemovedResource);
  SM_EXPECT_EQ(f.updated, 0u);
  SM_EXPECT_EQ(f.Total(), 0);
}

SM_TEST(ResourceAdaptationProcessor, RemovingResourceWhenMultipleMostLimtedHasNoEffect) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  f.Overuse(f.kOther);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 1);
  // kResource up and then down: both most limited at one adaptation.
  f.Overuse(f.kResource);
  f.RestrictSource();
  f.Underuse(f.kResource);
  f.RestrictSource();
  SM_EXPECT_EQ(f.Total(), 1);
  f.Remove(f.kResource);
  SM_EXPECT_EQ(f.Total(), 1);
}

SM_TEST(ResourceAdaptationProcessor, ResourceOverusedAtLimitReachedWillShareMostLimited) {
  Fixture f;
  f.SetPreference(DegradationPreference::kMaintainFramerate);
  f.SetInputStates(kDefaultFrameRate, kDefaultFrameSize);
  bool limit_reached = false;
  while (!limit_reached) {
    const int before = f.Total();
    f.Overuse(f.kResource);
    limit_reached = f.Total() == before;
    f.RestrictSource();
  }
  // kOther overuses at the limit: it takes the most limited level.
  f.Overuse(f.kOther);
  const auto other = f.processor.limitations(f.kOther);
  SM_ASSERT_TRUE(other.has_value());
  SM_EXPECT_TRUE(other->counters == f.adapter.counters());
  // Both are most limited: kResource alone can't adapt up.
  const int total = f.Total();
  f.Underuse(f.kResource);
  SM_EXPECT_EQ(f.Total(), total);
  f.Underuse(f.kOther);
  SM_EXPECT_EQ(f.Total(), total - 1);
}
