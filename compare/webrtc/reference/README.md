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
| Audio jitter buffer and concealment (NetEq) | `modules/audio_coding/neteq/` | `src/timing/delay_manager.cc`, `audio_jitter_buffer.cc`, `time_stretch.cc`, `src/loss/plc.cc` |
| Audio send path, Opus config, audio RED | `audio/audio_send_stream.*`, `modules/audio_coding/codecs/opus/audio_encoder_opus.*`, `modules/audio_coding/codecs/red/` | test bench audio model, `src/loss/red.cc` |
| A/V sync | `video/stream_synchronization.*`, `video/rtp_streams_synchronizer2.*`, `modules/rtp_rtcp/source/remote_ntp_time_estimator.cc`, `rtc_base/rtp_to_ntp_estimator.*`, `ntp_time_util.*` | `src/sync/` |
| Video engine defaults | `media/engine/webrtc_video_engine.cc` (default resolutions, bitrate limits) | test bench encoder model |
| Glue | `call/rtp_transport_controller_send.*`, `call/rtp_video_sender.*`, `video/rtp_video_stream_receiver2.*`, `api/transport/`, `api/video/video_bitrate_allocation.h` | `src/rtp/session.cc` |
