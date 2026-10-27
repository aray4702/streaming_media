## Parity with libwebrtc M157.8103@{#0}

123 scenarios, mean of 10 seeds. **61 gaps**: 15 behind libwebrtc, 46 ahead of it; 5 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

65 gaps on `main`, 61 here: **22 closed, 18 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.freeze_count` | 25.3 | 21.3 | ±2.2 | behind | 8/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 3071 | 2135 | ±793 | behind | 7/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `sync.av_offset_p95_ms` | 183 | 168 | ±12.4 | behind | 5/10 | **none** |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2 | 6.8 | ±3.8 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 413 | 1366 | ±701 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 219 | ±11.0 | ahead | 6/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1 | 4.2 | ±2.4 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 213 | 885 | ±484 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 128 | 247 | ±100 | ahead | 6/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 183 | 218 | ±10.9 | ahead | 9/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 82.0 | 117 | ±10 | ahead | 10/10 | #24, #37, #411 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.1 | 23.7 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.1 | 23.7 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| issue_24_timing_v2_default | `video.freeze_count` | 3.1 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_24_timing_v2_render_fps | `video.freeze_count` | 3.1 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_430_av_outage_6s | `audio.mouth_to_ear_p95_ms` | 237 | 202 | ±27.8 | behind | 5/10 | #63 |
| issue_430_av_outage_6s | `audio.jb_delay_p95_ms` | 144 | 116 | ±25.9 | behind | 5/10 | #63 |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 132 | 113 | ±10 | behind | 3/10 | #24, #37 |
| issue_430_av_outage_6s | `sync.av_offset_p95_ms` | 97.8 | 68.8 | ±16.7 | behind | 5/10 | #446 |
| issue_437_render_smoothing_very_low_capacity | `video.freeze_count` | 24.6 | 21.3 | ±2.2 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_count` | 25.3 | 21.3 | ±2.2 | behind | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25.3 | 21.3 | ±2.2 | behind | 8/10 | #5, #37, #24 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 627 | 1045 | ±208 | ahead | 9/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 586 | 1004 | ±208 | ahead | 9/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2631 | 3904 | ±462 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2505 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `net.loss_pct` | 9.8 | 12.7 | ±2.0 | ahead | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.9 | 79.5 | ±17.5 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 13.6 | 16.2 | ±2 | ahead | 6/10 | #21, #7, #29, #411 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.1 | 45.0 | ±4.7 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 251 | 232 | ±12.5 | behind | 6/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 131 | 112 | ±10 | behind | 5/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.5 | 45.2 | ±5.3 | ahead | 8/10 | #21, #7, #29 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2170 | 1602 | ±296 | behind | 6/10 | #5, #37, #24 |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1749 | 1577 | ±150 | behind | 7/10 | #24, #37 |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1226 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.5 | 8.3 | ±1.3 | ahead | 8/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 2.4 | 3.4 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 20.3 | 14.5 (closer) |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 20.3 | 14.5 (closer) |
| very_low_capacity | `video.render_fps` | 2.4 | 3.4 (further) |
| very_low_capacity | `repair.overhead_pct` | 20.3 | 14.5 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.4 | 3.4 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 20.3 | 14.5 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24, #411 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #19, #411 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.3 | 49.8 | ±9.4 | ahead | 9/10 | #21, #7, #29, #411 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #37, #24, #411 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 226 | 244 | ±12.6 | ahead | 4/10 | #63, #411 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 586 | 1004 | ±208 | ahead | 9/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 627 | 1045 | ±208 | ahead | 9/10 | #63, #217 |
| issue_12_lossbwe_policer | `net.loss_pct` | 9.8 | 12.7 | ±2.0 | ahead | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.9 | 79.5 | ±17.5 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2505 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2631 | 3904 | ±462 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 13.6 | 16.2 | ±2 | ahead | 6/10 | #21, #7, #29, #411 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.1 | 45.0 | ±4.7 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 131 | 112 | ±10 | behind | 5/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 251 | 232 | ±12.5 | behind | 6/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.5 | 45.2 | ±5.3 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 349 | ±113 | ahead | 6/10 | #24, #61, #411 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.3 | 6.2 | ±2.7 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.3 | 6.2 | ±2.7 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_23_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1329 | 1583 | ±232 | ahead | 5/10 | #411 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 298 | 405 | ±63.2 | ahead | 10/10 | #19, #15, #48, #12, #61, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 154 | 237 | ±70.1 | ahead | 9/10 | #61, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1288 | 1635 | ±217 | ahead | 7/10 | #360, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_346_reconfigure_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 6/10 | #21, #61, #12, #411 |
| issue_410_queue_sample_phase | `audio.jb_delay_p95_ms` | 220 | 130 | ±88.3 | behind | 10/10 | #63 |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1226 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1749 | 1577 | ±150 | behind | 7/10 | #24, #37 |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2170 | 1602 | ±296 | behind | 6/10 | #5, #37, #24 |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 35.3 | 49.8 | ±9.4 | ahead | 9/10 | #21, #7, #29 |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.5 | 8.3 | ±1.3 | ahead | 8/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1329 | 1583 | ±232 | ahead | 5/10 | #411 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 852 | 1029 | ±90.7 | ahead | 6/10 | #411 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1297 | 1527 | ±96.8 | ahead | 5/10 | #24, #37, #411 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1329 | 1583 | ±232 | ahead | 5/10 | #411 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| very_low_capacity | `repair.overhead_pct` | 35.3 | 49.8 | ±9.4 | ahead | 9/10 | #21, #7, #29, #411 |
| very_low_capacity | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #37, #24, #411 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.3 | 49.8 | ±9.4 | ahead | 9/10 | #21, #7, #29, #411 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #37, #24, #411 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2505 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1226 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 6/10 | **none** |

