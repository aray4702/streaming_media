#include "sm/loss/rtx.h"

#include <algorithm>

#include "sm/core/seq_num.h"

namespace sm::loss {

RtpPacket MakeRtxPacket(const RtpPacket& original, uint32_t rtx_ssrc, uint8_t rtx_pt, uint16_t rtx_seq) {
  RtpPacket r = original;
  r.ssrc = rtx_ssrc;
  r.payload_type = rtx_pt;
  r.seq = rtx_seq;
  r.payload.clear();
  r.payload.reserve(original.payload.size() + 2);
  r.payload.push_back(static_cast<uint8_t>(original.seq >> 8));
  r.payload.push_back(static_cast<uint8_t>(original.seq));
  r.payload.insert(r.payload.end(), original.payload.begin(), original.payload.end());
  return r;
}

std::optional<RtpPacket> RestoreFromRtx(const RtpPacket& rtx, uint32_t media_ssrc, uint8_t media_pt) {
  if (rtx.payload.size() < 2) return std::nullopt;
  RtpPacket p = rtx;
  p.ssrc = media_ssrc;
  p.payload_type = media_pt;
  p.seq = static_cast<uint16_t>((rtx.payload[0] << 8) | rtx.payload[1]);
  p.payload.assign(rtx.payload.begin() + 2, rtx.payload.end());
  return p;
}

namespace {
// rtp_packet_history.h
constexpr size_t kMaxCapacity = 9600;
constexpr TimeDelta kMinPacketDuration = TimeDelta::Seconds(1);
constexpr int kMinPacketDurationRtt = 3;
constexpr int kPacketCullingDelayFactor = 3;
}  // namespace

RtxSender::RtxSender(RtxConfig cfg)
    : cfg_(cfg), slots_(cfg.version >= 2 ? 1 : (cfg.history_capacity ? cfg.history_capacity : 1)) {}

// RtpPacketHistory::GetPacketIndex: the index of `seq` relative to the oldest stored packet,
// negative when it is older.
int RtxSender::V2Index(uint16_t seq) const {
  if (v2_.empty()) return 0;
  const uint16_t first = v2_.front().packet->seq;
  if (first == seq) return 0;
  int index = static_cast<int>(seq) - static_cast<int>(first);
  constexpr int kSpan = 65536;
  if (AheadOf<uint16_t>(seq, first)) {
    if (seq < first) index += kSpan;
  } else if (seq > first) {
    index -= kSpan;
  }
  return index;
}

RtxSender::Stored* RtxSender::V2Get(uint16_t seq) {
  const int i = V2Index(seq);
  if (i < 0 || static_cast<size_t>(i) >= v2_.size() || !v2_[static_cast<size_t>(i)].packet) return nullptr;
  return &v2_[static_cast<size_t>(i)];
}

void RtxSender::V2Remove(size_t index) {
  v2_[index].packet.reset();
  if (index == 0) {
    while (!v2_.empty() && !v2_.front().packet) v2_.pop_front();
  }
}

// RtpPacketHistory::CullOldPackets.
void RtxSender::V2Cull(Timestamp now) {
  const TimeDelta duration =
      rtt_.IsFinite() ? std::max(rtt_ * kMinPacketDurationRtt, kMinPacketDuration) : kMinPacketDuration;
  while (!v2_.empty()) {
    if (v2_.size() >= kMaxCapacity) {
      V2Remove(0);
      continue;
    }
    const Stored& front = v2_.front();
    if (front.pending) return;
    if (front.send_time + duration > now) return;
    if (v2_.size() >= cfg_.number_to_store || front.send_time + duration * kPacketCullingDelayFactor <= now) {
      V2Remove(0);
    } else {
      return;
    }
  }
}

bool RtxSender::V2VerifyRtt(const Stored& s, Timestamp now) const {
  // An RTX that was already sent once and went out less than an RTT ago is likely still in flight.
  return !(s.times_retransmitted > 0 && now - s.send_time < rtt_);
}

void RtxSender::SetRtt(TimeDelta rtt, Timestamp now) {
  if (cfg_.version < 2) return;
  rtt_ = rtt;
  V2Cull(now);
}

void RtxSender::MarkPacketAsSent(uint16_t original_seq, Timestamp now) {
  if (cfg_.version < 2) return;
  Stored* s = V2Get(original_seq);
  if (!s) return;
  s->send_time = now;
  s->pending = false;
  ++s->times_retransmitted;
}

void RtxSender::CullAcknowledged(std::span<const uint16_t> seqs) {
  if (cfg_.version < 2) return;
  for (uint16_t seq : seqs) {
    const int i = V2Index(seq);
    if (i < 0 || static_cast<size_t>(i) >= v2_.size() || !v2_[static_cast<size_t>(i)].packet) continue;
    V2Remove(static_cast<size_t>(i));
    ++stats_.culled_acked;
  }
}

void RtxSender::OnPacketSent(const RtpPacket& p, Timestamp now) {
  if (cfg_.version >= 2) {
    // RtpPacketHistory::PutRtpPacket.
    V2Cull(now);
    int index = V2Index(p.seq);
    if (index >= 0 && static_cast<size_t>(index) < v2_.size() && v2_[static_cast<size_t>(index)].packet) {
      V2Remove(static_cast<size_t>(index));
      index = V2Index(p.seq);
    }
    for (; index < 0; ++index) v2_.emplace_front();
    while (static_cast<int>(v2_.size()) <= index) v2_.emplace_back();
    Stored& s = v2_[static_cast<size_t>(index)];
    s.packet = p;
    s.send_time = now;
    s.pending = false;
    s.times_retransmitted = 0;
    ++stats_.packets_stored;
    return;
  }
  Slot& s = slots_[p.seq % slots_.size()];
  s.valid = true;
  s.packet = p;
  s.sent = now;
  s.last_resent = Timestamp::MinusInfinity();
  ++stats_.packets_stored;
}

std::vector<RtpPacket> RtxSender::OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt) {
  std::vector<RtpPacket> out;
  if (cfg_.version >= 2) {
    // RtpPacketHistory::GetPacketAndMarkAsPending with the RTX encapsulation.
    for (uint16_t seq : seqs) {
      ++stats_.nack_requests;
      Stored* s = V2Get(seq);
      if (!s) {
        ++stats_.not_found;
        continue;
      }
      if (s->pending || !V2VerifyRtt(*s, now)) {
        ++stats_.throttled;
        continue;
      }
      s->pending = true;
      out.push_back(MakeRtxPacket(*s->packet, cfg_.rtx_ssrc, cfg_.rtx_payload_type, rtx_seq_++));
      ++stats_.retransmitted;
      stats_.bytes_retransmitted += static_cast<int64_t>(out.back().size());
    }
    if (!out.empty()) SM_EVENT(cfg_.events, now, "rtx", "resend", out.size(), seqs.front());
    return out;
  }
  for (uint16_t seq : seqs) {
    ++stats_.nack_requests;
    Slot& s = slots_[seq % slots_.size()];
    if (!s.valid || s.packet.seq != seq || now - s.sent > cfg_.max_age) {
      ++stats_.not_found;
      continue;
    }
    if (s.last_resent.IsFinite() && now - s.last_resent < rtt * cfg_.min_resend_rtt_factor) {
      ++stats_.throttled;
      continue;
    }
    s.last_resent = now;
    out.push_back(MakeRtxPacket(s.packet, cfg_.rtx_ssrc, cfg_.rtx_payload_type, rtx_seq_++));
    ++stats_.retransmitted;
    stats_.bytes_retransmitted += static_cast<int64_t>(out.back().size());
  }
  if (!out.empty()) SM_EVENT(cfg_.events, now, "rtx", "resend", out.size(), seqs.front());
  return out;
}

void RtxSender::ExportMetrics(MetricsWriter& w) const {
  w.Counter("packets_stored", stats_.packets_stored);
  w.Counter("nack_requests", stats_.nack_requests);
  w.Counter("retransmitted", stats_.retransmitted);
  w.Counter("not_found", stats_.not_found);
  w.Counter("throttled", stats_.throttled);
  w.Counter("culled_acked", stats_.culled_acked);
  w.Counter("bytes_retransmitted", static_cast<uint64_t>(stats_.bytes_retransmitted));
}

}  // namespace sm::loss
