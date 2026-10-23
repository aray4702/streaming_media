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
constexpr size_t kOldPayloadPaddingSizeHysteresis = 100;
constexpr uint16_t kMaxOldPayloadPaddingSequenceNumber = 1 << 13;
}  // namespace

RtxSender::RtxSender(RtxConfig cfg) : cfg_(cfg) {}

// RtpPacketHistory::GetPacketIndex: the index of `seq` relative to the oldest stored packet,
// negative when it is older.
int RtxSender::Index(uint16_t seq) const {
  if (history_.empty()) return 0;
  const uint16_t first = history_.front().packet->seq;
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

RtxSender::Stored* RtxSender::Get(uint16_t seq) {
  const int i = Index(seq);
  if (i < 0 || static_cast<size_t>(i) >= history_.size() || !history_[static_cast<size_t>(i)].packet) return nullptr;
  return &history_[static_cast<size_t>(i)];
}

void RtxSender::Remove(size_t index) {
  history_[index].packet.reset();
  if (index == 0) {
    while (!history_.empty() && !history_.front().packet) history_.pop_front();
  }
}

// RtpPacketHistory::CullOldPackets.
void RtxSender::Cull(Timestamp now) {
  const TimeDelta duration =
      rtt_.IsFinite() ? std::max(rtt_ * kMinPacketDurationRtt, kMinPacketDuration) : kMinPacketDuration;
  while (!history_.empty()) {
    if (history_.size() >= kMaxCapacity) {
      Remove(0);
      continue;
    }
    const Stored& front = history_.front();
    if (front.pending) return;
    if (front.send_time + duration > now) return;
    if (history_.size() >= cfg_.number_to_store || front.send_time + duration * kPacketCullingDelayFactor <= now) {
      Remove(0);
    } else {
      return;
    }
  }
}

bool RtxSender::VerifyRtt(const Stored& s, Timestamp now) const {
  // An RTX that was already sent once and went out less than an RTT ago is likely still in flight.
  return !(s.times_retransmitted > 0 && now - s.send_time < rtt_);
}

void RtxSender::SetRtt(TimeDelta rtt, Timestamp now) {
  rtt_ = rtt;
  Cull(now);
}

void RtxSender::MarkPacketAsSent(uint16_t original_seq, Timestamp now) {
  Stored* s = Get(original_seq);
  if (!s) return;
  s->send_time = now;
  s->pending = false;
  ++s->times_retransmitted;
}

void RtxSender::CullAcknowledged(std::span<const uint16_t> seqs) {
  for (uint16_t seq : seqs) {
    const int i = Index(seq);
    if (i < 0 || static_cast<size_t>(i) >= history_.size() || !history_[static_cast<size_t>(i)].packet) continue;
    Remove(static_cast<size_t>(i));
    ++stats_.culled_acked;
  }
}

void RtxSender::OnPacketSent(const RtpPacket& p, Timestamp now) {
  // RtpPacketHistory::PutRtpPacket.
  Cull(now);
  int index = Index(p.seq);
  if (index >= 0 && static_cast<size_t>(index) < history_.size() && history_[static_cast<size_t>(index)].packet) {
    Remove(static_cast<size_t>(index));
    index = Index(p.seq);
  }
  for (; index < 0; ++index) history_.emplace_front();
  while (static_cast<int>(history_.size()) <= index) history_.emplace_back();
  // PaddingMode::kRecentLargePacket.
  if (!large_payload_packet_ ||
      p.payload.size() + kOldPayloadPaddingSizeHysteresis > large_payload_packet_->payload.size() ||
      AheadOf<uint16_t>(p.seq, static_cast<uint16_t>(large_payload_packet_->seq + kMaxOldPayloadPaddingSequenceNumber))) {
    large_payload_packet_ = p;
  }
  Stored& s = history_[static_cast<size_t>(index)];
  s.packet = p;
  s.send_time = now;
  s.pending = false;
  s.times_retransmitted = 0;
  ++stats_.packets_stored;
}

uint8_t RtxSender::RtxPayloadType(const RtpPacket& original) const {
  return cfg_.red_payload_type && original.payload_type == *cfg_.red_payload_type ? cfg_.rtx_red_payload_type
                                                                                   : cfg_.rtx_payload_type;
}

std::optional<RtpPacket> RtxSender::GetPayloadPaddingPacket(const std::function<bool(const RtpPacket&)>& accept) {
  if (!large_payload_packet_) return std::nullopt;
  if (!accept(*large_payload_packet_)) return std::nullopt;
  ++stats_.payload_padding;
  return MakeRtxPacket(*large_payload_packet_, cfg_.rtx_ssrc, RtxPayloadType(*large_payload_packet_), rtx_seq_++);
}

std::vector<RtpPacket> RtxSender::OnNack(std::span<const uint16_t> seqs, Timestamp now, TimeDelta /*rtt*/) {
  // RtpPacketHistory::GetPacketAndMarkAsPending with the RTX encapsulation.
  std::vector<RtpPacket> out;
  for (uint16_t seq : seqs) {
    ++stats_.nack_requests;
    Stored* s = Get(seq);
    if (!s) {
      ++stats_.not_found;
      continue;
    }
    if (s->pending || !VerifyRtt(*s, now)) {
      ++stats_.throttled;
      continue;
    }
    s->pending = true;
    out.push_back(MakeRtxPacket(*s->packet, cfg_.rtx_ssrc, RtxPayloadType(*s->packet), rtx_seq_++));
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
  w.Counter("payload_padding", stats_.payload_padding);
  w.Counter("bytes_retransmitted", static_cast<uint64_t>(stats_.bytes_retransmitted));
}

}  // namespace sm::loss
