#include "sm/cc/video_adaptation.h"

#include <algorithm>
#include <cmath>

namespace sm::cc {

namespace {
// Resolution steps keep the aspect ratio: 3/4 then 2/3 linear scale,
// alternating (pixel ratios ~0.56 and ~0.44), like libwebrtc's VideoAdapter.
std::vector<std::pair<int, int>> ResolutionSteps(const VideoFormat& in, int min_pixels) {
  std::vector<std::pair<int, int>> out{{in.width, in.height}};
  double w = in.width, h = in.height;
  bool three_quarters = true;
  for (;;) {
    double s = three_quarters ? 0.75 : 2.0 / 3.0;
    three_quarters = !three_quarters;
    w *= s;
    h *= s;
    int iw = static_cast<int>(std::lround(w / 2)) * 2;
    int ih = static_cast<int>(std::lround(h / 2)) * 2;
    if (int64_t{iw} * ih < min_pixels) break;
    out.emplace_back(iw, ih);
  }
  return out;
}
std::vector<double> FpsSteps(double from, double to) {
  std::vector<double> out;
  for (double f = from; f >= to - 1e-9; f = f * 2 / 3) out.push_back(f);
  if (out.empty() || out.back() > to + 1e-9) out.push_back(to);
  return out;
}
}  // namespace

DegradationController::DegradationController(DegradationConfig cfg) : cfg_(cfg) {
  auto res = ResolutionSteps(cfg_.input, cfg_.min_pixels);
  const double fps = cfg_.input.fps;
  switch (cfg_.preference) {
    case DegradationPreference::kDisabled:
      ladder_.push_back(cfg_.input);
      break;
    case DegradationPreference::kMaintainFramerate:
      for (auto [w, h] : res) ladder_.push_back({w, h, fps});
      break;
    case DegradationPreference::kMaintainResolution:
      for (double f : FpsSteps(fps, cfg_.min_fps)) ladder_.push_back({cfg_.input.width, cfg_.input.height, f});
      break;
    case DegradationPreference::kBalanced: {
      double bal = std::min(fps, cfg_.balanced_fps);
      for (double f : FpsSteps(fps, bal)) ladder_.push_back({cfg_.input.width, cfg_.input.height, f});
      for (size_t i = 1; i < res.size(); ++i) ladder_.push_back({res[i].first, res[i].second, bal});
      auto low = FpsSteps(bal, cfg_.min_fps);
      for (size_t i = 1; i < low.size(); ++i) ladder_.push_back({res.back().first, res.back().second, low[i]});
      break;
    }
  }
  current_ = ladder_.front();
}

bool DegradationController::Fits(const VideoFormat& f, DataRate r, double bpp) const {
  return static_cast<double>(r.bps()) >= static_cast<double>(f.pixels()) * f.fps * bpp;
}

VideoFormat DegradationController::OnTargetBitrate(DataRate target, Timestamp now) {
  last_target_ = target;
  size_t idx = index_;
  // Step down while the current format is over budget.
  while (idx + 1 < ladder_.size() && !Fits(ladder_[idx], target, cfg_.bpp_down)) ++idx;
  // Step up while the next better format fits with headroom.
  while (idx > 0 && Fits(ladder_[idx - 1], target, cfg_.bpp_up)) --idx;
  if (idx > index_) ++adapt_down_;
  if (idx < index_) ++adapt_up_;
  index_ = idx;
  VideoFormat next = ladder_[std::max(index_, resource_cap_)];
  if (!(next == current_)) {
    SM_EVENT(cfg_.events, now, "adaptation", "format", static_cast<double>(next.pixels()), static_cast<int64_t>(next.fps));
  }
  current_ = next;
  return current_;
}

void DegradationController::OnResourceOveruse() {
  size_t cur = std::max(index_, resource_cap_);
  if (cur + 1 < ladder_.size()) resource_cap_ = cur + 1;
  current_ = ladder_[std::max(index_, resource_cap_)];
}

void DegradationController::OnResourceUnderuse() {
  if (resource_cap_ > 0) --resource_cap_;
  current_ = ladder_[std::max(index_, resource_cap_)];
}

void DegradationController::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("width", current_.width);
  w.Gauge("height", current_.height);
  w.Gauge("fps", current_.fps);
  w.Gauge("ladder_index", static_cast<double>(std::max(index_, resource_cap_)));
  w.Gauge("resource_cap", static_cast<double>(resource_cap_));
  w.Counter("adapt_down", adapt_down_);
  w.Counter("adapt_up", adapt_up_);
}

void CpuOveruseDetector::OnFrameEncoded(TimeDelta encode_time, TimeDelta frame_interval) {
  if (frame_interval <= TimeDelta::Zero()) return;
  double u = encode_time / frame_interval;
  usage_ = has_usage_ ? usage_ + cfg_.smoothing * (u - usage_) : u;
  has_usage_ = true;
}

CpuOveruseDetector::Signal CpuOveruseDetector::Check(Timestamp now) {
  if (!has_usage_) return Signal::kNone;
  if (last_check_.IsFinite() && now - last_check_ < cfg_.check_interval) return Signal::kNone;
  last_check_ = now;
  if (usage_ > cfg_.high_threshold) {
    under_checks_ = 0;
    if (++over_checks_ >= cfg_.consecutive_checks) {
      over_checks_ = 0;
      // An adapt-up that immediately led to overuse: wait longer next time.
      under_checks_needed_ = last_was_up_ ? std::min(under_checks_needed_ * 2, 16) : 1;
      last_was_up_ = false;
      ++overuses_;
      return Signal::kOveruse;
    }
  } else if (usage_ < cfg_.low_threshold) {
    over_checks_ = 0;
    if (++under_checks_ >= under_checks_needed_) {
      under_checks_ = 0;
      last_was_up_ = true;
      ++underuses_;
      return Signal::kUnderuse;
    }
  } else {
    over_checks_ = 0;
    under_checks_ = 0;
  }
  return Signal::kNone;
}

void CpuOveruseDetector::ExportMetrics(MetricsWriter& w) const {
  w.Gauge("encode_usage", usage_);
  w.Counter("overuse_signals", overuses_);
  w.Counter("underuse_signals", underuses_);
}

}  // namespace sm::cc
