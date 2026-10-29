#include "sm/rtp/session.h"

#include <algorithm>
#include <cmath>

#include "sm/rtp/h264.h"
#include "sm/rtp/sps_vui_rewriter.h"

namespace sm::rtp {
namespace {
// VideoSendStreamImpl's kStrictPacingMultiplier (PacingConfig::pacing_factor's
// default): the pacing factor a video stream with send-side BWE sets.
constexpr double kStrictPacingMultiplier = 1.1;
RtcpConfig MakeRtcpConfig(const SessionConfig& c) {
  RtcpConfig r = c.rtcp;
  r.local_ssrc = c.local_ssrc;
  r.cname = c.cname;
  r.seed = c.seed;
  if (!r.events) r.events = c.events;
  return r;
}
// The rates as libwebrtc's int-bps BitrateConstraints can hold them (cc::ConvertConstraints, #464).
SessionConfig WithIntConstraints(SessionConfig c) {
  c.send_side.gcc = cc::ConvertConstraints(c.send_side.gcc);
  return c;
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
    : cfg_(WithIntConstraints(std::move(cfg))),
      rtcp_(MakeRtcpConfig(cfg_)),
      send_(MakeSendConfig(cfg_), &cfg_.extensions),
      tcc_(cfg_.local_ssrc, 0),
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
  SendStream as;
  as.sender = std::make_unique<RtpSender>(c);
  as.nominal_rate = a.nominal_rate;
  as.allocation_rate = a.allocation_rate > DataRate::Zero() ? a.allocation_rate : a.nominal_rate;
  if (a.allocation_rate <= DataRate::Zero() && a.max_bitrate > DataRate::Zero()) {
    as.allocation_rate = DataRate::Zero();
    as.min_bitrate = a.min_bitrate;
    as.max_bitrate = std::max(a.max_bitrate, a.min_bitrate);
    as.priority_bitrate = a.priority_bitrate;
    as.min_frame_length = a.min_frame_length;
    as.max_frame_length = std::max(a.max_frame_length, a.min_frame_length);
  }
  audio_send_.push_back(std::move(as));
  rtcp_.AddLocalMediaSsrc(a.ssrc);
  // AudioSendStream::ConfigureStream: with transport-cc (and allocate_audio_without_feedback_
  // off) the audio stream asks for periodic ALR probing (WebRTC-Audio-AlrProbing is on
  // unless disabled; audio_send_stream.cc:158-159, 297-308).
  if (cfg_.extensions.IdOf(sync::uri::kTransportCc)) send_.EnablePeriodicAlrProbing(true);
  if (!send_has_padding_) {
    send_.SetPaddingSender(audio_send_.back().sender.get());
    send_has_padding_ = true;
  }
  return {static_cast<int>(audio_send_.size()) - 1};
}

VideoSendId Session::AddVideoSend(const VideoStreamConfig& v) {
  RtpSenderConfig c = SenderConfig(v.ssrc, v.payload_type, v.clock_rate, true, cfg_.mid_video);
  c.max_payload_size = v.max_payload_size;  // Sized per frame from the header (SendVideo).
  c.rtx_ssrc = v.rtx_ssrc;
  c.rtx_payload_type = v.rtx_payload_type;
  c.rtx = v.rtx;
  c.fec = VideoFecEnabled(v);
  c.ulpfec = v.ulpfec;
  // RtpVideoSender's constructor: FecControllerDefault with the method the stream has on,
  // sized for RtpConfig::max_packet_size.
  SendStream vs;
  vs.sender = std::make_unique<RtpSender>(c);
  vs.fec = std::make_unique<loss::FecControllerDefault>(cfg_.events);
  vs.fec->SetProtectionMethod(c.fec, v.nack);
  vs.nack = v.nack;
  vs.fec->SetEncodingData(704, 576, 1, static_cast<size_t>(cfg_.send_side.max_packet_size));
  video_send_.push_back(std::move(vs));
  rtcp_.AddLocalMediaSsrc(v.ssrc);
  if (v.rtx_ssrc) rtcp_.AddLocalMediaSsrc(*v.rtx_ssrc);
  // Padding and probes go on the video stream's RTX SSRC, like libwebrtc.
  send_.SetPaddingSender(video_send_.back().sender.get());
  // VideoSendStreamImpl's constructor (video_send_stream_impl.cc:487-523): with
  // transport-cc the stricter pacing factor (GetConfiguredPacingFactor,
  // kStrictPacingMultiplier) applies from the start, and periodic ALR probing is
  // set to RateControlSettings::UseAlrProbing(), off without field trials. That
  // overrides the request an audio stream added earlier made (issue #355).
  if (cfg_.extensions.IdOf(sync::uri::kTransportCc)) {
    send_.SetPacingFactor(kStrictPacingMultiplier);
    send_.EnablePeriodicAlrProbing(false);
  }
  send_has_padding_ = true;
  return {static_cast<int>(video_send_.size()) - 1};
}

void Session::MaybeCreateSync() {
  if (!cfg_.av_sync || av_sync_ || audio_recv_.empty() || video_recv_.empty()) return;
  sync_audio_ = std::make_unique<SyncableStream>(audio_recv_.front().receiver.get(), audio_recv_.front().ssrc);
  sync_video_ = std::make_unique<SyncableStream>(video_recv_.front().receiver.get(), video_recv_.front().ssrc);
  av_sync_ = std::make_unique<sync::RtpStreamsSynchronizer>(sync_video_.get(), cfg_.events);
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
  c.receive_statistics_v2 = cfg_.receive_statistics_v2;
  c.events = cfg_.events;
  auto r = std::make_unique<RtpReceiver>(c);
  if (cfg_.audio_receive_rtcp) {
    // ChannelReceive's own receiver-only RtpRtcp module (channel_receive.cc:115-139, 608).
    if (!audio_recv_rtcp_) {
      RtcpConfig rc = MakeRtcpConfig(cfg_);
      rc.cname.clear();
      rc.has_video = false;
      rc.report_interval = TimeDelta::Zero();
      rc.first_report_delay = TimeDelta::Zero();
      rc.xr_rrtr = false;
      audio_recv_rtcp_ = std::make_unique<RtcpEngine>(rc);
    }
    audio_recv_rtcp_->AddReceiveStatistics(r->receive_statistics());
  } else {
    rtcp_.AddReceiveStatistics(r->receive_statistics());
  }
  audio_recv_.push_back({std::move(r), a.ssrc, 0});
  std::vector<uint8_t> pts = {a.payload_type};
  if (a.red_payload_type) pts.push_back(*a.red_payload_type);
  const int sink = static_cast<int>(sinks_.size());
  sinks_.emplace_back(false, static_cast<int>(audio_recv_.size()) - 1);
  // Call registers each receive stream by its SSRC (RtpStreamReceiverController), and the
  // BUNDLE transport by its MID and payload types; one RtpDemuxer here takes all three.
  sync::RtpDemuxerCriteria criteria;
  criteria.mid = cfg_.mid_audio;
  criteria.ssrcs = {a.ssrc};
  criteria.payload_types = std::move(pts);
  demux_.AddSink(sink, criteria);
  MaybeCreateSync();
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
  // Both H.264 rules apply only to H.264 packets, as libwebrtc: OnReceivedPayloadData runs the
  // tracker for kVideoCodecH264, and FindFrames keeps missing_packets_ for an H.264 descriptor.
  const bool h264 = v.codec == VideoCodecType::kH264;
  c.sps_pps_tracker = v.sps_pps_tracker && h264;
  c.render_smoothing = v.render_smoothing;
  c.deliver_bitstream = v.deliver_bitstream;
  c.media_observer = v.media_observer;
  c.rtx_ssrc = v.rtx_ssrc;
  c.nack = v.nack;
  c.fec = VideoFecEnabled(v);
  c.ulpfec = v.ulpfec;
  c.nack_config = v.nack_config;
  c.packet_buffer = v.packet_buffer;
  c.packet_buffer.missing_packets = v.packet_buffer.missing_packets && h264;
  c.keyframe = v.keyframe;
  c.scheduler = v.scheduler;
  c.timing = v.timing;
  c.decode_time = v.decode_time;
  c.receive_statistics_v2 = cfg_.receive_statistics_v2;
  c.events = cfg_.events;
  auto r = std::make_unique<RtpReceiver>(c);
  rtcp_.AddReceiveStatistics(r->receive_statistics());
  rtcp_.AddReceiveStatistics(r->rtx_receive_statistics());
  video_recv_.push_back({std::move(r), v.ssrc, v.rtx_ssrc.value_or(0)});
  std::vector<uint8_t> pts = {v.payload_type};
  if (v.rtx_ssrc) pts.push_back(v.rtx_payload_type);
  if (VideoFecEnabled(v)) {
    pts.push_back(v.ulpfec.red_payload_type);
    if (v.rtx_ssrc) pts.push_back(v.ulpfec.red_rtx_payload_type);
  }
  const int sink = static_cast<int>(sinks_.size());
  sinks_.emplace_back(true, static_cast<int>(video_recv_.size()) - 1);
  sync::RtpDemuxerCriteria criteria;
  criteria.mid = cfg_.mid_video;
  criteria.ssrcs = {v.ssrc};
  if (v.rtx_ssrc) criteria.ssrcs.push_back(*v.rtx_ssrc);
  criteria.payload_types = std::move(pts);
  demux_.AddSink(sink, criteria);
  MaybeCreateSync();
  return {static_cast<int>(video_recv_.size()) - 1};
}

SendSideController::AudioAllocation Session::AudioAllocationOf(const SendStream& a) const {
  if (a.max_bitrate <= DataRate::Zero()) return {a.allocation_rate, a.allocation_rate, a.allocation_rate};
  // AudioSendStream::GetMinMaxBitrateConstraints and ConfigureBitrateObserver
  // (audio_send_stream.cc:808-901): overhead_per_packet_ is the transport's bytes plus
  // RTPSender::ExpectedPerPacketOverhead, the media header with its non-volatile
  // extensions (UpdateOverheadPerPacket, :529-545).
  const DataSize overhead =
      DataSize::Bytes(cfg_.send_side.transport_overhead_bytes + static_cast<int64_t>(a.sender->MediaHeaderSize()));
  const DataRate at_longest = DataRate::FromSizeAndTime(overhead, a.max_frame_length);
  const DataRate at_shortest = DataRate::FromSizeAndTime(overhead, a.min_frame_length);
  return {a.min_bitrate + at_longest, a.max_bitrate + at_shortest, a.priority_bitrate + at_longest};
}

void Session::Start(Timestamp now) {
  if (started_) return;
  started_ = true;
  // Call::ConfigureSync: the audio and video stream of one sync group.
  if (av_sync_) av_sync_->ConfigureSync(sync_audio_.get(), now);
  // A receive-only endpoint has no congestion controller to run: its pacer,
  // GCC and outage monitor would only log decisions about a link it never
  // sends on.
  if (!HasSendStreams()) return;
  if (cfg_.audio_rtcp && !audio_send_.empty() && !video_send_.empty()) {
    RtcpConfig r = MakeRtcpConfig(cfg_);
    r.local_ssrc = audio_send_.front().sender->config().ssrc;
    r.has_video = false;
    r.report_interval = TimeDelta::Zero();
    r.first_report_delay = TimeDelta::Zero();
    // The audio channel's RTCPSender is built with the video one and seeded from the same clock time
    // (rtcp_sender.cc:130), so it draws the same intervals (#517).
    audio_rtcp_ = std::make_unique<RtcpEngine>(r);
  }
  SendSideController::AudioAllocation audio;
  for (const auto& a : audio_send_) {
    const SendSideController::AudioAllocation c = AudioAllocationOf(a);
    audio.min += c.min;
    audio.max += c.max;
    audio.priority_rate += c.priority_rate;
  }
  send_.ConfigureAllocation(audio, !video_send_.empty());
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
  // VideoStreamEncoder::AugmentEncodedImage -> FrameEncodeMetadataWriter::UpdateBitstream: the
  // SPS of a keyframe gets a VUI that forbids frame reordering, and AUDs are dropped (#342).
  std::vector<uint8_t> rewritten;
  std::span<const uint8_t> data = f.data;
  if (f.keyframe) {
    rewritten = ParseOutgoingBitstreamAndRewrite(f.data, &stats_.sps);
    // A frame without start codes (one bare NAL unit, which the packetizer takes) is sent as is.
    if (!rewritten.empty()) data = rewritten;
  }
  // rtp_sender_video.cc:676-687: packet_capacity, less FecPacketOverhead with FEC, less
  // RtxPacketOverhead when the frame may be retransmitted (allow_retransmission: an H.264 frame with
  // a finite expected retransmission time, which RtpVideoSender gives every frame; the reservation
  // is 0 without RTX, and with NACK off libwebrtc's stream has no RTX; issue #513), less the header
  // of a middle packet. The first packet carries no extension the middle one lacks here
  // (transport-cc, abs-send-time and MID go on every packet), so its reduction is zero; the last
  // and single packets also carry CVO when it is mapped and due (rtp_sender_video.cc:741-758).
  const size_t reserved =
      s.FecPacketOverhead() + (video_send_[static_cast<size_t>(id.index)].nack ? s.RtxPacketOverhead() : 0);
  const size_t whole = s.config().max_payload_size + RtpPacket::kFixedHeaderSize;
  const size_t capacity = whole > reserved ? whole - reserved : 0;
  const size_t header = s.MediaHeaderSize();
  PayloadSizeLimits limits;
  limits.max_payload_len = static_cast<int>(capacity > header ? capacity - header : 0);
  limits.last_packet_reduction_len = static_cast<int>(s.LastPacketReduction(f.keyframe, f.rotation));
  limits.single_packet_reduction_len = limits.last_packet_reduction_len;
  const std::vector<std::vector<uint8_t>> chunks = H264Packetizer::Packetize(data, limits);
  SendVideoChunks(id, chunks, f.rtp_timestamp, now, f.keyframe, data.size(), f.rotation);
}

void Session::SendVideoChunks(VideoSendId id, std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp,
                              Timestamp now, bool keyframe) {
  size_t encoded_size = 0;
  for (const auto& c : chunks) encoded_size += c.size();
  SendVideoChunks(id, chunks, rtp_timestamp, now, keyframe, encoded_size);
}

void Session::SendVideoChunks(VideoSendId id, std::span<const std::vector<uint8_t>> chunks, uint32_t rtp_timestamp,
                              Timestamp now, bool keyframe, size_t encoded_size, video::Rotation rotation) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_send_.size()) return;
  if (chunks.empty()) {
    ++stats_.empty_video_frames;
    return;
  }
  Start(now);
  SendStream& vs = video_send_[static_cast<size_t>(id.index)];
  // VideoSendStreamImpl::OnEncodedImage: encoder activity (issue #375).
  send_.OnVideoFrameEncoded(now);
  // RtpVideoSender::OnEncodedImage, and the sent frame rate SendStatisticsProxy counts.
  vs.fec->UpdateWithEncodedData(encoded_size, keyframe, now);
  if (!vs.first_frame.IsFinite()) vs.first_frame = now;
  vs.frame_times.push_back(now);
  RtpSender& s = *vs.sender;
  s.SendVideoFrame(chunks, rtp_timestamp, now, keyframe, rotation);
  // RtpRtcpImpl2::OnSendingRtpFrame: a report that is due within 100 ms is sent
  // before a keyframe instead of queueing behind it.
  if (keyframe && rtcp_.ReportDue(now, true)) {
    UpdateSenderState(now);
    for (RtcpOutgoing& o : rtcp_.SendReport(now)) {
      const DataSize wire = DataSize::Bytes(static_cast<int64_t>(o.bytes.size()) + cfg_.send_side.transport_overhead_bytes);
      out_.push_back({std::move(o.bytes), OutgoingKind::kRtcp, wire});
    }
  }
  EnqueueSenderPackets(s, now);
}

void Session::SetFecParams(VideoSendId id, const loss::ulpfec::FecProtectionParams& p) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_send_.size()) return;
  video_send_[static_cast<size_t>(id.index)].sender->SetFecParams(p);
}

void Session::SetVideoEncodingData(VideoSendId id, int width, int height, int num_temporal_layers) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_send_.size()) return;
  video_send_[static_cast<size_t>(id.index)].fec->SetEncodingData(
      static_cast<size_t>(std::max(width, 0)), static_cast<size_t>(std::max(height, 0)),
      static_cast<size_t>(std::max(num_temporal_layers, 0)), static_cast<size_t>(cfg_.send_side.max_packet_size));
}

int Session::SentFrameRate(SendStream& v, Timestamp now) {
  // A rate over the last second, or over the time since the first frame within it.
  while (!v.frame_times.empty() && v.frame_times.front() <= now - TimeDelta::Seconds(1)) v.frame_times.pop_front();
  if (!v.first_frame.IsFinite()) return 0;
  const TimeDelta window = std::min(TimeDelta::Seconds(1), now - v.first_frame);
  if (window <= TimeDelta::Zero()) return 0;
  return static_cast<int>(std::lround(static_cast<double>(v.frame_times.size()) * 1e6 / static_cast<double>(window.us())));
}

void Session::MaybeUpdateFecRates(Timestamp now) {
  if (video_send_.empty()) return;
  const cc::SendSideBandwidthEstimation& bwe = send_.gcc().send_side_bwe();
  FecEstimate e;
  e.target = send_.target();
  // GoogCC sends the loss as fraction_loss / 255.0f, and BitrateAllocator takes it back as
  // int(ratio * 255): RtpVideoSender's fraction lost.
  const float loss_rate_ratio = static_cast<float>(bwe.fraction_loss()) / 255.0f;
  e.fraction_loss = static_cast<uint8_t>(std::clamp(static_cast<int>(loss_rate_ratio * 255), 0, 255));
  e.rtt = bwe.round_trip_time();
  if (last_fec_estimate_ &&
      (last_fec_estimate_->target == e.target &&
       (e.target.IsZero() || (last_fec_estimate_->fraction_loss == e.fraction_loss && last_fec_estimate_->rtt == e.rtt)))) {
    return;
  }
  last_fec_estimate_ = e;
  const DataRate payload = send_.VideoPayloadRate();
  for (SendStream& v : video_send_) {
    const int fps = SentFrameRate(v, now);
    if (auto r = v.fec->UpdateFecRates(payload, fps, e.fraction_loss, e.rtt.IsFinite() ? e.rtt.ms() : 0, now)) {
      // ProtectionRequest: RtpRtcp::SetFecProtectionParams.
      v.sender->SetFecParams(r->delta, r->key);
    }
  }
}

void Session::SetRtt(TimeDelta rtt) {
  rtt_ = rtt;
  // With RRTR on, the XR measurement is what goes to CallStats.
  if (!cfg_.rtcp.xr_rrtr) pending_rr_rtt_ = rtt;
  send_.OnRtt(rtt_);
  for (auto& a : audio_recv_) a.receiver->OnRtt(rtt_);
}

// VideoReceiveStream2::OnRttUpdate: the maximum RTT goes to NackRequester
// (RtpVideoStreamReceiver2::UpdateRtt) and the jitter estimator.
void Session::OnCallStatsUpdate(const std::optional<CallStats::Update>& u) {
  if (!u) return;
  for (auto& v : video_recv_) v.receiver->OnRtt(TimeDelta::Millis(u->max_rtt_ms));
}

void Session::OnDecodeFailure(VideoRecvId id, Timestamp now) {
  if (id.index < 0 || static_cast<size_t>(id.index) >= video_recv_.size()) return;
  video_recv_[static_cast<size_t>(id.index)].receiver->OnDecodeFailure(now);
}

Session::SendStream* Session::StreamForSsrc(uint32_t ssrc) {
  for (auto& v : video_send_) {
    if (v.sender->config().ssrc == ssrc) return &v;
  }
  for (auto& a : audio_send_) {
    if (a.sender->config().ssrc == ssrc) return &a;
  }
  return nullptr;
}

RtpSender* Session::SenderForSsrc(uint32_t ssrc) {
  SendStream* st = StreamForSsrc(ssrc);
  return st ? st->sender.get() : nullptr;
}

void Session::OnPacket(std::span<const uint8_t> bytes, Timestamp now) {
  Start(now);
  packet_since_process_ = true;
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
  // RTCPReceiver::HandleXrDlrrReportBlock keeps the latest XR RTT for
  // GetAndResetXrRrRtt at the next periodic update.
  if (!in.xr_rtts.empty()) pending_rr_rtt_ = in.xr_rtts.back();
  if (!cfg_.per_stream_rtt) {
    if (!in.report_blocks.empty()) last_report_block_ = now;
    if (!in.rtts.empty()) {
      rtt_sum_ = rtt_sum_ + in.rtts.back();
      ++rtt_count_;
      send_.OnRtt(in.rtts.back());
    }
  } else {
    // Each send stream's RTCPReceiver::HandleReportBlock (rtcp_receiver.cc:516-575) for the blocks
    // of its media and RTX SSRCs: last_received_rb_ for any of them, RttStats::AddRtt only for the
    // media SSRC, and the packet's `rtt` from the last of them, which TriggerCallbacksFromRtcpPacket
    // hands to RtpTransportControllerSend::OnRttUpdate (rtcp_receiver.cc:1010-1012) once per
    // stream, video streams first (Call::DeliverRtcpPacket_w, call.cc:1357-1381).
    auto handle = [&](SendStream& st) {
      const RtpSenderConfig& c = st.sender->config();
      auto mine = [&](uint32_t ssrc) { return ssrc == c.ssrc || (c.rtx_ssrc && ssrc == *c.rtx_ssrc); };
      for (const auto& rb : in.report_blocks) {
        if (mine(rb.ssrc)) st.last_received_rb = now;
      }
      std::optional<TimeDelta> packet_rtt;
      for (size_t i = 0; i < in.rtts.size(); ++i) {
        if (!mine(in.rtt_ssrcs[i])) continue;
        if (in.rtt_ssrcs[i] == c.ssrc) {
          st.last_rtt = in.rtts[i];
          st.sum_rtt = st.sum_rtt + in.rtts[i];
          ++st.num_rtts;
        }
        packet_rtt = in.rtts[i];
      }
      if (packet_rtt) send_.OnRtt(*packet_rtt);
    };
    for (auto& v : video_send_) handle(v);
    for (auto& a : audio_send_) handle(a);
  }
  if (!in.rtts.empty()) {
    rtt_ = in.rtts.back();
    have_rtt_ = true;
    for (auto& a : audio_recv_) a.receiver->OnRtt(rtt_);
    // The packet history and CallStats (so the video receivers) take their RTT
    // from the 1 s periodic update, the history also from each NACK
    // (PeriodicRttUpdate, the NACK loop below), not from every sample.
    SessionEvent e;
    e.type = SessionEventType::kRttUpdated;
    e.time = now;
    e.rtt = rtt_;
    Emit(e);
  }
  {
    // Call::DeliverRtcpPacket_w (call.cc:1357-1381) hands the packet to every video send stream,
    // then every audio send stream. Each one's RTCPReceiver keeps only the report blocks for its
    // own SSRCs (RegisteredSsrcs: media and RTX, rtcp_receiver.cc:104-119, 532) and passes them
    // to RtpTransportControllerSend::OnReport (rtcp_receiver.cc:1006-1009), so GoogCC gets one
    // loss report per send stream, not one for the whole packet.
    auto report = [&](uint32_t ssrc, std::optional<uint32_t> rtx_ssrc) {
      // RtpTransportControllerSend::OnReport (rtp_transport_controller_send.cc:850-891).
      int total_packets_delta = 0, total_packets_lost_delta = 0;
      bool any = false;
      for (const auto& rb : in.report_blocks) {
        if (rb.ssrc != ssrc && rb.ssrc != rtx_ssrc) continue;
        any = true;
        auto [it, inserted] = last_report_blocks_.try_emplace(rb.ssrc);
        if (!inserted) {
          total_packets_delta += static_cast<int>(rb.extended_highest_seq - it->second.extended_highest_seq);
          total_packets_lost_delta += rb.cumulative_lost - it->second.cumulative_lost;
        }
        it->second.extended_highest_seq = rb.extended_highest_seq;
        it->second.cumulative_lost = rb.cumulative_lost;
      }
      // No previous blocks to compare to, so nothing to report.
      if (!any || !total_packets_delta) return;
      // To detect lost packets, at least one packet has to be received.
      const int packets_received_delta = total_packets_delta - total_packets_lost_delta;
      if (packets_received_delta < 1) return;
      send_.OnLossReport(total_packets_lost_delta, total_packets_lost_delta + packets_received_delta, now);
    };
    for (auto& v : video_send_) report(v.sender->config().ssrc, v.sender->config().rtx_ssrc);
    for (auto& a : audio_send_) report(a.sender->config().ssrc, std::nullopt);
  }
  // ModuleRtpRtcpImpl2::OnReceivedRtcpReportBlocks (rtp_rtcp_impl2.cc:694-713): a report block
  // for a sender's media or RTX SSRC acknowledges that SSRC (RtxPacketOverhead reads it).
  for (const auto& rb : in.report_blocks) {
    for (auto& v : video_send_) {
      const RtpSenderConfig& c = v.sender->config();
      if (rb.ssrc == c.ssrc) {
        v.sender->OnReceivedAckOnSsrc();
      } else if (c.rtx_ssrc && rb.ssrc == *c.rtx_ssrc) {
        v.sender->OnReceivedAckOnRtxSsrc();
      }
    }
    for (auto& a : audio_send_) {
      if (rb.ssrc == a.sender->config().ssrc) a.sender->OnReceivedAckOnSsrc();
    }
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
    if (SendStream* st = StreamForSsrc(n.media_ssrc)) {
      RtpSender* s = st->sender.get();
      if (!n.seqs.empty()) {
        // ModuleRtpRtcpImpl2::OnReceivedNack (rtp_rtcp_impl2.cc:678-692): the stream's RTT of the
        // last periodic update, else the average of its samples; RTPSender::OnReceivedNack adds 5 ms.
        int64_t rtt_ms = st->rtt_ms;
        if (rtt_ms == 0 && st->num_rtts > 0) rtt_ms = (st->sum_rtt / st->num_rtts).ms();
        if (!cfg_.per_stream_rtt) {
          rtt_ms = periodic_rtt_.ms();
          if (rtt_ms == 0 && rtt_count_ > 0) rtt_ms = (rtt_sum_ / rtt_count_).ms();
        }
        s->SetRtt(TimeDelta::Millis(5 + rtt_ms), now);
      }
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

void Session::UpdateSenderState(Timestamp now) {
  RtpSender* s = !video_send_.empty() ? video_send_.front().sender.get()
                                      : (!audio_send_.empty() ? audio_send_.front().sender.get() : nullptr);
  if (s) rtcp_.SetSenderState(s->SenderState(now));
}

// ModuleRtpRtcpImpl2::PeriodicUpdate (every kRttUpdateInterval, 1 s; rtp_rtcp_impl2.cc:740-751),
// with RTCPReceiver::OnPeriodicRttUpdate (rtcp_receiver.cc:259-286), for each send stream's
// module. While sending, the stream's last media-SSRC RTT sample goes to CallStats and to its
// packet history if it has had no report block yet or one within the interval. A receive stream
// reports its XR RTT to CallStats, here the measurement SetRtt gave since the last update
// (GetAndResetXrRrRtt).
void Session::PeriodicRttUpdate(Timestamp now) {
  if (!HasSendStreams() && video_recv_.empty()) return;
  if (!next_rtt_update_.IsFinite()) next_rtt_update_ = now + kRttUpdateInterval;
  while (now >= next_rtt_update_) {
    const Timestamp at = next_rtt_update_;
    const Timestamp check_since = at - kRttUpdateInterval;
    // The harness creates the audio send stream before the video one (compare/webrtc/webrtc_call.cc).
    auto update = [&](SendStream& st) {
      if (st.num_rtts == 0) return;  // RttStats::last_rtt() is nullopt.
      if (st.last_received_rb.IsFinite() && st.last_received_rb <= check_since) return;
      st.rtt_ms = st.last_rtt.ms();
      OnCallStatsUpdate(call_stats_.OnRttUpdate(st.rtt_ms, at));
      st.sender->SetRtt(TimeDelta::Millis(st.rtt_ms), now);  // set_rtt_ms: the packet history.
    };
    if (cfg_.per_stream_rtt) {
      for (auto& a : audio_send_) update(a);
      for (auto& v : video_send_) update(v);
    } else if (HasSendStreams() && have_rtt_ && rtt_count_ > 0 && last_report_block_ > check_since) {
      periodic_rtt_ = TimeDelta::Millis(rtt_.ms());
      OnCallStatsUpdate(call_stats_.OnRttUpdate(periodic_rtt_.ms(), at));
      for (auto& v : video_send_) v.sender->SetRtt(periodic_rtt_, now);
      for (auto& a : audio_send_) a.sender->SetRtt(periodic_rtt_, now);
    }
    if (!video_recv_.empty() && pending_rr_rtt_) {
      OnCallStatsUpdate(call_stats_.OnRttUpdate(pending_rr_rtt_->ms(), at));
      pending_rr_rtt_.reset();
    }
    next_rtt_update_ += kRttUpdateInterval;
  }
}

void Session::Process(Timestamp now) {
  // An infinite `now` is not a clock reading (libwebrtc's Clock never returns one). With the units
  // saturating (#441), RtcpEngine's catch-up loop would chase it forever, so it is ignored.
  if (!now.IsFinite()) return;
  Start(now);
  packet_since_process_ = false;
  call_stats_.EnsureStarted(now);
  PeriodicRttUpdate(now);
  OnCallStatsUpdate(call_stats_.Process(now));
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
  }
  // RtpStreamsSynchronizer's 1 s task.
  if (av_sync_) av_sync_->Process(now);
  // Transport-wide feedback covers every stream of the transport.
  ProcessTransportFeedback(now);
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
  if (HasSendStreams()) MaybeUpdateFecRates(now);
  // RTCP: reports and queued feedback.
  UpdateSenderState(now);
  for (RtcpOutgoing& o : rtcp_.Process(now)) {
    const DataSize wire = DataSize::Bytes(static_cast<int64_t>(o.bytes.size()) + cfg_.send_side.transport_overhead_bytes);
    out_.push_back({std::move(o.bytes), OutgoingKind::kRtcp, wire});
  }
  if (audio_rtcp_) {
    audio_rtcp_->SetSenderState(audio_send_.front().sender->SenderState(now));
    for (RtcpOutgoing& o : audio_rtcp_->Process(now)) {
      const DataSize wire = DataSize::Bytes(static_cast<int64_t>(o.bytes.size()) + cfg_.send_side.transport_overhead_bytes);
      out_.push_back({std::move(o.bytes), OutgoingKind::kRtcp, wire});
    }
  }
  if (audio_recv_rtcp_) {
    for (RtcpOutgoing& o : audio_recv_rtcp_->Process(now)) {
      const DataSize wire = DataSize::Bytes(static_cast<int64_t>(o.bytes.size()) + cfg_.send_side.transport_overhead_bytes);
      out_.push_back({std::move(o.bytes), OutgoingKind::kRtcp, wire});
    }
  }
}

// libwebrtc: the generator's interval follows this endpoint's own
// send estimate; a receive-only endpoint runs on the congestion controller's
// start rate, as libwebrtc's Call does before any feedback.
void Session::UpdateTransportFeedbackRate() {
  const DataRate estimate = HasSendStreams() && started_ ? send_.target() : cfg_.send_side.gcc.start_rate;
  if (estimate == last_tcc_estimate_) return;
  last_tcc_estimate_ = estimate;
  tcc_.OnSendBandwidthEstimateChanged(estimate);
}

// Call::EnsureStarted starts the RepeatingTask that runs ReceiveSideCongestionController::MaybeProcess
// before it registers for OnTargetTransferRate (call/call.cc), so the generator's first Process sees its default
// 100 ms interval and the first feedback goes out 100 ms after the start; the rate-based interval applies from
// then on. TransportSequenceNumberFeedbackGenenerator::Process
// (modules/remote_bitrate_estimator/transport_sequence_number_feedback_generator.cc) sends when
// last_process_time_ + send_interval_ has passed and returns the delay to the task's next run, so a longer
// interval takes effect at once and a shorter one at the next wake-up.
void Session::ProcessTransportFeedback(Timestamp now) {
  if (cfg_.transport_feedback_interval.us() > 0 && now >= tcc_wake_) {
    const TimeDelta interval = tcc_.Interval();
    if (!last_tcc_.IsFinite() || now >= last_tcc_ + interval) {
      last_tcc_ = now;
      tcc_wake_ = now + interval;
      for (auto& f : tcc_.Build()) rtcp_.QueueTransportFeedback(std::move(f));
    } else {
      tcc_wake_ = last_tcc_ + interval;
    }
  }
  UpdateTransportFeedbackRate();  // Call::OnTargetTransferRate -> ReceiveSideCongestionController::OnBitrateChanged.
}

Timestamp Session::NextProcessTime(Timestamp now) const {
  Timestamp next = rtcp_.NextProcessTime(now);
  if (audio_rtcp_) next = std::min(next, audio_rtcp_->NextProcessTime(now));
  if (audio_recv_rtcp_) next = std::min(next, audio_recv_rtcp_->NextProcessTime(now));
  if (HasSendStreams()) next = std::min(next, send_.NextProcessTime(now));
  if (next_rtt_update_.IsFinite()) next = std::min(next, next_rtt_update_);
  next = std::min(next, call_stats_.NextProcessTime());
  if (packet_since_process_) return now;
  for (const auto& v : video_recv_) next = std::min(next, v.receiver->NextProcessTime(now));
  if (av_sync_ && av_sync_->NextProcessTime()) next = std::min(next, *av_sync_->NextProcessTime());
  if (cfg_.transport_feedback_interval.us() > 0 && tcc_wake_.IsFinite()) next = std::min(next, tcc_wake_);
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
  w.Counter("sps_vui_rewritten", stats_.sps.rewritten);
  w.Counter("sps_vui_ok", stats_.sps.vui_ok);
  w.Counter("sps_parse_failures", stats_.sps.parse_failures);
  w.Gauge("rtt_ms", static_cast<double>(rtt_.us()) / 1000.0);
  // Call::GetStats().rtt_ms: CallStats::LastProcessedRtt(), -1 before the first update.
  w.Gauge("call_stats_rtt_ms", static_cast<double>(call_stats_.LastProcessedRtt()));
  w.Gauge("target_kbps", send_.target().kbps());
}

}  // namespace sm::rtp
