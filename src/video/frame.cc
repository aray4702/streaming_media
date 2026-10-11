#include "sm/video/frame.h"

#include <algorithm>
#include <cmath>

namespace sm::video {

I420Frame MakeTestFrame(int w, int h, int t) {
  I420Frame f(w, h);
  const double cx = w * (0.3 + 0.2 * std::sin(t * 0.07)), cy = h * (0.5 + 0.2 * std::cos(t * 0.05));
  const double r = std::min(w, h) * 0.15;
  for (int yy = 0; yy < h; ++yy) {
    for (int x = 0; x < w; ++x) {
      double g = 64 + 96.0 * x / w + 48.0 * yy / h;  // Smooth gradient.
      double dx = x - cx, dy = yy - cy;
      if (dx * dx + dy * dy < r * r) g = 220;           // Moving disc.
      int bar = (x * 32 / w);                            // Sharp vertical bars on the right.
      if (x > w * 3 / 4 && yy > h / 8 && yy < h / 4 && bar % 2 == 0) g = 16;
      f.Y(x, yy) = static_cast<uint8_t>(std::clamp(g, 0.0, 255.0));
    }
  }
  for (int yy = 0; yy < f.chroma_height(); ++yy) {
    for (int x = 0; x < f.chroma_width(); ++x) {
      size_t i = static_cast<size_t>(yy) * f.chroma_width() + x;
      f.u[i] = static_cast<uint8_t>(128 + 40.0 * x / f.chroma_width() - 20);
      f.v[i] = static_cast<uint8_t>(128 + 40.0 * yy / f.chroma_height() - 20);
    }
  }
  return f;
}

double PsnrY(const I420Frame& a, const I420Frame& b) {
  if (a.width != b.width || a.height != b.height || a.y.empty()) return 0;
  double mse = 0;
  for (size_t i = 0; i < a.y.size(); ++i) {
    double d = static_cast<double>(a.y[i]) - b.y[i];
    mse += d * d;
  }
  mse /= static_cast<double>(a.y.size());
  return mse <= 1e-10 ? 100.0 : 10 * std::log10(255.0 * 255.0 / mse);
}

}  // namespace sm::video
