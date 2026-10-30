// 7.4 Content-aware encoding policy.
//
// The track's content hint (WebRTC MediaStreamTrack.contentHint) selects
// how to trade quality under constraint, as libwebrtc's
// GetDegradationPreference (webrtc_video_engine.cc:2032-2064):
//   motion  (camera, sports)       keep frame rate, drop resolution
//   detail  (slides, documents)    keep resolution, drop frame rate
//   text    (code, spreadsheets)   keep resolution, drop frame rate
//   none                           screencast keeps resolution, camera
//                                  keeps frame rate
// The hint picks only the degradation preference: libwebrtc derives no
// QP ceiling, frame-rate floor or cap from it (#877).
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
};

ContentSettings SettingsFor(ContentHint hint, bool is_screencast);

}  // namespace sm::video
