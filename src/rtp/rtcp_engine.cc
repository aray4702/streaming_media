#include "sm/rtp/rtcp_engine.h"

#include <algorithm>

namespace sm::rtp {

namespace {
constexpr TimeDelta kVideoReportInterval = TimeDelta::Millis(1000);
constexpr TimeDelta kAudioReportInterval = TimeDelta::Millis(5000);
}  // namespace

RtcpEngine::RtcpEngine(RtcpConfig cfg) : cfg_(std::move(cfg)), rng_(cfg_.seed) {}

void RtcpEngine::SetSenderState(const RtcpSenderState& s) { sender_ = s; }
void RtcpEngine::AddReceiveStatistics(timing::ReceiveStatistics* stats) {
  if (stats) receive_stats_.push_back(stats);
}
void RtcpEngine::AddLocalMediaSsrc(uint32_t ssrc) { local_ssrcs_.push_back(ssrc); }

TimeDelta RtcpEngine::NominalInterval() const {
  return cfg_.report_interval.IsZero() ? (cfg_.has_video ? kVideoReportInterval : kAudioReportInterval)
                                       : cfg_.report_interval;
}

TimeDelta RtcpEngine::Interval() {
  const TimeDelta nominal = NominalInterval();
  TimeDelta min_interval = nominal;
  if (cfg_.has_video && sender_ && send_rate_ > DataRate::BitsPerSec(72'000)) {
    // TimeDelta::Seconds(360) / send_bitrate.kbps(): microseconds, read below with TimeDelta::ms(), which rounds
    // to the nearest millisecond (unit_base.h), as do the kbps.
    min_interval = std::min(TimeDelta::Micros(360'000'000 / ((send_rate_.bps() + 500) / 1000)), nominal);
  }
  // random_.Rand(min_interval_int * 1 / 2, min_interval_int * 3 / 2), clamped to 1 ms (rtcp_sender.cc:690-696).
  const int32_t m = static_cast<int32_t>((min_interval.us() + 500) / 1000);
  return TimeDelta::Millis(std::max<int64_t>(rng_.Rand(m * 1 / 2, m * 3 / 2), 1));
}

// The sender's bitrate for the interval rule: the caller's `send_bitrate`
// (libwebrtc's `GetSendRates(now).Sum()`, every packet type with its RTP header),
// else the rate over the last second from the SR octet counter, which counts
// media payload only.
void RtcpEngine::UpdateSendRate(Timestamp now) {
  if (sender_->send_bitrate) {
    send_rate_ = *sender_->send_bitrate;
    return;
  }
  rate_samples_.push_back({now, sender_->octet_count});
  while (rate_samples_.size() > 2 && now - rate_samples_[1].first >= TimeDelta::Seconds(1)) {
    rate_samples_.pop_front();
  }
  const auto& a = rate_samples_.front();
  const TimeDelta dt = now - a.first;
  if (dt >= TimeDelta::Millis(100)) {
    send_rate_ = DataRate::BitsPerSec(static_cast<int64_t>(static_cast<uint32_t>(sender_->octet_count - a.second)) * 8 *
                                      1000000 / dt.us());
  }
}

// --- Egress -------------------------------------------------------------------

void RtcpEngine::QueueNack(uint32_t media_ssrc, std::span<const uint16_t> seqs) {
  if (seqs.empty()) return;
  loss::Nack n;
  n.sender_ssrc = cfg_.local_ssrc;
  n.media_ssrc = media_ssrc;
  n.seqs.assign(seqs.begin(), seqs.end());
  auto bytes = loss::SerializeNack(n);
  if (!bytes.empty()) queue_.push_back({Tag::kNack, seqs.size(), std::move(bytes)});
}

void RtcpEngine::QueuePli(uint32_t media_ssrc) {
  queue_.push_back({Tag::kPli, 1, loss::SerializePli({cfg_.local_ssrc, media_ssrc})});
}

void RtcpEngine::QueueFir(uint32_t media_ssrc) {
  loss::Fir f;
  f.sender_ssrc = cfg_.local_ssrc;
  f.entries.push_back({media_ssrc, fir_seq_out_[media_ssrc]++});
  queue_.push_back({Tag::kFir, 1, loss::SerializeFir(f)});
}

void RtcpEngine::QueueLossNotification(loss::LossNotification l) {
  l.sender_ssrc = cfg_.local_ssrc;
  auto bytes = loss::SerializeLossNotification(l);
  if (!bytes.empty()) queue_.push_back({Tag::kLntf, 1, std::move(bytes)});
}

void RtcpEngine::QueueTransportFeedback(cc::TransportFeedback fb) {
  fb.sender_ssrc = cfg_.local_ssrc;
  auto bytes = cc::SerializeTransportFeedback(fb);
  if (!bytes.empty()) queue_.push_back({Tag::kTcc, 1, std::move(bytes)});
}

void RtcpEngine::QueueRemb(DataRate bitrate, std::span<const uint32_t> ssrcs) {
  auto bytes = cc::SerializeRemb(cfg_.local_ssrc, bitrate, std::vector<uint32_t>(ssrcs.begin(), ssrcs.end()));
  if (!bytes.empty()) queue_.push_back({Tag::kRemb, 1, std::move(bytes)});
}

void RtcpEngine::QueueBye(std::span<const uint32_t> ssrcs, std::string reason) {
  Bye b;
  b.ssrcs.assign(ssrcs.begin(), ssrcs.end());
  b.reason = std::move(reason);
  auto bytes = SerializeBye(b);
  if (!bytes.empty()) queue_.push_back({Tag::kBye, 1, std::move(bytes)});
}

// Transport-cc and REMB go out through RTCPSender::SendCombinedRtcpPacket,
// which adds no report; everything else is a compound RTCP packet.
bool RtcpEngine::CarriesReport(Tag tag) {
  return tag == Tag::kNack || tag == Tag::kPli || tag == Tag::kFir || tag == Tag::kLntf;
}

std::vector<uint8_t> RtcpEngine::BuildReport(Timestamp now) {
  std::vector<timing::ReportBlock> blocks;
  for (auto* s : receive_stats_) {
    if (blocks.size() == 31) break;  // 5-bit report count.
    blocks.push_back(s->MakeReportBlock(now));
  }
  if (sender_) {
    timing::SenderReport sr;
    sr.ssrc = sender_->ssrc ? sender_->ssrc : cfg_.local_ssrc;
    sr.ntp = timing::NtpTime::FromTimestamp(now);
    int64_t elapsed_ticks = (now - sender_->reference_time).us() * sender_->clock_rate / 1000000;
    sr.rtp_timestamp = sender_->rtp_timestamp + static_cast<uint32_t>(elapsed_ticks);
    sr.packet_count = sender_->packet_count;
    sr.octet_count = sender_->octet_count;
    sr.blocks = std::move(blocks);
    ++stats_.sr_sent;
    return timing::SerializeSenderReport(sr);
  }
  timing::ReceiverReport rr;
  rr.ssrc = cfg_.local_ssrc;
  rr.blocks = std::move(blocks);
  ++stats_.rr_sent;
  return timing::SerializeReceiverReport(rr);
}

// The packet that must open a compound datagram when there is no report to
// send: a receiver report with no blocks (RFC 3550 6.1).
std::vector<uint8_t> RtcpEngine::LeadingReport() const {
  timing::ReceiverReport rr;
  rr.ssrc = sender_ && sender_->ssrc ? sender_->ssrc : cfg_.local_ssrc;
  return timing::SerializeReceiverReport(rr);
}

void RtcpEngine::Flush(std::vector<std::vector<uint8_t>>& packets, RtcpOutKind kind,
                       std::vector<RtcpOutgoing>& out) {
  if (packets.empty()) return;
  RtcpOutgoing o;
  o.kind = kind;
  if (cfg_.reduced_size && kind == RtcpOutKind::kFeedback) {
    // RFC 5506: no leading SR/RR, so the compound rule does not apply.
    for (const auto& p : packets) o.bytes.insert(o.bytes.end(), p.begin(), p.end());
  } else {
    o.bytes = loss::BuildCompoundRtcp(packets);
  }
  stats_.bytes_sent += o.bytes.size();
  if (kind == RtcpOutKind::kFeedback) ++stats_.feedback_datagrams;
  if (!o.bytes.empty()) out.push_back(std::move(o));
  packets.clear();
}

// The SR or RR with its report blocks, then SDES CNAME (RTCPSender::PrepareReport).
void RtcpEngine::AppendReport(Timestamp now, std::vector<std::vector<uint8_t>>& packets) {
  packets.push_back(BuildReport(now));
  if (!cfg_.cname.empty()) {
    SdesChunk c;
    c.ssrc = sender_ && sender_->ssrc ? sender_->ssrc : cfg_.local_ssrc;
    c.items.push_back({kSdesCname, cfg_.cname});
    auto sdes = SerializeSdes(std::span<const SdesChunk>(&c, 1));
    if (!sdes.empty()) packets.push_back(std::move(sdes));
  }
}

std::vector<uint8_t> RtcpEngine::BuildExtendedReports(Timestamp now) {
  timing::ExtendedReports xr;
  xr.sender_ssrc = sender_ && sender_->ssrc ? sender_->ssrc : cfg_.local_ssrc;
  if (!sender_ && cfg_.xr_rrtr) xr.rrtr = timing::NtpTime::FromTimestamp(now);
  const uint32_t now_compact = timing::NtpTime::FromTimestamp(now).Compact();
  while (!received_rrtrs_.empty() && xr.dlrr.size() < timing::kMaxNumberOfDlrrItems) {
    const ReceivedRrtr& r = received_rrtrs_.front();
    xr.dlrr.push_back({r.ssrc, r.remote_compact, now_compact - r.local_compact});
    received_rrtrs_.pop_front();
  }
  if (!xr.rrtr && xr.dlrr.empty()) return {};
  ++stats_.xr_sent;
  if (xr.rrtr) ++stats_.rrtr_sent;
  stats_.dlrr_items_sent += xr.dlrr.size();
  return timing::SerializeExtendedReports(xr);
}

void RtcpEngine::EmitReport(Timestamp now, std::vector<RtcpOutgoing>& out) {
  std::vector<std::vector<uint8_t>> packets;
  AppendReport(now, packets);
  if (auto xr = BuildExtendedReports(now); !xr.empty()) packets.push_back(std::move(xr));
  ++stats_.reports_sent;
  SM_EVENT(cfg_.events, now, "rtcp", sender_ ? "sr" : "rr", packets[0].size(), cfg_.local_ssrc);
  Flush(packets, RtcpOutKind::kReport, out);
}

bool RtcpEngine::ReportDue(Timestamp now, bool keyframe_before_rtp) const {
  if (!started_) return false;
  // For video, a report due within 100 ms goes out before the keyframe.
  if (cfg_.has_video && keyframe_before_rtp) now += TimeDelta::Millis(100);
  return now >= next_report_;
}

std::vector<RtcpOutgoing> RtcpEngine::SendReport(Timestamp now) {
  std::vector<RtcpOutgoing> out;
  if (!started_) return out;
  if (sender_) UpdateSendRate(now);
  EmitReport(now, out);
  next_report_ = now + Interval();
  return out;
}

std::vector<RtcpOutgoing> RtcpEngine::Process(Timestamp now) {
  std::vector<RtcpOutgoing> out;
  if (sender_) UpdateSendRate(now);
  if (!started_) {
    started_ = true;
    // RTCPSender's constructor: SetNextRtcpSendEvaluationDuration(
    // report_interval_ / 2), half the nominal interval with no random draw
    // (rtcp_sender.cc:171), so the first audio SR goes out at 2.5 s (#331).
    TimeDelta first = cfg_.first_report_delay;
    if (first.IsZero()) first = NominalInterval() / 2;
    next_report_ = now + first;
  }

  // Regular report: SR or RR, then SDES, then feedback that is already due.
  if (now >= next_report_) {
    EmitReport(now, out);
    // Skip over missed slots rather than bursting reports.
    do {
      next_report_ += Interval();
    } while (next_report_ <= now);
  }

  // Early feedback: one compound per max_datagram_bytes.
  std::vector<std::vector<uint8_t>> packets;
  size_t bytes = 0;
  // The XR of a compound that carries the report goes last, after the
  // feedback (RTCPSender's report flags are ordered by packet type).
  std::vector<uint8_t> xr;
  auto flush = [&] {
    if (!xr.empty()) packets.push_back(std::move(xr));
    xr.clear();
    Flush(packets, RtcpOutKind::kFeedback, out);
  };
  for (auto& q : queue_) {
    if (!packets.empty() && bytes + q.bytes.size() > cfg_.max_datagram_bytes) {
      flush();
      bytes = 0;
    }
    if (packets.empty() && !cfg_.reduced_size) {
      if (CarriesReport(q.tag)) {
        // RTCPSender::SendCompoundRTCP in compound mode (#178): PrepareReport
        // adds the full SR/RR (report blocks) and SDES to NACK, PLI, FIR and
        // loss notification, and restarts the report timer.
        AppendReport(now, packets);
        xr = BuildExtendedReports(now);
        bytes += xr.size();
        next_report_ = now + Interval();
      } else {
        packets.push_back(LeadingReport());
      }
      for (const auto& p : packets) bytes += p.size();
    }
    bytes += q.bytes.size();
    switch (q.tag) {
      case Tag::kNack: ++stats_.nack_sent; stats_.nack_seqs_sent += q.units; break;
      case Tag::kPli: ++stats_.pli_sent; break;
      case Tag::kFir: ++stats_.fir_sent; break;
      case Tag::kLntf: ++stats_.lntf_sent; break;
      case Tag::kTcc: ++stats_.transport_cc_sent; break;
      case Tag::kRemb: ++stats_.remb_sent; break;
      case Tag::kBye: break;
    }
    SM_EVENT(cfg_.events, now, "rtcp", "feedback", q.bytes.size(), static_cast<int>(q.tag));
    packets.push_back(std::move(q.bytes));
  }
  queue_.clear();
  flush();
  return out;
}

Timestamp RtcpEngine::NextProcessTime(Timestamp now) const {
  if (!queue_.empty() || !started_) return now;
  return next_report_;
}

// --- Ingress ------------------------------------------------------------------

RtcpIncoming RtcpEngine::OnRtcp(std::span<const uint8_t> datagram, Timestamp now) {
  RtcpIncoming in;
  ++stats_.datagrams_received;
  auto packets = loss::SplitCompoundRtcp(datagram);
  if (!packets) {
    ++stats_.malformed;
    return in;
  }
  in.valid = true;

  auto add_blocks = [&](const std::vector<timing::ReportBlock>& blocks) {
    for (const auto& rb : blocks) {
      ++stats_.report_blocks_received;
      in.report_blocks.push_back(rb);
      if (!local_ssrcs_.empty() &&
          std::find(local_ssrcs_.begin(), local_ssrcs_.end(), rb.ssrc) == local_ssrcs_.end())
        continue;
      if (auto m = rtt_.OnReportBlock(rb, now)) in.rtts.push_back(*m);
    }
  };

  for (const auto& p : *packets) {
    bool ok = true;
    switch (p.type) {
      case loss::RtcpType::kSenderReport:
        if (auto sr = timing::ParseSenderReport(p.data)) {
          ++stats_.sr_received;
          add_blocks(sr->blocks);
          in.sender_reports.push_back(std::move(*sr));
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kReceiverReport:
        if (auto rr = timing::ParseReceiverReport(p.data)) {
          ++stats_.rr_received;
          add_blocks(rr->blocks);
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kNack:
        if (auto n = loss::ParseNack(p.data)) {
          ++stats_.nack_received;
          in.nacks.push_back(std::move(*n));
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kPli:
        if (auto pli = loss::ParsePli(p.data)) {
          ++stats_.pli_received;
          in.keyframe_requests.push_back(pli->media_ssrc);
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kFir:
        if (auto fir = loss::ParseFir(p.data)) {
          for (const auto& e : fir->entries) {
            // Like libwebrtc, ignore requests for streams we do not send.
            if (!local_ssrcs_.empty() &&
                std::find(local_ssrcs_.begin(), local_ssrcs_.end(), e.ssrc) == local_ssrcs_.end())
              continue;
            auto key = std::make_pair(fir->sender_ssrc, e.ssrc);
            auto it = fir_seq_in_.find(key);
            // A repeated sequence number is a retransmitted request.
            if (it != fir_seq_in_.end() && it->second == e.seq_nr) {
              ++stats_.fir_duplicates;
              continue;
            }
            // The key comes from the peer: bound the table. Forgetting the
            // history only risks acting on one retransmitted request twice.
            if (fir_seq_in_.size() >= kMaxFirTracked) fir_seq_in_.clear();
            fir_seq_in_[key] = e.seq_nr;
            ++stats_.fir_received;
            in.keyframe_requests.push_back(e.ssrc);
          }
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kLossNotification:
        if (auto l = loss::ParseLossNotification(p.data)) {
          ++stats_.lntf_received;
          in.loss_notifications.push_back(*l);
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kTransportCc:
        if (auto fb = cc::ParseTransportFeedback(p.data)) {
          ++stats_.transport_cc_received;
          in.transport_feedback.push_back(std::move(*fb));
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kRemb:
        if (auto r = cc::ParseRemb(p.data)) {
          ++stats_.remb_received;
          in.remb.push_back({r->first, std::move(r->second)});
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kSdes:
        if (auto s = ParseSdes(p.data)) {
          ++stats_.sdes_received;
          in.sdes.insert(in.sdes.end(), s->begin(), s->end());
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kBye:
        if (auto b = ParseBye(p.data)) {
          ++stats_.bye_received;
          in.byes.push_back(std::move(*b));
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kXr:
        if (auto xr = timing::ParseExtendedReports(p.data)) {
          ++stats_.xr_received;
          const uint32_t now_compact = timing::NtpTime::FromTimestamp(now).Compact();
          if (xr->rrtr) {
            // RTCPReceiver::HandleXrReceiveReferenceTime: the latest RRTR per sender.
            ++stats_.rrtr_received;
            auto it = std::find_if(received_rrtrs_.begin(), received_rrtrs_.end(),
                                   [&](const ReceivedRrtr& r) { return r.ssrc == xr->sender_ssrc; });
            if (it != received_rrtrs_.end()) {
              it->remote_compact = xr->rrtr->Compact();
              it->local_compact = now_compact;
            } else if (received_rrtrs_.size() < kMaxStoredRrtrs) {
              received_rrtrs_.push_back({xr->sender_ssrc, xr->rrtr->Compact(), now_compact});
            }
          }
          for (const auto& rti : xr->dlrr) {
            // HandleXrDlrrReportBlock: only an echo of our own RRTR, only with
            // the measurement on, and not before the peer has received one.
            ++stats_.dlrr_items_received;
            const bool ours = rti.ssrc == cfg_.local_ssrc ||
                              std::find(local_ssrcs_.begin(), local_ssrcs_.end(), rti.ssrc) != local_ssrcs_.end();
            if (!ours || !cfg_.xr_rrtr || rti.last_rr == 0) continue;
            const uint32_t rtt_compact = now_compact - rti.delay_since_last_rr - rti.last_rr;
            // CompactNtpRttToTimeDelta, as for report blocks.
            TimeDelta rtt = rtt_compact > 0x80000000u ? TimeDelta::Zero() : timing::CompactNtpToDelta(rtt_compact);
            in.xr_rtts.push_back(std::max(rtt, TimeDelta::Millis(1)));
          }
        } else {
          ok = false;
        }
        break;
      case loss::RtcpType::kOther:
        ++in.ignored;
        ++stats_.ignored;
        break;
    }
    // A packet with a valid header but a bad body is dropped; the rest of the
    // compound still counts (RTCPReceiver does the same).
    if (!ok) ++stats_.malformed;
  }
  return in;
}

void RtcpEngine::ExportMetrics(MetricsWriter& w) const {
  w.Counter("reports_sent", stats_.reports_sent);
  w.Counter("sr_sent", stats_.sr_sent);
  w.Counter("rr_sent", stats_.rr_sent);
  w.Counter("feedback_datagrams", stats_.feedback_datagrams);
  w.Counter("nack_sent", stats_.nack_sent);
  w.Counter("nack_seqs_sent", stats_.nack_seqs_sent);
  w.Counter("pli_sent", stats_.pli_sent);
  w.Counter("fir_sent", stats_.fir_sent);
  w.Counter("lntf_sent", stats_.lntf_sent);
  w.Counter("transport_cc_sent", stats_.transport_cc_sent);
  w.Counter("remb_sent", stats_.remb_sent);
  w.Counter("bytes_sent", stats_.bytes_sent);
  w.Counter("datagrams_received", stats_.datagrams_received);
  w.Counter("malformed", stats_.malformed);
  w.Counter("ignored", stats_.ignored);
  w.Counter("sr_received", stats_.sr_received);
  w.Counter("rr_received", stats_.rr_received);
  w.Counter("report_blocks_received", stats_.report_blocks_received);
  w.Counter("nack_received", stats_.nack_received);
  w.Counter("pli_received", stats_.pli_received);
  w.Counter("fir_received", stats_.fir_received);
  w.Counter("fir_duplicates", stats_.fir_duplicates);
  w.Counter("lntf_received", stats_.lntf_received);
  w.Counter("transport_cc_received", stats_.transport_cc_received);
  w.Counter("remb_received", stats_.remb_received);
  w.Counter("sdes_received", stats_.sdes_received);
  w.Counter("bye_received", stats_.bye_received);
  w.Counter("xr_sent", stats_.xr_sent);
  w.Counter("rrtr_sent", stats_.rrtr_sent);
  w.Counter("dlrr_items_sent", stats_.dlrr_items_sent);
  w.Counter("xr_received", stats_.xr_received);
  w.Counter("rrtr_received", stats_.rrtr_received);
  w.Counter("dlrr_items_received", stats_.dlrr_items_received);
  w.Gauge("fir_tracked", static_cast<double>(fir_seq_in_.size()));
  w.Gauge("queued_feedback", static_cast<double>(queue_.size()));
  rtt_.ExportMetrics(w);
}

}  // namespace sm::rtp
