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
      s.degradation = is_screencast ? Degradation::kMaintainResolution : Degradation::kBalanced;
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

namespace {
uint64_t HashBlock(const I420Frame& f, int bx, int by, int bs) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a.
  int x1 = std::min(f.width, (bx + 1) * bs), y1 = std::min(f.height, (by + 1) * bs);
  for (int y = by * bs; y < y1; ++y) {
    const uint8_t* row = &f.y[static_cast<size_t>(y) * f.width];
    for (int x = bx * bs; x < x1; ++x) {
      h ^= row[x];
      h *= 1099511628211ULL;
    }
  }
  return h;
}
}  // namespace

ChangeResult FrameChangeDetector::Analyze(const I420Frame& f) {
  ChangeResult r;
  int bw = (f.width + block_ - 1) / block_, bh = (f.height + block_ - 1) / block_;
  bool resized = f.width != w_ || f.height != h_;
  if (resized) {
    w_ = f.width;
    h_ = f.height;
    hashes_.assign(static_cast<size_t>(bw) * bh, 0);
  }
  for (int by = 0; by < bh; ++by) {
    for (int bx = 0; bx < bw; ++bx) {
      uint64_t h = HashBlock(f, bx, by, block_);
      uint64_t& prev = hashes_[static_cast<size_t>(by) * bw + bx];
      if (resized || h != prev) ++r.dirty_blocks;
      prev = h;
    }
  }
  r.changed = r.dirty_blocks > 0;
  r.changed_fraction = static_cast<double>(r.dirty_blocks) / static_cast<double>(bw * bh);
  return r;
}

bool FrameChangeDetector::ShouldEncode(const I420Frame& f, Timestamp now) {
  ++frames_;
  ChangeResult r = Analyze(f);
  bool idle_expired = !last_encoded_.IsFinite() || now - last_encoded_ >= max_idle_;
  if (r.changed || idle_expired) {
    if (!r.changed) ++keepalive_;
    last_encoded_ = now;
    ++encoded_;
    return true;
  }
  return false;
}

void FrameChangeDetector::ExportMetrics(MetricsWriter& w) const {
  w.Counter("frames", frames_);
  w.Counter("encoded", encoded_);
  w.Counter("keepalive", keepalive_);
  w.Gauge("encode_ratio", frames_ ? static_cast<double>(encoded_) / static_cast<double>(frames_) : 0.0);
}

}  // namespace sm::video
