## Parity with libwebrtc M157.8103@{#0}

137 scenarios, mean of 10 seeds. **59 gaps**: 26 behind libwebrtc, 33 ahead of it; 18 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

78 gaps on `main`, 59 here: **41 closed, 22 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504, #224 (closed, no note: #63, #476) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 240 | 225 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.4 | 6.2 | ±2.7 | ahead | 6/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 2488 | 6296 | ±2952 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1346 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2097 | 1514 | ±402 | behind | 6/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.4 | 6.2 | ±2.7 | ahead | 6/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 2488 | 6296 | ±2952 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1346 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 91.7 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 91.7 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1037 | 967 | ±52.5 | ahead | 5/10 | **none** (closed, no note: #37, #61) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2097 | 1514 | ±402 | behind | 6/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2205 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `audio.longest_interruption_ms` | 125 | 74.6 | ±34.8 | behind | 5/10 | #504, #451 (closed, no note: #63, #476) |
| issue_464_start_rate_above_int_bps | `video.freeze_total_ms` | 697 | 486 | ±208 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 653 | 441 | ±100 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 866 | 762 | ±82.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 414 | 306 | ±73.9 | behind | 9/10 | **none** |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.7 | 8.0 | ±1.1 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.longest_freeze_ms` | 3555 | 4711 | ±928 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_477_opus_rtp_clock | `audio.jb_delay_p95_ms` | 85 | 74 | ±10 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504, #224 (closed, no note: #63, #476) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1393 | 1527 | ±96.8 | ahead | 5/10 | #411 (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 906 | 1029 | ±90.7 | ahead | 5/10 | #411 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 229 | 244 | ±12.6 | ahead | 5/10 | #504, #411 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 720 | 1045 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 678 | 1004 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 45.9 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 94 | ±10 | ahead | 4/10 | #504 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 50.3 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2540 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 136 | 116 | ±18.6 | behind | 2/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 2026 | 2311 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1694 | 1979 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 27042 | 30195 | ±2918 | ahead | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.longest_freeze_ms` | 3814 | 4304 | ±420 | ahead | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.8 | 0.1 | ±0.5 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 4436 | 561 | ±1774 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 29.8 | 24.6 | ±4.9 | behind | 6/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4066 | 1245 | ±2661 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_5_burst15_freeze | `audio.longest_interruption_ms` | 193 | 144 | ±41.2 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| very_low_capacity | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 2.6 | 3.0 (further) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 523 | 1078 (further) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 416 | 815 (further) |
| issue_204_cross_traffic_join | `video.freeze_count` | 1.6 | 1.1 (closer) |
| issue_23_low_bandwidth | `video.render_fps` | 1.9 | 1.7 (closer) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 1.6 | 1.1 (closer) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 1710 | 2120 (further) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 216 | 184 (closer) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 221 | 183 (closer) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 1.4 | 1.9 (further) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 106 | 138 (further) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 50.6 | 45.4 (closer) |
| issue_457_libopus_audio_rate | `video.render_fps` | 1.9 | 1.7 (closer) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 1572 | 1081 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.9 | 1.7 (closer) |
| low_bandwidth | `video.render_fps` | 1.9 | 1.7 (closer) |
| very_low_capacity | `video.render_fps` | 2.6 | 3.0 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.6 | 3.0 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.render_fps` | 11.3 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 229 | 244 | ±12.6 | ahead | 5/10 | #504, #411 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 678 | 1004 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 720 | 1045 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 45.9 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2569 | 3383 | ±381 | ahead | 9/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2826 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 94 | ±10 | ahead | 4/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 149 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 50.3 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_204_cross_traffic_join | `video.freeze_count` | 3.8 | 2.7 | ±1.1 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.5 | 25.7 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 3.8 | 2.7 | ±1.1 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2540 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2910 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1234 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1761 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 136 | 116 | ±18.6 | behind | 2/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.2 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 588 | 726 | ±62.6 | ahead | 10/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 148 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9147300000000000 | 70.5 | ±457365000000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.8 | 64.8 | ±3.2 | behind | 9/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1224 | 1308 | ±65.4 | behind | 9/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 343 | 297 | ±17.2 | behind | 10/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3458 | 1216 | ±192 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2919 | 683 | ±212 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.1 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1694 | 1979 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 2026 | 2311 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 24.8 | 21 | ±2.1 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 27042 | 30195 | ±2918 | ahead | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.longest_freeze_ms` | 3814 | 4304 | ±420 | ahead | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.8 | 0.1 | ±0.5 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 4436 | 561 | ±1774 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 29.8 | 24.6 | ±4.9 | behind | 6/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 6023 | 4941 | ±962 | behind | 7/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.3 | 7.2 | ±1.5 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.4 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4066 | 1245 | ±2661 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.9 | 9.4 | ±1.7 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.5 | 28.9 | ±2.4 | behind | 8/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 888 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_5_burst15_freeze | `audio.longest_interruption_ms` | 193 | 144 | ±41.2 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.render_fps` | 11.3 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.3 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2826 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2569 | 3383 | ±381 | ahead | 9/10 | **none** |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 45.9 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 50.3 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2540 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2910 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1761 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1234 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 136 | 116 | ±18.6 | behind | 2/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.2 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| very_low_capacity | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3957 | 4431 | ±465 | ahead | 6/10 | **none** (closed, no note: #5, #37, #24) |

