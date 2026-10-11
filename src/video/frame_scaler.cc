#include "sm/video/frame_scaler.h"

#include <algorithm>
#include <cmath>

namespace sm::video {

namespace {
// Scales one plane. Each axis uses an area filter when shrinking and
// bilinear interpolation when enlarging (separable, two passes).
std::vector<uint8_t> ScalePlane(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh) {
  if (sw == dw && sh == dh) return src;
  // Horizontal pass into a float buffer (dw x sh).
  std::vector<float> tmp(static_cast<size_t>(dw) * sh);
  const double sx = static_cast<double>(sw) / dw;
  for (int x = 0; x < dw; ++x) {
    if (sx > 1.0) {
      double a = x * sx, b = (x + 1) * sx;
      for (int y = 0; y < sh; ++y) {
        const uint8_t* row = &src[static_cast<size_t>(y) * sw];
        double acc = 0;
        for (int i = static_cast<int>(a); i < static_cast<int>(std::ceil(b)) && i < sw; ++i) {
          double w = std::min(b, i + 1.0) - std::max(a, static_cast<double>(i));
          acc += w * row[i];
        }
        tmp[static_cast<size_t>(y) * dw + x] = static_cast<float>(acc / sx);
      }
    } else {
      double c = (x + 0.5) * sx - 0.5;
      int i0 = std::clamp(static_cast<int>(std::floor(c)), 0, sw - 1), i1 = std::min(i0 + 1, sw - 1);
      double f = std::clamp(c - i0, 0.0, 1.0);
      for (int y = 0; y < sh; ++y) {
        const uint8_t* row = &src[static_cast<size_t>(y) * sw];
        tmp[static_cast<size_t>(y) * dw + x] = static_cast<float>((1 - f) * row[i0] + f * row[i1]);
      }
    }
  }
  // Vertical pass.
  std::vector<uint8_t> dst(static_cast<size_t>(dw) * dh);
  const double sy = static_cast<double>(sh) / dh;
  for (int y = 0; y < dh; ++y) {
    for (int x = 0; x < dw; ++x) {
      double v;
      if (sy > 1.0) {
        double a = y * sy, b = (y + 1) * sy, acc = 0;
        for (int i = static_cast<int>(a); i < static_cast<int>(std::ceil(b)) && i < sh; ++i) {
          double w = std::min(b, i + 1.0) - std::max(a, static_cast<double>(i));
          acc += w * tmp[static_cast<size_t>(i) * dw + x];
        }
        v = acc / sy;
      } else {
        double c = (y + 0.5) * sy - 0.5;
        int i0 = std::clamp(static_cast<int>(std::floor(c)), 0, sh - 1), i1 = std::min(i0 + 1, sh - 1);
        double f = std::clamp(c - i0, 0.0, 1.0);
        v = (1 - f) * tmp[static_cast<size_t>(i0) * dw + x] + f * tmp[static_cast<size_t>(i1) * dw + x];
      }
      dst[static_cast<size_t>(y) * dw + x] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
    }
  }
  return dst;
}
}  // namespace

I420Frame Scale(const I420Frame& in, int w, int h) {
  I420Frame out(w, h);
  out.y = ScalePlane(in.y, in.width, in.height, w, h);
  out.u = ScalePlane(in.u, in.chroma_width(), in.chroma_height(), out.chroma_width(), out.chroma_height());
  out.v = ScalePlane(in.v, in.chroma_width(), in.chroma_height(), out.chroma_width(), out.chroma_height());
  return out;
}

I420Frame Crop(const I420Frame& in, int x, int y, int w, int h) {
  x &= ~1;
  y &= ~1;
  w = std::min(w, in.width - x);
  h = std::min(h, in.height - y);
  I420Frame out(w, h);
  for (int r = 0; r < h; ++r) {
    std::copy_n(&in.y[static_cast<size_t>(y + r) * in.width + x], w, &out.y[static_cast<size_t>(r) * w]);
  }
  for (int r = 0; r < out.chroma_height(); ++r) {
    size_t s = static_cast<size_t>(y / 2 + r) * in.chroma_width() + x / 2;
    std::copy_n(&in.u[s], out.chroma_width(), &out.u[static_cast<size_t>(r) * out.chroma_width()]);
    std::copy_n(&in.v[s], out.chroma_width(), &out.v[static_cast<size_t>(r) * out.chroma_width()]);
  }
  return out;
}

I420Frame ScaleToFit(const I420Frame& in, int w, int h, AspectMode mode) {
  if (mode == AspectMode::kStretch) return Scale(in, w, h);
  const double src_aspect = static_cast<double>(in.width) / in.height;
  const double dst_aspect = static_cast<double>(w) / h;
  if (mode == AspectMode::kCrop) {
    int cw = in.width, ch = in.height;
    if (src_aspect > dst_aspect) {
      cw = static_cast<int>(std::lround(in.height * dst_aspect)) & ~1;
    } else {
      ch = static_cast<int>(std::lround(in.width / dst_aspect)) & ~1;
    }
    return Scale(Crop(in, (in.width - cw) / 2, (in.height - ch) / 2, cw, ch), w, h);
  }
  // Letterbox: scale to fit inside, pad with black (Y=16, UV=128).
  int sw = w, sh = h;
  if (src_aspect > dst_aspect) {
    sh = static_cast<int>(std::lround(w / src_aspect)) & ~1;
  } else {
    sw = static_cast<int>(std::lround(h * src_aspect)) & ~1;
  }
  I420Frame scaled = Scale(in, sw, sh);
  I420Frame out(w, h);
  std::fill(out.y.begin(), out.y.end(), 16);
  std::fill(out.u.begin(), out.u.end(), 128);
  std::fill(out.v.begin(), out.v.end(), 128);
  int ox = ((w - sw) / 2) & ~1, oy = ((h - sh) / 2) & ~1;
  for (int r = 0; r < sh; ++r) {
    std::copy_n(&scaled.y[static_cast<size_t>(r) * sw], sw, &out.y[static_cast<size_t>(oy + r) * w + ox]);
  }
  for (int r = 0; r < scaled.chroma_height(); ++r) {
    size_t d = static_cast<size_t>(oy / 2 + r) * out.chroma_width() + ox / 2;
    std::copy_n(&scaled.u[static_cast<size_t>(r) * scaled.chroma_width()], scaled.chroma_width(), &out.u[d]);
    std::copy_n(&scaled.v[static_cast<size_t>(r) * scaled.chroma_width()], scaled.chroma_width(), &out.v[d]);
  }
  return out;
}

}  // namespace sm::video
