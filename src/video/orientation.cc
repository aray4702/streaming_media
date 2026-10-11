#include "sm/video/orientation.h"

namespace sm::video {

uint8_t EncodeCvo(const VideoOrientation& o) {
  return static_cast<uint8_t>((o.back_camera ? 0x08 : 0) | (o.flip ? 0x04 : 0) | static_cast<uint8_t>(o.rotation));
}

VideoOrientation DecodeCvo(uint8_t b) {
  return VideoOrientation{static_cast<Rotation>(b & 0x03), (b & 0x04) != 0, (b & 0x08) != 0};
}

namespace {
// Rotates one plane clockwise by r (w x h -> w' x h').
std::vector<uint8_t> RotatePlane(const std::vector<uint8_t>& src, int w, int h, Rotation r, bool flip) {
  std::vector<uint8_t> dst(src.size());
  const bool swap = r == Rotation::k90 || r == Rotation::k270;
  const int dw = swap ? h : w;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int sx = flip ? w - 1 - x : x;
      uint8_t v = src[static_cast<size_t>(y) * w + sx];
      int dx = x, dy = y;
      switch (r) {
        case Rotation::k0: break;
        case Rotation::k90: dx = h - 1 - y; dy = x; break;
        case Rotation::k180: dx = w - 1 - x; dy = h - 1 - y; break;
        case Rotation::k270: dx = y; dy = w - 1 - x; break;
      }
      dst[static_cast<size_t>(dy) * dw + dx] = v;
    }
  }
  return dst;
}
}  // namespace

I420Frame ApplyOrientation(const I420Frame& in, const VideoOrientation& o) {
  const bool swap = o.rotation == Rotation::k90 || o.rotation == Rotation::k270;
  I420Frame out;
  out.width = swap ? in.height : in.width;
  out.height = swap ? in.width : in.height;
  out.y = RotatePlane(in.y, in.width, in.height, o.rotation, o.flip);
  out.u = RotatePlane(in.u, in.chroma_width(), in.chroma_height(), o.rotation, o.flip);
  out.v = RotatePlane(in.v, in.chroma_width(), in.chroma_height(), o.rotation, o.flip);
  return out;
}

VideoOrientation Inverse(const VideoOrientation& o) {
  VideoOrientation inv = o;
  // (rotate r) o (flip) inverted = (flip) o (rotate -r) = (rotate r') o (flip)
  // with r' = r when flipped (mirror conjugates rotation), -r otherwise.
  uint8_t r = static_cast<uint8_t>(o.rotation);
  inv.rotation = static_cast<Rotation>(o.flip ? r : (4 - r) % 4);
  return inv;
}

}  // namespace sm::video
