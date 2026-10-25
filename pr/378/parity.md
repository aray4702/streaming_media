## Parity with libwebrtc M157.8103@{#0}

112 scenarios, mean of 10 seeds. **84 gaps**: 22 behind libwebrtc, 62 ahead of it; 5 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

78 gaps on `main`, 84 here: **33 closed, 39 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| capacity_drop | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.4 | 3.4 | ±0.8 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 134 | 146 | ±10 | ahead | 5/10 | #63 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 79 | 91 | ±10 | ahead | 5/10 | #63 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.8 | 43.8 | ±4.1 | ahead | 7/10 | #21, #7, #29 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 670 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 253 | 268 | ±13.4 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.4 | 3.4 | ±0.8 | behind | 7/10 | #21, #61, #12 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1950 | 2139 | ±180 | ahead | 6/10 | #63, #224 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 55.9 | 45.2 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_256_audio_24khz | `cc.queue_p95_ms` | 46.9 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_298_audio_24khz_plays | `cc.queue_p95_ms` | 46.9 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 670 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 253 | 268 | ±13.4 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_348_scaling_outage_500ms | `cc.queue_p95_ms` | 55.9 | 45.2 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1950 | 2139 | ±180 | ahead | 6/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.4 | 3.4 | ±0.8 | behind | 7/10 | #21, #61, #12 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 304 | 284 | ±15.2 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_84_encoder_qp_360p_low_rate | `audio.jb_delay_p95_ms` | 130 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `audio.jb_delay_p95_ms` | 130 | 146 | ±15.8 | ahead | 4/10 | #63 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1950 | 2139 | ±180 | ahead | 6/10 | #63, #224 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2933 | 3123 | ±188 | ahead | 4/10 | #5, #37, #24 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_123_v2_allocation_min | `video.freeze_count` | 25.5 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 36.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1450 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.4 | 10.1 | ±1.6 | ahead | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 38.6 | 56.5 | ±16.7 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 161 | 139 | ±10 | behind | 6/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 486 | 531 | ±30.1 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 159 | 185 | ±15.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3700 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.pause_total_ms` | 5761 | 2229 | ±2885 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1632 | 1349 | ±247 | behind | 4/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 5761 | 2229 | ±2885 | behind | 7/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1450 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| issue_283_longest_freeze_no_metrics | `video.freeze_count` | 2 | 1.4 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2287 | 2152 | ±118 | behind | 6/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.keyframe_requests` | 6.5 | 7.2 | ±0.6 | ahead | 6/10 | #23 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1184 | 1566 | ±328 | ahead | 7/10 | #360 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 583 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1632 | 1349 | ±247 | behind | 4/10 | #5, #37, #24 |
| issue_356_capacity_drop_xr_rtt | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_390_cross_join_seed15 | `video.freeze_count` | 7 | 3 | ±1.8 | behind | 7/10 | #5, #37, #24 |
| issue_390_cross_join_seed15 | `video.freeze_total_ms` | 2841 | 1639 | ±743 | behind | 6/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1450 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| pacer_v2_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| satellite | `video.freeze_count` | 31.3 | 28 | ±3.2 | behind | 7/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_count` | 25.5 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25.5 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 3.6 | 4.1 (further) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.9 | 4.5 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 757 | 886 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 11.5 | 16.2 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 27.8 | 19.5 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1 | 1.1 (further) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1 | 1.1 (further) |
| issue_390_cross_join_seed15 | `video.longest_freeze_ms` | 307 | 214 (closer) |
| very_low_capacity | `video.render_fps` | 3.6 | 4.1 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 3.6 | 4.1 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 32.9 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 25.5 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 12.3 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 36.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1450 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.4 | 10.1 | ±1.6 | ahead | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 38.6 | 56.5 | ±16.7 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.5 | 7 | ±3.1 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 515 | 1401 | ±595 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 85.2 | 117 | ±10 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 195 | 214 | ±10.7 | ahead | 8/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 161 | 139 | ±10 | behind | 6/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 486 | 531 | ±30.1 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3700 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 159 | 185 | ±15.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 243 | 259 | ±13.0 | ahead | 5/10 | #360 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.4 | 6.8 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.7 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_154_shallow_drop_repair_v2 | `video.pause_total_ms` | 5761 | 2229 | ±2885 | behind | 7/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1632 | 1349 | ±247 | behind | 4/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.4 | 6.8 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.7 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 5761 | 2229 | ±2885 | behind | 7/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1450 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 145 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `video.freeze_count` | 2 | 1.4 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2287 | 2152 | ±118 | behind | 6/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.keyframe_requests` | 6.5 | 7.2 | ±0.6 | ahead | 6/10 | #23 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1874 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.3 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 304 | 416 | ±66.1 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 147 | 256 | ±58.7 | ahead | 10/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1184 | 1566 | ±328 | ahead | 7/10 | #360 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.5 | ±1.4 | ahead | 10/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 583 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1632 | 1349 | ±247 | behind | 4/10 | #5, #37, #24 |
| issue_356_capacity_drop_xr_rtt | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_390_cross_join_seed15 | `video.freeze_count` | 7 | 3 | ±1.8 | behind | 7/10 | #5, #37, #24 |
| issue_390_cross_join_seed15 | `video.freeze_total_ms` | 2841 | 1639 | ±743 | behind | 6/10 | #5, #37, #24 |
| issue_390_cross_join_seed15 | `video.longest_freeze_ms` | 702 | 916 | ±205 | ahead | 6/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 145 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1450 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.6 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1415 | 1543 | ±116 | ahead | 5/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 6/10 | **none** |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 6/10 | **none** |
| low_bandwidth | `cc.rtt_p50_ms` | 145 | 238 | ±80.3 | ahead | 8/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| pacer_v2_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 14210 | 14980 | ±749 | ahead | 3/10 | #15, #48, #61, #20 |
| satellite | `video.freeze_count` | 31.3 | 28 | ±3.2 | behind | 7/10 | #5, #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 32.9 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 25.5 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 12.3 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 32.9 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25.5 | 20.3 | ±3.9 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 12.3 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 6/10 | #63, #20 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 6/10 | **none** |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 103 | 114 | ±10 | ahead | 6/10 | **none** |
| low_bandwidth | `sync.av_offset_p95_ms` | 1233 | 1565 | ±315 | ahead | 7/10 | **none** |

