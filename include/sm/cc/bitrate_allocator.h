// 5.7 Bitrate allocation among streams.
//
// Splits the target rate across streams (audio, camera, screen share,
// data) with min/max limits and priorities:
//  1. Fund minimums in priority order. A stream whose minimum cannot be
//     funded is paused (0), unless enforce_min, in which case it always gets
//     its minimum (audio). A paused stream only resumes once it can get
//     min * (1 + hysteresis), so streams do not flap on and off.
//  2. Distribute what is left by water-filling, proportional to priority,
//     up to each stream's max; leftover from capped streams is reshared.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/units.h"

namespace sm::cc {

struct AllocatableStream {
  std::string id;
  DataRate min;
  DataRate max;
  double priority = 1.0;
  bool enforce_min = false;
};

class BitrateAllocator : public MetricsSource {
 public:
  explicit BitrateAllocator(double resume_hysteresis = 0.1) : hysteresis_(resume_hysteresis) {}
  void AddStream(const AllocatableStream& s);
  void RemoveStream(const std::string& id);
  std::map<std::string, DataRate> Allocate(DataRate total);
  const std::map<std::string, DataRate>& last() const { return last_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  double hysteresis_;
  std::vector<AllocatableStream> streams_;
  std::map<std::string, bool> paused_;
  std::map<std::string, DataRate> last_;
  uint64_t pauses_ = 0, resumes_ = 0;
  DataRate last_total_;
};

}  // namespace sm::cc
