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
  // ULPFEC as RtpSenderEgress drives it: each media packet as it is sent, FEC at the frame end.
  ulpfec::UlpfecGenerator gen(118, 119);
  const ulpfec::FecProtectionParams params{.fec_rate = 128, .max_fec_frames = 1};
  gen.SetProtectionParameters(params, params);
  std::vector<RtpPacket> frame(10);
  for (int i = 0; i < 10; ++i) {
    frame[static_cast<size_t>(i)].seq = static_cast<uint16_t>(i);
    frame[static_cast<size_t>(i)].marker = i == 9;
    frame[static_cast<size_t>(i)].payload.assign(1100, static_cast<uint8_t>(i));
  }
  for (auto _ : st) {
    for (int i = 0; i < 10; ++i) {
      gen.AddPacketAndGenerateFec(frame[static_cast<size_t>(i)], false);
      bench::DoNotOptimize(gen.GetFecPackets());
    }
  }
  st.SetItemsPerIteration(10);  // Per media packet.
  st.BudgetNsPerItem(3000);
}

SM_BENCH(loss, FecDecode_RecoverOne) {
  // 10 media packets in RED, 3 FEC packets (30%), the first media packet lost.
  ulpfec::UlpfecGenerator gen(118, 119);
  const ulpfec::FecProtectionParams params{.fec_rate = 77, .max_fec_frames = 1};
  gen.SetProtectionParameters(params, params);
  std::vector<RtpPacket> red(10), fec;
  for (int i = 0; i < 10; ++i) {
    RtpPacket m;
    m.ssrc = 1;
    m.payload_type = 96;
    m.seq = static_cast<uint16_t>(i);
    m.marker = i == 9;
    m.payload.assign(1100, static_cast<uint8_t>(i));
    gen.AddPacketAndGenerateFec(m, false);
    red[static_cast<size_t>(i)] = m;
    red[static_cast<size_t>(i)].payload_type = 118;
    red[static_cast<size_t>(i)].payload.insert(red[static_cast<size_t>(i)].payload.begin(), 96);
    for (RtpPacket& f : gen.GetFecPackets()) fec.push_back(std::move(f));
  }
  for (size_t j = 0; j < fec.size(); ++j) fec[j].seq = static_cast<uint16_t>(10 + j);
  for (auto _ : st) {
    ulpfec::UlpfecReceiver rx(1, 119);
    for (int i = 1; i < 10; ++i) rx.AddReceivedRedPacket(red[static_cast<size_t>(i)]);
    for (auto& f : fec) rx.AddReceivedRedPacket(f);
    bench::DoNotOptimize(rx.ProcessReceivedFec());
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

SM_BENCH(loss, FecControllerDefault_UpdateFecRates) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, true);
  int l = 0;
  int64_t t = 0;
  for (auto _ : st) {
    bench::DoNotOptimize(c.UpdateFecRates(DataRate::KilobitsPerSec(1000), 30, static_cast<uint8_t>(l), 120, Timestamp::Millis(t)));
    l = l > 50 ? 0 : l + 1;
    t += 25;
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
  p.fec = {.fec_rate = 128, .max_fec_frames = 1, .fec_mask_type = ulpfec::FecMaskType::kRandom};
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
  // libwebrtc's decode time-out (3 s, then RTT + transit) is the only recovery from an unrepairable frame
  // (#167 deleted the v1 repair window, whose bounds were >= 0.78 and <= 700 ms); see E2E_Keyframe.
  st.Require("decodable", r.decodable_ratio, ">=", 0.25);
  st.Require("max_freeze_ms", r.max_freeze_ms, "<=", 3150);
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
