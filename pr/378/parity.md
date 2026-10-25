## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **96 gaps**: 35 behind libwebrtc, 61 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

104 gaps on `main`, 96 here: **35 closed, 27 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63, #19, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 181 | 749 | ±545 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 112 | 236 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `repair.overhead_pct` | 20.4 | 24.1 | ±3.2 | ahead | 6/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 20.4 | 24.1 | ±3.2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_24_timing_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_29_ulpfec_over_red | `video.longest_freeze_ms` | 3529 | 3298 | ±176 | behind | 1/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `cc.rtt_p50_ms` | 96.5 | 86.4 | ±10 | behind | 6/10 | #61 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2877 | 3104 | ±187 | ahead | 5/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.5 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 4270 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 16.0 | 18.0 | ±2 | ahead | 5/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1500 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 161 | 139 | ±10 | behind | 5/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.6 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 570 | 495 | ±72.8 | ahead | 7/10 | #37, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 531 | ±30.1 | ahead | 7/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 293 | 310 | ±16.3 | ahead | 5/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 157 | 185 | ±15.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3200 | 2310 | ±781 | behind | 3/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 412 | 453 | ±39.0 | behind | 5/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.2 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 43.2 | 47.6 | ±3.7 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 403 | 380 | ±20.2 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 412 | 453 | ±39.0 | behind | 5/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.2 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 43.2 | 47.6 | ±3.7 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1500 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1202 | 1566 | ±328 | ahead | 7/10 | #379 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 601 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 403 | 380 | ±20.2 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1500 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_84_encoder_qp_360p_low_rate | `video.e2e_delay_p95_ms` | 114 | 124 | ±10 | ahead | 5/10 | #24, #37 |
| issue_84_encoder_qp_720p_low_rate | `video.e2e_delay_p95_ms` | 114 | 124 | ±10 | ahead | 5/10 | #24, #37 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 3.3 | 3.8 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 4.0 | 3.5 (closer) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 14.9 | 21.0 (further) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 4.4 | 5.1 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 883 | 1019 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 14.8 | 13.2 (closer) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 22.6 | 30.3 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 30.5 | 39.0 (further) |
| issue_204_cross_traffic_join | `video.freeze_count` | 2.4 | 4.9 (further) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 723 | 1567 (further) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 78.9 | 124 (further) |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 25.0 | 39.6 (further) |
| issue_23_handover | `repair.overhead_pct` | 3.3 | 3.8 (further) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 398 | 507 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 247 | 363 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.6 | 1.4 (closer) |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1815 | 2494 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 2.4 | 4.9 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 723 | 1567 (further) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 78.9 | 124 (further) |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 25.0 | 39.6 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.2 | 3.0 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 3.3 | 3.8 (further) |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 40.8 | 33.2 (closer) |
| lossy_feedback | `audio.jb_delay_p95_ms` | 31 | 26 (closer) |
| lossy_feedback | `sync.av_offset_p95_ms` | 27.7 | 24.5 (closer) |
| very_low_capacity | `video.render_fps` | 4.0 | 3.5 (closer) |
| very_low_capacity | `repair.overhead_pct` | 14.9 | 21.0 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.0 | 3.5 (closer) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 14.9 | 21.0 (further) |
| wifi_to_lte_handover | `repair.overhead_pct` | 3.3 | 3.8 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 29.7 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 36.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1500 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 209 | 222 | ±11.1 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 1.9 | 7 | ±3.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 382 | 1401 | ±595 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 77.8 | 117 | ±10 | ahead | 10/10 | #379 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 184 | 214 | ±10.7 | ahead | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.6 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 161 | 139 | ±10 | behind | 5/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 570 | 495 | ±72.8 | ahead | 7/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 293 | 310 | ±16.3 | ahead | 5/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 531 | ±30.1 | ahead | 7/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3200 | 2310 | ±781 | behind | 3/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 157 | 185 | ±15.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 43.2 | 47.6 | ±3.7 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 412 | 453 | ±39.0 | behind | 5/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.8 | 6.8 | ±2.6 | ahead | 6/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.2 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 403 | 380 | ±20.2 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 271 | 231 | ±21.4 | behind | 5/10 | #379 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 575 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.2 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2916 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 43.2 | 47.6 | ±3.7 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 412 | 453 | ±39.0 | behind | 5/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.8 | 6.8 | ±2.6 | ahead | 6/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.2 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1500 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_handover | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 146 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 488 | 125 | ±24.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 713 | 205 | ±35.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 3720 | 300 | ±500 | behind | 9/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 208 | 43.5 | ±10.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.utilization_pct` | 74.8 | 85.7 | ±4.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 0.5 | 0 | ±0.5 | behind | 4/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 999 | 57.0 | ±50.0 | behind | 10/10 | #379 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1436 | 2002 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1335 | 122 | ±66.7 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 4.6 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3004 | 0 | ±150 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 7/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1112 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 23 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 281 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 2494 | 0 | ±125 | behind | 4/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.3 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 308 | 416 | ±66.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 148 | 256 | ±58.7 | ahead | 10/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1202 | 1566 | ±328 | ahead | 7/10 | #379 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.5 | ±1.4 | ahead | 10/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 601 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 403 | 380 | ±20.2 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 271 | 231 | ±21.4 | behind | 5/10 | #379 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 575 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 7.2 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2916 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 146 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1500 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.6 | 32.6 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_84_encoder_qp_360p_low_rate | `video.e2e_delay_p95_ms` | 114 | 124 | ±10 | ahead | 5/10 | #24, #37 |
| issue_84_encoder_qp_720p_low_rate | `video.e2e_delay_p95_ms` | 114 | 124 | ±10 | ahead | 5/10 | #24, #37 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 120 | 146 | ±18.4 | ahead | 6/10 | #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 203 | 236 | ±20.0 | ahead | 7/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 57.0 | 81.5 | ±19.1 | ahead | 6/10 | #379 |
| low_bandwidth | `cc.rtt_p50_ms` | 146 | 238 | ±80.3 | ahead | 8/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 29.7 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 11.7 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 29.7 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.7 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29, #20 |

