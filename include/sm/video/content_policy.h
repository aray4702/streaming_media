// 7.4 Content-aware encoding policy.
//
// The track's content hint (WebRTC MediaStreamTrack.contentHint) selects
// how to trade quality under constraint:
//   motion  (camera, sports)       keep frame rate, drop resolution
//   detail  (slides, documents)    keep resolution, drop frame rate
//   text    (code, spreadsheets)   keep resolution, low QP ceiling, allow
//                                  very low frame rates
// Screen content additionally uses variable frame rate: the capturer delivers
// a frame only when the screen changes, and FrameCadenceAdapter's zero-hertz
// mode (frame_cadence_adapter.h, as libwebrtc) repeats the last one while the
// screen is static (#878).
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
  Degradation degradation = Degradation::kMaintainFramerate;  // libwebrtc's camera default.
  double max_fps = 30;
  double min_fps = 10;
  int max_qp_h264 = 51;          // QP ceiling (H.264 scale 0..51).
  bool variable_framerate = false;
  bool screen_content_tools = false;  // e.g. AV1 palette/intra-BC, HEVC SCC.
};

ContentSettings SettingsFor(ContentHint hint, bool is_screencast);

}  // namespace sm::video
