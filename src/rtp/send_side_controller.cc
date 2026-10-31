#include "sm/rtp/send_side_controller.h"

#include <algorithm>

namespace sm::rtp {

namespace {
// video_send_stream_impl.cc:84: the encoder activity check interval.
constexpr TimeDelta kEncoderTimeOut = TimeDelta::Seconds(2);

cc::GccConfig WithEvents(cc::GccConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
cc::PacerConfig WithEvents(cc::PacerConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
cc::CongestionWindowConfig WithEvents(cc::CongestionWindowConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
cc::OutageConfig WithEvents(cc::OutageConfig c, EventSink* e) {
  if (!c.events) c.events = e;
  return c;
}
}  // namespace

SendSideController::SendSideController(SendSideConfig cfg, const sync::ExtensionMap* extensions)
    : cfg_(cfg),
      extensions_(extensions),
      gcc_(WithEvents(cfg.gcc, cfg.events)),
      pacer_(WithEvents(cfg.pacer, cfg.events)),
      outage_(WithEvents(cfg.outage, cfg.events)),
      cwnd_(WithEvents(cfg.cwnd, cfg.events)),
      allocator_(cfg.events),
      transport_seq_(1),
      video_rate_(cfg.repair_window.ms(), 8000.0f),
      rtx_rate_(cfg.repair_window.ms(), 8000.0f),
      fec_rate_(cfg.repair_window.ms(), 8000.0f),
      target_(gcc_.target()),
      pushback_target_(gcc_.target()),
      window_target_(gcc_.target()) {
  pacer_.SetTransportOverhead(DataSize::Bytes(cfg_.transport_overhead_bytes));
  pacer_.SetPacketSender([this](const cc::PacedPacket& sent, Timestamp now) { SendPacket(sent, now); });
}

void SendSideController::Apply(const cc::NetworkControlUpdate& u, Timestamp now) {
  ApplyTarget(u, now);
  ApplyPacer(u);
}

void SendSideController::ApplyTarget(const cc::NetworkControlUpdate& u, Timestamp now) {
  target_ = u.target_rate;
  rates_time_ = now;
  if (allocation_configured_ && !cfg_.control_handler) allocator_.OnTargetChanged(target_, now);
  if (allocation_configured_) gcc_.SetPacingLimits(allocator_.limits().min_allocatable, allocator_.limits().max_padding);
  outage_.SetCurrentRate(target_);
}

// RtpTransportControllerSend::PostUpdates: the pacer config, then the probe clusters. GetPacingRates runs at the
// end of MaybeTriggerOnNetworkChanged, after the pushback target of the same update, and the padding rate never
// exceeds that pushback target (goog_cc_network_control.cc:596-650, 675-679; issue #813).
void SendSideController::ApplyPacer(const cc::NetworkControlUpdate& u) {
  pacer_.SetPacingRates(gcc_.PacingRate(), std::min(gcc_.PaddingRate(), pushback_target_));
  for (const auto& c : u.probes) pacer_.AddProbeCluster(c);
}

// libwebrtc runs UpdateTargetBitrate() whenever the network state may have
// changed: after feedback, on the process interval and for every sent packet.
// UpdateCongestionWindowSize sizes the window from last_loss_based_target_rate_, the target the
// last MaybeTriggerOnNetworkChanged saw, before this update's trigger runs (OnProcessInterval).
// The pacer queue reaches the pushback only on the process interval (`window`): GoogCC's
// OnProcessInterval takes ProcessInterval::pacer_queue, the pacer's QueueSizeData
// (goog_cc_network_control.cc:215-217, rtp_transport_controller_send.cc:819-825).
void SendSideController::UpdatePushback(Timestamp now, bool window) {
  cwnd_.UpdateOutstandingData(gcc_.history().InFlight());
  if (window) cwnd_.UpdatePacingQueue(pacer_.QueueSizeData());
  if (window) cwnd_.UpdateWindow(window_target_);
  // MaybeTriggerOnNetworkChanged: the pushback target is at least the bandwidth estimator's minimum, and
  // the reduce ratio is taken from that clamped target (goog_cc_network_control.cc:596-608). Once the
  // pushback reaches the minimum, the ratio stops changing and no new update re-applies it (issue #738).
  pushback_target_ = std::max(gcc_.min_rate(), cwnd_.UpdateTargetBitrate(target_, now));
  pushback_reduce_ratio_ = cfg_.cwnd.enabled && target_.bps() > 0
                               ? static_cast<double>(target_.bps() - pushback_target_.bps()) / static_cast<double>(target_.bps())
                               : 0;
  window_target_ = target_;
}

// GoogCC's MaybeTriggerOnNetworkChanged hands its TargetTransferRate to the transport's
// CongestionControlHandler (drop-frame-only pushback: the loss-based target and the reduce ratio). It runs
// after feedback, on the process interval and, with congestion-window pushback, for every sent packet.
void SendSideController::PostTargetRate(Timestamp now) {
  if (!cfg_.control_handler) return;
  cc::TargetTransferRate msg;
  msg.target_rate = target_;
  msg.cwnd_reduce_ratio = pushback_reduce_ratio_;
  msg.fraction_loss = gcc_.send_side_bwe().fraction_loss();
  msg.round_trip_time = gcc_.send_side_bwe().round_trip_time();
  control_handler_.SetTargetRate(msg);
  UpdateControlState(now);
}

// RtpTransportControllerSend::UpdateControlState: only a message the handler passes on reaches
// the bitrate allocator (BitrateAllocator::OnNetworkEstimateChanged), and with it the streams.
void SendSideController::UpdateControlState(Timestamp now) {
  const std::optional<cc::TargetTransferRate> update = control_handler_.GetUpdate();
  if (!update) return;
  reported_reduce_ratio_ = update->cwnd_reduce_ratio;
  rates_time_ = now;
  if (allocation_configured_) allocator_.OnTargetChanged(update->target_rate, now, update->cwnd_reduce_ratio);
}

void SendSideController::WriteSendExtensions(RtpPacket& p, uint16_t transport_seq, Timestamp now) const {
  if (!extensions_) return;
  sync::ExtensionAccessor access(*extensions_);
  if (extensions_->IdOf(sync::uri::kTransportCc)) access.SetTransportSeq(p, transport_seq);
  if (extensions_->IdOf(sync::uri::kAbsSendTime)) access.SetAbsSendTime(p, now);
}

void SendSideController::ConfigureAllocation(DataRate audio_wire_rate, bool video) {
  ConfigureAllocation(AudioAllocation{audio_wire_rate, audio_wire_rate, audio_wire_rate}, video);
}

void SendSideController::ConfigureAllocation(const AudioAllocation& audio, bool video) {
  allocation_configured_ = true;
  if (audio.max > DataRate::Zero()) {
    cc::StreamAllocationConfig a;
    a.min = audio.min;
    a.max = audio.max;
    a.priority_rate = audio.priority_rate;
    a.enforce_min = true;
    allocator_.AddObserver("audio", a, [](DataRate) { return DataRate::Zero(); });
  }
  if (video) {
    cc::StreamAllocationConfig v;
    v.min = cfg_.video_min;
    v.max = cfg_.video_max;
    v.enforce_min = true;
    video_alloc_cfg_ = v;
    // OnBitrateUpdated: the protection is the share RTX and FEC take (FecControllerDefault).
    video_observer_ = [this](DataRate alloc) {
      // RtpVideoSender::OnBitrateUpdated sets encoder_target_rate_bps_ here, from the protection sent in
      // the last second, and the encoder keeps it until the next allocation.
      video_allocation_ = alloc;
      const VideoAllocationSplit split = SplitVideoAllocation(alloc);
      video_encoder_target_ = split.encoder_target;
      video_link_allocation_ = split.link_allocation;
      ++video_allocation_updates_;
      return split.protection;
    };
    allocator_.AddObserver("video", v, video_observer_);
    video_registered_ = true;
  }
  notified_limits_ = allocator_.limits();
  gcc_.SetMinAllocatable(allocator_.limits().min_allocatable);
  // SetAllocatedSendBitrateLimits: streams_config.max_total_allocated_bitrate.
  for (const auto& c : gcc_.OnMaxTotalAllocatedBitrate(allocator_.limits().max_allocatable, Timestamp::Micros(0))) pacer_.AddProbeCluster(c);
  target_ = std::max(target_, gcc_.target());
}

void SendSideController::Start(Timestamp now) {
  Apply(gcc_.Start(now), now);
  PostTargetRate(now);
  // StartupVideoSendStream: the activity check starts kEncoderTimeOut after the stream.
  if (video_registered_) next_video_activity_check_ = now + kEncoderTimeOut;
}

void SendSideController::RegisterVideo(Timestamp now) {
  rates_time_ = now;
  allocator_.AddObserver("video", video_alloc_cfg_, video_observer_);
  video_registered_ = true;
  NotifyAllocationLimits(now);
}

void SendSideController::UnregisterVideo(Timestamp now) {
  rates_time_ = now;
  allocator_.RemoveObserver("video");
  video_registered_ = false;
  NotifyAllocationLimits(now);
}

void SendSideController::NotifyAllocationLimits(Timestamp now) {
  const cc::AllocationLimits& l = allocator_.limits();
  if (l == notified_limits_) return;
  const bool min_changed = l.min_allocatable != notified_limits_.min_allocatable;
  const bool padding_changed = l.max_padding != notified_limits_.max_padding;
  notified_limits_ = l;
  for (const auto& c : gcc_.OnMaxTotalAllocatedBitrate(l.max_allocatable, now)) pacer_.AddProbeCluster(c);
  if (min_changed) gcc_.UpdateMinAllocatable(l.min_allocatable);
  if (min_changed || padding_changed) {
    gcc_.SetPacingLimits(l.min_allocatable, l.max_padding);
    pacer_.SetPacingRates(gcc_.PacingRate(), std::min(gcc_.PaddingRate(), pushback_target_));
  }
}

void SendSideController::CheckVideoActivity(Timestamp at) {
  if (!video_activity_) {
    // SignalEncoderTimedOut: only a stream that is supposed to produce frames leaves the allocator.
    if (!video_timed_out_ && video_registered_ && !video_encoder_target_.IsZero()) {
      ++video_timeouts_;
      SM_EVENT(cfg_.events, at, "send_side", "video_encoder_timed_out", allocator_.limits().max_allocatable.kbps(), 0);
      UnregisterVideo(at);
    }
    video_timed_out_ = true;
    video_disable_padding_ = true;
  } else if (video_timed_out_) {
    RegisterVideo(at);  // SignalEncoderActive.
    SM_EVENT(cfg_.events, at, "send_side", "video_encoder_active", allocator_.limits().max_allocatable.kbps(), 0);
    video_timed_out_ = false;
  }
  video_activity_ = false;
}

void SendSideController::OnVideoFrameEncoded(Timestamp now) {
  if (!next_video_activity_check_.IsFinite()) return;
  video_activity_ = true;
  if (video_disable_padding_) {
    video_disable_padding_ = false;
    RegisterVideo(now);  // To ensure that padding bitrate is propagated to the bitrate allocator.
    SM_EVENT(cfg_.events, now, "send_side", "video_encoder_active", allocator_.limits().max_allocatable.kbps(), 1);
  }
}

void SendSideController::OnVideoPacketized(size_t encoder_output_size, size_t packetized_payload_size, Timestamp now) {
  // AV1 and H264 packetizers may produce less packetized bytes than unpacketized.
  if (packetized_payload_size >= encoder_output_size)
    post_encode_overhead_rate_.Update(static_cast<int64_t>(packetized_payload_size - encoder_output_size), now.ms());
}

void SendSideController::RunDueVideoActivityChecks(Timestamp now) {
  while (next_video_activity_check_ <= now) {
    const Timestamp due = next_video_activity_check_;
    next_video_activity_check_ = due + kEncoderTimeOut;
    CheckVideoActivity(due);
  }
}

void SendSideController::OnVideoEncoderReconfigured(Timestamp now) {
  // The encoder queue posts OnEncoderConfigurationChanged to the worker queue, so
  // an activity task already due at `now` runs before it (issue #375).
  RunDueVideoActivityChecks(now);
  // IsRunning(): the send stream is started, so the allocator gets the new limits.
  if (next_video_activity_check_.IsFinite()) RegisterVideo(now);
}

void SendSideController::EnablePeriodicAlrProbing(bool enable) { gcc_.EnablePeriodicAlrProbing(enable); }

DataRate SendSideController::VideoPayloadRate() const {
  if (!allocation_configured_) return DataRate::Zero();
  // RtpVideoSender keeps its last allocation while the stream is out of the allocator.
  const DataRate alloc = video_allocation_;
  return std::max(DataRate::Zero(), alloc - OverheadRate(alloc, cfg_.max_packet_size + cfg_.transport_overhead_bytes));
}

void SendSideController::SetPacingFactor(double factor) {
  gcc_.SetPacingFactor(factor);
  // OnStreamsConfig returns the new pacer config at once (GetPacingRates).
  pacer_.SetPacingRates(gcc_.PacingRate(), std::min(gcc_.PaddingRate(), pushback_target_));
}

// RtpTransportControllerSend::OnReport posts GoogCC's update, which is empty for a loss report: the
// pacer, the pushback and the allocator get the new target with the next sent packet (with the pushback
// on, its default), feedback or process interval.
void SendSideController::OnLossReport(int64_t packets_lost_delta, int64_t packets_delta, Timestamp now) {
  gcc_.OnTransportLossReport(packets_lost_delta, packets_delta, now);
}

void SendSideController::SetPaddingSender(RtpSender* sender) {
  padding_senders_.clear();
  last_padding_sender_ = nullptr;
  if (sender) AddPaddingSender(sender);
  if (!sender) pacer_.SetPaddingGenerator(nullptr);
}

void SendSideController::AddPaddingSender(RtpSender* sender) {
  // PacketRouter::AddSendRtpModule / AddSendRtpModuleToMap (packet_router.cc:52-118): audio streams
  // at the back, so video is asked first; a stream that supports RTX payload padding becomes the
  // one asked before all others.
  if (sender->config().video) {
    padding_senders_.insert(padding_senders_.begin(), sender);
  } else {
    padding_senders_.push_back(sender);
  }
  if (sender->SupportsRtxPayloadPadding()) last_padding_sender_ = sender;
  // Padding is made as libwebrtc's RTPSender::GeneratePadding does, each packet with its real size.
  pacer_.SetPaddingGenerator([this](DataSize target, Timestamp now) {
    std::vector<cc::PacedPacket> out;
    for (SenderPacket& sp : GeneratePadding(static_cast<size_t>(target.bytes()), now)) {
      WriteSendExtensions(sp.packet, 0, now);  // Reserve the extensions written at release.
      const uint64_t id = next_id_++;
      cc::PacedPacket pp{id, cc::PacketPriority::kPadding, DataSize::Bytes(static_cast<int64_t>(sp.packet.size()) + cfg_.transport_overhead_bytes), now};
      pp.header_size = DataSize::Bytes(static_cast<int64_t>(sp.packet.HeaderSize()));
      out.push_back(pp);
      queued_[id] = std::move(sp);
      ++stats_.padding;
    }
    return out;
  });
}

std::vector<SenderPacket> SendSideController::GeneratePadding(size_t target_bytes, Timestamp now) {
  // PacketRouter::GeneratePadding (packet_router.cc:255-297): first the last stream that padded, if
  // it supports RTX payload padding; else the first stream, video before audio, that makes any.
  std::vector<SenderPacket> padding;
  if (last_padding_sender_ && last_padding_sender_->SupportsRtxPayloadPadding()) {
    padding = last_padding_sender_->GeneratePadding(target_bytes, now);
  }
  if (padding.empty()) {
    for (RtpSender* sender : padding_senders_) {
      if (!sender->SupportsPadding()) continue;
      padding = sender->GeneratePadding(target_bytes, now);
      if (!padding.empty()) {
        last_padding_sender_ = sender;
        break;
      }
    }
  }
  return padding;
}

void SendSideController::Enqueue(SenderPacket p, Timestamp now) {
  // Reserve the extensions written at release, so the pacer budgets the size
  // that goes on the wire.
  WriteSendExtensions(p.packet, 0, now);
  int64_t size = static_cast<int64_t>(p.packet.size()) + cfg_.transport_overhead_bytes;
  cc::PacketPriority prio = p.priority;
  uint64_t id = next_id_++;
  cc::PacedPacket pp{id, prio, DataSize::Bytes(size), now};
  pp.header_size = DataSize::Bytes(static_cast<int64_t>(p.packet.HeaderSize()));
  pp.is_key_frame = p.is_key_frame;
  pp.first_packet_of_frame = p.first_packet_of_frame;
  queued_[id] = std::move(p);
  pacer_.Enqueue(pp);
  // A keyframe flushed the video stream's queue: those packets are never sent. The sender is not told:
  // libwebrtc's PacingController::EnqueuePacket drops the flushed RTX packets (pacing_controller.cc:227-231)
  // and RtpPacketHistory keeps them pending, so a later NACK for them is ignored (rtp_packet_history.cc:183).
  for (const cc::PacedPacket& f : pacer_.TakeFlushed()) {
    queued_.erase(f.id);
    ++stats_.flushed;
  }
}

void SendSideController::SendPacket(const cc::PacedPacket& sent, Timestamp now) {
  // PacketRouter::SendPacket and RtpSenderEgress::SendPacket, inside the pacer's send loop
  // (PacingController::ProcessPackets, pacing_controller.cc:523-527): the packet gets its sequence
  // numbers and goes into its stream's history before the loop asks for padding, and the FEC it
  // completes joins the queue the loop is draining (issue #771).
  if (sent.is_padding) return;  // The pacer's own placeholder: no stream to pad on (PacketRouter has no module).
  auto it = queued_.find(sent.id);
  if (it == queued_.end()) {
    ++stats_.unknown_ids;
    return;
  }
  SenderPacket sp = std::move(it->second);
  queued_.erase(it);
  // ModuleRtpRtcpImpl2::CanSendPacket (rtp_rtcp_impl2.cc:337-350): media-SSRC padding that a new
  // media packet preempted (it no longer follows a frame's last packet) is dropped.
  if (sp.kind == PacketKind::kPadding && sp.sender && sp.packet.ssrc == sp.sender->config().ssrc &&
      !sp.sender->CanSendPaddingOnMediaSsrc()) {
    ++stats_.padding_dropped;
    return;
  }
  if (sp.sender) sp.sender->AssignSequenceNumber(sp, now);
  if (cfg_.gcc.bwe_trace) {
    // The pacer as this packet leaves it, before OnPacketSent charges it; sm_compare reads libwebrtc's
    // PacingController at the same point, from its send transport (issue #893).
    const cc::PacingController::TraceState ps = pacer_.trace_state();
    cc::BweTraceRow row;
    row.time_us = now.us();
    row.kind = cc::BweTraceRow::Kind::kPacer;
    row.bitrate_bps = ps.adjusted_media_rate.bps();
    row.loss_limit_bps = ps.pacing_rate.bps();
    row.instant_ub_bps = ps.media_debt.bytes();
    row.packets = static_cast<int>(ps.queue_packets);
    row.fraction_loss = static_cast<int>(ps.queue_data.bytes());
    row.modified_trend = ps.average_queue_time.ms_f();
    row.threshold = static_cast<double>(static_cast<int>(sp.kind));
    row.send_delta_ms = static_cast<double>(sp.packet.seq);
    row.send_ms = ps.last_process_time.IsFinite() ? static_cast<double>(ps.last_process_time.us()) / 1000.0 : -1;
    cfg_.gcc.bwe_trace->push_back(row);
  }
  ReleasedPacket r;
  // PacketRouter::SendPacket (packet_router.cc:213-228): only a packet that carries the
  // transport-wide sequence number extension gets a number (from 1), and only a numbered packet is
  // reported to the bandwidth estimator (RtpTransportControllerSend::NotifyBweOfPacedSentPacket).
  const bool numbered = extensions_ && extensions_->IdOf(sync::uri::kTransportCc);
  if (numbered) r.transport_seq = transport_seq_++;
  WriteSendExtensions(sp.packet, r.transport_seq, now);
  r.bytes = sp.packet.Serialize();
  r.kind = sp.kind;
  // RtpSenderEgress::SendPacket counts the packet in its send rates (UpdateRtpStats, rtp_sender_egress.cc:335,
  // 495: the RTP packet size, by media type) as it sends it, inside the pacer's loop. The sent-packet
  // notification that can post a new target is posted from the transport and runs after the loop
  // (RtpTransportControllerSend::OnSentPacket), so the protection share an allocation reads then
  // (RtpVideoSender::ProtectionRequest, GetSendRates) counts every packet of the loop (issue #875).
  if (r.kind == PacketKind::kRtx || r.kind == PacketKind::kFec || r.kind == PacketKind::kVideo) {
    (r.kind == PacketKind::kVideo ? video_rate_ : r.kind == PacketKind::kRtx ? rtx_rate_ : fec_rate_)
        .Update(static_cast<int64_t>(r.bytes.size()), now.ms());
  }
  // UpdateRtpStats then calls GetSendRates(now) for its bitrate callback (the stats proxy, rtp_sender_egress.cc:
  // 495-498), for every packet the video module sends, padding too. Each Rate() erases the buckets that left the
  // window, so a type with no recent packet empties, and its next packet restarts its active window
  // (RateStatistics::Update): an RTX burst after a quiet second is rated over the time since it started (#893).
  if (r.kind == PacketKind::kRtx || r.kind == PacketKind::kFec || r.kind == PacketKind::kVideo ||
      (r.kind == PacketKind::kPadding && sp.sender && sp.sender->config().video)) {
    (void)video_rate_.Rate(now.ms());
    (void)rtx_rate_.Rate(now.ms());
    (void)fec_rate_.Rate(now.ms());
  }
  r.wire_size = sent.size;
  r.probe_cluster = sent.probe_cluster;
  r.send_time = now;
  if (sp.sender) {
    sp.sender->OnPacketReleased(sp, now);
    // PacingController::ProcessPackets: the FEC the packet completed joins the queue.
    for (SenderPacket& f : sp.sender->TakeFecPackets()) Enqueue(std::move(f), now);
    if (numbered && (sp.kind == PacketKind::kAudio || sp.kind == PacketKind::kVideo || sp.kind == PacketKind::kFec)) {
      sent_media_[r.transport_seq] = {sp.sender, sp.packet.seq};
      // Feedback never covers more than a few seconds; bound the table.
      sent_media_.erase(static_cast<uint16_t>(r.transport_seq - 20000));
    }
  }
  egress_.push_back({sent, std::move(r)});
}

std::vector<ReleasedPacket> SendSideController::Process(Timestamp now) {
  std::vector<ReleasedPacket> out;
  pacer_.Process(now);  // Each packet goes through SendPacket() as the pacer sends it.
  // RtpTransportControllerSend::OnSentPacket, posted from the send loop: the estimator and the
  // congestion window see the packets once the loop is done (the send rates counted them in SendPacket).
  std::vector<Egress> egress;
  egress.swap(egress_);
  // An allocation a sent packet posts reads the send rates at the current time
  // (RtpVideoSender::ProtectionRequest, RtpSenderEgress::GetSendRates(now); issue #875).
  rates_time_ = now;
  for (Egress& e : egress) {
    const cc::PacedPacket& sent = e.sent;
    ReleasedPacket& r = e.released;
    const bool numbered = extensions_ && extensions_->IdOf(sync::uri::kTransportCc);
    if (numbered) gcc_.OnPacketSent(r.transport_seq, now, sent.size, sent.probe_cluster);
    if (numbered && cfg_.cwnd.enabled) {
      // RtpTransportControllerSend::ProcessSentPacket (rtp_transport_controller_send.cc:456-485): with the
      // pushback on, GoogCC's OnSentPacket runs MaybeTriggerOnNetworkChanged, and PostUpdates hands its
      // update to the pacer and the control handler. The update carries the target and the pacer config
      // when the loss-based target, its state, the loss fraction, the RTT, the ALR state or the pushback
      // target changed (goog_cc_network_control.cc:610-648).
      const DataRate last_pushback = pushback_target_;
      const cc::NetworkControlUpdate u = gcc_.MaybeTriggerOnSentPacket(now);
      if (u.triggered) ApplyTarget(u, now);
      UpdatePushback(now, false);
      if (u.triggered || pushback_target_ != last_pushback) ApplyPacer(u);
      PostTargetRate(now);
    } else {
      UpdatePushback(now, false);
      if (cfg_.cwnd.enabled) PostTargetRate(now);
    }
    if (r.kind == PacketKind::kRtx || r.kind == PacketKind::kFec || r.kind == PacketKind::kVideo) {
      repair_window_.push_back({now, sent.size.bytes(), r.kind});
      (r.kind == PacketKind::kVideo ? video_bytes_ : repair_bytes_) += sent.size.bytes();
    }
    window_.emplace_back(now, sent.size.bytes());
    window_bytes_ += sent.size.bytes();
    ++stats_.released;
    stats_.released_bytes += sent.size.bytes();
    out.push_back(std::move(r));
  }
  ExpireRepair(now);
  RunDueVideoActivityChecks(now);
  if (!last_gcc_.IsFinite() || now - last_gcc_ >= cfg_.gcc_interval) {
    last_gcc_ = now;
    // The transport's pacer-queue task (kPacerQueueUpdateInterval, 25 ms, started with the controller's
    // and before it): the encoder pauses while the pacer's expected queue time is above 2 s.
    if (cfg_.control_handler) control_handler_.SetPacerQueue(pacer_.QueueSizeData() == DataSize::Zero() ? TimeDelta::Zero() : pacer_.ExpectedQueueTime());
    if (cfg_.control_handler) UpdateControlState(now);
    const cc::NetworkControlUpdate u = gcc_.OnProcessInterval(now, send_rate(now));
    ApplyTarget(u, now);
    UpdatePushback(now, true);
    ApplyPacer(u);
    PostTargetRate(now);
  }
  // libwebrtc has no outage monitor: its sender keeps sending (audio at full rate, video at the target the
  // feedback time-out backs off to) and GoogCC recovers from the feedback that comes back. So does this one;
  // the monitor only follows the link (its state, metrics and events), and its decisions are not acted on.
  (void)outage_.Process(now);
  return out;
}

Timestamp SendSideController::NextProcessTime(Timestamp now) const {
  Timestamp next = pacer_.NextSendTime(now);
  if (last_gcc_.IsFinite()) next = std::min(next, last_gcc_ + cfg_.gcc_interval);
  next = std::min(next, next_video_activity_check_);
  return std::max(next, now);
}

void SendSideController::OnTransportFeedback(const cc::TransportFeedback& fb, Timestamp now) {
  if (!sent_media_.empty()) {
    // RtpVideoSender::OnPacketFeedbackVector (call/rtp_video_sender.cc:1001-1047). The feedback
    // demuxer hands each stream the packets of its media SSRC only (not RTX or padding), in
    // transport-wide order. A video stream's lost packets that no received packet of the stream
    // follows may go unseen by the receiver's NACK, so they are resent now ("early loss
    // detection"); the history's checks then hold back the NACK for them.
    std::unordered_map<RtpSender*, std::vector<uint16_t>> acked, early_loss;
    for (const auto& p : fb.packets) {
      auto it = sent_media_.find(p.seq);
      if (it == sent_media_.end()) continue;
      RtpSender* sender = it->second.sender;
      if (!p.received) {
        if (sender->config().video) early_loss[sender].push_back(it->second.seq);
        continue;
      }
      early_loss.erase(sender);
      acked[sender].push_back(it->second.seq);
      sent_media_.erase(it);
    }
    for (auto& [sender, seqs] : early_loss) {
      sender->OnEarlyLoss(seqs, now);
      // While an RTCP packet is handled, the resends wait for its end (Session::FlushResends).
      if (!hold_resends_) {
        for (SenderPacket& p : sender->TakePackets()) Enqueue(std::move(p), now);
      }
    }
    for (auto& [sender, seqs] : acked) sender->OnAcknowledged(seqs);
  }
  const cc::NetworkControlUpdate u = gcc_.OnTransportFeedback(fb, now);
  if (!gcc_.last_feedback_used()) {
    // TransportFeedbackAdapter found none of the feedback's packets (all reported received before,
    // or unknown): RtpTransportControllerSend passes nothing on (transport_feedback_adapter.cc:457-458).
    outage_.OnFeedback(now);
    return;
  }
  ApplyTarget(u, now);
  // GoogCcNetworkController::OnTransportPacketsFeedback (goog_cc_network_control.cc:421-541): the
  // pushback runs (MaybeTriggerOnNetworkChanged) only when the delay-based estimate updated, and
  // before UpdateCongestionWindowSize, so it weighs the outstanding data against the window from
  // before this feedback. After an outage the first feedback clears the lost packets from flight;
  // against the grown window that fill would fall under 0.1 and end the pushback at once (#446).
  if (gcc_.last_feedback_delay_updated()) UpdatePushback(now, false);
  ApplyPacer(u);
  if (auto rtt = gcc_.last_feedback_max_rtt()) {
    cwnd_.OnFeedbackRtt(*rtt);
    cwnd_.UpdateWindow(window_target_);
  }
  PostTargetRate(now);
  outage_.OnFeedback(now);
}

ControllerActions SendSideController::TakeActions() {
  ControllerActions a = actions_;
  actions_ = {};
  return a;
}

bool SendSideController::DropCapturedFrame() {
  const DataRate base = !allocation_configured_ ? target_
                        : cfg_.control_handler ? video_encoder_target_
                                               : AllocatedVideoTarget(allocator_.allocation("video"));
  const auto drop = cc::CongestionWindowPushback::FrameDropInterval(base, reduce_ratio(), cfg_.video_min);
  if (drop.interval == 0) return false;
  if (cwnd_frame_counter_++ % static_cast<uint64_t>(drop.interval) != 0) return false;
  ++cwnd_frames_dropped_;
  return true;
}

DataRate SendSideController::send_rate(Timestamp now) {
  while (!window_.empty() && window_.front().first <= now - cfg_.send_rate_window) {
    window_bytes_ -= window_.front().second;
    window_.pop_front();
  }
  return DataRate::BitsPerSec(window_bytes_ * 8 * 1000000 / cfg_.send_rate_window.us());
}

SendRates SendSideController::ComputeRates(DataRate audio_wire_rate, double fec_protection) const {
  DataRate r = target_ - audio_wire_rate;
  r = r * (1.0 / (1.0 + fec_protection));
  SendRates out;
  out.target = target_;
  out.audio_rate = audio_wire_rate;
  out.video_target = std::clamp(r, cfg_.video_min, cfg_.video_max);
  ApplyPushback(out);
  return out;
}

void SendSideController::ApplyPushback(SendRates& out) const {
  // Pushback acts on the whole target; the encoder drops frames and takes
  // the matching share of the video target.
  // The allocator passes the reduce ratio to the video stream (cwnd_reduce_ratio
  // in BitrateAllocationUpdate), so the encoder takes it against its own target.
  const auto drop = cc::CongestionWindowPushback::FrameDropInterval(allocation_configured_ ? out.video_target : target_, reduce_ratio(), cfg_.video_min);
  if (drop.interval > 0) {
    out.frame_drop_interval = drop.interval;
    out.video_target = std::clamp(out.video_target - out.video_target * (1.0 / drop.interval), cfg_.video_min, cfg_.video_max);
  }
}

void SendSideController::ExpireRepair(Timestamp now) {
  while (!repair_window_.empty() && repair_window_.front().time <= now - cfg_.repair_window) {
    const Released& f = repair_window_.front();
    (f.kind == PacketKind::kVideo ? video_bytes_ : repair_bytes_) -= f.bytes;
    repair_window_.pop_front();
  }
}

double SendSideController::repair_share() const {
  if (!cfg_.send_rate_stats) {
    // FecControllerDefault::UpdateFecRates: (sent_nack + sent_fec) / (video + nack + fec).
    const int64_t total = video_bytes_ + repair_bytes_;
    return total > 0 ? static_cast<double>(repair_bytes_) / static_cast<double>(total) : 0.0;
  }
  if (!rates_time_.IsFinite()) return 0.0;
  // RtpVideoSender::ProtectionRequest reads RtpSenderEgress::GetSendRates (rtp_video_sender.cc:973,
  // rtp_sender_egress.cc:340): each type's rate, or zero when its tracker has none.
  const int64_t now_ms = rates_time_.ms();
  const int64_t video = video_rate_.Rate(now_ms).value_or(0);
  const int64_t nack = rtx_rate_.Rate(now_ms).value_or(0);
  const int64_t fec = fec_rate_.Rate(now_ms).value_or(0);
  // FecControllerDefault::UpdateFecRates: (sent_nack + sent_fec) / (video + nack + fec).
  const int64_t total = video + nack + fec;
  return total > 0 ? static_cast<double>(static_cast<float>(nack + fec) / static_cast<float>(total)) : 0.0;
}

DataRate SendSideController::OverheadRate(DataRate rate, int64_t packet_size_bytes) const {
  if (packet_size_bytes <= 0) return DataRate::Zero();
  // packet_rate = rate / packet_size, rounded up to whole Hz; times the overhead per packet.
  const int64_t pkt_bits = packet_size_bytes * 8;
  const int64_t packets_per_sec = (rate.bps() + pkt_bits - 1) / pkt_bits;
  return DataRate::BitsPerSec(packets_per_sec * (cfg_.rtp_overhead_bytes + cfg_.transport_overhead_bytes) * 8);
}

SendSideController::VideoAllocationSplit SendSideController::SplitVideoAllocation(DataRate alloc) const {
  // RtpVideoSender::OnBitrateUpdated (rtp_video_sender.cc:885-939). has_packet_feedback_: the payload
  // rate is the allocation less the overhead rate; FecControllerDefault::UpdateFecRates keeps the share
  // RTX and FEC do not take.
  const double keep = 1.0 - std::min(repair_share(), cfg_.max_repair_share);
  const int64_t pkt = cfg_.max_packet_size + cfg_.transport_overhead_bytes;
  const DataRate payload = std::max(DataRate::Zero(), alloc - OverheadRate(alloc, pkt));
  // encoder_target_rate_bps_, not yet capped at the encoder maximum. UpdateFecRates returns
  // `estimated_bitrate_bps * (1.0 - protection_overhead_rate)` as a uint32_t, so the product is truncated.
  DataRate encoder = DataRate::BitsPerSec(static_cast<int64_t>(static_cast<double>(payload.bps()) * keep));
  // rtp_video_sender.cc:913-915: less the post-encode overhead (RtpVideoSender::GetPostEncodeOverhead),
  // at most half the target.
  const DataRate post_encode =
      std::min(rates_time_.IsFinite()
                   ? DataRate::BitsPerSec(post_encode_overhead_rate_.Rate(rates_time_.ms()).value_or(0))
                   : DataRate::Zero(),
               encoder / 2);
  encoder -= post_encode;
  // The protection is the allocation less the encoder target, its packet overhead and the post-encode
  // overhead (rtp_video_sender.cc:918-938).
  DataRate enc_overhead = encoder.IsZero() ? DataRate::Zero() : OverheadRate(encoder, pkt - cfg_.rtp_overhead_bytes);
  enc_overhead = std::min(enc_overhead, std::max(DataRate::Zero(), alloc - encoder));
  VideoAllocationSplit out;
  out.protection = std::max(DataRate::Zero(), alloc - (encoder + enc_overhead + post_encode));
  // VideoSendStreamImpl::OnBitrateUpdated (video_send_stream_impl.cc:902-914): the link allocation is the
  // encoder target less the protection, taken before the target is capped at the encoder maximum, and at
  // least the capped target. Above the maximum the encoder bitrate adjuster gets the difference as headroom.
  const DataRate link = encoder > out.protection ? encoder - out.protection : DataRate::Zero();
  out.encoder_target = std::min(cfg_.video_max, encoder);
  out.link_allocation = cfg_.link_headroom ? std::max(out.encoder_target, link) : out.encoder_target;
  return out;
}

DataRate SendSideController::AllocatedVideoTarget(DataRate alloc) const { return SplitVideoAllocation(alloc).encoder_target; }

SendRates SendSideController::AllocatedRates() const {
  // VideoSendStreamImpl::OnBitrateUpdated: the encoder gets the allocation less
  // the protection bitrate, at most its maximum. A paused stream gets nothing.
  SendRates out;
  out.target = target_;
  out.audio_rate = allocator_.allocation("audio");
  if (cfg_.control_handler) {
    out.video_target = video_encoder_target_;
    out.video_link_allocation = video_link_allocation_;
    out.video_allocation_updates = video_allocation_updates_;
  } else {
    const VideoAllocationSplit split = SplitVideoAllocation(allocator_.allocation("video"));
    out.video_target = split.encoder_target;
    out.video_link_allocation = split.link_allocation;
  }
  out.encoder_paused = cfg_.control_handler && control_handler_.encoder_paused();
  ApplyPushback(out);
  return out;
}

SendRates SendSideController::ComputeMeasuredRates(DataRate audio_wire_rate) const {
  if (allocation_configured_) return AllocatedRates();
  // Capped at kProtectionOverheadRateThreshold (0.5), as in FecControllerDefault.
  const double share = std::min(repair_share(), cfg_.max_repair_share);
  DataRate r = (target_ - audio_wire_rate) * (1.0 - share);
  SendRates out;
  out.target = target_;
  out.audio_rate = audio_wire_rate;
  out.video_target = std::clamp(r, cfg_.video_min, cfg_.video_max);
  ApplyPushback(out);
  return out;
}

void SendSideController::ExportMetrics(MetricsWriter& w) const {
  w.Counter("released", stats_.released);
  w.Counter("padding", stats_.padding);
  w.Counter("padding_dropped", stats_.padding_dropped);
  w.Counter("unknown_ids", stats_.unknown_ids);
  w.Counter("flushed", stats_.flushed);
  w.Counter("released_bytes", static_cast<uint64_t>(stats_.released_bytes));
  w.Gauge("target_kbps", target_.kbps());
  w.Counter("cwnd_frames_dropped", cwnd_frames_dropped_);
  w.Counter("video_timeouts", video_timeouts_);
}

}  // namespace sm::rtp
