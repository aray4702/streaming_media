#include "sm/loss/rtx.h"

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

RtxSender::RtxSender(RtxConfig cfg) : cfg_(cfg), slots_(cfg.history_capacity ? cfg.history_capacity : 1) {}

void RtxSender::OnPacketSent(const RtpPacket& p, Timestamp now) {
  Slot& s = slots_[p.seq % slots_.size()];
  s.valid = true;
  s.packet = p;
  s.sent = now;
  s.last_resent = Timestamp::MinusInfinity();
  ++stats_.packets_stored;
}

namespace {
constexpr TimeDelta kRateWindow = TimeDelta::Seconds(1);
}

void RtxSender::Expire(Timestamp now) {
  while (!window_.empty() && window_.front().first <= now - kRateWindow) {
    window_bytes_ -= window_.front().second;
    window_.pop_front();
  }
}

DataRate RtxSender::resent_rate(Timestamp now) {
  Expire(now);
  return DataRate::BitsPerSec(window_bytes_ * 8);
}

std::vector<RtpPacket> RtxSender::OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta rtt) {
  std::vector<RtpPacket> out;
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
    Expire(now);
    const int64_t size = static_cast<int64_t>(s.packet.size()) + 2;
    if (max_rate_.IsFinite() && (window_bytes_ + size) * 8 > max_rate_.bps()) {
      ++stats_.rate_limited;
      continue;
    }
    s.last_resent = now;
    window_.emplace_back(now, size);
    window_bytes_ += size;
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
  w.Counter("rate_limited", stats_.rate_limited);
  w.Counter("bytes_retransmitted", static_cast<uint64_t>(stats_.bytes_retransmitted));
}

}  // namespace sm::loss
