#include "sm/video/content_policy.h"

#include <algorithm>

namespace sm::video {

// As GetDegradationPreference (webrtc_video_engine.cc:2032-2064) with CPU
// overuse detection on and no degradation preference in the RTP parameters:
// kFluid keeps the frame rate, screencast, kDetailed and kText keep the
// resolution, and anything else keeps the frame rate until BALANCED is tuned
// (the WebRTC-Video-BalancedDegradation trial is off).
ContentSettings SettingsFor(ContentHint hint, bool is_screencast) {
  ContentSettings s;
  if (hint == ContentHint::kMotion) {
    s.degradation = Degradation::kMaintainFramerate;
  } else if (is_screencast || hint == ContentHint::kDetail || hint == ContentHint::kText) {
    s.degradation = Degradation::kMaintainResolution;
  } else {
    s.degradation = Degradation::kMaintainFramerate;
  }
  return s;
}

}  // namespace sm::video
