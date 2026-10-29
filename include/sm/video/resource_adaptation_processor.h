// 7.8 libwebrtc M157's ResourceAdaptationProcessor (issue #646), ported from
// compare/webrtc/reference/call/adaptation/resource_adaptation_processor.cc:
// the resources (quality scaler, CPU usage) that ask a VideoStreamAdapter to
// adapt, and the limit each one imposes.
//   - Overuse: the adapter's step down is applied and recorded as the
//     resource's limit; at kLimitReached the resource takes the most limited
//     limit so far (unrestricted when none).
//   - Underuse: a step up is applied only when no resource is more limited
//     than the stream, or the resource is the only most limited one; a
//     resource that shares the most limited level only relaxes its own limit
//     (kSharedMostLimitedResource), and one below it is refused
//     (kNotMostLimitedResource).
//   - RemoveResource drops the resource's limit and ignores its later
//     signals. When it was the only adapted resource the restrictions are
//     cleared; when it was more limited than the most limited one left, the
//     stream goes straight to that one's restrictions and counters
//     (VideoStreamAdapter::GetAdaptationTo, applied without a reason);
//     otherwise nothing changes (RemoveLimitationsImposedByResource).
//   - Steps applied with no reason that bring the counters to zero clear
//     every limit (OnVideoSourceRestrictionsUpdated). As in
//     VideoStreamAdapter::BroadcastVideoRestrictionsUpdate, the processor
//     hears of a step only when it changes the restrictions the degradation
//     preference lets through. Steps and preference changes go through the
//     processor (ApplyAdaptation, ClearRestrictions, SetDegradationPreference)
//     so that it hears of them, as the adapter's restrictions listener.
// Resources are small integer ids, in place of libwebrtc's Resource objects;
// FindMostLimitedResources goes through them in id order. Tasks run inline:
// the library is single-threaded and takes no task queue. Logging and
// previous_mitigation_results_ (it only rate-limits a log line) are not ported.
#pragma once

#include <optional>
#include <vector>

#include "sm/video/video_stream_adapter.h"

namespace sm::video {

enum class ResourceUsageState { kOveruse, kUnderuse };

class ResourceAdaptationProcessor {
 public:
  enum class MitigationResult {
    kNotMostLimitedResource,
    kSharedMostLimitedResource,
    kRejectedByAdapter,
    kAdaptationApplied,
    kRemovedResource,  // OnResourceUsageStateMeasured's "Ignoring signal from removed resource".
  };
  // VideoStreamAdapter::RestrictionsWithCounters.
  struct RestrictionsWithCounters {
    VideoSourceRestrictions restrictions;
    VideoAdaptationCounters counters;
  };

  // `adapter` (not owned) is the stream's VideoStreamAdapter.
  explicit ResourceAdaptationProcessor(VideoStreamAdapter& adapter) : adapter_(adapter) {}

  void AddResource(int resource);
  void RemoveResource(int resource);
  bool HasResource(int resource) const;
  MitigationResult OnResourceUsageStateMeasured(int resource, ResourceUsageState state);

  // VideoStreamAdapter::ApplyAdaptation with `reason` (none when empty), and
  // this processor's OnVideoSourceRestrictionsUpdated for it.
  void ApplyAdaptation(const VideoStreamAdapter::Adaptation& a, std::optional<int> reason);
  // VideoStreamAdapter::ClearRestrictions, which broadcasts no reason and zero
  // counters: every limit is cleared.
  void ClearRestrictions();
  // VideoStreamAdapter::SetDegradationPreference, and its broadcast.
  void SetDegradationPreference(DegradationPreference p);

  // adaptation_limits_by_resources_' entry for `resource`.
  std::optional<RestrictionsWithCounters> limitations(int resource) const;

 private:
  MitigationResult OnResourceUnderuse(int resource);
  MitigationResult OnResourceOveruse(int resource);
  void RemoveLimitationsImposedByResource(int resource);
  // The most limited limit (unrestricted when none) and the resources at it.
  RestrictionsWithCounters FindMostLimitedResources(std::vector<int>* most) const;
  void UpdateResourceLimitations(int resource, const VideoSourceRestrictions& r, const VideoAdaptationCounters& c);
  void Broadcast(const VideoSourceRestrictions& filtered_before, std::optional<int> reason);
  void OnVideoSourceRestrictionsUpdated(const VideoAdaptationCounters& counters, std::optional<int> reason,
                                        const VideoSourceRestrictions& unfiltered);

  VideoStreamAdapter& adapter_;
  std::vector<bool> registered_;
  std::vector<std::optional<RestrictionsWithCounters>> limits_;
};

}  // namespace sm::video
