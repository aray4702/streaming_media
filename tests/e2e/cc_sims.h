// End-to-end congestion-control simulation for section 5.
//
// Sender: synthetic video encoder (30 fps, optional overshoot, keyframes
// on start/request or periodically) + audio (50 pps) -> [frame dropper] -> pacer -> transport-wide
// sequence numbers -> bottleneck link (capacity schedule, drop-tail queue,
// loss, outages). Receiver: transport feedback every 50 ms over the reverse
// link -> GCC -> target rate -> [allocator] -> [degradation controller] ->
// encoder. Optional outage monitor.
#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "sm/cc/bitrate_allocator.h"
#include "sm/cc/frame_dropper.h"
#include "sm/cc/outage_monitor.h"
#include "sm/cc/pacer.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"
#include "sm/cc/video_adaptation.h"
#include "sm/core/random.h"
#include "sm/sim/network_link.h"

namespace sm::e2e {

struct CcSimParams {
  std::vector<std::pair<int64_t, int64_t>> capacity_kbps = {{0, 2000}};  // (start ms, kbps)
  std::vector<std::pair<int64_t, int64_t>> outages;                       // [start, end) ms
  TimeDelta one_way = TimeDelta::Millis(30);
  double loss = 0;
  std::vector<std::pair<int64_t, double>> loss_schedule;  // (start ms, loss), overrides `loss`.
  DataSize queue_limit = DataSize::Bytes(200000);
  double seconds = 60;
  bool use_pacer = true;
  bool pacer_v2 = false;  // libwebrtc's PacingController instead of the library's own pacer.
  bool probing = true;
  bool audio = true;
  DataRate start_rate = DataRate::KilobitsPerSec(300);
  DataRate max_rate = DataRate::KilobitsPerSec(8000);
  double overshoot = 1.0;
  bool frame_dropper = false;
  bool adaptation = false;
  cc::DegradationPreference preference = cc::DegradationPreference::kBalanced;
  bool allocator = false;  // audio + camera + screen share
  bool outage_monitor = false;
  // Periodic keyframes (0 = only the first one and on request, which is the
  // WebRTC default).
  int64_t keyframe_interval_ms = 0;
  uint64_t seed = 1;
};

struct PhaseStats {
  int64_t start_ms = 0, end_ms = 0, capacity_kbps = 0;
  double throughput_kbps = 0;  // Delivered, second half of the phase.
  double target_kbps = 0;      // Mean target, second half of the phase.
  double queue_p95_ms = 0;     // Second half of the phase.
  cc::VideoFormat format;
  std::map<std::string, double> alloc_kbps;
};

struct CcSimResult {
  std::vector<PhaseStats> phases;
  double queue_p95_ms = 0;
  double audio_p95_ms = 0;
  double video_p95_ms = 0;
  double loss_ratio = 0;
  double ramp_s = -1;  // Time for target to reach 80% of the first capacity.
  uint64_t frames = 0, frames_dropped = 0;
  double video_sent_kbps = 0, video_target_kbps = 0;
  uint64_t video_packets_while_down = 0;
  double recovery_s = -1;  // After the last outage: target >= 50% capacity.
  uint64_t ice_restarts = 0, keyframes = 0;
  double min_audio_alloc_kbps = 1e18;
};

namespace detail {
struct SimPacket {
  uint16_t tseq;
  Timestamp sent;
  int kind;  // 0 audio, 1 video, 2 padding
  int64_t size;
};
inline double P95(std::vector<double> v) {
  if (v.empty()) return 0;
  size_t k = std::min(v.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(v.size())));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}
}  // namespace detail

inline CcSimResult RunCcSim(const CcSimParams& p) {
  using namespace sm::cc;
  using detail::SimPacket;
  sim::LinkConfig fcfg;
  fcfg.base_delay = p.one_way;
  fcfg.loss_rate = p.loss;
  fcfg.queue_limit = p.queue_limit;
  fcfg.capacity = DataRate::KilobitsPerSec(p.capacity_kbps.front().second);
  sim::NetworkLink<SimPacket> fwd(fcfg, p.seed);
  sim::LinkConfig rcfg;
  rcfg.base_delay = p.one_way;
  sim::NetworkLink<std::vector<uint8_t>> rev(rcfg, p.seed + 1);

  GccConfig gcfg;
  gcfg.start_rate = p.start_rate;
  gcfg.max_rate = p.max_rate;
  gcfg.enable_probing = p.probing;
  GccController gcc(gcfg);
  gcc.OnRttUpdate(p.one_way * 2);
  PacerConfig pacer_cfg;
  if (p.pacer_v2) pacer_cfg.version = PacerVersion::kV2;
  Pacer pacer(pacer_cfg);
  TransportFeedbackBuilder fb_builder;
  FrameDropper dropper;
  DegradationConfig dcfg;
  dcfg.preference = p.preference;
  DegradationController degradation(dcfg);
  BitrateAllocator alloc;
  alloc.AddStream({"audio", DataRate::KilobitsPerSec(20), DataRate::KilobitsPerSec(40), 1.0, true});
  alloc.AddStream({"camera", DataRate::KilobitsPerSec(150), DataRate::KilobitsPerSec(2500), 1.0, false});
  alloc.AddStream({"screen", DataRate::KilobitsPerSec(250), DataRate::KilobitsPerSec(1500), 2.0, false});
  OutageMonitor outage;
  Random rng(p.seed + 2);

  CcSimResult r;
  DataRate target = p.start_rate;
  DataRate video_target = target;
  std::map<std::string, DataRate> allocation;
  bool encoder_paused = false;
  bool force_keyframe = true;
  uint16_t tseq = 0;
  uint64_t next_id = 0;
  std::map<uint64_t, std::pair<int, DataSize>> pending;  // pacer id -> (kind, size)

  auto apply = [&](const NetworkControlUpdate& u, Timestamp now) {
    target = u.target_rate;
    pacer.SetPacingRates(u.pacing_rate, DataRate::Zero());
    for (const auto& c : u.probes) pacer.AddProbeCluster(c);
    outage.SetCurrentRate(target);
    DataRate audio_rate = p.audio ? DataRate::KilobitsPerSec(40) : DataRate::Zero();
    if (p.allocator) {
      allocation = alloc.Allocate(target);
      video_target = allocation["camera"] + allocation["screen"];
      r.min_audio_alloc_kbps = std::min(r.min_audio_alloc_kbps, allocation["audio"].kbps());
    } else {
      video_target = std::max(DataRate::KilobitsPerSec(30), target - audio_rate);
    }
    if (p.adaptation) degradation.OnTargetBitrate(video_target, now);
  };
  apply(gcc.Start(Timestamp::Millis(0)), Timestamp::Millis(0));

  std::vector<double> qdelay, adelay, vdelay;
  std::vector<std::pair<int64_t, double>> qdelay_t;  // (ms, delay)
  std::vector<std::pair<int64_t, int64_t>> delivered;  // (ms, bytes) media only
  std::vector<std::pair<int64_t, double>> targets;
  int64_t video_bytes_sent = 0;
  double video_target_sum = 0;
  int64_t video_target_n = 0;
  size_t cap_idx = 0;
  const int64_t end_ms = static_cast<int64_t>(p.seconds * 1000);
  int64_t last_outage_end = -1;
  bool link_down = false;

  std::vector<int64_t> phase_end;
  for (size_t i = 0; i < p.capacity_kbps.size(); ++i) {
    phase_end.push_back(i + 1 < p.capacity_kbps.size() ? p.capacity_kbps[i + 1].first : end_ms);
  }
  std::vector<cc::VideoFormat> phase_format(phase_end.size());
  std::vector<std::map<std::string, double>> phase_alloc(phase_end.size());

  for (int64_t t = 0; t < end_ms; ++t) {
    Timestamp now = Timestamp::Millis(t);
    for (size_t i = 0; i < phase_end.size(); ++i) {
      if (t + 1 == phase_end[i]) {
        phase_format[i] = degradation.current();
        for (auto& [k, v] : allocation) phase_alloc[i][k] = v.kbps();
      }
    }
    // Network schedule.
    if (cap_idx + 1 < p.capacity_kbps.size() && t >= p.capacity_kbps[cap_idx + 1].first) {
      ++cap_idx;
      fcfg.capacity = DataRate::KilobitsPerSec(p.capacity_kbps[cap_idx].second);
      fwd.SetConfig(fcfg);
    }
    for (auto [start, l] : p.loss_schedule) {
      if (t == start) {
        fcfg.loss_rate = l;
        fwd.SetConfig(fcfg);
      }
    }
    bool down = false;
    for (auto [a, b] : p.outages) {
      down = down || (t >= a && t < b);
      if (t == b) last_outage_end = b;
    }
    if (down != link_down) {
      link_down = down;
      fcfg.down = down;
      rcfg.down = down;
      fwd.SetConfig(fcfg);
      rev.SetConfig(rcfg);
    }
    // Media sources.
    if (t % 20 == 0 && p.audio) {
      uint64_t id = next_id++;
      pending[id] = {0, DataSize::Bytes(120)};
      pacer.Enqueue({id, PacketPriority::kAudio, DataSize::Bytes(120), now});
    }
    if (t % 33 == 0 && !encoder_paused) {
      ++r.frames;
      double fps = 30;
      DataRate enc_target = video_target;
      if (p.frame_dropper) dropper.SetRates(enc_target, fps);
      if (p.frame_dropper && dropper.ShouldDrop()) {
        ++r.frames_dropped;
      } else {
        bool key = force_keyframe || (p.keyframe_interval_ms > 0 && t % p.keyframe_interval_ms < 33);
        force_keyframe = false;
        if (key) ++r.keyframes;
        double bytes = enc_target.bps() / 8.0 / fps * rng.Uniform(0.85, 1.15) * p.overshoot;
        if (key) bytes *= 3;
        int64_t size = std::max<int64_t>(200, static_cast<int64_t>(bytes));
        if (p.frame_dropper) dropper.OnEncodedFrame(DataSize::Bytes(size), key);
        video_target_sum += enc_target.kbps();
        ++video_target_n;
        for (int64_t off = 0; off < size; off += 1200) {
          int64_t ps = std::min<int64_t>(1200, size - off) + 40;
          uint64_t id = next_id++;
          pending[id] = {1, DataSize::Bytes(ps)};
          pacer.Enqueue({id, PacketPriority::kVideo, DataSize::Bytes(ps), now});
        }
      }
    }
    // Pacer (or no pacer: flush everything immediately).
    std::vector<PacedPacket> to_send;
    if (p.use_pacer) {
      to_send = pacer.Process(now);
    } else {
      pacer.SetPacingRates(DataRate::KilobitsPerSec(1000000), DataRate::Zero());
      to_send = pacer.Process(now);
    }
    for (const PacedPacket& pp : to_send) {
      int kind = pp.is_padding ? 2 : pending[pp.id].first;
      pending.erase(pp.id);
      if (kind == 1) {
        video_bytes_sent += pp.size.bytes();
        if (link_down) ++r.video_packets_while_down;
      }
      gcc.OnPacketSent(tseq, now, pp.size, pp.probe_cluster);
      fwd.Send(now, SimPacket{tseq, now, kind, pp.size.bytes()}, pp.size);
      ++tseq;
    }
    // Receiver.
    fwd.ReceiveEach(now, [&](SimPacket sp, Timestamp at) {
      fb_builder.OnPacket(sp.tseq, at);
      double q = (at - sp.sent - p.one_way).ms_f();
      if (sp.kind != 2) {
        qdelay.push_back(q);
        qdelay_t.emplace_back(t, q);
        delivered.emplace_back(t, sp.size);
      }
      if (sp.kind == 0) adelay.push_back(q);
      if (sp.kind == 1) vdelay.push_back(q);
    });
    if (t % 50 == 0) {
      for (const auto& fb : fb_builder.Build()) {
        auto bytes = SerializeTransportFeedback(fb);
        rev.Send(now, bytes, DataSize::Bytes(static_cast<int64_t>(bytes.size())));
      }
    }
    rev.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp) {
      if (auto fb = ParseTransportFeedback(bytes)) {
        if (p.outage_monitor) outage.OnFeedback(now);
        auto u = gcc.OnTransportFeedback(*fb, now);
        if (!p.outage_monitor || outage.state() == LinkState::kUp) apply(u, now);
      }
    });
    if (t % 25 == 0) {
      auto u = gcc.OnProcessInterval(now, video_target);
      apply(u, now);
      if (p.outage_monitor) {
        OutageActions a = outage.Process(now);
        if (a.pause_encoder) {
          encoder_paused = true;
          pacer.Pause();
        }
        if (a.ice_restart) ++r.ice_restarts;
        if (a.resume_encoder) {
          encoder_paused = false;
          pacer.Resume();
        }
        if (a.request_keyframe) force_keyframe = true;
        if (a.reset_estimate) apply(gcc.ResetEstimate(a.reset_rate, now), now);
      }
      targets.emplace_back(t, target.kbps());
      if (r.ramp_s < 0 && target.kbps() >= 0.8 * static_cast<double>(p.capacity_kbps.front().second)) {
        r.ramp_s = static_cast<double>(t) / 1000.0;
      }
      if (last_outage_end >= 0 && r.recovery_s < 0 && t > last_outage_end) {
        int64_t cap = 0;
        for (auto [s, c] : p.capacity_kbps) {
          if (t >= s) cap = c;
        }
        if (target.kbps() >= 0.5 * static_cast<double>(cap)) r.recovery_s = static_cast<double>(t - last_outage_end) / 1000.0;
      }
    }
  }
  // Delivered bytes per ms for throughput.
  const auto& ls = fwd.stats();
  r.loss_ratio = static_cast<double>(ls.lost + ls.queue_dropped) / static_cast<double>(std::max<uint64_t>(1, ls.sent));
  r.queue_p95_ms = detail::P95(qdelay);
  r.audio_p95_ms = detail::P95(adelay);
  r.video_p95_ms = detail::P95(vdelay);
  r.video_sent_kbps = video_bytes_sent * 8.0 / p.seconds / 1000.0;
  r.video_target_kbps = video_target_n ? video_target_sum / static_cast<double>(video_target_n) : 0;
  // Phase stats.
  for (size_t i = 0; i < p.capacity_kbps.size(); ++i) {
    PhaseStats ph;
    ph.start_ms = p.capacity_kbps[i].first;
    ph.end_ms = i + 1 < p.capacity_kbps.size() ? p.capacity_kbps[i + 1].first : end_ms;
    ph.capacity_kbps = p.capacity_kbps[i].second;
    int64_t mid = (ph.start_ms + ph.end_ms) / 2;
    double tsum = 0;
    int tn = 0;
    for (auto [tt, v] : targets) {
      if (tt >= mid && tt < ph.end_ms) {
        tsum += v;
        ++tn;
      }
    }
    ph.target_kbps = tn ? tsum / tn : 0;
    std::vector<double> q;
    for (auto [tt, v] : qdelay_t) {
      if (tt >= mid && tt < ph.end_ms) q.push_back(v);
    }
    ph.queue_p95_ms = detail::P95(q);
    int64_t bytes = 0;
    for (auto [tt, b] : delivered) {
      if (tt >= mid && tt < ph.end_ms) bytes += b;
    }
    ph.throughput_kbps = ph.end_ms > mid ? bytes * 8.0 / static_cast<double>(ph.end_ms - mid) : 0;
    ph.format = phase_format[i];
    ph.alloc_kbps = phase_alloc[i];
    r.phases.push_back(ph);
  }
  return r;
}

}  // namespace sm::e2e
