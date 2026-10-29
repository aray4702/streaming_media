## Parity with libwebrtc M157.8103@{#0}

143 scenarios, mean of 10 seeds. **41 gaps**: 16 behind libwebrtc, 25 ahead of it; 6 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

69 gaps on `main`, 41 here: **40 closed, 12 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3855 | 4431 | ±465 | ahead | 6/10 | #437, #479 (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 696 | 1045 | ±208 | ahead | 9/10 | #504, #217, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 655 | 1004 | ±208 | ahead | 9/10 | #504, #217, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474, #479 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 135 | 149 | ±10 | ahead | 8/10 | #504, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 80 | 94 | ±10 | ahead | 5/10 | #504, #479 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 3046 | 6296 | ±2952 | ahead | 6/10 | #411, #479 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1401 | 2673 | ±969 | ahead | 7/10 | #411, #479 (closed, no note: #5, #37, #24) |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474, #479 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474, #479 |
| issue_204_cross_traffic_join | `video.freeze_count` | 4 | 2.7 | ±1.1 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 3046 | 6296 | ±2952 | ahead | 6/10 | #411, #479 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1401 | 2673 | ±969 | ahead | 7/10 | #411, #479 (closed, no note: #5, #37, #24) |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2169 | 2287 | ±114 | ahead | 5/10 | #411, #479 (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 25102 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 54.9 | 47.1 | ±6.4 | behind | 6/10 | #514 (closed, no note: #21, #7, #29) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4 | 2.7 | ±1.1 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2560 | 780 | ±1448 | behind | 4/10 | #411, #479 (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2490 | 790 | ±1445 | behind | 4/10 | #411, #479 (closed, no note: #15, #48, #61, #20) |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 137 | 116 | ±18.6 | behind | 3/10 | #474, #479 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 581 | 726 | ±62.6 | ahead | 10/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3420 | 1216 | ±192 | behind | 10/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2930 | 683 | ±212 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.2 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 136 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9050530000000000 | 70.5 | ±452526500000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1873 | 2311 | ±250 | ahead | 8/10 | #466, #504, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1541 | 1979 | ±250 | ahead | 8/10 | #466, #504, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 25964 | 30195 | ±2918 | ahead | 7/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.9 | 0.1 | ±0.5 | behind | 8/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5958 | 4941 | ±962 | behind | 5/10 | #466, #479 |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4427 | 1245 | ±2661 | behind | 6/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1361 | 1527 | ±96.8 | ahead | 3/10 | #411, #479 (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 927 | 1029 | ±90.7 | ahead | 3/10 | #411, #479 |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474, #479 |
| random_loss_5pct | `sync.av_offset_p95_ms` | 101 | 86.1 | ±10 | behind | 5/10 | #474, #479 |
| very_low_capacity | `video.longest_freeze_ms` | 3855 | 4431 | ±465 | ahead | 6/10 | #437, #479 (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3855 | 4431 | ±465 | ahead | 6/10 | #437, #479 (closed, no note: #5, #37, #24) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2794 | 3383 | ±381 | ahead | 7/10 | #411 |
| issue_23_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 294 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.3 | 8.0 | ±1.1 | ahead | 7/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.9 | 20.7 | ±2.9 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.e2e_delay_p95_ms` | 7858 | 9166 | ±1159 | ahead | 8/10 | #466 (closed, no note: #24, #37) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6461 | 7576 | ±787 | ahead | 8/10 | #466 |
| issue_522_outage_to_500kbps | `sync.av_offset_p95_ms` | 352 | 193 | ±118 | behind | 6/10 | **none** |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.6 | 27.1 | ±1.4 | ahead | 4/10 | **none** (closed, no note: #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24, #20) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 3.1 | 2.5 (closer) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 13 | 16 (further) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 514 | 656 (further) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 16.7 | 13.3 (closer) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 1.7 | 1.4 (closer) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 51.3 | 18.1 (closer) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 89.3 | 102 (further) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 4.3 | 4.8 (further) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 4871 | 2456 (closer) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 1.3 | 1 (closer) |
| very_low_capacity | `video.render_fps` | 3.1 | 2.5 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 3.1 | 2.5 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.render_fps` | 10.8 | 8.3 | ±1.3 | ahead | 10/10 | #411 (closed, no note: #37, #24) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 228 | 244 | ±12.6 | ahead | 7/10 | #504, #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.2 | 12.7 | ±2.0 | ahead | 7/10 | #411, #479 (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 50.5 | 63.8 | ±13.3 | ahead | 6/10 | #411, #479 (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2794 | 3383 | ±381 | ahead | 7/10 | #411 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3248 | 3904 | ±462 | ahead | 8/10 | #411, #479 (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411, #479 (closed, no note: #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411, #479 (closed, no note: #37, #24) |
| issue_23_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411, #479 (closed, no note: #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 8/10 | #411, #479 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3910 | 4431 | ±465 | ahead | 5/10 | #437, #479 (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.7 | 8.3 | ±1.3 | ahead | 9/10 | #411, #437, #479 (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.0 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 294 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1205 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 315 | 297 | ±17.2 | behind | 4/10 | #451 (closed, no note: #24, #37) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411, #479 (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 312 | 336 | ±16.8 | ahead | 9/10 | #514 (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 23.2 | 21 | ±2.1 | behind | 5/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3017 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.3 | 8.0 | ±1.1 | ahead | 7/10 | #466 (closed, no note: #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 9.8 | 7.2 | ±1.5 | ahead | 10/10 | #466, #479 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6461 | 7576 | ±787 | ahead | 8/10 | #466 |
| issue_466_undershoot_very_low_capacity | `video.e2e_delay_p95_ms` | 7858 | 9166 | ±1159 | ahead | 8/10 | #466 (closed, no note: #24, #37) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.9 | 20.7 | ±2.9 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.4 | 4.4 | ±0.8 | behind | 6/10 | #466, #479 (closed, no note: #23) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.9 | 9.4 | ±1.7 | ahead | 8/10 | #466, #411 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.time_to_first_frame_ms` | 1096 | 1015 | ±77.0 | behind | 1/10 | #466, #479 (closed, no note: #24, #61) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411, #479 (closed, no note: #37, #24) |
| issue_522_outage_to_500kbps | `sync.av_offset_p95_ms` | 352 | 193 | ±118 | behind | 6/10 | **none** |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.6 | 28.9 | ±2.4 | behind | 8/10 | #504, #479 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 886 | 996 | ±107 | ahead | 6/10 | #504, #479 (closed, no note: #63, #476) |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.6 | 27.1 | ±1.4 | ahead | 4/10 | **none** (closed, no note: #37, #24) |
| low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411, #479 (closed, no note: #37, #24) |
| very_low_capacity | `video.render_fps` | 10.8 | 8.3 | ±1.3 | ahead | 10/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.8 | 8.3 | ±1.3 | ahead | 10/10 | #411 (closed, no note: #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24, #20) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_522_outage_to_500kbps | `sync.av_offset_p95_ms` | 352 | 193 | ±118 | behind | 6/10 | **none** |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.6 | 27.1 | ±1.4 | ahead | 4/10 | **none** (closed, no note: #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.3 | 3.1 | ±1.0 | behind | 7/10 | **none** (closed, no note: #5, #37, #24, #20) |

