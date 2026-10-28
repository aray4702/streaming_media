## Parity with libwebrtc M157.8103@{#0}

138 scenarios, mean of 10 seeds. **52 gaps**: 24 behind libwebrtc, 28 ahead of it; 14 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

52 gaps on `main`, 52 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 11.0 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 678 | 1004 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 720 | 1045 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 45.9 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2569 | 3383 | ±381 | ahead | 9/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2826 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 94 | ±10 | ahead | 4/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 149 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 48.5 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_204_cross_traffic_join | `video.freeze_count` | 3.8 | 2.7 | ±1.1 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1030 | 967 | ±52.5 | ahead | 4/10 | **none** (closed, no note: #37, #61) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.5 | 25.7 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 3.8 | 2.7 | ±1.1 | behind | 3/10 | #408 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2540 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2910 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1213 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1738 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.9 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
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
| issue_457_libopus_audio_rate | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1695 | 1979 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 2027 | 2311 | ±250 | ahead | 8/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 24.5 | 21 | ±2.1 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 27075 | 30195 | ±2918 | ahead | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.8 | 0.1 | ±0.5 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 4473 | 561 | ±1774 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 29.6 | 24.6 | ±4.9 | behind | 6/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 6123 | 4941 | ±962 | behind | 8/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.0 | 7.2 | ±1.5 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.6 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4066 | 1245 | ±2661 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 12.2 | 9.4 | ±1.7 | ahead | 10/10 | #466 (closed, no note: #37, #24) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.4 | 28.9 | ±2.4 | behind | 8/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 888 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| low_bandwidth | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity | `video.render_fps` | 11.0 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.0 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2826 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2569 | 3383 | ±381 | ahead | 9/10 | **none** |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 45.9 | 63.8 | ±13.3 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 48.5 | 65.3 | ±14.6 | ahead | 7/10 | **none** |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1030 | 967 | ±52.5 | ahead | 4/10 | **none** (closed, no note: #37, #61) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2540 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2910 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1738 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1213 | 1051 | ±151 | behind | 7/10 | **none** |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.9 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.6 | 25.9 | ±1.4 | ahead | 6/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |

