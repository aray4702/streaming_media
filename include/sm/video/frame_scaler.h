// 7.6 Resolution adaptation: scaling and aspect-ratio handling.
//
// Mid-stream resolution changes (from bandwidth/CPU adaptation or simulcast
// layers) need a scaler on both sides: the sender downscales before
// encoding, the receiver upscales for display. Downscaling uses an area
// (box) filter, which integrates every source pixel and so does not alias;
// upscaling uses bilinear interpolation. Aspect-ratio changes are handled
// by centre cropping (fill) or letterboxing (fit) on 2-pixel boundaries so
// chroma stays aligned.
#pragma once

#include "sm/core/metrics.h"
#include "sm/video/frame.h"

namespace sm::video {

enum class AspectMode { kStretch, kCrop, kLetterbox };

// Scales a whole frame to w x h (box filter when shrinking, bilinear when
// enlarging, per axis).
I420Frame Scale(const I420Frame& in, int w, int h);
// Fits `in` into w x h with the given aspect handling.
I420Frame ScaleToFit(const I420Frame& in, int w, int h, AspectMode mode);
// Crops a rectangle (x, y, w, h); coordinates rounded down to even.
I420Frame Crop(const I420Frame& in, int x, int y, int w, int h);

}  // namespace sm::video
