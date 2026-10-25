## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **86 gaps**: 19 behind libwebrtc, 67 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

73 gaps on `main`, 86 here: **24 closed, 37 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63, #19, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `repair.overhead_pct` | 20.0 | 24.1 | ±3.2 | ahead | 7/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 20.0 | 24.1 | ±3.2 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1941 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_24_timing_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_24_timing_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1941 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_76_session_layer_v2 | `sync.av_offset_p95_ms` | 64.8 | 51.1 | ±10 | behind | 5/10 | #379 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.6 | 39.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 4260 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1941 | 2139 | ±180 | ahead | 7/10 | #63, #224 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.freeze_count` | 25.4 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.6 | 36.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1350 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2166 | 3118 | ±823 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 1975 | 2703 | ±726 | ahead | 6/10 | #379 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.0 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 531 | ±30.1 | ahead | 7/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 293 | 310 | ±16.3 | ahead | 5/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 157 | 185 | ±15.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3200 | 2310 | ±781 | behind | 3/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 406 | 380 | ±20.3 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `video.freeze_count` | 6.7 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2891 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 682 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 603 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 280 | 231 | ±21.4 | behind | 5/10 | #379 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1350 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1216 | 1565 | ±315 | ahead | 9/10 | #379 |
| issue_257_zero_max_payload | `video.avg_kbps` | 970 | 875 | ±93.9 | ahead | 3/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 601 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 406 | 380 | ±20.3 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 6.7 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2891 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 682 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 603 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 280 | 231 | ±21.4 | behind | 5/10 | #379 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1216 | 1565 | ±315 | ahead | 9/10 | #379 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1350 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.7 | 32.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.5 | 27.1 | ±1.4 | ahead | 4/10 | #37, #24 |
| issue_84_encoder_qp_360p_low_rate | `video.e2e_delay_p95_ms` | 113 | 124 | ±10 | ahead | 6/10 | #24, #37 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 5/10 | #379 |
| issue_84_encoder_qp_720p_low_rate | `video.e2e_delay_p95_ms` | 113 | 124 | ±10 | ahead | 6/10 | #24, #37 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 5/10 | #379 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1216 | 1565 | ±315 | ahead | 9/10 | #379 |
| very_low_capacity | `video.freeze_count` | 25.4 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25.4 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 3.3 | 3.9 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 4.0 | 4.5 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 13.5 | 15.5 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 28.9 | 24.6 (closer) |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 11.4 | 13.8 (further) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.3 | 2.7 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 0.9 | 0.7 (closer) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.3 | 2.7 (closer) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0.9 | 0.7 (closer) |
| issue_23_handover | `repair.overhead_pct` | 3.3 | 3.9 (further) |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 97.9 | 87.5 (closer) |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 332 | 384 (further) |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 119 | 106 (closer) |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 97.9 | 87.5 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 3.3 | 3.9 (further) |
| low_bandwidth | `cc.rtt_p50_ms` | 97.9 | 87.5 (closer) |
| very_low_capacity | `video.render_fps` | 4.0 | 4.5 (further) |
| very_low_capacity | `repair.overhead_pct` | 13.5 | 15.5 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.0 | 4.5 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 13.5 | 15.5 (further) |
| wifi_to_lte_handover | `repair.overhead_pct` | 3.3 | 3.9 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.9 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.2 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 25.4 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 12.7 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.6 | 36.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1350 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 1975 | 2703 | ±726 | ahead | 6/10 | #379 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2166 | 3118 | ±823 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.9 | 7 | ±3.1 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 574 | 1401 | ±595 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 81.3 | 117 | ±10 | ahead | 9/10 | #379 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 190 | 214 | ±10.7 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 181 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.0 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 153 | 139 | ±10 | behind | 4/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 293 | 310 | ±16.3 | ahead | 5/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 531 | ±30.1 | ahead | 7/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3200 | 2310 | ±781 | behind | 3/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 157 | 185 | ±15.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.1 | 6.8 | ±2.6 | ahead | 5/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 406 | 380 | ±20.3 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 280 | 231 | ±21.4 | behind | 5/10 | #379 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 603 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.freeze_count` | 6.7 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2891 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 682 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.1 | 6.8 | ±2.6 | ahead | 5/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1350 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_handover | `repair.overhead_pct` | 27.9 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 150 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1216 | 1565 | ±315 | ahead | 9/10 | #379 |
| issue_23_low_bandwidth | `video.render_fps` | 28.0 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_257_zero_max_payload | `video.avg_kbps` | 970 | 875 | ±93.9 | ahead | 3/10 | #37, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1237 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.2 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 309 | 416 | ±66.1 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 152 | 256 | ±58.7 | ahead | 10/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1182 | 1566 | ±328 | ahead | 9/10 | #379 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.9 | 25.5 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 601 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 406 | 380 | ±20.3 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 280 | 231 | ±21.4 | behind | 5/10 | #379 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 603 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 6.7 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2891 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 682 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 150 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1216 | 1565 | ±315 | ahead | 9/10 | #379 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 28.0 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1350 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.7 | 32.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 27.9 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.5 | 27.1 | ±1.4 | ahead | 4/10 | #37, #24 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 5/10 | #379 |
| issue_84_encoder_qp_360p_low_rate | `video.e2e_delay_p95_ms` | 113 | 124 | ±10 | ahead | 6/10 | #24, #37 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 5/10 | #379 |
| issue_84_encoder_qp_720p_low_rate | `video.e2e_delay_p95_ms` | 113 | 124 | ±10 | ahead | 6/10 | #24, #37 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 7/10 | #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 197 | 236 | ±20.0 | ahead | 7/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 54.2 | 81.5 | ±19.1 | ahead | 7/10 | #379 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 111 | 122 | ±10 | ahead | 4/10 | #24, #37 |
| low_bandwidth | `cc.rtt_p50_ms` | 150 | 238 | ±80.3 | ahead | 8/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1216 | 1565 | ±315 | ahead | 9/10 | #379 |
| low_bandwidth | `video.render_fps` | 28.0 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 35.2 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 25.4 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 12.7 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.2 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25.4 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 12.7 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.9 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29, #20 |

