#include "sm/rtp/session.h"

#include <algorithm>

#include "sm/rtp/h264.h"

namespace sm::rtp {
namespace {
RtcpConfig MakeRtcpConfig(const SessionConfig& c) {
  RtcpConfig r = c.rtcp;
  r.local_ssrc = c.local_ssrc;
  r.cname = c.cname;
  r.seed = c.seed;
  if (!r.events) r.events = c.events;
  return r;
}
SendSideConfig MakeSendConfig(const SessionConfig& c) {
  SendSideConfig s = c.send_side;
  if (!s.events) s.events = c.events;
  return s;
}
// RFC 5761 section 4: RTCP packet types 192 to 223 map to payload-type byte
// values 64 to 95 once the RTP marker bit is masked off.
bool LooksLikeRtcp(std::span<const uint8_t> b) {
  if (b.size() < 2) return false;
  const uint8_t pt = b[1] & 0x7f;
  return b[1] >= 192 && b[1] <= 223 && pt >= 64 && pt <= 95;
}
OutgoingKind ToOutgoing(PacketKind k) {
  switch (k) {
    case PacketKind::kAudio: return OutgoingKind::kAudio;
    case PacketKind::kVideo: return OutgoingKind::kVideo;
    case PacketKind::kRtx: return OutgoingKind::kRtx;
    case PacketKind::kFec: return OutgoingKind::kFec;
    case PacketKind::kPadding: return OutgoingKind::kPadding;
  }
  return OutgoingKind::kVideo;
}
}  // namespace

Session::Session(SessionConfig cfg)
    : cfg_(std::move(cfg)),
      rtcp_(MakeRtcpConfig(cfg_)),
      send_(MakeSendConfig(cfg_), &cfg_.extensions),
      tcc_(cfg_.local_ssrc, 0, cc::FeedbackBuilderConfig{cfg_.transport_feedback_version}),
      demux_(cfg_.extensions) {}

RtpSenderConfig Session::SenderConfig(uint32_t ssrc, uint8_t pt, int clock, bool video, const std::string& mid) const {
  RtpSenderConfig c;
  c.ssrc = ssrc;
  c.payload_type = pt;
  c.clock_rate = clock;
  c.video = video;
  c.mid = mid;
  c.mid_packets = mid.empty() ? 0 : cfg_.mid_packets;
  c.extensions = &cfg_.extensions;
  c.events = cfg_.events;
  return c;
}

bool PayloadTypeSupportsSkippingFecPackets(VideoCodecType codec, bool generic_picture_id_trial) {
  if (codec == VideoCodecType::kVP8 || codec == VideoCodecType::kVP9) return true;
  return codec == VideoCodecType::kGeneric && generic_picture_id_trial;
}

bool ShouldDisableRedAndUlpfec(const RedUlpfecDecisionInput& in) {
  bool disable = false;
  if (in.disable_ulpfec_experiment) disable = true;
  if (in.flexfec_enabled) disable = true;  // FlexFEC takes priority.
  // NACK + ULPFEC without a picture id wastes bandwidth: FEC packets must be retransmitted too.
  if (in.nack_enabled && in.ulpfec_enabled &&
      !PayloadTypeSupportsSkippingFecPackets(in.codec, in.generic_picture_id_trial)) {
    disable = true;
  }
  if (in.ulpfec_enabled != in.red_enabled) disable = true;  // Only one of the two: both off.
  return disable;
}

bool VideoFecEnabled(const VideoStreamConfig& v) {
  if (!v.fec) return false;
  RedUlpfecDecisionInput in;
  in.nack_enabled = v.nack;
  in.red_enabled = true;
  in.ulpfec_enabled = true;
  in.codec = v.codec;
  return !ShouldDisableRedAndUlpfec(in);
}

AudioSendId Session::AddAudioSend(const AudioStreamConfig& a) {
  RtpSenderConfig c = SenderConfig(a.ssrc, a.payload_type, a.clock_rate, false, cfg_.mid_audio);
  c.red_payload_type = a.red_payload_type;
  c.red_distance = a.red_distance;
  audio_send_.push_back({std::make_unique<RtpSender>(c), a.nominal_rate});
  rtcp_.AddLocalMediaSsrc(a.ssrc);
  if (!send_has_padding_) {
    send_.SetPaddingSender(audio_send_.back().sender.get());
    send_has_padding_ = true;
  }
  return {static_cast<int>(audio_send_.size()) - 1};
}

VideoSendId Session::AddVideoSend(const VideoStreamConfig& v) {
  RtpSenderConfig c = SenderConfig(v.ssrc, v.payload_type, v.clock_rate, true, cfg_.mid_video);
  // The whole packet stays within the MTU: leave room for the header
  // extensions (transport-cc, abs-send-time and MID, 4 byte header plus 3, 4
  // and 2 bytes padded) that are written after the payload is sized.
  constexpr size_t kExtensionReserve = 16;
  if (cfg_.packetization_version >= 2) {
    c.max_payload_size = v.max_payload_size;  // Sized per frame from the header (SendVideo).
  } else {
    c.max_payload_size = v.max_payload_size > kExtensionReserve + 1 ? v.max_payload_size - kExtensionReserve : v.max_payload_size;
  }
  c.rtx_ssrc = v.rtx_ssrc;
  c.rtx_payload_type = v.rtx_payload_type;
  c.rtx = v.rtx;
  c.rtx.version = std::max(c.rtx.version, cfg_.rtx_version);
  c.fec = VideoFecEnabled(v);
  c.fec_config = v.fec_config;
  video_send_.push_back({std::make_unique<RtpSender>(c), DataRate::Zero()});
  rtcp_.AddLocalMediaSsrc(v.ssrc);
  if (v.rtx_ssrc) rtcp_.AddLocalMediaSsrc(*v.rtx_ssrc);
  // Padding and probes go on the video stream's RTX SSRC, like libwebrtc.
  send_.SetPaddingSender(video_send_.back().sender.get());
  send_has_padding_ = true;
  return {static_cast<int>(video_send_.size()) - 1};
}

AudioRecvId Session::AddAudioReceive(const AudioStreamConfig& a, AudioDecoder* decoder) {
  RtpReceiverConfig c;
  c.ssrc = a.ssrc;
  c.payload_type = a.payload_type;
  c.clock_rate = a.clock_rate;
  c.video = false;
  c.extensions = &cfg_.extensions;
  c.feedback = &tcc_;
  c.decoder = decoder;
  c.red = a.red_payload_type.has_value();
  c.jitter_buffer = a.jitter_buffer;
  c.events = cfg_.events;
  auto r = std::make_unique<RtpReceiver>(c);
  rtcp_.AddReceiveStatistics(r->receive_statistics());
  audio_recv_.push_back({std::move(r), a.ssrc, 0});
  std::vector<uint8_t> pts = {a.payload_type};
  if (a.red_payload_type) pts.push_back(*a.red_payload_type);
  const int sink = static_cast<int>(sinks_.size());
  sinks_.emplace_back(false, static_cast<int>(audio_recv_.size()) - 1);
  demux_.AddSink(sink, cfg_.mid_audio, "", std::move(pts));
  return {static_cast<int>(audio_recv_.size()) - 1};
}

VideoRecvId Session::AddVideoReceive(const VideoStreamConfig& v) {
  RtpReceiverConfig c;
  c.ssrc = v.ssrc;
  c.payload_type = v.payload_type;
  c.clock_rate = v.clock_rate;
  c.video = true;
  c.extensions = &cfg_.extensions;
  c.feedback = &tcc_;
  c.parser = v.parser;
  c.keyframe_needs_parameter_sets = v.keyframe_needs_parameter_sets;
  c.deliver_bitstream = v.deliver_bitstream;
  c.media_observer = v.media_observer;
  c.rtx_ssrc = v.rtx_ssrc;
  c.nack = v.nack;
  c.fec = VideoFecEnabled(v);
  c.fec_config = v.fec_config;
  c.nack_config = v.nack_config;
  c.packet_buffer = v.packet_buffer;
  c.keyframe = v.keyframe;
  c.scheduler = v.scheduler;
  c.timing = v.timing;
  c.decode_time = v.decode_time;
  c.events = cfg_.events;
  auto r = std::make_unique<RtpReceiver>(c);
  rtcp_.AddReceiveStatistics(r->receive_statistics());
  // Version 2 asks only if the first frame is not a keyframe (libwebrtc).
  if (c.keyframe.version < 2) r->Start(Timestamp::Millis(0));
  video_recv_.push_back({std::move(r), v.ssrc, v.rtx_ssrc.value_or(0)});
  std::vector<uint8_t> pts = {v.payload_type};
  if (v.rtx_ssrc) pts.push_back(v.rtx_payload_type);
  if (VideoFecEnabled(v)) pts.push_back(v.fec_config.fec_payload_type);
  const int sink = static_cast<int>(sinks_.size());
  sinks_.emplace_back(true, static_cast<int>(video_recv_.size()) - 1);
  demux_.AddSink(sink, cfg_.mid_video, "", std::move(pts));
  return {static_cast<int>(video_recv_.size()) - 1};
}

void Session::Start(Timestamp now) {
  if (started_) return;
  started_ = true;
  // A receive-only endpoint has no congestion controller to run: its pacer,
  // GCC and outage monitor would only log decisions about a link it never
  // sends on.
  if (!HasSendStreams()) return;
  send_.Start(now);
  send_.OnRtt(rtt_);
  last_target_ = send_.target();
}

void Session::EnqueueSenderPackets(RtpSender& s, Timestamp now) {
  for (SenderPacket& p : s.TakePackets()) send_.Enqueue(std::move(p), now);
}

void Session::SendAudio(AudioSendId id, const EncodedAudioFrame& f, Timestamp now) { SendAudio(id, f, std::nullopt, now); }

void Session::SendAudio(AudioSendId id, const EncodedAudioFrame& f, std::optional<audio::AudioLevel> level, Timestamp now) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= audio_send_.size()) return;
  Start(now);
  RtpSender& s = *audio_send_[static_cast<size_t>(id.index)].sender;
  s.SendAudio(f.data, f.rtp_timestamp, f.start_of_talkspurt, now, level);
  EnqueueSenderPackets(s, now);
}

void Session::SendVideo(VideoSendId id, const EncodedVideoFrame& f, Timestamp now) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_send_.size()) return;
  RtpSender& s = *video_send_[static_cast<size_t>(id.index)].sender;
  if (f.data.empty()) {
    ++stats_.empty_video_frames;
    return;
  }
  std::vector<std::vector<uint8_t>> chunks;
  if (cfg_.packetization_version >= 2) {
    // rtp_sender_video.cc: packet_capacity less the header of a middle packet.
    // The first, last and single packets carry no extension the middle one
    // lacks here (transport-cc, abs-send-time and MID go on every packet), so
    // their reductions are zero.
    const size_t capacity = s.config().max_payload_size + RtpPacket::kFixedHeaderSize;
    const size_t header = s.MediaHeaderSize();
    PayloadSizeLimits limits;
    limits.max_payload_len = static_cast<int>(capacity > header ? capacity - header : 0);
    chunks = H264Packetizer::Packetize(f.data, limits);
  } else {
    chunks = H264Packetizer::Packetize(f.data, s.config().max_payload_size);
  }
  SendVideoChunks(id, chunks, f.rtp_timestamp, now, f.keyframe);
}

void Session::SendVideoChunks(VideoSendId id, std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp,
                              Timestamp now, bool keyframe) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_send_.size()) return;
  if (chunks.empty()) {
    ++stats_.empty_video_frames;
    return;
  }
  Start(now);
  RtpSender& s = *video_send_[static_cast<size_t>(id.index)].sender;
  s.SendVideoFrame(chunks, rtp_timestamp, now, keyframe);
  EnqueueSenderPackets(s, now);
}

void Session::SetFecParams(VideoSendId id, const loss::FecParams& p) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_send_.size()) return;
  video_send_[static_cast<size_t>(id.index)].sender->SetFecParams(p);
}

void Session::SetRtt(TimeDelta rtt) {
  rtt_ = rtt;
  send_.OnRtt(rtt_);
  for (auto& v : video_recv_) v.receiver->OnRtt(rtt_);
  for (auto& a : audio_recv_) a.receiver->OnRtt(rtt_);
}

void Session::OnDecodeFailure(VideoRecvId id, Timestamp now) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_recv_.size()) return;
  video_recv_[static_cast<size_t>(id.index)].receiver->OnDecodeFailure(now);
}

RtpSender* Session::SenderForSsrc(uint32_t ssrc) {
  for (auto& v : video_send_) {
    if (v.sender->config().ssrc == ssrc) return v.sender.get();
  }
  for (auto& a : audio_send_) {
    if (a.sender->config().ssrc == ssrc) return a.sender.get();
  }
  return nullptr;
}

void Session::OnPacket(std::span<const uint8_t> bytes, Timestamp now) {
  Start(now);
  ++stats_.datagrams_in;
  if (LooksLikeRtcp(bytes)) {
    ++stats_.rtcp_in;
    HandleRtcp(bytes, now);
  } else {
    HandleRtp(bytes, now);
  }
}

void Session::HandleRtp(std::span<const uint8_t> bytes, Timestamp now) {
  auto p = RtpPacket::Parse(bytes);
  if (!p) {
    ++stats_.malformed;
    return;
  }
  ++stats_.rtp_in;
  if (!peer_mid_seen_ && sync::ExtensionAccessor(cfg_.extensions).GetString(*p, sync::uri::kMid)) {
    // The peer answered with a MID: it has ours, stop repeating it.
    peer_mid_seen_ = true;
    for (auto& a : audio_send_) a.sender->AcknowledgeMid();
    for (auto& v : video_send_) v.sender->AcknowledgeMid();
  }
  auto sink = demux_.Demux(*p);
  if (!sink || *sink < 0 || static_cast<size_t>(*sink) >= sinks_.size()) {
    ++stats_.unroutable;
    SM_EVENT(cfg_.events, now, "rtp_session", "unroutable", p->ssrc, p->payload_type);
    return;
  }
  const auto [video, index] = sinks_[static_cast<size_t>(*sink)];
  RtpReceiver& r = video ? *video_recv_[static_cast<size_t>(index)].receiver : *audio_recv_[static_cast<size_t>(index)].receiver;
  if (r.OnPacket(bytes, now) == ReceivedKind::kMalformed) ++stats_.malformed;
}

void Session::HandleRtcp(std::span<const uint8_t> bytes, Timestamp now) {
  RtcpIncoming in = rtcp_.OnRtcp(bytes, now);
  if (!in.valid) {
    ++stats_.malformed;
    return;
  }
  for (const auto& sr : in.sender_reports) {
    for (auto& v : video_recv_) {
      if (v.ssrc == sr.ssrc) v.receiver->OnSenderReport(sr, now);
    }
    for (auto& a : audio_recv_) {
      if (a.ssrc == sr.ssrc) a.receiver->OnSenderReport(sr, now);
    }
  }
  if (!in.rtts.empty()) {
    rtt_ = in.rtts.back();
    have_rtt_ = true;
    send_.OnRtt(rtt_);
    for (auto& v : video_recv_) v.receiver->OnRtt(rtt_);
    for (auto& a : audio_recv_) a.receiver->OnRtt(rtt_);
    for (auto& v : video_send_) v.sender->SetRtt(rtt_, now);
    for (auto& a : audio_send_) a.sender->SetRtt(rtt_, now);
    SessionEvent e;
    e.type = SessionEventType::kRttUpdated;
    e.time = now;
    e.rtt = rtt_;
    Emit(e);
  }
  for (const auto& rb : in.report_blocks) {
    SessionEvent e;
    e.type = SessionEventType::kReportBlock;
    e.time = now;
    e.ssrc = rb.ssrc;
    e.loss_fraction = rb.fraction_lost / 256.0;
    Emit(e);
  }
  for (const auto& n : in.nacks) {
    if (RtpSender* s = SenderForSsrc(n.media_ssrc)) {
      s->OnNack(n.seqs, now, rtt_);
      EnqueueSenderPackets(*s, now);
    } else {
      ++stats_.nacks_unmatched;
    }
  }
  for (const auto& fb : in.transport_feedback) send_.OnTransportFeedback(fb, now);
  for (const auto& r : in.remb) send_.OnRemb(r.bitrate);
  for (uint32_t ssrc : in.keyframe_requests) {
    SessionEvent e;
    e.type = SessionEventType::kKeyframeRequested;
    e.time = now;
    e.ssrc = ssrc;
    Emit(e);
  }
}

void Session::UpdateSenderState() {
  RtpSender* s = !video_send_.empty() ? video_send_.front().sender.get()
                                      : (!audio_send_.empty() ? audio_send_.front().sender.get() : nullptr);
  if (s) rtcp_.SetSenderState(s->SenderState());
}

void Session::Process(Timestamp now) {
  Start(now);
  // Receivers: frames, NACK and keyframe requests.
  for (auto& v : video_recv_) {
    RtpReceiver& r = *v.receiver;
    r.Process(now);
    ReceiverFeedback fb = r.TakeFeedback();
    if (!fb.nacks.empty()) rtcp_.QueueNack(v.ssrc, fb.nacks);
    for (const auto& m : fb.keyframe) {
      switch (m.type) {
        case loss::FeedbackType::kPli: rtcp_.QueuePli(v.ssrc); break;
        case loss::FeedbackType::kFir: rtcp_.QueueFir(v.ssrc); break;
        case loss::FeedbackType::kLntf: {
          loss::LossNotification l;
          l.media_ssrc = v.ssrc;
          l.last_decoded = static_cast<uint16_t>(m.last_decodable);
          l.last_received = static_cast<uint16_t>(m.last_received);
          rtcp_.QueueLossNotification(l);
          break;
        }
      }
    }
    if (fb.pli) rtcp_.QueuePli(v.ssrc);
  }
  // Transport-wide feedback covers every stream of the transport.
  UpdateTransportFeedbackRate();
  if (cfg_.transport_feedback_interval.us() > 0 &&
      (!last_tcc_.IsFinite() || now - last_tcc_ >= TransportFeedbackInterval())) {
    last_tcc_ = now;
    for (auto& f : tcc_.Build()) rtcp_.QueueTransportFeedback(std::move(f));
  }
  // Paced media.
  if (HasSendStreams()) {
    for (ReleasedPacket& r : send_.Process(now)) {
      out_.push_back({std::move(r.bytes), ToOutgoing(r.kind), r.wire_size});
    }
  }
  ControllerActions act = send_.TakeActions();
  if (act.pause_encoder) Emit({SessionEventType::kEncoderPause, now, 0, DataRate::Zero(), TimeDelta::Zero()});
  if (act.resume_encoder) Emit({SessionEventType::kEncoderResume, now, 0, DataRate::Zero(), TimeDelta::Zero()});
  if (act.request_keyframe) Emit({SessionEventType::kKeyframeRequested, now, 0, DataRate::Zero(), TimeDelta::Zero()});
  if (send_.target() != last_target_) {
    last_target_ = send_.target();
    Emit({SessionEventType::kTargetRateChanged, now, 0, last_target_, TimeDelta::Zero()});
  }
  // RTCP: reports and queued feedback.
  UpdateSenderState();
  for (RtcpOutgoing& o : rtcp_.Process(now)) {
    const DataSize wire = DataSize::Bytes(static_cast<int64_t>(o.bytes.size()) + cfg_.send_side.transport_overhead_bytes);
    out_.push_back({std::move(o.bytes), OutgoingKind::kRtcp, wire});
  }
}

// Version 2 (libwebrtc): the generator's interval follows this endpoint's own
// send estimate; a receive-only endpoint runs on the congestion controller's
// start rate, as libwebrtc's Call does before any feedback.
void Session::UpdateTransportFeedbackRate() {
  if (cfg_.transport_feedback_version < 2) return;
  const DataRate estimate = HasSendStreams() && started_ ? send_.target() : cfg_.send_side.gcc.start_rate;
  if (estimate == last_tcc_estimate_) return;
  last_tcc_estimate_ = estimate;
  tcc_.OnSendBandwidthEstimateChanged(estimate);
}

TimeDelta Session::TransportFeedbackInterval() const {
  return cfg_.transport_feedback_version < 2 ? cfg_.transport_feedback_interval : tcc_.Interval();
}

Timestamp Session::NextProcessTime(Timestamp now) const {
  Timestamp next = rtcp_.NextProcessTime(now);
  if (HasSendStreams()) next = std::min(next, send_.NextProcessTime(now));
  if (!video_recv_.empty()) next = std::min(next, now + TimeDelta::Millis(1));
  if (cfg_.transport_feedback_interval.us() > 0 && last_tcc_.IsFinite()) {
    next = std::min(next, last_tcc_ + TransportFeedbackInterval());
  }
  return std::max(next, now);
}

std::vector<OutgoingPacket> Session::TakeOutgoing() {
  std::vector<OutgoingPacket> out;
  out.swap(out_);
  stats_.datagrams_out += out.size();
  return out;
}

std::vector<ReceivedVideoFrame> Session::TakeVideoFrames(VideoRecvId id) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_recv_.size()) return {};
  return video_recv_[static_cast<size_t>(id.index)].receiver->TakeFrames();
}

std::vector<SessionEvent> Session::TakeEvents() {
  std::vector<SessionEvent> out;
  out.swap(events_);
  return out;
}

timing::AudioOperation Session::GetAudio(AudioRecvId id, std::span<float> out, Timestamp now) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= audio_recv_.size()) return timing::AudioOperation::kSilence;
  return audio_recv_[static_cast<size_t>(id.index)].receiver->GetAudio(out, now);
}

SendRates Session::rates() const {
  DataRate audio = DataRate::Zero();
  for (const auto& a : audio_send_) audio = audio + a.nominal_rate;
  return send_.ComputeMeasuredRates(audio);
}

void Session::ExportMetrics(MetricsWriter& w) const {
  w.Counter("datagrams_in", stats_.datagrams_in);
  w.Counter("datagrams_out", stats_.datagrams_out);
  w.Counter("rtcp_in", stats_.rtcp_in);
  w.Counter("rtp_in", stats_.rtp_in);
  w.Counter("malformed", stats_.malformed);
  w.Counter("unroutable", stats_.unroutable);
  w.Counter("empty_video_frames", stats_.empty_video_frames);
  w.Counter("nacks_unmatched", stats_.nacks_unmatched);
  w.Gauge("rtt_ms", static_cast<double>(rtt_.us()) / 1000.0);
  w.Gauge("target_kbps", send_.target().kbps());
}

}  // namespace sm::rtp
