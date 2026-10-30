#include "sm/video/content_policy.h"

#include <algorithm>

namespace sm::video {

ContentSettings SettingsFor(ContentHint hint, bool is_screencast) {
  ContentSettings s;
  switch (hint) {
    case ContentHint::kMotion:
      s.degradation = Degradation::kMaintainFramerate;
      s.min_fps = 15;
      break;
    case ContentHint::kDetail:
      s.degradation = Degradation::kMaintainResolution;
      s.min_fps = 2;
      s.max_qp_h264 = 40;
      break;
    case ContentHint::kText:
      s.degradation = Degradation::kMaintainResolution;
      s.min_fps = 1;
      s.max_qp_h264 = 32;  // Text must stay legible: prefer dropping frames.
      break;
    case ContentHint::kNone:
      // As GetDegradationPreference (webrtc_video_engine.cc:2032-2064):
      // camera keeps MAINTAIN_FRAMERATE until BALANCED is tuned.
      s.degradation = is_screencast ? Degradation::kMaintainResolution : Degradation::kMaintainFramerate;
      if (is_screencast) s.min_fps = 2;
      break;
  }
  if (is_screencast) {
    s.variable_framerate = true;
    s.screen_content_tools = true;
    s.max_fps = hint == ContentHint::kMotion ? 30 : 15;
  }
  return s;
}

}  // namespace sm::video
