// 7.3 Video orientation (CVO, 3GPP TS 26.114 / WebRTC
// "urn:3gpp:video-orientation").
//
// A phone sensor is mounted in landscape; when the user holds the phone
// upright the captured frames are rotated. Instead of rotating every frame
// before encoding (costly, and it changes the encoded resolution
// mid-stream), the sender signals the rotation in a one-byte RTP header
// extension:  0 0 0 0 C F R1 R0  (C: back camera, F: horizontal flip,
// R: rotation in 90-degree steps, clockwise). The receiver rotates at
// render time.
#pragma once

#include <cstdint>

#include "sm/video/frame.h"

namespace sm::video {

enum class Rotation : uint8_t { k0 = 0, k90 = 1, k180 = 2, k270 = 3 };

struct VideoOrientation {
  Rotation rotation = Rotation::k0;
  bool flip = false;         // Horizontal mirror (applied before rotation).
  bool back_camera = false;
};

uint8_t EncodeCvo(const VideoOrientation& o);
VideoOrientation DecodeCvo(uint8_t b);
// Returns the frame mirrored (if flip) and rotated clockwise.
I420Frame ApplyOrientation(const I420Frame& in, const VideoOrientation& o);
// Inverse: what the camera captured, given the upright image.
VideoOrientation Inverse(const VideoOrientation& o);

}  // namespace sm::video
