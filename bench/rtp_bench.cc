// RTP session layer benchmarks.
#include <algorithm>
#include <memory>
#include <vector>

#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm/rtp/session.h"
#include "sm/rtp/simulcast_send_controller.h"
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

SM_BENCH(rtp, SimulcastSend_3layers_frame) {
  // 7.9: the send-side cost of one 720p camera frame sent as three H.264
  // layers at 30 fps on a 3 Mbps session (the SimulcastSend unit tests'
  // fixture): SimulcastSendController's decision (frame rate, the per-second
  // rate update through SimulcastRateAllocator and the bitrate adjuster, the
  // frame dropper, keyframe state) and each layer's encoded-frame report at
  // its target size, as the fake encoder of the unit tests reports it.
  SessionConfig cfg;
  cfg.local_ssrc = 1;
  cfg.extensions.Register(1, sync::uri::kTransportCc);
  cfg.extensions.Register(2, sync::uri::kRid);
  cfg.extensions.Register(3, sync::uri::kRepairedRid);
  cfg.send_side.gcc.start_rate = DataRate::KilobitsPerSec(3000);
  cfg.send_side.gcc.max_rate = DataRate::KilobitsPerSec(5000);
  auto session = std::make_unique<Session>(cfg);
  std::vector<VideoSendId> ids;
  const char* rids[] = {"q", "h", "f"};
  for (uint32_t i = 0; i < 3; ++i) {
    VideoStreamConfig v;
    v.ssrc = 100 + i;
    v.rtx_ssrc = 200 + i;
    v.rid = rids[i];
    ids.push_back(session->AddVideoSend(v));
  }
  SimulcastSendController c({}, *session, ids);
  int64_t t = 0;
  size_t encoded = 0;
  for (auto _ : st) {
    const Timestamp now = Timestamp::Millis(t);
    const auto d = c.OnCapturedFrame({1280, 720}, now);
    for (size_t i = 0; i < d.layers.size(); ++i) {
      if (!d.layers[i].encode) continue;
      ++encoded;
      c.OnEncoded(i, {DataSize::Bytes(std::max<int64_t>(200, d.layers[i].target.bps() / 8 / 30)), d.layers[i].keyframe},
                  now);
    }
    t += 33;
  }
  // Every layer encodes every frame at 3 Mbps (none paused, none dropped).
  const double layers = static_cast<double>(encoded) / static_cast<double>(st.iterations());
  st.Counter("layers_encoded_per_frame", layers);
  st.Require("layers_encoded_per_frame", layers, ">=", 2.9);
  // Measured 318 ns per frame on an Apple M4 (Release); 1/10 of a 33 ms
  // frame interval would be 3.3 ms, the budget is 10x the measured cost.
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
