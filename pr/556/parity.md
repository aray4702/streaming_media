## Parity with libwebrtc M157.8103@{#0}

138 scenarios, mean of 10 seeds. **61 gaps**: 34 behind libwebrtc, 27 ahead of it; 21 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

52 gaps on `main`, 61 here: **10 closed, 19 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 45.9 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 48.5 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 2027 | 2311 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1695 | 1979 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 27075 | 30195 | ±2918 | ahead | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.8 | 0.1 | ±0.5 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 4473 | 561 | ±1774 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 29.6 | 24.6 | ±4.9 | behind | 6/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4066 | 1245 | ±2661 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.4 | 23.8 | ±2.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2214 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 496 | 422 | ±73.2 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.4 | 23.8 | ±2.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24528 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2214 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 496 | 422 | ±73.2 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2038 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.6 | 8.0 | ±1.1 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.freeze_total_ms` | 27725 | 22269 | ±4437 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.pause_count` | 0.5 | 1.1 | ±0.6 | ahead | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.pause_total_ms` | 3070 | 9421 | ±5613 | ahead | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 325 | 228 (closer) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 326 | 228 (closer) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 1078 | 735 (closer) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 815 | 598 (closer) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 11.0 | 14.0 (further) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 11 | 14 (further) |
| issue_204_cross_traffic_join | `video.freeze_count` | 1.1 | 2.3 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 1.1 | 2.3 (further) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 3.5 | 4.8 (further) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 2.8 | 2.3 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 11.1 | 8.3 | ±1.3 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 776 | 1004 | ±208 | ahead | 7/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 817 | 1045 | ±208 | ahead | 7/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2786 | 3383 | ±381 | ahead | 7/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3169 | 3904 | ±462 | ahead | 8/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 80 | 94 | ±10 | ahead | 4/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 135 | 149 | ±10 | ahead | 7/10 | #504 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.4 | 23.8 | ±2.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 496 | 422 | ±73.2 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.freeze_count` | 5 | 2.7 | ±1.1 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2214 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.4 | 23.8 | ±2.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_23_low_bandwidth | `video.render_fps` | 27.4 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1026 | 967 | ±52.5 | ahead | 3/10 | **none** (closed, no note: #37, #61) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24528 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.3 | 25.7 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 496 | 422 | ±73.2 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 5 | 2.7 | ±1.1 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2214 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2520 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2970 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1225 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1749 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2038 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.8 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 588 | 726 | ±62.6 | ahead | 10/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 148 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9147300000000000 | 70.5 | ±457365000000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.8 | 64.8 | ±3.2 | behind | 9/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1224 | 1308 | ±65.4 | behind | 9/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 342 | 297 | ±17.2 | behind | 10/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3458 | 1216 | ±192 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2919 | 683 | ±212 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.1 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.4 | 25.9 | ±1.4 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 8/10 | **none** (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 25.8 | 21 | ±2.1 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.6 | 8.0 | ±1.1 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 6202 | 4941 | ±962 | behind | 8/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.freeze_total_ms` | 27725 | 22269 | ±4437 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.pause_count` | 0.5 | 1.1 | ±0.6 | ahead | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.pause_total_ms` | 3070 | 9421 | ±5613 | ahead | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 9.9 | 7.2 | ±1.5 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.7 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.7 | 9.4 | ±1.7 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.4 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.4 | 28.9 | ±2.4 | behind | 8/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 888 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| low_bandwidth | `video.render_fps` | 27.4 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity | `video.render_fps` | 11.1 | 8.3 | ±1.3 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.1 | 8.3 | ±1.3 | ahead | 8/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3169 | 3904 | ±462 | ahead | 8/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2786 | 3383 | ±381 | ahead | 7/10 | **none** |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 496 | 422 | ±73.2 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1026 | 967 | ±52.5 | ahead | 3/10 | **none** (closed, no note: #37, #61) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 496 | 422 | ±73.2 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2520 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2970 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2038 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1749 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1225 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.8 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.4 | 25.9 | ±1.4 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 8/10 | **none** (closed, no note: #21, #7, #29) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 425 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 255 | 181 | ±42.6 | behind | 8/10 | **none** |

