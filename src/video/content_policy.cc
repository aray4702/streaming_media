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
