# libwebrtc reference sources

Read-only copies of the libwebrtc files that this library follows, at the
commit the comparison build is pinned to (M157,
`b321f634929af0af61a8964487fbd7894a008f12`, see `compare/webrtc/fetch.sh`).
They are here so that people and agents can check our code against
libwebrtc's own, including from environments that can't reach
webrtc.googlesource.com.

- **Not built and not linked.** Nothing in CMake or CI uses these files.
- **Don't edit them.** To refresh them or add files, edit the lists in
  `fetch.sh` (whole directories) or `files.txt` (single files), run
  `compare/webrtc/reference/fetch.sh` on a machine that can reach
  googlesource, and commit the result. If the pinned commit in
  `compare/webrtc/fetch.sh` changes, change `COMMIT` in `fetch.sh` to match.
- **License.** libwebrtc is BSD-licensed. See `LICENSE`, `PATENTS` and
  `AUTHORS` in this directory, which apply to every file under it.

## What is here

| Area | libwebrtc path | Ours |
|---|---|---|
| Delay-based BWE, acked rate, link capacity, probing, loss-based BWE | `modules/congestion_controller/goog_cc/`, `modules/remote_bitrate_estimator/` | `src/cc/delay_based_bwe.cc`, `probing.cc`, `loss_based_bwe.cc`, `send_side_bwe.cc` |
| Pacer and prober | `modules/pacing/` | `src/cc/pacer.cc` |
| NACK, packet buffers, LNTF, frame references | `modules/video_coding/nack_requester.*`, `packet_buffer.*`, `h26x_packet_buffer.*`, `loss_notification_controller.*`, `rtp_frame_reference_finder.*` | `src/loss/`, `src/timing/packet_buffer.cc` |
| FEC: protection choice and ULPFEC | `modules/video_coding/fec_controller_default.*`, `media_opt_util.*`, `modules/rtp_rtcp/source/forward_error_correction*`, `ulpfec_*`, `fec_private_tables_*` | `src/loss/` |
| RTP/RTCP, H.264, dependency descriptor | `modules/rtp_rtcp/source/rtp_sender_video.*`, `rtp_sender_egress.*`, `rtp_packet_history.*`, `rtcp_sender.*`, `rtcp_receiver.*`, `receive_statistics_impl.*`, `rtp_rtcp_impl2.*`, `rtp_format_h264.*`, `video_rtp_depacketizer_h264.*`, `rtp_dependency_descriptor_extension.*` | `src/rtp/` |
| Frame dropper, quality scaler, rate allocation, encoder adaptation | `modules/video_coding/utility/`, `modules/video_coding/svc/svc_rate_allocator.*`, `video/adaptation/`, `video/video_stream_encoder.*`, `video/encoder_bitrate_adjuster.*`, `video/encoder_overshoot_detector.*` | `src/cc/frame_dropper.cc`, `video_adaptation.cc`, `bitrate_allocator.cc` |
| Receive timing | `modules/video_coding/timing/timing.*` | `src/timing/` |
| Default settings (field trials) | `rtc_base/experiments/rate_control_settings.*` (congestion window and pushback defaults), `experiments/field_trials.py` (the registry) | `src/cc/`, `src/rtp/send_side_controller.cc` |
| Video receive timing and jitter | `video/timing/` (`jitter_estimator`, `frame_delay_variation_kalman_filter`, `inter_frame_delay_variation_calculator`), `modules/video_coding/timing/`, `api/video/frame_buffer.*`, `video/video_stream_buffer_controller.*` | `src/timing/video_jitter_estimator.cc`, `src/timing/packet_buffer.cc` |
| Audio jitter buffer and concealment (NetEq) | `modules/audio_coding/neteq/`, `common_audio/signal_processing/` and `common_audio/third_party/spl_sqrt_floor/` (the `WebRtcSpl_*` fixed-point helpers NetEq calls), `modules/audio_coding/codecs/opus/audio_decoder_opus.*` (receive-side Opus, in-band FEC) | `src/timing/delay_manager.cc`, `audio_jitter_buffer.cc`, `time_stretch.cc`, `src/loss/plc.cc` |
| Opus in-band FEC (LBRR) decisions | `third_party/opus/src/` (libopus 55513e81, as libwebrtc M157 builds it): `silk/control_codec.c` (`silk_setup_LBRR`), `src/opus_encoder.c` (`decide_fec`), `silk/enc_API.c`, `silk/fixed/encode_frame_FIX.c`. Licensed under `third_party/opus/src/COPYING`. | test bench audio model, `src/timing/audio_jitter_buffer.cc` |
| Audio send path, Opus config, audio RED | `audio/audio_send_stream.*`, `audio/channel_send.*`, `audio/audio_state.cc`, `modules/rtp_rtcp/source/rtp_sender_audio.*`, `modules/audio_coding/acm2/audio_coding_module.cc`, `modules/audio_coding/codecs/opus/audio_encoder_opus.*`, `opus_interface.cc`, `api/audio_codecs/opus/audio_encoder_opus*` (Opus defaults), `modules/audio_coding/codecs/red/`, `media/engine/webrtc_voice_engine.*`, `media/base/media_constants.cc` | test bench audio model, `src/loss/red.cc` |
| A/V sync | `rtc_base/numerics/moving_percentile_filter.h` (`MovingMedianFilter`), `percentile_filter.h`, `rtc_base/task_utils/repeating_task.*` (the 1 s sync task), `video/stream_synchronization.*`, `video/rtp_streams_synchronizer2.*`, `modules/rtp_rtcp/source/remote_ntp_time_estimator.cc`, `rtc_base/rtp_to_ntp_estimator.*`, `ntp_time_util.*` | `src/sync/` |
| Frame cadence for screen sharing (zero-hertz mode) and quality convergence | `video/frame_cadence_adapter.*`, `video/quality_convergence_controller.*`, `video/quality_convergence_monitor.*` | `src/video/frame_cadence_adapter.cc`, `quality_convergence.cc` (#878) |
| Keyframe requests on the sender, frame release timing | `video/encoder_rtcp_feedback.*` (PLI/FIR handling), `video/frame_decode_timing.*` | `src/rtp/send_side_controller.cc`, `src/video/frame_scheduler.cc` |
| Units (`DataRate`, `DataSize`, `TimeDelta`, `Timestamp`, `Frequency`) | `api/units/` | rounding in every port's unit arithmetic |
| Video engine defaults | `media/engine/webrtc_video_engine.cc` (default resolutions, bitrate limits), `video/config/` (encoder stream factory, simulcast and bitrate limits) | test bench encoder model |
| Audio processing (APM) | `modules/audio_processing/{aec3,ns,agc,agc2,vad,transient}/` (`agc/` is AGC1: `agc_manager_direct.*` and `legacy/` analog and digital AGC), `gain_control_impl.*`, `high_pass_filter.*`, `audio_processing_impl.*`, band splitting (`audio_buffer.*`, `splitting_filter.*`, `three_band_filter_bank.*`), the config and its defaults (`api/audio/audio_processing.*`, `api/audio/echo_canceller3_config.*`), `gain_controller2.*`, `utility/` (cascaded biquad, delay estimator, pffft wrapper), `common_audio/include/audio_util.h`, AGC2's RNN VAD weights (`third_party/rnnoise/`, licence in its `COPYING`), AGC1's VAD dependencies (`modules/audio_coding/codecs/isac/main/source/` pitch and filter code, `modules/third_party/fft/`, `common_audio/third_party/ooura/fft_size_256/`), AEC3's 128-point FFT (`common_audio/third_party/ooura/fft_size_128/`) and APM's analog mic gain emulation (`capture_levels_adjuster/`), the call settings (`media/engine/webrtc_voice_engine.cc`), `common_audio/{vad,resampler}/` | `src/audio/` |
| Call glue, sync groups, demuxing | `call/call.cc`, `modules/congestion_controller/receive_side_congestion_controller.*` (the receive-side feedback task), `call/syncable.h`, `call/rtp_demuxer.*` | `src/sync/stream_sync.cc`, `src/rtp/session.cc` |
| Capture time, RTP packet and extensions, transport feedback adapter | `modules/rtp_rtcp/source/absolute_capture_time_{sender,interpolator}.*`, `capture_clock_offset_updater.*`, `rtp_packet.*`, `rtp_header_extension_map.cc`, `modules/congestion_controller/rtp/transport_feedback_adapter.*` | `src/sync/capture_time.cc`, `src/core/rtp_packet.cc`, `src/cc/transport_feedback.cc` |
| Glue | `call/rtp_transport_controller_send.*`, `call/rtp_video_sender.*`, `video/rtp_video_stream_receiver2.*`, `api/transport/`, `api/video/video_bitrate_allocation.h` | `src/rtp/session.cc` |
| Video render stage (used when `enable_prerenderer_smoothing` is on) | `video/render/video_render_frames.*`, `video/render/incoming_video_stream.*` | none yet (#437) |
| Retransmission rate limiter | `rtc_base/rate_limiter.*`, `modules/rtp_rtcp/source/rtp_rtcp_interface.h` (`Configuration::retransmission_rate_limiter`), `rtc_base/units/unit_base.h`. In M157 only tests set `retransmission_rate_limiter`; the default call leaves it null, so `RTPSender::ReSendPacket` never calls `TryUseRate`. | none (#443) |
| H.264 SPS/PPS tracker | `modules/video_coding/h264_sps_pps_tracker.*` | `src/rtp/rtp_receiver.cc` (#453, PR #454) |

Not here, on purpose: `modules/audio_coding/audio_network_adaptor/`. libwebrtc only turns it on when the application passes an ANA config, so the default call never uses it.
