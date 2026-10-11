// Section 8 end-to-end tests.
#include <cmath>
#include <map>

#include "sm/core/random.h"
#include "sm/sim/network_link.h"
#include "sm/sync/capture_time.h"
#include "sm/sync/header_extensions.h"
#include "sm/sync/rtp_to_ntp.h"
#include "sm/sync/stream_sync.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::sync;
using timing::NtpTime;

namespace {
// Simulation time -> a wall-clock instant (Unix epoch based).
Timestamp Wall(Timestamp sim_now) { return Timestamp::Seconds(1700000000) + TimeDelta::Micros(sim_now.us()); }
}  // namespace

SM_TEST(E2E_RtpToNtp, MapsCaptureTimeDespiteDriftAndJitter) {
  // The sender's audio clock runs 80 ppm fast relative to its NTP clock.
  // SRs (every second) cross a jittery link; the receiver maps packet RTP
  // timestamps back to the sender's capture time.
  const double ppm = 80;
  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(30);
  lc.jitter_stddev = TimeDelta::Millis(10);
  sim::NetworkLink<std::pair<NtpTime, uint32_t>> link(lc, 1);
  RtpToNtpEstimator est;
  double max_err = 0;
  int checked = 0;
  for (int64_t t = 0; t < 120000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    auto rtp_at = [&](int64_t ms) { return static_cast<uint32_t>(std::llround(ms * 48.0 * (1 + ppm * 1e-6))); };
    if (t % 1000 == 0) link.Send(now, {NtpTime::FromTimestamp(Wall(now)), rtp_at(t)}, DataSize::Bytes(28));
    link.ReceiveEach(now, [&](std::pair<NtpTime, uint32_t> sr, Timestamp) { est.UpdateMeasurements(sr.first, sr.second); });
    if (t > 5000 && t % 20 == 0) {
      auto e = est.Estimate(rtp_at(t));
      if (e) {
        max_err = std::max(max_err, std::fabs((*e - Wall(now)).ms_f()));
        ++checked;
      }
    }
  }
  SM_RECORD("max_capture_time_error_ms", max_err);
  SM_RECORD("estimated_clock_hz", *est.EstimatedFrequencyHz());
  SM_EXPECT_GT(checked, 5000);
  SM_EXPECT_LT(max_err, 1.0);
  SM_EXPECT_NEAR(*est.EstimatedFrequencyHz(), 48000 * (1 + ppm * 1e-6), 0.5);
}

SM_TEST(E2E_StreamSync, LipSyncAcrossPathsAndRouteChange) {
  // Audio: 30 ms network, 60 ms jitter buffer. Video: 90 ms network (bigger
  // frames, pacing), 40 ms decode+render. At 40 s the video path gains
  // 120 ms (route change). Playout delay per stream = network base + buffer
  // + the extra delay requested by StreamSynchronization.
  sim::LinkConfig al, vl;
  al.base_delay = TimeDelta::Millis(30);
  al.jitter_stddev = TimeDelta::Millis(5);
  al.allow_reordering = false;
  vl.base_delay = TimeDelta::Millis(90);
  vl.jitter_stddev = TimeDelta::Millis(5);
  vl.allow_reordering = false;
  sim::NetworkLink<uint32_t> alink(al, 2), vlink(vl, 3);
  sim::NetworkLink<std::pair<bool, std::pair<NtpTime, uint32_t>>> rtcp(sim::LinkConfig{}, 4);
  StreamSynchronization sync;
  MediaSyncGroups groups;
  groups.AddStream(1111, "cam-1", MediaSyncGroups::Kind::kAudio);
  groups.AddStream(2222, "cam-1", MediaSyncGroups::Kind::kVideo);
  SM_ASSERT_EQ(groups.SyncPairs().size(), 1u);
  const TimeDelta audio_buffer = TimeDelta::Millis(60), video_buffer = TimeDelta::Millis(40);
  double before = 0, settled1 = 0, settled2 = 0;
  for (int64_t t = 0; t < 80000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t == 40000) {
      vl.base_delay = TimeDelta::Millis(210);
      vlink.SetConfig(vl);
    }
    if (t % 20 == 0) alink.Send(now, static_cast<uint32_t>(t * 48), DataSize::Bytes(100));
    if (t % 33 == 0) vlink.Send(now, static_cast<uint32_t>(t * 90), DataSize::Bytes(5000));
    if (t % 1000 == 0) {
      rtcp.Send(now, {true, {NtpTime::FromTimestamp(Wall(now)), static_cast<uint32_t>(t * 48)}}, DataSize::Bytes(28));
      rtcp.Send(now, {false, {NtpTime::FromTimestamp(Wall(now)), static_cast<uint32_t>(t * 90)}}, DataSize::Bytes(28));
    }
    alink.ReceiveEach(now, [&](uint32_t rtp, Timestamp at) { sync.OnAudioPacket(rtp, at); });
    vlink.ReceiveEach(now, [&](uint32_t rtp, Timestamp at) { sync.OnVideoFrame(rtp, at); });
    rtcp.ReceiveEach(now, [&](std::pair<bool, std::pair<NtpTime, uint32_t>> sr, Timestamp) {
      (sr.first ? sync.audio_map() : sync.video_map()).UpdateMeasurements(sr.second.first, sr.second.second);
    });
    if (t % 1000 == 500) sync.Update(audio_buffer, video_buffer, now);
    // True A/V offset of what the viewer sees.
    TimeDelta d_audio = al.base_delay + audio_buffer + sync.delays().audio_extra;
    TimeDelta d_video = vl.base_delay + video_buffer + sync.delays().video_extra;
    double off = (d_video - d_audio).ms_f();
    if (t == 0) before = off;  // No correction applied yet.
    if (t == 39000) settled1 = off;
    if (t == 79000) settled2 = off;
  }
  SM_RECORD("av_offset_before_sync_ms", before);
  SM_RECORD("av_offset_settled_ms", settled1);
  SM_RECORD("av_offset_after_route_change_ms", settled2);
  SM_RECORD("audio_extra_delay_ms", sync.delays().audio_extra.ms_f());
  SM_EXPECT_GT(before, 30.0);
  // ITU-R BT.1359: detectability threshold is about +45 ms / -125 ms.
  SM_EXPECT_LT(std::fabs(settled1), 35.0);
  SM_EXPECT_LT(std::fabs(settled2), 35.0);
}

SM_TEST(E2E_CaptureTime, GlassToGlassLatencyThroughSfu) {
  // Sender clock is 2.5 s ahead of the receiver's. The SFU rewrites RTP
  // timestamps and SSRCs but forwards abs-capture-time. The receiver
  // learns the clock offset from SRs and measures capture-to-render time.
  const TimeDelta sender_ahead = TimeDelta::Millis(2500);
  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(35);
  lc.jitter_stddev = TimeDelta::Millis(3);
  lc.allow_reordering = false;
  struct Pkt {
    uint32_t rtp;
    std::optional<std::vector<uint8_t>> ext;
    Timestamp capture_local;
  };
  sim::NetworkLink<Pkt> up(lc, 5), down(lc, 6);
  sim::NetworkLink<NtpTime> sr_link(lc, 7);
  AbsoluteCaptureTimeSender sender;
  AbsoluteCaptureTimeInterpolator interp;
  LatencyEstimator latency;
  const uint32_t sfu_rtp_offset = 0x12345678;
  double max_err = 0;
  int frames = 0;
  for (int64_t t = 0; t < 60000; ++t) {
    Timestamp now = Timestamp::Millis(t);
    if (t % 33 == 0) {
      Timestamp capture = now - TimeDelta::Millis(15);  // Camera pipeline latency.
      AbsoluteCaptureTime act{NtpTime::FromTimestamp(capture + sender_ahead).value, std::nullopt};
      uint32_t rtp = static_cast<uint32_t>((t - 15) * 90);
      auto ext = sender.OnPacket(1, rtp, 90000, act, now);
      up.Send(now, {rtp, ext ? std::optional(SerializeAbsoluteCaptureTime(*ext)) : std::nullopt, capture}, DataSize::Bytes(4000));
    }
    if (t % 1000 == 0) sr_link.Send(now, NtpTime::FromTimestamp(now + sender_ahead), DataSize::Bytes(28));
    up.ReceiveEach(now, [&](Pkt p, Timestamp) {
      p.rtp += sfu_rtp_offset;  // SFU rewrite.
      down.Send(now, p, DataSize::Bytes(4000));
    });
    sr_link.ReceiveEach(now, [&](NtpTime sr, Timestamp at) { latency.OnSenderReport(sr, at, TimeDelta::Millis(70)); });
    down.ReceiveEach(now, [&](Pkt p, Timestamp at) {
      std::optional<AbsoluteCaptureTime> ext;
      if (p.ext) ext = ParseAbsoluteCaptureTime(*p.ext);
      auto cap = interp.OnPacket(1, p.rtp, 90000, ext, at);
      Timestamp render = at + TimeDelta::Millis(30);
      if (!cap) return;
      auto l = latency.OnRendered(*cap, render);
      if (!l || t < 3000) return;
      ++frames;
      max_err = std::max(max_err, std::fabs((*l - (render - p.capture_local)).ms_f()));
    });
  }
  auto m = CollectMetrics(sender);
  SM_RECORD("latency_error_max_ms", max_err);
  SM_RECORD("extension_send_ratio", m["send_ratio"]);
  SM_EXPECT_GT(frames, 1500);
  // Error comes from the one-way-delay asymmetry assumption (RTT/2) and jitter.
  SM_EXPECT_LT(max_err, 10.0);
  SM_EXPECT_LT(m["send_ratio"], 0.05);
}

SM_TEST(E2E_HeaderExtensions, BundleDemuxOfSimulcastAudioAndRtx) {
  // One 5-tuple carries audio (mid a), simulcast video (mid v, rids h/m/l)
  // and RTX (repaired rid). Like WebRTC, senders stop including MID/RID
  // after the first packets of an SSRC; one stream changes SSRC mid-call.
  ExtensionMap map;
  map.Register(1, uri::kMid);
  map.Register(2, uri::kRid);
  map.Register(3, uri::kRepairedRid);
  map.Register(4, uri::kTransportCc);
  ExtensionAccessor acc(map);
  RtpDemuxer demux(map);
  demux.AddSink(0, "a", "", {111});
  demux.AddSink(1, "v", "h", {96, 97});
  demux.AddSink(2, "v", "m", {96, 97});
  demux.AddSink(3, "v", "l", {96, 97});
  struct Stream {
    uint32_t ssrc;
    std::string mid, rid;
    uint8_t pt;
    int sink;
    bool rtx;
    int sent = 0;
  };
  std::vector<Stream> streams = {{100, "a", "", 111, 0, false},  {200, "v", "h", 96, 1, false},
                                 {201, "v", "m", 96, 2, false},  {202, "v", "l", 96, 3, false},
                                 {300, "v", "h", 97, 1, true}};
  sim::LinkConfig lc;
  lc.jitter_stddev = TimeDelta::Millis(5);  // Reordering.
  sim::NetworkLink<std::pair<std::vector<uint8_t>, int>> link(lc, 8);
  Random rng(9);
  uint16_t tseq = 0;
  for (int64_t t = 0; t < 20000; t += 5) {
    if (t == 10000) streams[2].ssrc = 251, streams[2].sent = 0;  // SSRC change.
    Stream& s = streams[static_cast<size_t>(rng.UniformInt(0, 4))];
    RtpPacket p;
    p.ssrc = s.ssrc;
    p.payload_type = s.pt;
    p.seq = static_cast<uint16_t>(s.sent);
    if (s.sent < 10) {  // Identification extensions on the first packets only.
      acc.SetString(p, uri::kMid, s.mid);
      if (!s.rid.empty()) acc.SetString(p, s.rtx ? uri::kRepairedRid : uri::kRid, s.rid);
    }
    acc.SetTransportSeq(p, tseq++);
    ++s.sent;
    link.Send(Timestamp::Millis(t), {p.Serialize(), s.sink}, DataSize::Bytes(100));
  }
  int total = 0, correct = 0;
  link.ReceiveEach(Timestamp::Seconds(100), [&](std::pair<std::vector<uint8_t>, int> pk, Timestamp) {
    auto p = RtpPacket::Parse(pk.first);
    auto sink = demux.Demux(*p);
    ++total;
    correct += sink && *sink == pk.second;
  });
  SM_RECORD("demux_accuracy", static_cast<double>(correct) / total);
  SM_RECORD("demux_by_ssrc", demux.stats().by_ssrc);
  SM_RECORD("demux_by_mid", demux.stats().by_mid);
  SM_RECORD("demux_unroutable", demux.stats().unroutable);
  // Reordering can deliver a packet without MID/RID before the first one
  // that binds its SSRC; those are the only acceptable misses.
  SM_EXPECT_GE(static_cast<double>(correct) / total, 0.995);
  SM_EXPECT_GT(demux.stats().by_ssrc, demux.stats().by_mid);
}
