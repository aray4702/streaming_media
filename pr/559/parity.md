## Parity with libwebrtc M157.8103@{#0}

138 scenarios, mean of 10 seeds. **69 gaps**: 30 behind libwebrtc, 39 ahead of it; 11 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

69 gaps on `main`, 69 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3855 | 4431 | ±465 | ahead | 6/10 | #437 (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.render_fps` | 11.4 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 231 | 244 | ±12.6 | ahead | 5/10 | #504, #411 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 655 | 1004 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 696 | 1045 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.0 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 47.2 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3390 | 3904 | ±462 | ahead | 8/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 80 | 94 | ±10 | ahead | 5/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 135 | 149 | ±10 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 3046 | 6296 | ±2952 | ahead | 6/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1401 | 2673 | ±969 | ahead | 7/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.6 | 23.8 | ±2.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474 |
| issue_204_cross_traffic_join | `video.freeze_count` | 4 | 2.7 | ±1.1 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 3046 | 6296 | ±2952 | ahead | 6/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1401 | 2673 | ±969 | ahead | 7/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.6 | 23.8 | ±2.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_23_low_bandwidth | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2169 | 2287 | ±114 | ahead | 5/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 54.9 | 47.1 | ±6.4 | behind | 6/10 | #514 (closed, no note: #21, #7, #29) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 25102 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4 | 2.7 | ±1.1 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2560 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2490 | 790 | ±1445 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 137 | 116 | ±18.6 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3905 | 4431 | ±465 | ahead | 5/10 | #437 (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.0 | 8.3 | ±1.3 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 581 | 726 | ±62.6 | ahead | 10/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 136 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9050530000000000 | 70.5 | ±452526500000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.5 | 64.8 | ±3.2 | behind | 9/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1218 | 1308 | ±65.4 | behind | 9/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 348 | 297 | ±17.4 | behind | 10/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3420 | 1216 | ±192 | behind | 10/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2930 | 683 | ±212 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.2 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 310 | 336 | ±16.8 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1541 | 1979 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1873 | 2311 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 23.2 | 21 | ±2.1 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 25964 | 30195 | ±2918 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.9 | 0.1 | ±0.5 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 5432 | 561 | ±1774 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5958 | 4941 | ±962 | behind | 5/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 9.6 | 7.2 | ±1.5 | ahead | 8/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.7 | 4.4 | ±0.8 | behind | 6/10 | #466 (closed, no note: #23) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4427 | 1245 | ±2661 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 12.2 | 9.4 | ±1.7 | ahead | 8/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.time_to_first_frame_ms` | 1096 | 1015 | ±77.0 | behind | 1/10 | #466 (closed, no note: #24, #61) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.5 | 28.9 | ±2.4 | behind | 8/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 885 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 927 | 1029 | ±90.7 | ahead | 3/10 | #411 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1361 | 1527 | ±96.8 | ahead | 3/10 | #411 (closed, no note: #24, #37) |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474 |
| low_bandwidth | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| random_loss_5pct | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474 |
| very_low_capacity | `video.longest_freeze_ms` | 3855 | 4431 | ±465 | ahead | 6/10 | #437 (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.render_fps` | 11.4 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3855 | 4431 | ±465 | ahead | 6/10 | #437 (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.4 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3390 | 3904 | ±462 | ahead | 8/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.0 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 47.2 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2169 | 2287 | ±114 | ahead | 5/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2560 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2490 | 790 | ±1445 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 137 | 116 | ±18.6 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.0 | 8.3 | ±1.3 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 310 | 336 | ±16.8 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |

