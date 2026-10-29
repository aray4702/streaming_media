## Parity with libwebrtc M157.8103@{#0}

138 scenarios, mean of 10 seeds. **70 gaps**: 28 behind libwebrtc, 42 ahead of it; 22 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

52 gaps on `main`, 70 here: **8 closed, 26 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 48.5 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1030 | 967 | ±52.5 | ahead | 4/10 | **none** (closed, no note: #37, #61) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1738 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1213 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 24.5 | 21 | ±2.1 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 27075 | 30195 | ±2918 | ahead | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4066 | 1245 | ±2661 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 230 | 244 | ±12.6 | ahead | 4/10 | #504, #411 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 167 | 137 | ±20 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 15.5 | 13.3 | ±2 | ahead | 5/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1588 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2295 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 539 | 422 | ±73.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1588 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_256_audio_24khz | `video.freeze_count` | 0.1 | 0.9 | ±0.7 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `video.freeze_total_ms` | 19.7 | 200 | ±161 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `video.longest_freeze_ms` | 37.9 | 163 | ±108 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2169 | 2287 | ±114 | ahead | 5/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.freeze_count` | 0.1 | 0.9 | ±0.7 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.freeze_total_ms` | 19.7 | 200 | ±161 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.longest_freeze_ms` | 37.9 | 163 | ±108 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24256 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2295 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 539 | 422 | ±73.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 137 | 116 | ±18.6 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_466_scene_change_very_low_capacity | `video.pause_total_ms` | 3432 | 9421 | ±5613 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.time_to_first_frame_ms` | 1096 | 1015 | ±77.0 | behind | 1/10 | #466 (closed, no note: #24, #61) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.5 | 4.4 | ±0.8 | behind | 7/10 | #466 (closed, no note: #23) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1322 | 1527 | ±96.8 | ahead | 4/10 | #411 (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 915 | 1029 | ±90.7 | ahead | 4/10 | #411 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 325 | 378 (further) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 326 | 378 (further) |
| issue_12_lossbwe_policer | `net.loss_pct` | 2.5 | 3.2 (further) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 17.9 | 24.0 (further) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 11.0 | 13.0 (further) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 11 | 13 (further) |
| issue_204_cross_traffic_join | `video.freeze_count` | 1.1 | 2.6 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 1.1 | 2.6 (further) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2120 | 1630 (closer) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 45.4 | 52.9 (further) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 284 | 360 (further) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 284 | 360 (further) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.7 | 0.6 (closer) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 1181 | 990 (closer) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 5.0 | 5.5 (further) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 3.9 | 6 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 11.2 | 8.3 | ±1.3 | ahead | 10/10 | #411 (closed, no note: #37, #24) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 230 | 244 | ±12.6 | ahead | 4/10 | #504, #411 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 626 | 1004 | ±208 | ahead | 8/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 668 | 1045 | ±208 | ahead | 8/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 15.5 | 13.3 | ±2 | ahead | 5/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_12_lossbwe_policer | `net.loss_pct` | 9.5 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 39.8 | 63.8 | ±13.3 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2512 | 3383 | ±381 | ahead | 10/10 | **none** |
| issue_12_lossbwe_policer | `video.avg_kbps` | 167 | 137 | ±20 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2726 | 3904 | ±462 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 81 | 94 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 136 | 149 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1588 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 539 | 422 | ±73.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.freeze_count` | 5.3 | 2.7 | ±1.1 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2295 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1588 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_256_audio_24khz | `video.freeze_count` | 0.1 | 0.9 | ±0.7 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `video.freeze_total_ms` | 19.7 | 200 | ±161 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `video.longest_freeze_ms` | 37.9 | 163 | ±108 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2169 | 2287 | ±114 | ahead | 5/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.freeze_count` | 0.1 | 0.9 | ±0.7 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.freeze_total_ms` | 19.7 | 200 | ±161 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.longest_freeze_ms` | 37.9 | 163 | ±108 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24256 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 539 | 422 | ±73.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 5.3 | 2.7 | ±1.1 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2295 | 1514 | ±402 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2410 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2420 | 790 | ±1445 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 137 | 116 | ±18.6 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.8 | 8.3 | ±1.3 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 581 | 726 | ±62.6 | ahead | 10/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 136 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9050530000000000 | 70.5 | ±452526500000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.9 | 64.8 | ±3.2 | behind | 9/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1226 | 1308 | ±65.4 | behind | 9/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 350 | 297 | ±17.5 | behind | 10/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3420 | 1216 | ±192 | behind | 10/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2930 | 683 | ±212 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.2 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 312 | 336 | ±16.8 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1619 | 1979 | ±250 | ahead | 7/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1951 | 2311 | ±250 | ahead | 7/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.7 | 0.1 | ±0.5 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 4405 | 561 | ±1774 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 30.2 | 24.6 | ±4.9 | behind | 7/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5931 | 4941 | ±962 | behind | 7/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.pause_total_ms` | 3432 | 9421 | ±5613 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.2 | 7.2 | ±1.5 | ahead | 8/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 26.7 | 20.7 | ±2.9 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.5 | 4.4 | ±0.8 | behind | 7/10 | #466 (closed, no note: #23) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 12.0 | 9.4 | ±1.7 | ahead | 8/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.time_to_first_frame_ms` | 1096 | 1015 | ±77.0 | behind | 1/10 | #466 (closed, no note: #24, #61) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.5 | 28.9 | ±2.4 | behind | 8/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 885 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 915 | 1029 | ±90.7 | ahead | 4/10 | #411 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1322 | 1527 | ±96.8 | ahead | 4/10 | #411 (closed, no note: #24, #37) |
| low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity | `video.render_fps` | 11.2 | 8.3 | ±1.3 | ahead | 10/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.2 | 8.3 | ±1.3 | ahead | 10/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2726 | 3904 | ±462 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 167 | 137 | ±20 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2512 | 3383 | ±381 | ahead | 10/10 | **none** |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 15.5 | 13.3 | ±2 | ahead | 5/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_12_lossbwe_policer | `net.loss_pct` | 9.5 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 39.8 | 63.8 | ±13.3 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 539 | 422 | ±73.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_256_audio_24khz | `video.freeze_count` | 0.1 | 0.9 | ±0.7 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `video.freeze_total_ms` | 19.7 | 200 | ±161 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `video.longest_freeze_ms` | 37.9 | 163 | ±108 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2169 | 2287 | ±114 | ahead | 5/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.freeze_count` | 0.1 | 0.9 | ±0.7 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.freeze_total_ms` | 19.7 | 200 | ±161 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_298_audio_24khz_plays | `video.longest_freeze_ms` | 37.9 | 163 | ±108 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 539 | 422 | ±73.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2410 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2420 | 790 | ±1445 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 137 | 116 | ±18.6 | behind | 3/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.8 | 8.3 | ±1.3 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 312 | 336 | ±16.8 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |

