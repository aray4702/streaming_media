// The remote video on the GPU (#1133), as libwebrtc's RTCMTLVideoView with its I420 renderer (RTCMTLI420Renderer):
// an MTKView whose display-driven draw loop asks for the latest decoded frame, skips the draw when there is none
// new, uploads the three I420 planes as R8 textures (no per-pixel CPU work) and converts to RGB in a fragment shader,
// with the frame's rotation applied in the texture coordinates (RTCMTLRenderer's rotation) and the drawable sized to
// the turned frame, shown aspect-fit. Objective-C++ only (CallBridge.mm); Swift sees it as the bridge's UIView.
#import <UIKit/UIKit.h>

#include <cstdint>
#include <functional>

#include "sm/video/frame.h"
#include "sm/video/orientation.h"

// The frame source, called on the main thread once per display refresh: fills a new frame and the clockwise turn
// that shows it upright and returns true, or returns false when no frame arrived since the last call.
using SMRemoteFrameSource = std::function<bool(sm::video::I420Frame* frame, sm::video::Rotation* rotation)>;

@interface SMRemoteVideoView : UIView
- (void)setFrameSource:(SMRemoteFrameSource)source;
@property(nonatomic, readonly) uint64_t framesDrawn;  // Frames drawn since the view was made.
@end
