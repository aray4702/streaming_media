// Minimal I420 (YUV 4:2:0 planar) frame used by the video algorithms, plus
// test-pattern and PSNR helpers.
#pragma once

#include <cstdint>
#include <vector>

namespace sm::video {

struct I420Frame {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> y, u, v;

  I420Frame() = default;
  I420Frame(int w, int h)
      : width(w), height(h), y(static_cast<size_t>(w) * h),
        u(static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2)), v(u.size()) {}
  int chroma_width() const { return (width + 1) / 2; }
  int chroma_height() const { return (height + 1) / 2; }
  uint8_t& Y(int x, int yy) { return y[static_cast<size_t>(yy) * width + x]; }
  uint8_t Y(int x, int yy) const { return y[static_cast<size_t>(yy) * width + x]; }
  bool operator==(const I420Frame& o) const = default;
};

// Smooth gradients, a moving disc and sharp text-like bars: exercises both
// low-frequency and edge detail. `t` animates the content.
I420Frame MakeTestFrame(int width, int height, int t = 0);
// Luma PSNR in dB (frames must have equal size).
double PsnrY(const I420Frame& a, const I420Frame& b);

}  // namespace sm::video
