// The test bench's simulated SFU (C21, #1180). See sfu.h.
#include "sfu.h"

#include <algorithm>
#include <cmath>

#include "sm/cc/transport_feedback.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/h264.h"
#include "sm/sync/header_extensions.h"
#include "sm/timing/rtcp_stats.h"

namespace sm::testbench {

namespace {

bool SeqNewer(uint16_t a, uint16_t b) { return a != b && static_cast<int16_t>(static_cast<uint16_t>(a - b)) > 0; }

// The ingress session decodes nothing: its audio stream only reports to the publisher.
class NullAudioDecoder : public rtp::AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t>, uint32_t) override { return std::vector<float>(480, 0.f); }
};

constexpr int64_t kUdpIpOverhead = 28;
constexpr size_t kCachePackets = 2000;            // Per layer: about 10 s of a 1.5 Mbps layer.
constexpr TimeDelta kRateWindow = TimeDelta::Seconds(1);
constexpr TimeDelta kGccInterval = TimeDelta::Millis(25);
constexpr TimeDelta kAudioFlushInterval = TimeDelta::Seconds(1);
constexpr uint8_t kMaxPadding = 255;

bool IsRtcp(std::span<const uint8_t> b) { return b.size() >= 2 && b[1] >= 192 && b[1] <= 223; }

}  // namespace

// --- Layer selection ------------------------------------------------------------------------------------------------

std::vector<int> SfuAllocate(DataRate available, const std::vector<SfuAllocTrack>& tracks, double upgrade_margin) {
  std::vector<int> target(tracks.size(), -1);
  auto usable = [&](const SfuAllocTrack& t, int l) {
    return l >= 0 && l < static_cast<int>(t.layer_rates.size()) && l <= t.max_layer && t.layer_rates[static_cast<size_t>(l)].bps() > 0;
  };
  // What holding layer l costs: moving above the current layer costs the margin too.
  auto cost = [&](const SfuAllocTrack& t, int l) {
    if (l < 0) return 0.0;
    const double r = static_cast<double>(t.layer_rates[static_cast<size_t>(l)].bps());
    return l > t.current ? r * upgrade_margin : r;
  };
  const double budget = std::max<double>(0, static_cast<double>(available.bps()));
  double used = 0;
  // The lowest layer of every track, in order, while it fits.
  for (size_t i = 0; i < tracks.size(); ++i) {
    for (int l = 0; l < static_cast<int>(tracks[i].layer_rates.size()); ++l) {
      if (!usable(tracks[i], l)) continue;
      const double c = cost(tracks[i], l);
      if (used + c <= budget) {
        target[i] = l;
        used += c;
      }
      break;
    }
  }
  // Then one layer up for each track in turn, while the step fits.
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < tracks.size(); ++i) {
      if (target[i] < 0) continue;
      int next = target[i] + 1;
      while (next < static_cast<int>(tracks[i].layer_rates.size()) && !usable(tracks[i], next) && next <= tracks[i].max_layer) ++next;
      if (!usable(tracks[i], next)) continue;
      const double step = cost(tracks[i], next) - cost(tracks[i], target[i]);
      if (used + step <= budget) {
        target[i] = next;
        used += step;
        changed = true;
      }
    }
  }
  return target;
}

// --- One video downtrack -------------------------------------------------------------------------------------------

void SfuVideoForwarder::SetTarget(int layer) {
  target_ = layer;
  if (layer < 0) current_ = -1;  // A pause stops forwarding at once.
}

bool SfuVideoForwarder::SwitchTo(int layer, const RtpPacket& in, Timestamp now) {
  Segment seg;
  seg.layer = layer;
  seg.in_start = in.seq;
  seg.out_start = started_ ? static_cast<uint16_t>(last_out_seq_ + 1) : in.seq;
  seg.seq_offset = static_cast<uint16_t>(in.seq - seg.out_start);
  if (started_) {
    // The new layer's timestamps continue the downtrack's: aligned through both layers' sender reports (the same
    // capture instant gets the same downtrack timestamp), else LiveKit's fallback, the time since the last packet.
    const int prev = segments_.back().layer;
    std::optional<uint32_t> offset;
    auto a = layer_sr_.find(layer), b = layer_sr_.find(prev);
    if (a != layer_sr_.end() && b != layer_sr_.end()) {
      const auto [t_new, r_new] = a->second;
      const auto [t_old, r_old] = b->second;
      const int64_t old_at_new = static_cast<int64_t>(r_old) +
                                 std::llround(static_cast<double>((t_new - t_old).us()) * cfg_.clock_rate / 1e6);
      offset = static_cast<uint32_t>(segments_.back().ts_offset + static_cast<uint32_t>(old_at_new - static_cast<int64_t>(r_new)));
      // Never backwards: a report that doesn't fit falls back to the time since the last packet.
      if (static_cast<int32_t>(in.timestamp + *offset - last_out_ts_) <= 0) offset.reset();
    }
    if (!offset) {
      const int64_t elapsed = last_out_time_.IsFinite() ? std::max<int64_t>(1, (now - last_out_time_).us() * cfg_.clock_rate / 1000000) : 1;
      offset = static_cast<uint32_t>(last_out_ts_ + static_cast<uint32_t>(elapsed) - in.timestamp);
    }
    seg.ts_offset = *offset;
  }
  if (current_ >= 0) {
    ++switches_;
  } else {
    ++resumes_;
  }
  current_ = layer;
  segments_.push_back(seg);
  if (segments_.size() > 64) segments_.pop_front();
  return true;
}

std::optional<RtpPacket> SfuVideoForwarder::Forward(const RtpPacket& in, int layer, Timestamp now) {
  if (target_ < 0) return std::nullopt;
  if (layer != current_) {
    if (layer != target_) return std::nullopt;
    // Switch at the first packet of a keyframe of the target layer (LiveKit's forwarder checks the packet's keyframe).
    const rtp::H264PayloadInfo info = rtp::H264Depacketizer::Parse(in.payload);
    if (!(info.keyframe && info.first_packet_in_frame)) return std::nullopt;
    SwitchTo(layer, in, now);
  }
  const Segment& seg = segments_.back();
  if (SeqNewer(seg.in_start, in.seq)) return std::nullopt;  // From before the switch.
  const auto out_seq = static_cast<uint16_t>(in.seq - seg.seq_offset);
  if (started_ && !recent_out_.insert(out_seq).second) return std::nullopt;  // Already forwarded.
  if (!started_) recent_out_.insert(out_seq);
  // Keep the duplicate set to the last 2048 sequence numbers.
  while (recent_out_.size() > 2048) {
    auto oldest = recent_out_.begin();
    for (auto it = recent_out_.begin(); it != recent_out_.end(); ++it) {
      if (SeqNewer(*oldest, *it)) oldest = it;
    }
    recent_out_.erase(oldest);
  }
  RtpPacket out = in;
  out.ssrc = cfg_.out_ssrc;
  out.seq = out_seq;
  out.timestamp = in.timestamp + seg.ts_offset;
  if (!started_ || SeqNewer(out_seq, last_out_seq_)) {
    last_out_seq_ = out_seq;
    last_out_ts_ = out.timestamp;
    last_out_time_ = now;
    started_ = true;
  }
  ++packets_;
  octets_ += in.payload.size();
  return out;
}

void SfuVideoForwarder::OnLayerSenderReport(int layer, Timestamp ntp_time, uint32_t rtp_timestamp) {
  layer_sr_[layer] = {ntp_time, rtp_timestamp};
}

std::optional<uint32_t> SfuVideoForwarder::TimestampAt(Timestamp ntp_time) const {
  if (current_ < 0 || segments_.empty()) return std::nullopt;
  auto it = layer_sr_.find(segments_.back().layer);
  if (it == layer_sr_.end()) return std::nullopt;
  const auto [t, r] = it->second;
  const int64_t at = static_cast<int64_t>(r) + std::llround(static_cast<double>((ntp_time - t).us()) * cfg_.clock_rate / 1e6);
  return static_cast<uint32_t>(static_cast<uint32_t>(at) + segments_.back().ts_offset);
}

const SfuVideoForwarder::Segment* SfuVideoForwarder::SegmentFor(uint16_t out_seq) const {
  if (!started_ || SeqNewer(out_seq, last_out_seq_)) return nullptr;
  for (auto it = segments_.rbegin(); it != segments_.rend(); ++it) {
    if (!SeqNewer(it->out_start, out_seq)) return &*it;
  }
  return nullptr;
}

int SfuVideoForwarder::LayerOf(uint16_t out_seq) const {
  const Segment* s = SegmentFor(out_seq);
  return s ? s->layer : -1;
}

std::vector<RtpPacket> SfuVideoForwarder::Resend(const std::vector<uint16_t>& seqs, const CacheLookup& cache, Timestamp now,
                                                 TimeDelta min_interval, int64_t* budget_bytes) {
  std::vector<RtpPacket> out;
  if (cfg_.out_rtx_ssrc == 0) return out;
  for (uint16_t seq : seqs) {
    const Segment* s = SegmentFor(seq);
    if (!s) continue;
    auto it = resent_at_.find(seq);
    if (it != resent_at_.end() && (now - it->second.at < min_interval || it->second.count >= kMaxResends)) continue;
    const RtpPacket* p = cache(s->layer, static_cast<uint16_t>(seq + s->seq_offset));
    if (!p) continue;
    const auto size = static_cast<int64_t>(p->size()) + 2;
    if (*budget_bytes < size) break;
    *budget_bytes -= size;
    Resent& r_at = resent_at_[seq];
    r_at.at = now;
    ++r_at.count;
    if (resent_at_.size() > 4096) resent_at_.erase(resent_at_.begin());
    RtpPacket r = *p;
    r.ssrc = cfg_.out_rtx_ssrc;
    r.payload_type = cfg_.rtx_payload_type;
    r.seq = rtx_seq_++;
    r.timestamp = p->timestamp + s->ts_offset;
    r.padding_size = 0;
    r.payload.insert(r.payload.begin(), {static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq)});
    ++resent_;
    out.push_back(std::move(r));
  }
  return out;
}

RtpPacket SfuVideoForwarder::MakePadding() {
  RtpPacket p;
  p.payload_type = cfg_.rtx_payload_type;
  p.ssrc = cfg_.out_rtx_ssrc;
  p.seq = rtx_seq_++;
  p.timestamp = last_out_ts_;
  p.padding_size = kMaxPadding;
  return p;
}

// --- The SFU --------------------------------------------------------------------------------------------------------

struct Sfu::Ingress {
  std::unique_ptr<rtp::Session> session;
  NullAudioDecoder decoder;
  std::optional<rtp::AudioRecvId> audio;
  uint32_t audio_ssrc = 0;
  int audio_clock = 48000;
  std::optional<std::pair<Timestamp, uint32_t>> audio_sr;  // NTP (call time) and RTP of the audio's last SR.
  std::vector<rtp::VideoRecvId> layers;
  std::vector<uint32_t> ssrc, rtx_ssrc;
  std::vector<uint8_t> payload_type;
  struct Cache {
    std::unordered_map<uint16_t, RtpPacket> packets;
    std::deque<uint16_t> order;
  };
  std::vector<Cache> cache;
  std::vector<std::deque<std::pair<Timestamp, int64_t>>> rate_window;
  std::vector<int64_t> rate_bytes;
  std::vector<Timestamp> last_keyframe_request;
  std::vector<bool> active;  // Dynacast: the layer as last requested.
  Timestamp next_flush = Timestamp::MinusInfinity();
  uint64_t keyframe_requests = 0;
};

struct Sfu::Egress {
  explicit Egress(const cc::GccConfig& c) : gcc(c) {}
  int owner = 0;
  uint32_t ssrc = 0;  // RTCP sender SSRC of the SFU on this leg.
  cc::GccController gcc;
  cc::BitrateProber prober;
  uint16_t transport_seq = 0;
  std::map<int, std::unique_ptr<SfuVideoForwarder>> video;  // By publisher.
  std::map<uint32_t, int> video_ssrc_pub;                   // Downtrack SSRC -> publisher.
  std::map<int, std::pair<uint32_t, uint32_t>> audio_counts;  // Publisher -> packets and payload octets forwarded.
  std::map<uint32_t, std::pair<int64_t, int64_t>> last_block;  // SSRC -> cumulative lost, extended highest seq.
  std::deque<std::pair<Timestamp, int64_t>> sent_window, audio_window;
  int64_t sent_bytes = 0, audio_bytes = 0;
  Timestamp next_gcc = Timestamp::MinusInfinity(), next_alloc = Timestamp::MinusInfinity();
  DataRate max_allocated = DataRate::Zero();
  uint64_t probes = 0;
};

struct Sfu::Member {
  explicit Member(const cc::GccConfig& c) : egress(c) {}
  int id = 0;
  SfuPublisher pub;
  int max_layer = 2;
  Ingress in;
  Egress egress;
};

Sfu::Sfu(SfuConfig cfg) : cfg_(std::move(cfg)) {}
Sfu::~Sfu() = default;

void Sfu::Join(int id, const SfuPublisher& pub, int max_layer, Timestamp now) {
  auto m = std::make_unique<Member>(cfg_.egress_gcc);
  m->id = id;
  m->pub = pub;
  m->max_layer = max_layer;
  rtp::SessionConfig sc = cfg_.ingress;
  sc.local_ssrc = 50000 + static_cast<uint32_t>(id);
  m->in.session = std::make_unique<rtp::Session>(sc);
  if (pub.audio) {
    m->in.audio = m->in.session->AddAudioReceive(*pub.audio, &m->in.decoder);
    m->in.audio_ssrc = pub.audio->ssrc;
    m->in.audio_clock = pub.audio->clock_rate;
  }
  for (const rtp::VideoStreamConfig& l : pub.layers) {
    rtp::VideoStreamConfig c = l;
    c.rid.clear();
    m->in.layers.push_back(m->in.session->AddVideoReceive(c));
    m->in.ssrc.push_back(l.ssrc);
    m->in.rtx_ssrc.push_back(l.rtx_ssrc.value_or(0));
    m->in.payload_type.push_back(l.payload_type);
  }
  const size_t n = pub.layers.size();
  m->in.cache.resize(n);
  m->in.rate_window.resize(n);
  m->in.rate_bytes.assign(n, 0);
  m->in.last_keyframe_request.assign(n, Timestamp::MinusInfinity());
  m->in.active.assign(n, true);
  m->egress.owner = id;
  m->egress.ssrc = 60000 + static_cast<uint32_t>(id);
  Member& me = *m;
  members_[id] = std::move(m);
  // The downtracks both ways with every member already there.
  auto add = [&](Member& sub, const Member& pubm) {
    if (pubm.pub.layers.empty()) return;
    SfuForwarderConfig fc;
    fc.out_ssrc = pubm.pub.layers[0].ssrc;
    fc.out_rtx_ssrc = pubm.pub.layers[0].rtx_ssrc.value_or(0);
    fc.rtx_payload_type = cfg_.rtx_payload_type;
    sub.egress.video[pubm.id] = std::make_unique<SfuVideoForwarder>(fc);
    sub.egress.video_ssrc_pub[fc.out_ssrc] = pubm.id;
  };
  for (auto& [oid, other] : members_) {
    if (oid == id) continue;
    add(me, *other);
    add(*other, me);
  }
  Apply(me.egress, me.egress.gcc.Start(now + cfg_.clock0));
  UpdateDynacast(now);
}

void Sfu::Leave(int id, Timestamp now) {
  auto it = members_.find(id);
  if (it == members_.end()) return;
  const uint32_t video_ssrc = it->second->pub.layers.empty() ? 0 : it->second->pub.layers[0].ssrc;
  members_.erase(it);
  for (auto& [oid, m] : members_) {
    m->egress.video.erase(id);
    m->egress.video_ssrc_pub.erase(video_ssrc);
    m->egress.audio_counts.erase(id);
  }
  UpdateDynacast(now);
}

void Sfu::Apply(Egress& e, const cc::NetworkControlUpdate& u) {
  for (const cc::ProbeCluster& c : u.probes) e.prober.CreateProbeCluster(c);
}

void Sfu::SendDown(Egress& e, RtpPacket p, SfuPacketKind kind, Timestamp now, int probe_cluster) {
  // The subscribe leg's own transport-wide sequence number; the publish leg's extensions (RID, ...) stay behind.
  const uint16_t seq = ++e.transport_seq;
  p.extension.clear();
  p.extension_profile = 0;
  sync::WriteHeaderExtensions({{cfg_.transport_cc_id, {static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq)}}}, p);
  std::vector<uint8_t> bytes = p.Serialize();
  const int64_t wire = static_cast<int64_t>(bytes.size()) + kUdpIpOverhead;
  e.gcc.OnPacketSent(seq, now + cfg_.clock0, DataSize::Bytes(wire), probe_cluster);
  e.sent_window.emplace_back(now, wire);
  e.sent_bytes += wire;
  if (kind == SfuPacketKind::kAudio) {
    e.audio_window.emplace_back(now, wire);
    e.audio_bytes += wire;
  }
  out_.push_back({e.owner, true, kind, std::move(bytes)});
}

void Sfu::SendRtcpDown(int to, std::vector<uint8_t> bytes) {
  if (!bytes.empty()) out_.push_back({to, true, SfuPacketKind::kRtcp, std::move(bytes)});
}

void Sfu::RequestKeyframe(int pub, int layer, Timestamp now) {
  auto it = members_.find(pub);
  if (it == members_.end() || layer < 0 || layer >= static_cast<int>(it->second->in.layers.size())) return;
  Ingress& in = it->second->in;
  Timestamp& last = in.last_keyframe_request[static_cast<size_t>(layer)];
  if (last.IsFinite() && now - last < cfg_.keyframe_request_interval) return;
  last = now;
  ++in.keyframe_requests;
  // The ingress session asks the publisher with a PLI on the layer's SSRC.
  in.session->OnDecodeFailure(in.layers[static_cast<size_t>(layer)], now + cfg_.clock0);
}

DataRate Sfu::LayerRate(const Ingress& in, int layer, Timestamp now) const {
  (void)now;
  return DataRate::BitsPerSec(in.rate_bytes[static_cast<size_t>(layer)] * 8 * 1000000 / kRateWindow.us());
}

void Sfu::ForwardVideo(Member& m, int layer, const RtpPacket& media, Timestamp at) {
  for (auto& [sid, sub] : members_) {
    if (sid == m.id) continue;
    auto f = sub->egress.video.find(m.id);
    if (f == sub->egress.video.end()) continue;
    std::optional<RtpPacket> out = f->second->Forward(media, layer, at);
    if (!out) continue;
    if (on_forward_video) on_forward_video(m.id, sid, layer, media.timestamp, out->timestamp);
    const size_t size = out->size() + kUdpIpOverhead;
    SendDown(sub->egress, std::move(*out), SfuPacketKind::kVideo, at);
    sub->egress.prober.OnIncomingPacket(DataSize::Bytes(static_cast<int64_t>(size)));
  }
}

void Sfu::OnPublisherRtcp(Member& m, std::span<const uint8_t> bytes, Timestamp at) {
  auto views = loss::SplitCompoundRtcp(bytes);
  if (!views) return;
  const timing::NtpTime now_ntp = timing::NtpTime::FromTimestamp(at + cfg_.clock0);
  for (const loss::RtcpPacketView& v : *views) {
    if (v.type != loss::RtcpType::kSenderReport) continue;
    auto sr = timing::ParseSenderReport(v.data);
    if (!sr) continue;
    const Timestamp t = sr->ntp.ToTimestamp() - cfg_.clock0;
    if (m.in.audio && sr->ssrc == m.in.audio_ssrc) {
      m.in.audio_sr = std::make_pair(t, sr->rtp_timestamp);
      // Each subscriber's audio downtrack reports now, its RTP timestamp extrapolated from the publisher's report.
      const uint32_t rtp = sr->rtp_timestamp +
                           static_cast<uint32_t>(std::llround(static_cast<double>((at - t).us()) * m.in.audio_clock / 1e6));
      for (auto& [sid, sub] : members_) {
        if (sid == m.id) continue;
        timing::SenderReport out;
        out.ssrc = m.in.audio_ssrc;
        out.ntp = now_ntp;
        out.rtp_timestamp = rtp;
        const auto counts = sub->egress.audio_counts[m.id];
        out.packet_count = counts.first;
        out.octet_count = counts.second;
        SendRtcpDown(sid, timing::SerializeSenderReport(out));
      }
      continue;
    }
    for (size_t l = 0; l < m.in.ssrc.size(); ++l) {
      if (sr->ssrc != m.in.ssrc[l]) continue;
      for (auto& [sid, sub] : members_) {
        if (sid == m.id) continue;
        auto f = sub->egress.video.find(m.id);
        if (f == sub->egress.video.end()) continue;
        SfuVideoForwarder& fw = *f->second;
        fw.OnLayerSenderReport(static_cast<int>(l), t, sr->rtp_timestamp);
        if (fw.current() != static_cast<int>(l)) continue;
        const std::optional<uint32_t> rtp = fw.TimestampAt(at);
        if (!rtp) continue;
        timing::SenderReport out;
        out.ssrc = m.pub.layers[0].ssrc;
        out.ntp = now_ntp;
        out.rtp_timestamp = *rtp;
        out.packet_count = static_cast<uint32_t>(fw.packets());
        out.octet_count = static_cast<uint32_t>(fw.octets());
        SendRtcpDown(sid, timing::SerializeSenderReport(out));
      }
    }
  }
}

void Sfu::OnPublishPacket(int from, std::span<const uint8_t> bytes, Timestamp at) {
  auto it = members_.find(from);
  if (it == members_.end()) return;
  Member& m = *it->second;
  m.in.session->OnPacket(bytes, at + cfg_.clock0);
  if (IsRtcp(bytes)) {
    OnPublisherRtcp(m, bytes, at);
  } else if (auto p = RtpPacket::Parse(bytes)) {
    if (m.in.audio && p->ssrc == m.in.audio_ssrc) {
      if (!p->payload.empty()) {
        for (auto& [sid, sub] : members_) {
          if (sid == m.id) continue;
          auto& c = sub->egress.audio_counts[m.id];
          ++c.first;
          c.second += static_cast<uint32_t>(p->payload.size());
          SendDown(sub->egress, *p, SfuPacketKind::kAudio, at);
        }
      }
    } else {
      for (size_t l = 0; l < m.in.ssrc.size(); ++l) {
        std::optional<RtpPacket> media;
        if (p->ssrc == m.in.ssrc[l]) {
          if (!p->payload.empty()) media = *p;
        } else if (m.in.rtx_ssrc[l] != 0 && p->ssrc == m.in.rtx_ssrc[l] && p->payload.size() > 2) {
          // RFC 4588: the publisher's resend becomes the packet it repairs.
          media = *p;
          media->ssrc = m.in.ssrc[l];
          media->payload_type = m.in.payload_type[l];
          media->seq = static_cast<uint16_t>((p->payload[0] << 8) | p->payload[1]);
          media->payload.erase(media->payload.begin(), media->payload.begin() + 2);
          media->padding_size = 0;
        }
        if (!media) continue;
        Ingress::Cache& c = m.in.cache[l];
        if (c.packets.emplace(media->seq, *media).second) {
          c.order.push_back(media->seq);
          if (c.order.size() > kCachePackets) {
            c.packets.erase(c.order.front());
            c.order.pop_front();
          }
          const int64_t wire = static_cast<int64_t>(media->size()) + kUdpIpOverhead;
          m.in.rate_window[l].emplace_back(at, wire);
          m.in.rate_bytes[l] += wire;
        }
        ForwardVideo(m, static_cast<int>(l), *media, at);
        break;
      }
    }
  }
  m.in.session->Process(at + cfg_.clock0);
  for (rtp::OutgoingPacket& o : m.in.session->TakeOutgoing()) out_.push_back({m.id, false, SfuPacketKind::kRtcp, std::move(o.bytes)});
}

void Sfu::OnSubscribePacket(int from, std::span<const uint8_t> bytes, Timestamp at) {
  auto it = members_.find(from);
  if (it == members_.end() || !IsRtcp(bytes)) return;
  Egress& e = it->second->egress;
  auto views = loss::SplitCompoundRtcp(bytes);
  if (!views) return;
  const Timestamp clock = at + cfg_.clock0;
  int64_t lost_delta = 0, packets_delta = 0;
  for (const loss::RtcpPacketView& v : *views) {
    switch (v.type) {
      case loss::RtcpType::kReceiverReport: {
        auto rr = timing::ParseReceiverReport(v.data);
        if (!rr) break;
        for (const timing::ReportBlock& b : rr->blocks) {
          if (b.last_sr != 0) {
            const uint32_t rtt_compact = timing::NtpTime::FromTimestamp(clock).Compact() - b.last_sr - b.delay_since_last_sr;
            if (rtt_compact < 0x80000000u) e.gcc.OnRttUpdate(std::max(TimeDelta::Millis(1), timing::CompactNtpToDelta(rtt_compact)));
          }
          if (!e.video_ssrc_pub.count(b.ssrc)) continue;
          auto [prev, fresh] = e.last_block.try_emplace(b.ssrc, b.cumulative_lost, static_cast<int64_t>(b.extended_highest_seq));
          if (!fresh) {
            const int64_t dp = static_cast<int64_t>(b.extended_highest_seq) - prev->second.second;
            if (dp > 0) {
              packets_delta += dp;
              lost_delta += b.cumulative_lost - prev->second.first;
            }
            prev->second = {b.cumulative_lost, static_cast<int64_t>(b.extended_highest_seq)};
          }
        }
        break;
      }
      case loss::RtcpType::kTransportCc:
        if (auto fb = cc::ParseTransportFeedback(v.data)) Apply(e, e.gcc.OnTransportFeedback(*fb, clock));
        break;
      case loss::RtcpType::kNack: {
        auto n = loss::ParseNack(v.data);
        if (!n) break;
        auto pub = e.video_ssrc_pub.find(n->media_ssrc);
        if (pub == e.video_ssrc_pub.end()) break;
        auto f = e.video.find(pub->second);
        auto pm = members_.find(pub->second);
        if (f == e.video.end() || pm == members_.end()) break;
        Ingress& in = pm->second->in;
        auto lookup = [&in](int layer, uint16_t seq) -> const RtpPacket* {
          auto& c = in.cache[static_cast<size_t>(layer)].packets;
          auto p = c.find(seq);
          return p == c.end() ? nullptr : &p->second;
        };
        // The retransmission budget: what keeps media and resends within the estimate over the last second, as
        // libwebrtc's retransmission rate limiter caps resends at the target rate (RtpTransportControllerSend).
        const int64_t window_bytes = e.gcc.target().bps() / 8 * kRateWindow.us() / 1000000;
        int64_t budget = std::max<int64_t>(0, window_bytes - e.sent_bytes);
        const TimeDelta rtt = e.gcc.last_rtt() > TimeDelta::Zero() ? e.gcc.last_rtt() : TimeDelta::Millis(100);
        for (RtpPacket& r : f->second->Resend(n->seqs, lookup, at, rtt, &budget)) SendDown(e, std::move(r), SfuPacketKind::kRtx, at);
        break;
      }
      case loss::RtcpType::kPli:
      case loss::RtcpType::kFir: {
        std::vector<uint32_t> ssrcs;
        if (v.type == loss::RtcpType::kPli) {
          if (auto p = loss::ParsePli(v.data)) ssrcs.push_back(p->media_ssrc);
        } else if (auto f = loss::ParseFir(v.data)) {
          for (const auto& entry : f->entries) ssrcs.push_back(entry.ssrc);
        }
        for (uint32_t ssrc : ssrcs) {
          auto pub = e.video_ssrc_pub.find(ssrc);
          if (pub == e.video_ssrc_pub.end()) continue;
          auto f = e.video.find(pub->second);
          if (f != e.video.end() && f->second->current() >= 0) RequestKeyframe(pub->second, f->second->current(), at);
        }
        break;
      }
      case loss::RtcpType::kXr: {
        auto xr = timing::ParseExtendedReports(v.data);
        if (!xr || !xr->rrtr) break;
        // The subscriber measures its RTT with RRTR; the SFU echoes it at once (DLRR).
        timing::ExtendedReports reply;
        reply.sender_ssrc = e.ssrc;
        reply.dlrr.push_back({xr->sender_ssrc, xr->rrtr->Compact(), 0});
        timing::ReceiverReport rr;
        rr.ssrc = e.ssrc;
        const std::vector<std::vector<uint8_t>> parts = {timing::SerializeReceiverReport(rr), timing::SerializeExtendedReports(reply)};
        SendRtcpDown(from, loss::BuildCompoundRtcp(parts));
        break;
      }
      default: break;
    }
  }
  if (packets_delta > 0) e.gcc.OnTransportLossReport(lost_delta, packets_delta, clock);
}

void Sfu::Allocate(Egress& e, Timestamp now) {
  std::vector<SfuAllocTrack> tracks;
  std::vector<SfuVideoForwarder*> fws;
  std::vector<int> pubs;
  const int max_layer = members_.at(e.owner)->max_layer;
  double wanted_bps = 0;
  for (auto& [pid, fw] : e.video) {
    const Ingress& in = members_.at(pid)->in;
    SfuAllocTrack t;
    for (size_t l = 0; l < in.layers.size(); ++l) t.layer_rates.push_back(LayerRate(in, static_cast<int>(l), now));
    t.max_layer = max_layer;
    t.current = fw->current();
    for (int l = std::min<int>(max_layer, static_cast<int>(t.layer_rates.size()) - 1); l >= 0; --l) {
      if (t.layer_rates[static_cast<size_t>(l)].bps() > 0) {
        wanted_bps += static_cast<double>(t.layer_rates[static_cast<size_t>(l)].bps());
        break;
      }
    }
    tracks.push_back(std::move(t));
    fws.push_back(fw.get());
    pubs.push_back(pid);
  }
  const DataRate audio = DataRate::BitsPerSec(e.audio_bytes * 8 * 1000000 / kRateWindow.us());
  const DataRate available = e.gcc.target() > audio ? e.gcc.target() - audio : DataRate::Zero();
  const std::vector<int> target = SfuAllocate(available, tracks, cfg_.upgrade_margin);
  for (size_t i = 0; i < fws.size(); ++i) {
    fws[i]->SetTarget(target[i]);
    if (fws[i]->WantsKeyframe()) RequestKeyframe(pubs[i], target[i], now);
  }
  // What the subscriber would take at its wanted layers: a rise lets GoogCC probe toward it, as libwebrtc's
  // OnMaxTotalAllocatedBitrate (LiveKit probes before it moves a track up).
  const DataRate wanted = DataRate::BitsPerSec(static_cast<int64_t>(wanted_bps)) + audio;
  if (wanted > e.max_allocated * 1.1 || wanted < e.max_allocated * 0.9) {
    e.max_allocated = wanted;
    for (const cc::ProbeCluster& c : e.gcc.OnMaxTotalAllocatedBitrate(wanted, now + cfg_.clock0)) e.prober.CreateProbeCluster(c);
  }
}

void Sfu::UpdateDynacast(Timestamp now) {
  if (!cfg_.dynacast) return;
  for (auto& [pid, m] : members_) {
    // The highest layer any other member wants; none when nobody else is in the room.
    int want = -1;
    for (const auto& [sid, sub] : members_) {
      if (sid != pid) want = std::max(want, sub->max_layer);
    }
    for (size_t l = 0; l < m->in.active.size(); ++l) {
      const bool active = static_cast<int>(l) <= want;
      if (active == m->in.active[l]) continue;
      m->in.active[l] = active;
      activity_.push_back({pid, static_cast<int>(l), active, now + cfg_.dynacast_delay});
    }
  }
}

void Sfu::Process(Timestamp now) {
  const Timestamp clock = now + cfg_.clock0;
  for (auto& [id, mp] : members_) {
    Member& m = *mp;
    // The ingress session: its reports and feedback to the publisher; what it assembles is dropped.
    m.in.session->Process(clock);
    for (rtp::OutgoingPacket& o : m.in.session->TakeOutgoing()) out_.push_back({id, false, SfuPacketKind::kRtcp, std::move(o.bytes)});
    for (rtp::VideoRecvId v : m.in.layers) m.in.session->TakeVideoFrames(v);
    m.in.session->TakeEvents();
    if (m.in.audio && now >= m.in.next_flush) {
      m.in.session->FlushAudio(*m.in.audio, clock);
      m.in.next_flush = now + kAudioFlushInterval;
    }
    for (size_t l = 0; l < m.in.rate_window.size(); ++l) {
      auto& w = m.in.rate_window[l];
      while (!w.empty() && w.front().first <= now - kRateWindow) {
        m.in.rate_bytes[l] -= w.front().second;
        w.pop_front();
      }
    }
    Egress& e = m.egress;
    for (auto [w, sum] : {std::pair{&e.sent_window, &e.sent_bytes}, std::pair{&e.audio_window, &e.audio_bytes}}) {
      while (!w->empty() && w->front().first <= now - kRateWindow) {
        *sum -= w->front().second;
        w->pop_front();
      }
    }
    if (now >= e.next_gcc) {
      Apply(e, e.gcc.OnProcessInterval(clock, DataRate::BitsPerSec(e.sent_bytes * 8 * 1000000 / kRateWindow.us())));
      e.next_gcc = now + kGccInterval;
    }
    // Probes: padding on a downtrack's RTX SSRC at the cluster's rate (BitrateProber).
    SfuVideoForwarder* pad = nullptr;
    for (auto& [pid, fw] : e.video) {
      if (!pad && fw->started()) pad = fw.get();
    }
    for (int guard = 0; pad && guard < 200 && e.prober.NextProbeTime(clock) <= clock; ++guard) {
      const std::optional<cc::ProbeSendInfo> info = e.prober.CurrentCluster(clock);
      if (!info) break;
      RtpPacket p = pad->MakePadding();
      const int64_t wire = static_cast<int64_t>(p.size() + 4) + kUdpIpOverhead;  // With the transport-cc extension.
      SendDown(e, std::move(p), SfuPacketKind::kPadding, now, info->cluster_id);
      e.prober.ProbeSent(clock, DataSize::Bytes(wire));
      ++e.probes;
    }
    if (now >= e.next_alloc) {
      Allocate(e, now);
      e.next_alloc = now + cfg_.allocation_interval;
    }
  }
}

std::vector<SfuOutgoing> Sfu::TakeOutgoing() { return std::exchange(out_, {}); }
std::vector<SfuLayerActivity> Sfu::TakeLayerActivity() { return std::exchange(activity_, {}); }

const SfuVideoForwarder* Sfu::forwarder(int pub, int sub) const {
  auto s = members_.find(sub);
  if (s == members_.end()) return nullptr;
  auto f = s->second->egress.video.find(pub);
  return f == s->second->egress.video.end() ? nullptr : f->second.get();
}

DataRate Sfu::estimate(int sub) const {
  auto s = members_.find(sub);
  return s == members_.end() ? DataRate::Zero() : s->second->egress.gcc.target();
}

uint64_t Sfu::keyframe_requests(int pub) const {
  auto s = members_.find(pub);
  return s == members_.end() ? 0 : s->second->in.keyframe_requests;
}

uint64_t Sfu::probes_sent(int sub) const {
  auto s = members_.find(sub);
  return s == members_.end() ? 0 : s->second->egress.probes;
}

}  // namespace sm::testbench
