## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **99 gaps**: 36 behind libwebrtc, 63 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

110 gaps on `main`, 99 here: **36 closed, 25 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.5 | 3.5 | ±2.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 98.3 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 91.2 | 236 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.3 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 533 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 258 | 231 | ±21.4 | behind | 4/10 | #379 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.3 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 533 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 258 | 231 | ±21.4 | behind | 4/10 | #379 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2877 | 3104 | ±187 | ahead | 5/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 2527 | 1932 | ±486 | behind | 8/10 | #5, #37, #24 |
| issue_85_video_pause | `cc.max_settle_ms` | 4270 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| satellite | `cc.queue_p95_ms` | 43.2 | 31.3 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 202 | 222 | ±11.1 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 107 | 118 | ±10 | ahead | 5/10 | #379 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 578 | 495 | ±72.8 | ahead | 7/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 25.9 | 22.7 | ±2.9 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 531 | ±30.1 | ahead | 7/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 293 | 310 | ±16.3 | ahead | 5/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 157 | 185 | ±15.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3200 | 2310 | ±781 | behind | 3/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 43.8 | 47.6 | ±3.7 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 43.8 | 47.6 | ±3.7 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1202 | 1566 | ±328 | ahead | 7/10 | #379 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1406 | 979 | ±426 | behind | 6/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 713 | 930 | ±156 | behind | 7/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 0.7 | 0.1 | ±0.5 | behind | 6/10 | #23 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 40.2 | 51.6 | ±8.0 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.7 | 32.6 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1366 | 1543 | ±116 | ahead | 4/10 | #24, #37 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.3 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_84_encoder_qp_360p_low_rate | `video.e2e_delay_p95_ms` | 114 | 124 | ±10 | ahead | 5/10 | #24, #37 |
| issue_84_encoder_qp_720p_low_rate | `video.e2e_delay_p95_ms` | 114 | 124 | ±10 | ahead | 5/10 | #24, #37 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 3.2 | 3.8 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 4.0 | 3.5 (closer) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 14.9 | 21.0 (further) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.4 | 2.0 (closer) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 31.1 | 35.2 (further) |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 11.1 | 21.8 (further) |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 4.1 | 5.2 (further) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.8 | 3.2 (further) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 942 | 802 (closer) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.8 | 3.2 (further) |
| issue_23_handover | `repair.overhead_pct` | 3.2 | 3.8 (further) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 399 | 507 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 248 | 363 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.6 | 1.4 (closer) |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 628 | 1247 (further) |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 0.6 | 0.8 (further) |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 114 | 200 (further) |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 0.6 | 0.7 (further) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 3787 | 4420 (further) |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 464 | 588 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 942 | 802 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 3.2 | 3.8 (further) |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 40.8 | 33.2 (closer) |
| lossy_feedback | `audio.jb_delay_p95_ms` | 31 | 26 (closer) |
| lossy_feedback | `sync.av_offset_p95_ms` | 27.7 | 24.5 (closer) |
| very_low_capacity | `video.render_fps` | 4.0 | 3.5 (closer) |
| very_low_capacity | `repair.overhead_pct` | 14.9 | 21.0 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.0 | 3.5 (closer) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 14.9 | 21.0 (further) |
| wifi_to_lte_handover | `repair.overhead_pct` | 3.2 | 3.8 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 29.7 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.9 | 36.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 107 | 118 | ±10 | ahead | 5/10 | #379 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 202 | 222 | ±11.1 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.1 | 7 | ±3.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 435 | 1401 | ±595 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 81.6 | 117 | ±10 | ahead | 10/10 | #379 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 191 | 214 | ±10.7 | ahead | 8/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.7 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 161 | 139 | ±10 | behind | 5/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 25.9 | 22.7 | ±2.9 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 578 | 495 | ±72.8 | ahead | 7/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 293 | 310 | ±16.3 | ahead | 5/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 531 | ±30.1 | ahead | 7/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3200 | 2310 | ±781 | behind | 3/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 157 | 185 | ±15.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 43.8 | 47.6 | ±3.7 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.6 | 6.8 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `video.freeze_count` | 5 | 2.3 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2151 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 43.8 | 47.6 | ±3.7 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.6 | 6.8 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 8/10 | #21, #7, #29 |
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
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3005 | 0 | ±150 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 7/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1112 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 23 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 281 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1247 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.6 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 308 | 416 | ±66.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 148 | 256 | ±58.7 | ahead | 10/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1202 | 1566 | ±328 | ahead | 7/10 | #379 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.5 | ±1.4 | ahead | 10/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 2.6 | 0.5 | ±1.5 | behind | 6/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 0.9 | 0.1 | ±0.5 | behind | 6/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1406 | 979 | ±426 | behind | 6/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 3510 | 9440 | ±3638 | ahead | 8/10 | #15, #48, #61, #20 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 409 | 127 | ±202 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 40.2 | 51.6 | ±8.0 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 713 | 930 | ±156 | behind | 7/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2029 | 1441 | ±394 | behind | 7/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 0.7 | 0.1 | ±0.5 | behind | 6/10 | #23 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 426 | 226 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 0.7 | 0 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 4420 | 0 | ±221 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.time_to_first_frame_ms` | 6058 | 5485 | ±481 | behind | 6/10 | #24, #61 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 5 | 2.3 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2151 | 1349 | ±247 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 710 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 146 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1227 | 1565 | ±315 | ahead | 7/10 | #379 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.7 | 32.6 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1366 | 1543 | ±116 | ahead | 4/10 | #24, #37 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.3 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
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
| wifi_to_lte_handover | `repair.overhead_pct` | 28.1 | 31.9 | ±3.0 | ahead | 8/10 | #21, #7, #29, #20 |

