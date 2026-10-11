// 7.4 Content-aware encoding policy and screen-content change detection.
//
// The track's content hint (WebRTC MediaStreamTrack.contentHint) selects
// how to trade quality under constraint:
//   motion  (camera, sports)       keep frame rate, drop resolution
//   detail  (slides, documents)    keep resolution, drop frame rate
//   text    (code, spreadsheets)   keep resolution, low QP ceiling, allow
//                                  very low frame rates
// Screen content additionally uses variable frame rate: FrameChangeDetector
// hashes 16x16 luma blocks and reports which changed; unchanged frames are
// not encoded at all (a static slide costs nothing), with a keep-alive
// frame every `max_idle` so the receiver knows the stream is alive.
#pragma once

#include <cstdint>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/video/frame.h"

namespace sm::video {

enum class ContentHint { kNone, kMotion, kDetail, kText };
enum class Degradation { kMaintainFramerate, kMaintainResolution, kBalanced };

struct ContentSettings {
  Degradation degradation = Degradation::kBalanced;
  double max_fps = 30;
  double min_fps = 10;
  int max_qp_h264 = 51;          // QP ceiling (H.264 scale 0..51).
  bool variable_framerate = false;
  bool screen_content_tools = false;  // e.g. AV1 palette/intra-BC, HEVC SCC.
};

ContentSettings SettingsFor(ContentHint hint, bool is_screencast);

struct ChangeResult {
  bool changed = false;
  int dirty_blocks = 0;
  double changed_fraction = 0;
};

class FrameChangeDetector : public MetricsSource {
 public:
  explicit FrameChangeDetector(TimeDelta max_idle = TimeDelta::Seconds(1), int block = 16)
      : max_idle_(max_idle), block_(block) {}
  ChangeResult Analyze(const I420Frame& f);
  // Encode decision for variable-frame-rate screen content.
  bool ShouldEncode(const I420Frame& f, Timestamp now);
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  TimeDelta max_idle_;
  int block_;
  int w_ = 0, h_ = 0;
  std::vector<uint64_t> hashes_;
  Timestamp last_encoded_ = Timestamp::MinusInfinity();
  uint64_t frames_ = 0, encoded_ = 0, keepalive_ = 0;
};

}  // namespace sm::video
