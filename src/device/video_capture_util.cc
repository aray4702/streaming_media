#include "sm/device/video_capture_util.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sm::device {

int SelectCaptureFormat(const std::vector<CaptureFormat>& formats, int width, int height, double fps) {
  int best = -1;
  bool best_fps_ok = false;
  long best_diff = LONG_MAX;
  for (size_t i = 0; i < formats.size(); ++i) {
    const CaptureFormat& f = formats[i];
    const bool fps_ok = f.max_fps + 1e-6 >= fps;
    const long diff = std::labs(static_cast<long>(f.width) - width) + std::labs(static_cast<long>(f.height) - height);
    // A format that reaches the rate beats one that doesn't; then the closest
    // size; ties keep the first (the device's own order).
    if (best < 0 || (fps_ok && !best_fps_ok) || (fps_ok == best_fps_ok && diff < best_diff)) {
      best = static_cast<int>(i);
      best_fps_ok = fps_ok;
      best_diff = diff;
    }
  }
  return best;
}

void FramerateController::SetMaxFramerate(double max_fps) {
  max_fps_ = max_fps;
}

bool FramerateController::ShouldDropFrame(int64_t in_timestamp_ns) {
  if (max_fps_ <= 0) return false;  // no limit set
  const int64_t frame_interval_ns = static_cast<int64_t>(1e9 / max_fps_);
  if (frame_interval_ns <= 0) return false;
  if (next_frame_timestamp_ns_) {
    const int64_t time_until_next_frame_ns = *next_frame_timestamp_ns_ - in_timestamp_ns;
    // Continue if the timestamp is within the expected range.
    if (std::llabs(time_until_next_frame_ns) < 2 * frame_interval_ns) {
      if (time_until_next_frame_ns > 0) return true;  // too early: drop
      *next_frame_timestamp_ns_ += frame_interval_ns;
      return false;
    }
  }
  // First frame, or far outside the expected range: reset. The first target
  // is half an interval away, to prefer keeping frames under jitter.
  next_frame_timestamp_ns_ = in_timestamp_ns + frame_interval_ns / 2;
  return false;
}

void Nv12ToI420(const uint8_t* src_y, int stride_y, const uint8_t* src_uv, int stride_uv, int width, int height,
                video::I420Frame* out) {
  *out = video::I420Frame(width, height);
  for (int r = 0; r < height; ++r) {
    std::memcpy(out->y.data() + static_cast<size_t>(r) * width, src_y + static_cast<size_t>(r) * stride_y,
                static_cast<size_t>(width));
  }
  const int cw = out->chroma_width();
  const int ch = out->chroma_height();
  for (int r = 0; r < ch; ++r) {
    const uint8_t* row = src_uv + static_cast<size_t>(r) * stride_uv;
    uint8_t* u = out->u.data() + static_cast<size_t>(r) * cw;
    uint8_t* v = out->v.data() + static_cast<size_t>(r) * cw;
    for (int c = 0; c < cw; ++c) {
      u[c] = row[2 * c];
      v[c] = row[2 * c + 1];
    }
  }
}

void CopyNv12(const uint8_t* src_y, int stride_y, const uint8_t* src_uv, int stride_uv, int width, int height,
              Nv12Frame* out) {
  *out = Nv12Frame(width, height);
  for (int r = 0; r < height; ++r) {
    std::memcpy(out->y.data() + static_cast<size_t>(r) * width, src_y + static_cast<size_t>(r) * stride_y,
                static_cast<size_t>(width));
  }
  const size_t uv_row = static_cast<size_t>((width + 1) / 2) * 2;
  for (int r = 0; r < (height + 1) / 2; ++r) {
    std::memcpy(out->uv.data() + static_cast<size_t>(r) * uv_row, src_uv + static_cast<size_t>(r) * stride_uv, uv_row);
  }
}

namespace {

// Bilinear scale of one plane's crop window [x0, x0 + sw) x [y0, y0 + sh).
void ScalePlane(const uint8_t* src, int src_stride, int x0, int y0, int sw, int sh, uint8_t* dst, int dw, int dh) {
  for (int r = 0; r < dh; ++r) {
    // Pixel centres map onto each other.
    const double fy = std::clamp((r + 0.5) * sh / dh - 0.5, 0.0, static_cast<double>(sh - 1));
    const int y_lo = static_cast<int>(fy);
    const int y_hi = std::min(y_lo + 1, sh - 1);
    const double wy = fy - y_lo;
    const uint8_t* row_lo = src + static_cast<size_t>(y0 + y_lo) * src_stride + x0;
    const uint8_t* row_hi = src + static_cast<size_t>(y0 + y_hi) * src_stride + x0;
    for (int c = 0; c < dw; ++c) {
      const double fx = std::clamp((c + 0.5) * sw / dw - 0.5, 0.0, static_cast<double>(sw - 1));
      const int x_lo = static_cast<int>(fx);
      const int x_hi = std::min(x_lo + 1, sw - 1);
      const double wx = fx - x_lo;
      const double top = row_lo[x_lo] * (1 - wx) + row_lo[x_hi] * wx;
      const double bottom = row_hi[x_lo] * (1 - wx) + row_hi[x_hi] * wx;
      dst[static_cast<size_t>(r) * dw + c] = static_cast<uint8_t>(std::lround(top * (1 - wy) + bottom * wy));
    }
  }
}

}  // namespace

video::I420Frame CropAndScaleI420(const video::I420Frame& in, int width, int height) {
  if (in.width == width && in.height == height) return in;
  // Largest centred window with the target's aspect ratio, on even
  // coordinates so the chroma planes crop the same window.
  int cw = in.width;
  int ch = in.height;
  if (static_cast<int64_t>(in.width) * height > static_cast<int64_t>(in.height) * width) {
    cw = static_cast<int>(static_cast<int64_t>(in.height) * width / height);
  } else {
    ch = static_cast<int>(static_cast<int64_t>(in.width) * height / width);
  }
  cw = std::max(2, cw & ~1);
  ch = std::max(2, ch & ~1);
  const int x0 = ((in.width - cw) / 2) & ~1;
  const int y0 = ((in.height - ch) / 2) & ~1;
  video::I420Frame out(width, height);
  ScalePlane(in.y.data(), in.width, x0, y0, std::min(cw, in.width), std::min(ch, in.height), out.y.data(), width,
             height);
  const int icw = in.chroma_width();
  const int ich = in.chroma_height();
  const int scw = std::min((cw + 1) / 2, icw - x0 / 2);
  const int sch = std::min((ch + 1) / 2, ich - y0 / 2);
  ScalePlane(in.u.data(), icw, x0 / 2, y0 / 2, scw, sch, out.u.data(), out.chroma_width(), out.chroma_height());
  ScalePlane(in.v.data(), icw, x0 / 2, y0 / 2, scw, sch, out.v.data(), out.chroma_width(), out.chroma_height());
  return out;
}

}  // namespace sm::device
