// RTP session layer benchmarks.
#include <vector>

#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::rtp;

SM_BENCH(rtp, SdesCname_SerializeParse) {
  std::vector<SdesChunk> chunks = {{0x1234, {{kSdesCname, "streaming-media-peer"}}}};
  for (auto _ : st) {
    auto b = SerializeSdes(chunks);
    bench::DoNotOptimize(ParseSdes(b));
  }
  st.BudgetNsPerItem(3000);
}

SM_BENCH(rtp, Bye_SerializeParse) {
  Bye bye{{0x1234, 0x5678}, "hangup"};
  for (auto _ : st) {
    auto b = SerializeBye(bye);
    bench::DoNotOptimize(ParseBye(b));
  }
  st.BudgetNsPerItem(3000);
}
