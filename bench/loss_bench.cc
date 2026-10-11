// Section 3 benchmarks. Throughput budgets are per item on a desktop-class
// core (scale with --budget-scale on slower targets); quality benchmarks
// gate the e2e behaviour.
#include "../tests/e2e/loss_sims.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::loss;

SM_BENCH(loss, NackTracker_PacketWith5PctLoss) {
  NackTracker n;
  Random rng(1);
  uint16_t seq = 0;
  int64_t t = 0;
  for (auto _ : st) {
    if (!rng.Bernoulli(0.05)) n.OnReceivedPacket(seq, false, Timestamp::Micros(t));
    ++seq;
    t += 1000;
    if ((seq & 7) == 0) bench::DoNotOptimize(n.GetNackBatch(Timestamp::Micros(t)));
  }
  st.BudgetNsPerItem(400);
}

SM_BENCH(loss, RtxSender_StoreAndResend) {
  RtxSender s;
  RtpPacket p;
  p.payload.assign(1000, 1);
  uint16_t seq = 0;
  int64_t t = 0;
  std::vector<uint16_t> req(1);
  for (auto _ : st) {
    p.seq = seq++;
    s.OnPacketSent(p, Timestamp::Micros(t));
    req[0] = static_cast<uint16_t>(seq - 10);
    if ((seq & 15) == 0) bench::DoNotOptimize(s.OnNack(req, Timestamp::Micros(t), TimeDelta::Millis(50)));
    t += 1000;
  }
  st.BudgetNsPerItem(1500);
}

SM_BENCH(loss, FecEncode_10pkt_frame_50pct) {
  FecEncoder enc;
  enc.SetParams({0.5, FecMaskType::kInterleaved});
  std::vector<RtpPacket> frame(10);
  for (int i = 0; i < 10; ++i) {
    frame[static_cast<size_t>(i)].seq = static_cast<uint16_t>(i);
    frame[static_cast<size_t>(i)].payload.assign(1100, static_cast<uint8_t>(i));
  }
  for (auto _ : st) {
    for (int i = 0; i < 10; ++i) bench::DoNotOptimize(enc.AddMediaPacket(frame[static_cast<size_t>(i)], i == 9));
  }
  st.SetItemsPerIteration(10);  // Per media packet.
  st.BudgetNsPerItem(3000);
}

SM_BENCH(loss, FecDecode_RecoverOne) {
  FecEncoder enc;
  enc.SetParams({0.3, FecMaskType::kBlock});
  std::vector<RtpPacket> frame(10), fec;
  for (int i = 0; i < 10; ++i) {
    frame[static_cast<size_t>(i)].seq = static_cast<uint16_t>(i);
    frame[static_cast<size_t>(i)].payload.assign(1100, static_cast<uint8_t>(i));
    auto f = enc.AddMediaPacket(frame[static_cast<size_t>(i)], i == 9);
    fec.insert(fec.end(), f.begin(), f.end());
  }
  for (auto _ : st) {
    FecDecoder dec;
    for (int i = 1; i < 10; ++i) dec.OnMediaPacket(frame[static_cast<size_t>(i)]);
    for (auto& f : fec) bench::DoNotOptimize(dec.OnFecPacket(f));
  }
  st.SetItemsPerIteration(13);
  st.BudgetNsPerItem(5000);
}

SM_BENCH(loss, RedEncodeParse_d2) {
  RedEncoder enc(2);
  RedReceiver rx;
  std::vector<uint8_t> frame(80, 3);
  uint32_t ts = 0;
  for (auto _ : st) {
    auto pkt = enc.Encode(111, ts, frame);
    bench::DoNotOptimize(rx.OnPacket(pkt, ts));
    ts += 960;
  }
  st.BudgetNsPerItem(3000);
}

SM_BENCH(loss, Plc_Conceal10ms_16k) {
  PacketLossConcealer plc;
  auto x = sim::SpeechLike(16000, 1.0, 1, false);
  std::vector<float> buf(160);
  int f = 0;
  for (auto _ : st) {
    std::copy(x.begin() + (f % 90) * 160, x.begin() + (f % 90 + 1) * 160, buf.begin());
    plc.OnGoodFrame(buf);
    plc.Conceal(buf);  // Worst case: every other frame starts a concealment.
    ++f;
  }
  st.RequireRealtimeFactor(0.010, 50);  // One concealment per 10 ms frame.
}

SM_BENCH(loss, KeyframeRequester_Frame) {
  KeyframeRequester k;
  int64_t id = 0;
  for (auto _ : st) {
    FrameInfo f{id, id % 300 == 0, {}};
    if (!f.keyframe) f.references = {id - 1};
    k.OnFrame(f, Timestamp::Millis(id * 33));
    bench::DoNotOptimize(k.Process(Timestamp::Millis(id * 33)));
    bench::DoNotOptimize(k.TakeDecodableFrames());
    ++id;
  }
  st.BudgetNsPerItem(2000);
}

SM_BENCH(loss, TemporalLayers_ScheduleAndFilter) {
  TemporalLayerScheduler s(3);
  TemporalLayerFilter f;
  f.SetTargetLayer(1);
  for (auto _ : st) bench::DoNotOptimize(f.ShouldForward(s.NextFrame()));
  st.BudgetNsPerItem(500);
}

SM_BENCH(loss, ProtectionController_Update) {
  ProtectionController pc;
  double l = 0;
  for (auto _ : st) {
    bench::DoNotOptimize(pc.Update({TimeDelta::Millis(120), l, 1.0, 10}));
    l = l > 0.2 ? 0 : l + 0.001;
  }
  st.BudgetNsPerItem(20000);
}

// --- Quality ------------------------------------------------------------------

SM_BENCH_QUALITY(loss, Q_NackRtx_5pct_rtt80) {
  e2e::VideoSimParams p;
  p.fwd.loss_rate = 0.05;
  p.reverse_loss = 0.05;
  auto r = e2e::RunVideoSim(p);
  st.Counter("on_time", r.on_time);
  st.Counter("overhead", r.overhead);
  st.Require("on_time", r.on_time, ">=", 0.995);
  st.Require("overhead", r.overhead, "<=", 0.08);
}

SM_BENCH_QUALITY(loss, Q_Fec_5pct_rtt300) {
  e2e::VideoSimParams p;
  p.fwd.loss_rate = 0.05;
  p.one_way_delay = TimeDelta::Millis(150);
  p.nack = false;
  p.fec = {0.5, FecMaskType::kBlock};
  auto r = e2e::RunVideoSim(p);
  st.Counter("frames_on_time", r.frames_on_time);
  st.Counter("overhead", r.overhead);
  st.Require("frames_on_time", r.frames_on_time, ">=", 0.97);
}

SM_BENCH_QUALITY(loss, Q_Red_10pct) {
  sim::LinkConfig cfg;
  cfg.loss_rate = 0.10;
  auto r = e2e::RunRedSim(1, cfg, 20000, 3);
  st.Counter("raw_loss", r.raw_frame_loss);
  st.Counter("residual_loss", r.residual_frame_loss);
  st.Require("residual_loss", r.residual_frame_loss, "<=", 0.02);
}

SM_BENCH_QUALITY(loss, Q_Plc_10pct_speech) {
  sim::LinkConfig cfg;
  cfg.loss_rate = 0.10;
  auto r = e2e::RunPlcSim(cfg, 20.0, 11);
  st.Counter("snr_plc_db", r.snr_plc_db);
  st.Counter("snr_zero_fill_db", r.snr_zero_db);
  st.Counter("lost_frames_snr_db", r.lost_snr_plc_db);
  st.Require("snr_gain_db", r.snr_plc_db - r.snr_zero_db, ">=", 1.0);
}

SM_BENCH_QUALITY(loss, Q_Keyframe_2pct_frame_loss) {
  e2e::FrameSimParams p;
  p.frame_loss = 0.02;
  auto r = e2e::RunFrameSim(p);
  st.Counter("decodable", r.decodable_ratio);
  st.Counter("max_freeze_ms", r.max_freeze_ms);
  st.Counter("pli", static_cast<double>(r.pli));
  st.Require("decodable", r.decodable_ratio, ">=", 0.78);
  st.Require("max_freeze_ms", r.max_freeze_ms, "<=", 700);
}

SM_BENCH_QUALITY(loss, Q_TemporalLayers_5pct) {
  e2e::FrameSimParams p;
  p.frame_loss = 0.05;
  auto l1 = e2e::RunFrameSim(p);
  p.temporal_layers = 3;
  auto l3 = e2e::RunFrameSim(p);
  st.Counter("l1t1_decodable", l1.decodable_ratio);
  st.Counter("l1t3_decodable", l3.decodable_ratio);
  st.Require("decodable_gain", l3.decodable_ratio - l1.decodable_ratio, ">=", 0.05);
}

SM_BENCH_QUALITY(loss, Q_Protection_adaptive_rtt300) {
  e2e::VideoSimParams p;
  p.fwd.loss_rate = 0.05;
  p.reverse_loss = 0.05;
  p.one_way_delay = TimeDelta::Millis(150);
  p.adaptive = true;
  auto r = e2e::RunVideoSim(p);
  st.Counter("on_time", r.on_time);
  st.Counter("overhead", r.overhead);
  st.Require("on_time", r.on_time, ">=", 0.985);
  st.Require("overhead", r.overhead, "<=", 0.45);
}
