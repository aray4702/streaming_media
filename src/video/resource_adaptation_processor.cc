#include "sm/video/resource_adaptation_processor.h"

#include <algorithm>

namespace sm::video {

void ResourceAdaptationProcessor::AddResource(int resource) {
  if (resource < 0) return;
  const size_t i = static_cast<size_t>(resource);
  if (i >= registered_.size()) {
    registered_.resize(i + 1, false);
    limits_.resize(i + 1);
  }
  registered_[i] = true;
}

bool ResourceAdaptationProcessor::HasResource(int resource) const {
  return resource >= 0 && static_cast<size_t>(resource) < registered_.size() && registered_[resource];
}

void ResourceAdaptationProcessor::RemoveResource(int resource) {
  if (!HasResource(resource)) return;
  registered_[resource] = false;
  RemoveLimitationsImposedByResource(resource);
}

void ResourceAdaptationProcessor::RemoveLimitationsImposedByResource(int resource) {
  if (!limits_[resource]) return;
  const RestrictionsWithCounters adaptation_limits = *limits_[resource];
  limits_[resource].reset();
  if (std::none_of(limits_.begin(), limits_.end(), [](const auto& l) { return l.has_value(); })) {
    // Only the resource being removed was adapted so clear restrictions.
    ClearRestrictions();
    return;
  }
  const RestrictionsWithCounters most_limited = FindMostLimitedResources(nullptr);
  // The removed limitations were less limited than the most limited resource.
  // Don't change the current restrictions.
  if (adaptation_limits.counters.Total() <= most_limited.counters.Total()) return;
  // Apply the new most limited resource as the next restrictions.
  ApplyAdaptation(adapter_.GetAdaptationTo(most_limited.counters, most_limited.restrictions), std::nullopt);
}

ResourceAdaptationProcessor::MitigationResult ResourceAdaptationProcessor::OnResourceUsageStateMeasured(
    int resource, ResourceUsageState state) {
  // `resource` could have been removed after signalling.
  if (!HasResource(resource)) return MitigationResult::kRemovedResource;
  return state == ResourceUsageState::kOveruse ? OnResourceOveruse(resource) : OnResourceUnderuse(resource);
}

ResourceAdaptationProcessor::MitigationResult ResourceAdaptationProcessor::OnResourceUnderuse(int resource) {
  // How can this stream be adapted up?
  const VideoStreamAdapter::Adaptation adaptation = adapter_.GetAdaptationUp();
  if (adaptation.status != VideoStreamAdapter::Status::kValid) return MitigationResult::kRejectedByAdapter;
  // Check that resource is most limited.
  std::vector<int> most_limited_resources;
  const RestrictionsWithCounters most_limited = FindMostLimitedResources(&most_limited_resources);
  // If the most restricted resource is less limited than current restrictions
  // then proceed with adapting up.
  if (!most_limited_resources.empty() && most_limited.counters.Total() >= adapter_.counters().Total()) {
    // If `resource` is not one of the most limiting resources then abort.
    if (std::find(most_limited_resources.begin(), most_limited_resources.end(), resource) ==
        most_limited_resources.end()) {
      return MitigationResult::kNotMostLimitedResource;
    }
    if (most_limited_resources.size() > 1) {
      // If there are multiple most limited resources, all must signal underuse
      // before the adaptation is applied.
      UpdateResourceLimitations(resource, adaptation.restrictions, adaptation.counters());
      return MitigationResult::kSharedMostLimitedResource;
    }
  }
  ApplyAdaptation(adaptation, resource);
  return MitigationResult::kAdaptationApplied;
}

ResourceAdaptationProcessor::MitigationResult ResourceAdaptationProcessor::OnResourceOveruse(int resource) {
  const VideoStreamAdapter::Adaptation adaptation = adapter_.GetAdaptationDown();
  if (adaptation.status == VideoStreamAdapter::Status::kLimitReached) {
    // Add resource as most limited.
    const RestrictionsWithCounters most = FindMostLimitedResources(nullptr);
    UpdateResourceLimitations(resource, most.restrictions, most.counters);
  }
  if (adaptation.status != VideoStreamAdapter::Status::kValid) return MitigationResult::kRejectedByAdapter;
  UpdateResourceLimitations(resource, adaptation.restrictions, adaptation.counters());
  ApplyAdaptation(adaptation, resource);
  return MitigationResult::kAdaptationApplied;
}

ResourceAdaptationProcessor::RestrictionsWithCounters ResourceAdaptationProcessor::FindMostLimitedResources(
    std::vector<int>* most) const {
  RestrictionsWithCounters most_limited;
  std::vector<int> resources;
  for (size_t i = 0; i < limits_.size(); ++i) {
    if (!limits_[i]) continue;
    if (limits_[i]->counters.Total() > most_limited.counters.Total()) {
      most_limited = *limits_[i];
      resources.assign(1, static_cast<int>(i));
    } else if (most_limited.counters == limits_[i]->counters) {
      resources.push_back(static_cast<int>(i));
    }
  }
  if (most) *most = std::move(resources);
  return most_limited;
}

void ResourceAdaptationProcessor::UpdateResourceLimitations(int resource, const VideoSourceRestrictions& r,
                                                            const VideoAdaptationCounters& c) {
  limits_[resource] = RestrictionsWithCounters{r, c};
}

void ResourceAdaptationProcessor::ApplyAdaptation(const VideoStreamAdapter::Adaptation& a, std::optional<int> reason) {
  if (a.status != VideoStreamAdapter::Status::kValid) return;
  const VideoSourceRestrictions before = adapter_.filtered_restrictions();
  adapter_.ApplyAdaptation(a);
  Broadcast(before, reason);
}

void ResourceAdaptationProcessor::ClearRestrictions() {
  const VideoSourceRestrictions before = adapter_.filtered_restrictions();
  adapter_.ClearRestrictions();
  Broadcast(before, std::nullopt);
}

void ResourceAdaptationProcessor::SetDegradationPreference(DegradationPreference p) {
  const VideoSourceRestrictions before = adapter_.filtered_restrictions();
  adapter_.SetDegradationPreference(p);
  Broadcast(before, std::nullopt);
}

void ResourceAdaptationProcessor::Broadcast(const VideoSourceRestrictions& filtered_before, std::optional<int> reason) {
  // BroadcastVideoRestrictionsUpdate: listeners hear of a change in the
  // filtered restrictions only (last_filtered_restrictions_ is the filtered
  // value before this change, as every change broadcasts).
  if (adapter_.filtered_restrictions() == filtered_before) return;
  OnVideoSourceRestrictionsUpdated(adapter_.counters(), reason, adapter_.restrictions());
}

void ResourceAdaptationProcessor::OnVideoSourceRestrictionsUpdated(const VideoAdaptationCounters& counters,
                                                                   std::optional<int> reason,
                                                                   const VideoSourceRestrictions& unfiltered) {
  if (reason) {
    if (HasResource(*reason)) UpdateResourceLimitations(*reason, unfiltered, counters);
  } else if (counters.Total() == 0) {
    // Adaptations are cleared.
    for (auto& l : limits_) l.reset();
  }
}

std::optional<ResourceAdaptationProcessor::RestrictionsWithCounters> ResourceAdaptationProcessor::limitations(
    int resource) const {
  if (resource < 0 || static_cast<size_t>(resource) >= limits_.size()) return std::nullopt;
  return limits_[resource];
}

}  // namespace sm::video
