# Feature and configuration inventory against libwebrtc M157 (step 3)

Step 3 of the rest of stage 1 (`agents/program.md` 0.2, issue #480): every
feature, default, field trial and switch that libwebrtc's call runs in
`sm_compare`, beside ours. It covers the ten areas #480 names. It starts from
#560's register (`tools/requirements.json`) and does not repeat the
differences its five items own (#565-#569). The algorithm-level differences
stay in the deviation inventory, [libwebrtc-parity.md](libwebrtc-parity.md).

Paths `ref/` are `compare/webrtc/reference/`; `harness` is
`compare/webrtc/webrtc_call.cc`; `bench` is `testbench/call.cc` and
`testbench/scenario.h`. Every `ref/` line was opened at M157. A value whose
defining header is not in `ref/` is marked *unverified* (list at the end).

**Verdicts.** `same`; `different`; `missing` (libwebrtc has it, we don't);
`off` (ported, off by default); `bench-only` (the library's own default
differs, but `rtp::Session` or the bench sets libwebrtc's value, so the suite
runs libwebrtc's). A **flip** is a change to libwebrtc's default, which the
coordinator decides (#480).

**What the harness configures.** It sets one field trial,
`WebRTC-Video-QualityScaling/Enabled-29,95,149,205,24,37,24,37,0.9995,0.9999,1/`,
only with `quality_scaling` (harness:803-806); it differs from the M157
default (`ref/rtc_base/experiments/quality_scaling_experiment.cc:43-44`) only
in the Generic slot, and the H.264 slot (24/37) is the one read. Every other
trial keeps its M157 default. `experiments/field_trials.py` lists keys only;
a trial counts as on by default where the code reads it with `!IsDisabled`.
Bitrate constraints come from the scenario (30/300/5000 kbps,
harness:863-865; bench `call.cc:573-575`), transport-cc is the only header
extension (id 1; harness:874-876, bench `call.cc:571`), feedback is
transport-cc, not RFC 8888 (harness:403), and the route overhead is 28 B on
both.

## 1. Differences

Every row that is not `same`. Each has an open item, or is an approved
deviation with its decision. Audit #891 (on `main` 2d9445a) re-checked each
row against the code: rows 3 and 16 are now `same`, row 17 is an approved
deviation, and the rest were confirmed. Sections 2 and 3 were checked the
same way; what is still not `same` is the list in section 3 (#900).

| # | Area | Feature or default | libwebrtc M157 | Ours | Verdict | Item |
|---|---|---|---|---|---|---|
| 1 | Render smoothing | Pre-render queue | `enable_prerenderer_smoothing` on: `bool enable_prerenderer_smoothing = true;` in M157's `call/video_receive_stream.h:291` and `media/base/media_config.h:62` (the prebuilt's headers; not in `ref/`), which the harness leaves at its default; `ref/video/video_receive_stream2.cc:400-403` | `render_smoothing` on (`include/sm/rtp/rtp_receiver.h`, `session.h`, bench `scenario.h`), flipped by #437 | same | #437 |
| 2 | Pacer | Keyframe flush also drops padding queued on RTX | `ref/modules/pacing/pacing_controller.cc:227-231` | `keyframe_flush_padding` on (`include/sm/cc/pacing_controller.h`, bench `scenario.h`), flipped by #437 | same | #437 |
| 3 | Audio processing | AEC3, NS, AGC1, HPF, VAD, resampler | `ref/modules/audio_processing/` | the ported modules are the only ones: `apm v1` and its switch were retired by #565 (the chain is the default since #585) | same (audit #891) | #565 |
| 4 | NetEq | Muted state | `enable_muted_state = true` (`ref/audio/channel_receive.cc:107`); `ref/modules/audio_coding/neteq/neteq_impl.cc:682-698` | `AudioJitterBufferConfig::enable_muted_state`, on in `AudioStreamConfig` (`include/sm/rtp/session.h`) as `ChannelReceive`, off on its own as `NetEq::Config`; the muted branch in `AudioJitterBuffer::GetAudioV2` (`src/timing/audio_jitter_buffer.cc`) | same | #574 |
| 5 | RTCP statistics | Max reordering threshold, stream restart | 450 with NACK (`ref/video/rtp_video_stream_receiver2.cc:106`, `:365-367`); `ref/modules/rtp_rtcp/source/receive_statistics_impl.cc:95-110` | ported (`SessionConfig::receive_statistics_v2`, on by default as libwebrtc), with libwebrtc's 50 default and 255-scaled fraction lost (#575) | same | #575 |
| 6 | RTCP | Max datagram (compound split) | 1460 (`ref/modules/rtp_rtcp/source/rtp_rtcp_impl2.cc:146-150`, `:463`) | 1460 (`RtcpConfig::max_datagram_bytes`, `include/sm/rtp/rtcp_engine.h`); a video-sending `rtp::Session` sets min(`max_packet_size`, 1500 - transport overhead) as `ref/call/rtp_video_sender.cc:272`, `:860-865` (#576) | same | #576 |
| 7 | Allocation | Per-packet RTP overhead in the video overhead rate | `ExpectedPerPacketOverhead`: 22 B with NACK, 20 B without, in the harness (`ref/call/rtp_video_sender.cc:870-889`, `ref/modules/rtp_rtcp/source/rtp_sender.cc:736-786`) | `RtpSender::ExpectedPerPacketOverhead`, averaged over the video streams, 22 B with NACK and 20 B without in the bench (`SendSideConfig::expected_packet_overhead`, on by default since #577's flip; off: the old fixed 24 B) | same | #577 |
| 8 | Allocation | Audio priority rate (`WebRTC-Audio-PriorityBitrate`, on) | 0 + overhead / longest frame (`ref/audio/audio_send_stream.cc:166-167`, `:811-831`) | the same with `AudioStreamConfig::max_bitrate` set, as the bench sets it (`src/rtp/session.cc` `AudioAllocationOf`, #569); the whole fixed rate without it | same in the bench (inert while audio min = max) | #578 (ported by #569's first PR) |
| 9 | Audio min/max allocation | `GetMinMaxBitrateConstraints` | `ref/audio/audio_send_stream.cc:855-901` | `AudioStreamConfig::min_bitrate`/`max_bitrate` + overhead / frame length range (`src/rtp/session.cc` `AudioAllocationOf`, #569); overhead from the header the next packet carries; `ExpectedPerPacketOverhead` (audio level, max-size MID) with `expected_packet_overhead` (on by default, #577) | same in the bench (51.2 kbps both) | #569 |
| 10 | RTCP | Audio report block | the audio channel's own RTCP, 5 s (`ref/audio/channel_receive.cc:122-131`) | the audio receive streams' own RTCP, 5 s (`SessionConfig::audio_receive_rtcp`, on by default, #484) | same | #484 |
| 11 | RTP | Padding on the media SSRC without RTX, audio-only padding | `ref/modules/rtp_rtcp/source/rtp_sender.cc` | ported: media-SSRC padding without RTX, audio padding counts and sizes, pre-media padding timestamp (#707); random start sequence number and timestamp, first audio marker (#704) | same | #514 (closed) |
| 12 | RTP | Header extensions other than transport-cc; the fade on mute | RFC 8285 map and demuxer, abs-capture-time, CVO, audio level; `AudioFrameOperations::Mute` on every capture frame (`ref/audio/channel_send.cc:909-911`, `ref/audio/utility/audio_frame_operations.cc:146-198`) | ported (#567, #677); the mute fade ported by #879: `Session::ProcessAudioCapture` / `RtpSender::ProcessAudioCapture` mute the capture frame in place with `dsp::MuteAudioFrame` (`include/sm/core/audio_frame_operations.h`), 128-sample ramp, before the level | same | #879 |
| 14 | Library defaults | `SessionConfig::audio_rtcp`; `send_lntf`; `min_keyframe_request_interval`; `RtpReceiverConfig::sps_pps_tracker`; `PacketBufferConfig::missing_packets`; `GccConfig::max_rate`; max packet; `allocation_max_bitrate` | always on (`ref/audio/channel_send.cc:551`); LNTF off for H.264 (`ref/video/rtp_video_stream_receiver2.cc:373`); 300 ms (`ref/video/encoder_rtcp_feedback.cc:37`); tracker on; hold on; unbounded (`ref/call/rtp_transport_controller_send.cc:68-83`); 1460; the app's max | the same as libwebrtc since #580: `audio_rtcp` on (still built only beside video, `src/rtp/session.cc` `Start`), `send_lntf` off, 300 ms, tracker on, hold on (Session: H.264 only), `max_rate` unbounded, 1460 (`SendSideConfig::max_packet_size`, `VideoStreamConfig` / `RtpSenderConfig::max_payload_size` 1448), `allocation_max_bitrate` unbounded (the session's video allocation bounds it); `tests/unit/rtp/libwebrtc_defaults_test.cc` | same | #580 |
| 15 | Adaptation | Camera degradation preference | MAINTAIN_FRAMERATE (`ref/media/engine/webrtc_video_engine.cc:2032-2064`) | MAINTAIN_FRAMERATE: `DegradationConfig` and `ContentSettings` default to it, `SettingsFor` camera with no hint maps to it (#581); the send path hard-codes it | same (`SettingsFor` sets only the preference since #877; its QP ceilings and fps floors are removed) | #581, #877 |
| 16 | Adaptation | `BalancedDegradationSettings` | read only under BALANCED (`ref/rtc_base/experiments/balanced_degradation_settings.cc:29`) | ported: `BalancedDegradationSettings` (`include/sm/video/video_stream_adapter.h:117`), read under kBalanced by `VideoStreamAdapter` (`src/video/video_stream_adapter.cc:317`, `:347`) (#646) | same (audit #891) | #646 |
| 17 | Library defaults | `VideoStreamConfig::decode_time` 5 ms (`session.h:149`) | measured by the decoder | the bench sets 0, as the harness's model decoder | approved deviation: a model input, kept by the Coordinator's decision on #580 ([comment](https://github.com/aray4702/streaming_media/issues/580#issuecomment-6489011729)) (audit #891) | #580 |
| 18 | Video receive | Frame buffer cap | 800 frames; a keyframe clears a full buffer, a delta frame is refused; an older-id keyframe with a newer timestamp clears it (`ref/video/video_stream_buffer_controller.cc:46`, `ref/api/video/frame_buffer.cc:84-106`) | timing v2: the same rules on the receiver's frame buffer (`RtpReceiver::FrameBufferAccepts`, `kMaxFramesBuffered` 800 in `src/rtp/rtp_receiver.cc`); a decoded or dropped frame takes the frames before it out of the buffer, as `ExtractNextDecodableTemporalUnit` / `DropNextDecodableTemporalUnit` (timing v1 keeps its 1024-entry metadata bound) | same | #583 |

## 2. Same as libwebrtc

Compared and equal. Constants inside ported algorithms are in
[libwebrtc-parity.md](libwebrtc-parity.md); these are the defaults and
switches a call runs with.

### Field trials and `RateControlSettings`

| Feature or default | libwebrtc M157 | Ours |
|---|---|---|
| `WebRTC-Pacer-DrainQueue` (on) | `ref/modules/pacing/pacing_controller.cc:62-63` | `drain_large_queues = true` (`include/sm/cc/pacing_controller.h:49`) |
| `WebRTC-Bwe-MinAllocAsLowerBound` (on) | `ref/modules/congestion_controller/goog_cc/goog_cc_network_control.cc:97-98` | always on (`include/sm/cc/send_side_bwe.h:106-113`) |
| `WebRTC-Bwe-LimitProbesLowerThanThroughputEstimate` (on, 0.85) | `goog_cc_network_control.cc:64`, `:102-104` | `src/cc/send_side_bwe.cc:41-43`, `:234` |
| `WebRTC-Audio-AlrProbing` (on), then the video stream turns periodic ALR probing off (`UseAlrProbing()` false) | `ref/audio/audio_send_stream.cc:158-159`, `:297-308`; `ref/video/video_send_stream_impl.cc:503-522` | `src/rtp/session.cc:109-112`, `:141-149`: no periodic ALR probes in an A+V call on either stack |
| `WebRTC-BitrateAdjusterUseNewfangledHeadroomAdjustment` (on) | `ref/video/encoder_bitrate_adjuster.cc:66-67` | `newfangled_headroom_adjustment = true`; bench `rate_utilization_tracker` on |
| `WebRTC-Video-QualityScaling` (on; H.264 24/37) | `quality_scaling_experiment.cc:43-44`, `:84-92` | `QpThresholds{24, 37}` (`include/sm/video/quality_scaler.h:45-46`) |
| Off by default and off here: `Pacer-FastRetransmissions`, `PadInSilence`, `BlockAudio`, `IgnoreTransportOverhead`, `Bwe-LimitPacingFactorByUpperLinkCapacityEstimate`, `ElasticBitrateAllocation`, `Bwe-SeparateAudioPackets`, RFC 8888 feedback, `Bwe-SafeResetOnRouteChange`, `Audio-ABWENoTWCC`, `Audio-LegacyOverhead`, `Audio-OpusBitrateMultipliers`, `AdjustOpusBandwidth`, `OpusDecodeStereoByDefault`, `OpusGeneratePlc`, `NetEqSmartFlushing`, `Video-BalancedDegradation`, `DisableUlpFecExperiment`, `GenericPictureId`, `SpsPpsIdrIsH264Keyframe`, `Video-H26xPacketBuffer` | `pacing_controller.cc:64-70`; `goog_cc_network_control.cc:106-108`, `:140-141`; `ref/call/bitrate_allocator.cc:744-751`; `ref/audio/audio_send_stream.cc:156-157`, `:164-165`; `ref/modules/audio_coding/codecs/opus/audio_encoder_opus.cc:182-188`, `:386-387`; `ref/modules/audio_coding/neteq/packet_buffer.cc:51-71`; `ref/call/rtp_video_sender.cc:99-110`; `ref/video/rtp_video_stream_receiver2.cc:411-412`, `:1380` | no switch, or off |
| Congestion window pushback on, `QueueSize:350,MinBitrate:30000,DropFrame:true`; pacing not added | `ref/rtc_base/experiments/rate_control_settings.cc:32-33`, `:71-77`, `:96-99`; `congestion_window_pushback_controller.cc:24-25` | `include/sm/cc/congestion_window_pushback.h:34-41` |
| `VideoRateControlConfig`: bitrate adjuster on, headroom on, ALR probing off, pacing factor unset | `ref/rtc_base/experiments/rate_control_settings.h:37-44` | `video_send_controller.h:89`, `encoder_bitrate_adjuster.h:117`; bench `bitrate_adjuster`, `link_headroom` on |

### GoogCC, probing, pacer, allocation

| Feature or default | libwebrtc M157 | Ours |
|---|---|---|
| Pacing factor 2.5 before feedback, 1.1 after; 1.1 from the start for video with transport-cc | `goog_cc_network_control.cc:55-58`; `ref/video/video_send_stream_impl.cc:97`, `:220-236` | `src/cc/send_side_bwe.cc:145-148`; `src/rtp/session.cc:13`, `:145-147` |
| Pacer queue limit 2 s; burst interval 40 ms; 63 kB burst, 500 ms debt, 5 ms / 50 ms padding | `video_send_stream_impl.cc:396-398`; `pacing_controller.h:89`, `:92`; `pacing_controller.cc:40-55` | `include/sm/cc/pacing_controller.h:48-50`; `src/cc/pacing_controller.cc:13-19` |
| Priority levels (audio, retransmission, video/FEC, padding) | `ref/modules/pacing/prioritized_packet_queue.cc:35-61` | `src/cc/pacing_controller.cc:85-89` |
| `TaskQueuePacedSender` hold-back 5 ms / 3 packets | `ref/call/rtp_transport_controller_send.cc:99-104` | `include/sm/cc/pacer.h:35-37` |
| Pacer accounts for audio and transport overhead | `rtp_transport_controller_send.cc:534-539`, `:617` | `pacing_controller.h:55`; overhead 28 B |
| `BitrateProber` 10 ms max delay, 200 B min packet | `ref/modules/pacing/bitrate_prober.cc:35-41` | `include/sm/cc/bitrate_prober.h:33-34` |
| `ProbeControllerConfig` (3x/6x, step 2, 0.7, ALR scale 2 every 5 s, allocation probes, 5 packets / 15 ms / 2 ms); periodic ALR probing off at start | `goog_cc/probe_controller.cc:85-125`, `:171` | `include/sm/cc/probing.h:55-74`; `send_side_bwe.h:63` |
| `LossBasedBweV2` enabled, all `CreateConfig` defaults | `goog_cc/loss_based_bwe_v2.cc:408-479` | `include/sm/cc/loss_based_bwe_v2.h:45-83` |
| SSBE: 1 Gbps default max, 5 kbps delay-based floor, `RttBasedBackoff` (3 s, 0.8), v1 loss thresholds and timers | `goog_cc/send_side_bandwidth_estimation.cc:39-51`, `:113-131`, `:159` | `src/cc/send_side_bandwidth_estimation.cc:9-19`; `include/sm/cc/send_side_bandwidth_estimation.h:41-44` |
| `RobustThroughputEstimator` on (20/500 packets, 1 s/5 s window) | `goog_cc/acknowledged_bitrate_estimator_interface.h:36-53` | `include/sm/cc/aimd_rate_control.h:89-95`, used by GoogCC (`send_side_bwe.h:164-165`; the "Not yet used" comment at `aimd_rate_control.h:84` is stale) |
| `BitrateAllocator` multiplier 2, toggle factor 0.1, 20 kbps toggle floor | `ref/call/bitrate_allocator.cc:45-50` | `src/cc/bitrate_allocator_v2.cc:10-13` |

### RTP and RTCP

| Feature or default | libwebrtc M157 | Ours |
|---|---|---|
| Report interval: video 1 s, audio 5 s; compound mode | `ref/modules/rtp_rtcp/source/rtp_rtcp_impl2.cc:116-120`; `ref/audio/channel_send.cc:551`, `channel_receive.cc:129` | `src/rtp/rtcp_engine.cc:8-9`; `rtcp_engine.h:53` |
| XR RRTR on the video receiver (off by default; the harness turns it on) | harness:1129 | `xr_rrtr` off; bench on (`call.cc:610`) |
| Transport-cc feedback: 100 ms, then 50-250 ms at 5% of the estimate | `ref/modules/remote_bitrate_estimator/transport_sequence_number_feedback_generator.cc:37-40`, `:143-166` | `src/cc/transport_feedback.cc:177-182`, `:221-227` |
| RTX history 600 packets | `ref/call/rtp_video_sender.cc:93`, `:263` | `include/sm/loss/rtx.h:40` |
| Max RTP packet 1460 in the call | `rtp_rtcp_impl2.cc:146-150` | bench 1460 (`call.cc:588`); library default in row 14 |

### Opus encoder (`AudioEncoderOpusConfig`)

The library runs no codec; the bench encodes with libopus
(`testbench/opus_encoder.h`, `call.cc:557`) as the harness asks libwebrtc
(`opus/48000/2 {minptime=10, useinbandfec=1}`, harness:1055-1062).

| Feature or default | libwebrtc M157 | Ours (bench) |
|---|---|---|
| 20 ms frames, 48 kHz mono, VoIP | `ref/api/audio_codecs/opus/audio_encoder_opus_config.h:24`, `:39-43`; `audio_encoder_opus.cc:254`, `:263-265` | `opus_encoder.h:59-60`, `:86` |
| 32 kbps, clamped 6-510 kbps | `audio_encoder_opus_config.h:28-29`, `:47`; `audio_encoder_opus.cc:716-719` | scenario `audio_bitrate` 32 kbps |
| Complexity 9 | `audio_encoder_opus_config.h:33-35`, `:59` | `opus_encoder.h:66` |
| In-band FEC on (`useinbandfec=1`), DTX off, VBR, 48 kHz max playback | `audio_encoder_opus.cc:139-145`, `:257-259` | `opus_encoder.h:64-65`, `:90-95` |
| Loss input: 0.9999 smoother, capped at 0.2 | `audio_encoder_opus.cc:84-85`, `:487-498`, `:699-707` | `opus_encoder.h:191-197` |
| No ANA, so no bitrate smoothing | `ref/audio/audio_send_stream.cc:606-616`; `audio_encoder_opus.cc:374-375`, `:507-511` | none |
| RED for Opus, one redundant block | `ref/modules/audio_coding/codecs/red/audio_encoder_copy_red.cc:46-61` | `red_distance = 1` (`session.h:97`) |

### NetEq

| Feature or default | libwebrtc M157 | Ours (NetEq v2, the session default) |
|---|---|---|
| Delay manager: quantile 0.95, forget 0.983, start weight 2, resample 500 ms, reorder optimizer on (0.9993, 20 ms per loss percent), start delay 80 ms | `ref/modules/audio_coding/neteq/delay_manager.h:33-40`, `delay_manager.cc:27` | `include/sm/timing/neteq_delay.h:158-164`, `:188` |
| Decision logic: 2000 ms history, postpone level 50, window 100 ms, min timescale interval 5 | `decision_logic.cc:33-38`, `decision_logic.h:113` | `neteq_delay.h:189`, `:273-275` |
| 200 packets, full flush on overflow (smart flushing off) | `ref/audio/channel_receive.cc:105`; `packet_buffer.cc:51-71` | `neteq_delay.h:165`; `src/timing/audio_jitter_buffer.cc:102-111` |
| Fast accelerate off; base minimum delay 0 | `channel_receive.cc:106`, `:108` | plain accelerate; `base_min_delay_ms = 0` |

### Video jitter and timing

| Feature or default | libwebrtc M157 | Ours (timing v2, the session default) |
|---|---|---|
| `JitterEstimator` constants (2.33 sigma - 30 ms, 10 ms OS jitter, clamps and outliers) | `ref/video/timing/jitter_estimator.cc:52-81` | `src/timing/vcm_timing.cc:35-40` |
| Render delay 10 ms; playout delay 0 / 10 s; low latency at max <= 500 ms | `ref/modules/video_coding/timing/timing.h:37`, `:59`, `:62`; `timing.cc:37` | `include/sm/timing/vcm_timing.h:171-173`; `vcm_timing.cc:75` |
| Decode-time filter: 95th percentile, 10 s, 5 ignored | `decode_time_percentile_filter.cc:20-24` | `vcm_timing.cc:70-72` |
| Max wait 200 ms keyframe / 3 s frame; inactive 5 s; fast-forward 5 ms; zero-playout queue 8 | `ref/video/video_receive_stream2.h:73-74`, `video_receive_stream2.cc:1012`; `ref/video/frame_decode_timing.h:34`; `video_stream_buffer_controller.cc:54` | `include/sm/loss/keyframe_request.h:56-58`; `include/sm/timing/frame_release.h:11-12`, `:58-59`; `src/timing/frame_release.cc:10` |
| Packet buffer 512 / 2048; H.264 `missing_packets_` hold | `ref/video/rtp_video_stream_receiver2.cc:102-103`; `ref/modules/video_coding/packet_buffer.cc:380-381` | `include/sm/timing/packet_buffer.h:46-47`; Session `missing_packets` on |

### Library defaults (standalone configs)

| Feature or default | libwebrtc M157 | Ours |
|---|---|---|
| A component built on its own runs libwebrtc's port: NetEq, VCMTiming and frame release, QualityScaler, OveruseFrameDetector (was row 13 of section 1) | `ref/modules/audio_coding/neteq/`, `ref/modules/video_coding/timing/`, `ref/modules/video_coding/utility/quality_scaler.cc`, `ref/video/adaptation/overuse_frame_detector.cc` | `version = 2` by default in `AudioJitterBufferConfig` (also `RtpReceiverConfig::jitter_buffer`), `VideoTimingConfig`, `QualityScalerConfig` and `CpuOveruseConfig` (#579); `version = 1` still selects the library's own until it is retired |

### NACK, RTX and FEC

| Feature or default | libwebrtc M157 | Ours |
|---|---|---|
| NACK on (harness `rtp_history_ms` 1000); `NackRequester` max age 10000, list 1000, retries 100, RTT 100 ms, 20 ms tick | harness:1080-1083; `ref/modules/video_coding/nack_requester.cc:34-39` | `include/sm/loss/nack_tracker.h:22-28` |
| RED and ULPFEC off beside NACK for H.264 (`ShouldDisableRedAndUlpfec`) | `ref/call/rtp_video_sender.cc:112-160` | `include/sm/rtp/session.h:154-174` |
| `FecControllerDefault`: overhead 0.5, low RTT 20 ms, FEC-frame limits, off above 200 ms RTT | `fec_controller_default.cc:30`, `:64-81`; `media_opt_util.h:46`, `:176-184`; `media_opt_util.cc:192` | `src/loss/fec_controller.cc:14-19`, `:214` |
| PLI as the keyframe request; no SPS+PPS+IDR requirement | `rtp_video_stream_receiver2.cc:411-412`, `:1002-1005` | PLI (FIR only after `RtpReceiver::Start`, which Session never calls) |

### Adaptation and A/V sync

| Feature or default | libwebrtc M157 | Ours |
|---|---|---|
| Quality scaling on with MAINTAIN_FRAMERATE (the harness: DISABLED when off) | harness:1105-1106 | `quality_scaling` on; the send path's MAINTAIN_FRAMERATE |
| `QualityScaler`: 2000 ms, x2.5 after the first step down, 60% drop threshold, 60 frames | `ref/modules/video_coding/utility/quality_scaler.cc:34-37` | `src/video/quality_scaler.cc:11-14` |
| CPU overuse: 85 / 42, 1500 ms frame timeout, 120 samples, process count 3, 2 checks, 5000 ms / 100 ms | `ref/video/adaptation/overuse_frame_detector.h:34-55`, `overuse_frame_detector.cc:47-48` | `CpuOveruseConfig` v2 (the send path) |
| Initial frame drop: 4 frames, 300 / 500 kbps size rule; min pixels 320x180 | `video_stream_encoder_resource_manager.cc:243`; `ref/video/video_stream_encoder.cc:2562-2564`; `ref/api/video_codecs/video_encoder.h:38` | `src/rtp/video_send_controller.cc:26-32`; `video_send_controller.h:110` |
| Video min 50 / max 2500 kbps / 30 fps, frame dropping on | harness:1098-1101 | bench `call.cc:666-674` |
| Sync: max change 80 ms, max delta 10 s, filter 4, min delta 30 ms, 1 s interval, audio delay clamp 0-10 s; opt-in by sync group | `ref/video/stream_synchronization.cc:22-26`; `ref/video/rtp_streams_synchronizer2.cc:35`; `ref/audio/channel_receive.cc:94-95` | `include/sm/sync/stream_sync.h:48-51`, `:115`; `src/rtp/rtp_receiver.cc:337`; `SessionConfig::av_sync` (bench on) |

## 3. Unverified

These defaults are defined in headers that are not in `ref/`; #900 asks for
them (#57, which took such requests, is closed). The harness sets none of
them except where noted, so libwebrtc runs with the header defaults. Verdict
for each: `file needed (#900)` (audit #891).

- `VideoReceiveStreamInterface::Config` (`call/video_receive_stream.h`) and
  `media/base/media_config.h`: `enable_prerenderer_smoothing` (true),
  `render_delay_ms` (10), `rtp.lntf.enabled` (false),
  `rtp.keyframe_method` (PLI), `rtcp_mode` (compound).
- `IP_PACKET_SIZE` (1500), the GoogCC process interval (ours 25 ms;
  `GetProcessInterval`, `goog_cc_factory.cc`),
  `BweSettings::allow_probe_without_media` (read at
  `ref/call/rtp_transport_controller_send.cc:179`, `:199`, `:271`), the
  default of `WebRTC-RetransmitFlushedPackets` (read at
  `ref/modules/pacing/pacing_controller.cc:101`, `:219`), the default ALR
  experiment string (ours 0.80 / 0.40 / -0.60, matched to the prebuilt
  library's rows, #152), and the build flag
  `WEBRTC_OPUS_VARIABLE_COMPLEXITY` (off upstream, so the low-rate
  complexity is 9, as ours).

Verified by audit #891 and moved out of this list: the `NetEq::Config`
defaults, now in `ref/api/neteq/neteq.h:135-141` (`max_packets_in_buffer`
200, `max_delay_ms` 0, `min_delay_ms` 0, `enable_fast_accelerate` false,
`enable_muted_state` false, `enable_rtx_handling` false,
`for_test_no_time_stretching` false). `ChannelReceive` takes the buffer size
and fast accelerate from the stream config (`ref/audio/channel_receive.cc:105-106`;
200 and off, section 2, NetEq) and turns muted state on (`:107`, row 4): **same**.
