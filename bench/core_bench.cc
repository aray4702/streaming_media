#include <vector>

#include "sm/core/fft.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/rtp_packet.h"
#include "sm_bench.h"

using namespace sm;

SM_BENCH(core, Histogram_Add) {
  Histogram h;
  double v = 1;
  for (auto _ : st) {
    h.Add(v);
    v = v * 1.0001 + 0.5;
    if (v > 1e6) v = 1;
  }
  bench::DoNotOptimize(h);
  st.BudgetNsPerItem(50);
}

SM_BENCH(core, RealFft_256) {
  RealFft fft(256);
  std::vector<float> x(256, 0.1f);
  std::vector<Complex> y(129);
  for (auto _ : st) {
    fft.Forward(x.data(), y.data());
    bench::DoNotOptimize(y);
  }
  st.BudgetNsPerItem(10000);
}

SM_BENCH(core, RtpPacket_SerializeParse) {
  RtpPacket p;
  p.payload.assign(1200, 7);
  for (auto _ : st) {
    auto b = p.Serialize();
    auto q = RtpPacket::Parse(b);
    bench::DoNotOptimize(q);
  }
  st.BudgetNsPerItem(5000);
}
