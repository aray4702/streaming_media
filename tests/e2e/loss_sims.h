// End-to-end simulations for section 3, shared by the e2e tests and the
// quality benchmarks. Each runs a sender, a simulated lossy network (both
// directions) and a receiver, and reports delivery/quality metrics.
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include "sm/core/dsp.h"
#include "sm/loss/fec.h"
#include "sm/loss/keyframe_request.h"
#include "sm/loss/nack_tracker.h"
#include "sm/loss/plc.h"
#include "sm/loss/protection_controller.h"
#include "sm/loss/red.h"
#include "sm/loss/rtx.h"
#include "sm/loss/temporal_layers.h"
#include "sm/sim/network_link.h"
#include "sm/sim/signals.h"

namespace sm::e2e {

// --- Video packet transport: NACK/RTX + FEC + adaptive protection ----------

struct VideoSimParams {
  sim::LinkConfig fwd;
  TimeDelta one_way_delay = TimeDelta::Millis(40);
  double reverse_loss = 0.0;
  bool nack = true;
  bool adaptive = false;                     // ProtectionController decides FEC.
  loss::FecParams fec;                       // Fixed FEC when !adaptive.
  int packets_per_frame = 8;
  int fps = 30;
  TimeDelta duration = TimeDelta::Seconds(20);
  TimeDelta deadline = TimeDelta::Millis(250);  // Playout budget per packet.
  uint64_t seed = 1;
};

struct VideoSimResult {
  double on_time = 0;         // Packets available before the deadline.
  double eventually = 0;      // Packets available at all.
  double frames_on_time = 0;  // Frames with every packet on time.
  double overhead = 0;        // (RTX + FEC bytes) / media bytes.
  double raw_loss = 0;
  uint64_t nack_requests = 0, retransmitted = 0, fec_recovered = 0;
};

inline VideoSimResult RunVideoSim(VideoSimParams p) {
  using namespace sm::loss;
  p.fwd.base_delay = p.one_way_delay;
  sim::NetworkLink<RtpPacket> fwd(p.fwd, p.seed);
  sim::LinkConfig rcfg;
  rcfg.base_delay = p.one_way_delay;
  rcfg.loss_rate = p.reverse_loss;
  sim::NetworkLink<std::vector<uint16_t>> rev(rcfg, p.seed + 1);

  RtxConfig rtx_cfg;
  rtx_cfg.rtx_ssrc = 2;
  RtxSender rtx(rtx_cfg);
  FecConfig fec_cfg;
  FecEncoder fec_enc(fec_cfg);
  fec_enc.SetParams(p.fec);
  FecDecoder fec_dec(fec_cfg);
  NackTracker nack;
  const TimeDelta rtt = p.one_way_delay * 2;
  nack.UpdateRtt(rtt);
  ProtectionController controller;

  std::vector<Timestamp> sent_at;
  std::vector<Timestamp> arrived;
  int64_t media_bytes = 0, extra_bytes = 0;
  uint16_t seq = 0;
  // Receiver-side loss/burst statistics for the adaptive controller.
  uint64_t last_missing = 0, last_received = 0;
  int64_t highest = -1;
  uint64_t gap_events = 0, last_gap_events = 0;

  auto on_media = [&](const RtpPacket& m, Timestamp now) {
    int64_t s = m.seq;  // Unwrapped below (single run < 65536 packets).
    if (s < static_cast<int64_t>(arrived.size()) && !arrived[static_cast<size_t>(s)].IsFinite()) {
      arrived[static_cast<size_t>(s)] = now;
    }
    if (s > highest + 1) ++gap_events;
    highest = std::max(highest, s);
    nack.OnReceivedPacket(m.seq, s == 0, now);
  };

  const int frame_interval_ms = 1000 / p.fps;
  const int64_t end_ms = p.duration.ms() + 2000;
  for (int64_t t = 0; t <= end_ms; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t < p.duration.ms() && t % frame_interval_ms == 0) {
      for (int i = 0; i < p.packets_per_frame; ++i) {
        RtpPacket m;
        m.seq = seq++;
        m.ssrc = 1;
        m.payload_type = 96;
        m.timestamp = static_cast<uint32_t>(t * 90);
        m.marker = i + 1 == p.packets_per_frame;
        m.payload.assign(1000, static_cast<uint8_t>(m.seq));
        sent_at.push_back(now);
        arrived.push_back(Timestamp::MinusInfinity());
        media_bytes += static_cast<int64_t>(m.size());
        rtx.OnPacketSent(m, now);
        for (RtpPacket& f : fec_enc.AddMediaPacket(m, m.marker)) {
          extra_bytes += static_cast<int64_t>(f.size());
          fwd.Send(now, f, DataSize::Bytes(static_cast<int64_t>(f.size())));
        }
        fwd.Send(now, m, DataSize::Bytes(static_cast<int64_t>(m.size())));
      }
    }
    fwd.ReceiveEach(now, [&](RtpPacket pkt, Timestamp at) {
      if (pkt.payload_type == 96) {
        on_media(pkt, at);
        for (auto& r : fec_dec.OnMediaPacket(pkt)) on_media(r, at);
      } else if (pkt.payload_type == 97) {
        if (auto m = RestoreFromRtx(pkt, 1, 96)) {
          on_media(*m, at);
          for (auto& r : fec_dec.OnMediaPacket(*m)) on_media(r, at);
        }
      } else {
        for (auto& r : fec_dec.OnFecPacket(pkt)) on_media(r, at);
      }
    });
    if (p.nack) {
      auto batch = nack.GetNackBatch(now);
      if (!batch.empty()) rev.Send(now, batch, DataSize::Bytes(16 + 2 * static_cast<int64_t>(batch.size())));
    }
    rev.ReceiveEach(now, [&](std::vector<uint16_t> seqs, Timestamp) {
      for (RtpPacket& r : rtx.OnNack(seqs, now, rtt)) {
        extra_bytes += static_cast<int64_t>(r.size());
        fwd.Send(now, r, DataSize::Bytes(static_cast<int64_t>(r.size())));
      }
    });
    if (p.adaptive && t % 500 == 0 && t > 0) {
      uint64_t missing = nack.stats().missing_detected - last_missing;
      uint64_t received = nack.stats().packets_received - last_received;
      uint64_t gaps = gap_events - last_gap_events;
      last_missing = nack.stats().missing_detected;
      last_received = nack.stats().packets_received;
      last_gap_events = gap_events;
      double loss = missing + received ? static_cast<double>(missing) / static_cast<double>(missing + received) : 0;
      double burst = gaps ? static_cast<double>(missing) / static_cast<double>(gaps) : 1.0;
      auto d = controller.Update({rtt, loss, burst, static_cast<double>(p.packets_per_frame)}, now);
      fec_enc.SetParams(d.fec);
    }
  }

  VideoSimResult r;
  size_t n = sent_at.size(), on_time = 0, ever = 0;
  size_t frames = 0, frames_ok = 0;
  for (size_t i = 0; i < n; ++i) {
    if (arrived[i].IsFinite()) {
      ++ever;
      if (arrived[i] - sent_at[i] <= p.deadline) ++on_time;
    }
  }
  for (size_t f = 0; f + static_cast<size_t>(p.packets_per_frame) <= n; f += static_cast<size_t>(p.packets_per_frame)) {
    ++frames;
    bool ok = true;
    for (size_t i = f; i < f + static_cast<size_t>(p.packets_per_frame); ++i) {
      ok = ok && arrived[i].IsFinite() && arrived[i] - sent_at[i] <= p.deadline;
    }
    frames_ok += ok;
  }
  r.on_time = static_cast<double>(on_time) / static_cast<double>(n);
  r.eventually = static_cast<double>(ever) / static_cast<double>(n);
  r.frames_on_time = static_cast<double>(frames_ok) / static_cast<double>(frames);
  r.overhead = static_cast<double>(extra_bytes) / static_cast<double>(media_bytes);
  r.raw_loss = static_cast<double>(fwd.stats().lost) / static_cast<double>(fwd.stats().sent);
  r.nack_requests = nack.stats().nack_requests;
  r.retransmitted = rtx.stats().retransmitted;
  r.fec_recovered = fec_dec.stats().recovered;
  return r;
}

// --- Audio: RED redundancy ---------------------------------------------------

struct RedSimResult {
  double raw_frame_loss = 0;
  double residual_frame_loss = 0;
  double overhead = 0;
};

inline RedSimResult RunRedSim(int distance, const sim::LinkConfig& link_cfg, int frames, uint64_t seed) {
  using namespace sm::loss;
  sim::NetworkLink<std::pair<uint32_t, std::vector<uint8_t>>> link(link_cfg, seed);
  RedEncoder enc(distance);
  RedReceiver rx;
  Random rng(seed);
  std::vector<bool> got(static_cast<size_t>(frames), false);
  for (int i = 0; i < frames; ++i) {
    std::vector<uint8_t> frame(static_cast<size_t>(rng.UniformInt(60, 90)), static_cast<uint8_t>(i));
    uint32_t ts = static_cast<uint32_t>(i) * 960;
    auto red = enc.Encode(111, ts, frame);
    link.Send(Timestamp::Millis(i * 20), {ts, red}, DataSize::Bytes(static_cast<int64_t>(red.size()) + 12));
  }
  for (auto& [ts, payload] : link.Receive(Timestamp::Seconds(3600))) {
    for (auto& b : rx.OnPacket(payload, ts)) got[b.timestamp / 960] = true;
  }
  RedSimResult r;
  r.raw_frame_loss = static_cast<double>(link.stats().lost) / frames;
  r.residual_frame_loss = static_cast<double>(std::count(got.begin(), got.end(), false)) / frames;
  r.overhead = static_cast<double>(enc.stats().redundant_bytes) / static_cast<double>(enc.stats().primary_bytes);
  return r;
}

// --- Audio: PLC ---------------------------------------------------------------

struct PlcSimResult {
  double snr_plc_db = 0;        // Whole signal.
  double snr_zero_db = 0;
  double lost_snr_plc_db = 0;   // Lost frames only.
  double lost_snr_zero_db = 0;  // Always 0 dB by definition.
  double lost_level_ratio = 0;  // RMS(concealed)/RMS(original) on lost frames.
  double loss = 0;
};

inline PlcSimResult RunPlcSim(const sim::LinkConfig& link_cfg, double seconds, uint64_t seed) {
  const int fs = 16000, frame = 160;
  auto x = sim::SpeechLike(fs, seconds, seed, false);
  size_t frames = x.size() / frame;
  sim::NetworkLink<size_t> link(link_cfg, seed + 7);
  for (size_t f = 0; f < frames; ++f) link.Send(Timestamp::Millis(static_cast<int64_t>(f * 10)), f, DataSize::Bytes(40));
  std::vector<bool> got(frames, false);
  for (size_t f : link.Receive(Timestamp::Seconds(3600))) got[f] = true;

  loss::PacketLossConcealer plc;
  std::vector<float> out_plc(frames * frame), out_zero(frames * frame, 0.0f);
  std::vector<float> lost_ref, lost_plc;
  std::vector<float> buf(frame);
  size_t lost = 0;
  for (size_t f = 0; f < frames; ++f) {
    auto src = x.begin() + static_cast<std::ptrdiff_t>(f * frame);
    if (got[f]) {
      std::copy(src, src + frame, buf.begin());
      plc.OnGoodFrame(buf);
      std::copy(src, src + frame, out_zero.begin() + static_cast<std::ptrdiff_t>(f * frame));
    } else {
      ++lost;
      plc.Conceal(buf);
      lost_ref.insert(lost_ref.end(), src, src + frame);
      lost_plc.insert(lost_plc.end(), buf.begin(), buf.end());
    }
    std::copy(buf.begin(), buf.end(), out_plc.begin() + static_cast<std::ptrdiff_t>(f * frame));
  }
  std::span<const float> ref(x.data(), frames * frame);
  PlcSimResult r;
  r.loss = static_cast<double>(lost) / static_cast<double>(frames);
  r.snr_plc_db = dsp::SnrDb(ref, out_plc);
  r.snr_zero_db = dsp::SnrDb(ref, out_zero);
  r.lost_snr_plc_db = dsp::SnrDb(lost_ref, lost_plc);
  r.lost_snr_zero_db = 0.0;
  double rr = dsp::Rms(lost_ref);
  r.lost_level_ratio = rr > 0 ? dsp::Rms(lost_plc) / rr : 0;
  return r;
}

// --- Video frames: keyframe requests and temporal layers ---------------------

struct FrameSimParams {
  int temporal_layers = 1;
  double frame_loss = 0.05;        // Whole-frame loss after NACK has given up.
  bool gilbert = false;
  TimeDelta one_way_delay = TimeDelta::Millis(50);
  bool keyframe_requests = true;
  int fps = 30;
  TimeDelta duration = TimeDelta::Seconds(30);
  uint64_t seed = 1;
};

struct FrameSimResult {
  double decodable_ratio = 0;
  double max_freeze_ms = 0;
  double total_freeze_ms = 0;    // Sum of gaps above 3 frame intervals.
  uint64_t pli = 0, lntf = 0, keyframes_sent = 0, frames = 0;
};

inline FrameSimResult RunFrameSim(const FrameSimParams& p) {
  using namespace sm::loss;
  sim::LinkConfig fcfg;
  fcfg.base_delay = p.one_way_delay;
  if (p.gilbert) {
    fcfg.gilbert = true;
    fcfg.p_bad_to_good = 0.5;
    fcfg.p_good_to_bad = p.frame_loss * fcfg.p_bad_to_good / (1 - p.frame_loss);
  } else {
    fcfg.loss_rate = p.frame_loss;
  }
  sim::NetworkLink<LayerFrame> fwd(fcfg, p.seed);
  sim::LinkConfig rcfg;
  rcfg.base_delay = p.one_way_delay;
  sim::NetworkLink<FeedbackType> rev(rcfg, p.seed + 1);
  TemporalLayerScheduler sched(p.temporal_layers);
  KeyframeRequester req;
  req.UpdateRtt(p.one_way_delay * 2);
  bool force_key = false;
  FrameSimResult r;
  const int interval = 1000 / p.fps;
  Timestamp last_render = Timestamp::Millis(0);
  for (int64_t t = 0; t <= p.duration.ms() + 500; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t < p.duration.ms() && t % interval == 0) {
      LayerFrame f = sched.NextFrame(force_key);
      if (f.keyframe) ++r.keyframes_sent;
      force_key = false;
      ++r.frames;
      fwd.Send(now, f, DataSize::Bytes(f.keyframe ? 20000 : 4000));
    }
    fwd.ReceiveEach(now, [&](LayerFrame f, Timestamp at) { req.OnFrame(f.info, at); });
    for (auto id : req.TakeDecodableFrames()) {
      (void)id;
      if (now - last_render > TimeDelta::Millis(3 * interval)) {
        r.total_freeze_ms += (now - last_render).ms_f();
      }
      r.max_freeze_ms = std::max(r.max_freeze_ms, (now - last_render).ms_f());
      last_render = now;
      r.decodable_ratio += 1;
    }
    for (auto& m : req.Process(now)) {
      if (m.type == FeedbackType::kLntf) ++r.lntf;
      if (!p.keyframe_requests || m.type == FeedbackType::kLntf) continue;
      ++r.pli;
      rev.Send(now, m.type, DataSize::Bytes(12));
    }
    rev.ReceiveEach(now, [&](FeedbackType, Timestamp) { force_key = true; });
  }
  r.decodable_ratio /= static_cast<double>(r.frames);
  return r;
}

}  // namespace sm::e2e
