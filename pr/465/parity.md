## Parity with libwebrtc M157.8103@{#0}

125 scenarios, mean of 10 seeds. **88 gaps**: 49 behind libwebrtc, 39 ahead of it; 5 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

83 gaps on `main`, 88 here: **0 closed, 5 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_464_start_rate_above_int_bps | `video.freeze_total_ms` | 690 | 435 | ±100 | behind | 5/10 | #5, #37, #24 |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 690 | 435 | ±100 | behind | 5/10 | #5, #37, #24 |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 862 | 730 | ±45.0 | behind | 7/10 | #24, #37 |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 410 | 276 | ±38.5 | behind | 9/10 | **none** |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 309 | 341 | ±17.1 | ahead | 10/10 | #21, #7, #29 |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `video.e2e_delay_p95_ms` | 120 | 110 | ±10 | behind | 3/10 | #24, #37 |
| cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24, #411 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19, #411 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 29.7 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29, #411 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 7.1 | 5.7 | ±1.1 | behind | 8/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 9.2 | ±1.5 | ahead | 8/10 | #37, #24, #411 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 120 | 110 | ±10 | behind | 3/10 | #24, #37 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 296 | 188 | ±50.6 | behind | 8/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 465 | 360 | ±72.5 | behind | 7/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 4.6 | 3 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1554 | 1034 | ±320 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.5 | 6.6 | ±1.6 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4 | 2.5 | ±1.2 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 4373 | 3381 | ±967 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 2.1 | 1.2 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 13072 | 7600 | ±5222 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 199 | 219 | ±11.0 | ahead | 8/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 1.8 | 6.8 | ±3.8 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 369 | 1366 | ±701 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 87.9 | 117 | ±10 | ahead | 10/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 187 | 218 | ±10.9 | ahead | 10/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.8 | 4.2 | ±2.4 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 173 | 885 | ±484 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 126 | 247 | ±100 | ahead | 5/10 | #5, #37, #24, #411 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61, #411 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 239 | 224 | ±11.9 | behind | 6/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 8.9 | 7.8 | ±0.7 | behind | 6/10 | #21, #61, #12 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.0 | 23.7 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 8.9 | 7.8 | ±0.7 | behind | 6/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.0 | 23.7 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 296 | 188 | ±50.6 | behind | 8/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 465 | 360 | ±72.5 | behind | 7/10 | #24, #37 |
| issue_224_congestive_e2e_flip | `video.freeze_count` | 4.6 | 3 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `video.freeze_total_ms` | 1554 | 1034 | ±320 | behind | 8/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 250 | 209 | ±35.1 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 413 | 338 | ±55.0 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 127 | 93.2 | ±20.6 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 161 | 119 | ±37.5 | behind | 5/10 | **none** |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.7 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.4 | ±1.7 | ahead | 7/10 | #21, #61, #12, #411 |
| issue_415_cross_80pct_standing_queue | `net.loss_pct` | 7.9 | 7.2 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_430_av_outage_6s | `audio.jb_delay_p95_ms` | 142 | 116 | ±25.9 | behind | 4/10 | #63 |
| issue_430_av_outage_6s | `audio.mouth_to_ear_p95_ms` | 237 | 202 | ±27.8 | behind | 4/10 | #63 |
| issue_430_av_outage_6s | `sync.av_offset_p95_ms` | 95.5 | 68.8 | ±16.7 | behind | 4/10 | #446 |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 130 | 113 | ±10 | behind | 2/10 | #24, #37 |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 29.7 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29 |
| issue_437_render_smoothing_very_low_capacity | `video.keyframe_requests` | 7.1 | 5.7 | ±1.1 | behind | 8/10 | #23 |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 309 | 341 | ±17.1 | ahead | 10/10 | #21, #7, #29 |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 410 | 276 | ±38.5 | behind | 9/10 | **none** |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 862 | 730 | ±45.0 | behind | 7/10 | #24, #37 |
| issue_464_start_rate_above_int_bps | `video.freeze_total_ms` | 690 | 435 | ±100 | behind | 5/10 | #5, #37, #24 |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 690 | 435 | ±100 | behind | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 296 | 188 | ±50.6 | behind | 8/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 465 | 360 | ±72.5 | behind | 7/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 4.6 | 3 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1554 | 1034 | ±320 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_85_video_pause | `cc.max_settle_ms` | 4140 | 5530 | ±1357 | ahead | 5/10 | #15, #48, #61, #20 |
| low_bandwidth | `video.render_fps` | 27.8 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 5/10 | #24, #37 |
| very_low_capacity | `repair.overhead_pct` | 29.7 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29, #411 |
| very_low_capacity | `video.keyframe_requests` | 7.1 | 5.7 | ±1.1 | behind | 8/10 | #23 |
| very_low_capacity | `video.render_fps` | 11.7 | 9.2 | ±1.5 | ahead | 8/10 | #37, #24, #411 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 29.7 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29, #411 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 7.1 | 5.7 | ±1.1 | behind | 8/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.7 | 9.2 | ±1.5 | ahead | 8/10 | #37, #24, #411 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 296 | 188 | ±50.6 | behind | 8/10 | **none** |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 296 | 188 | ±50.6 | behind | 8/10 | **none** |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 161 | 119 | ±37.5 | behind | 5/10 | **none** |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 410 | 276 | ±38.5 | behind | 9/10 | **none** |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 296 | 188 | ±50.6 | behind | 8/10 | **none** |

