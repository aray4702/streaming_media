## Parity with libwebrtc M157.8103@{#0}

137 scenarios, mean of 10 seeds. **91 gaps**: 48 behind libwebrtc, 43 ahead of it; 41 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

78 gaps on `main`, 91 here: **21 closed, 34 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504, #224 (closed, no note: #63, #476) |
| issue_204_cross_traffic_join | `video.freeze_count` | 4.3 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2097 | 1514 | ±402 | behind | 6/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.3 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2097 | 1514 | ±402 | behind | 6/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2205 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1793 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1272 | 1051 | ±151 | behind | 6/10 | **none** |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.7 | 8.3 | ±1.3 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `video.freeze_total_ms` | 697 | 486 | ±208 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 24.7 | 21 | ±2.1 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 6514 | 4941 | ±962 | behind | 6/10 | #466 |
| issue_466_undershoot_very_low_capacity | `video.longest_freeze_ms` | 3555 | 4711 | ±928 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_477_opus_rtp_clock | `audio.jb_delay_p95_ms` | 85 | 74 | ±10 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504, #224 (closed, no note: #63, #476) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1393 | 1527 | ±96.8 | ahead | 5/10 | #411 (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 906 | 1029 | ±90.7 | ahead | 5/10 | #411 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 803 | 1045 | ±208 | ahead | 7/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 761 | 1004 | ±208 | ahead | 7/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 82 | 94 | ±10 | ahead | 5/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 6.6 | 7.2 | ±0.6 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_23_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24465 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 39.8 | 47.1 | ±6.4 | ahead | 8/10 | **none** (closed, no note: #21, #7, #29) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2580 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3805 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 282 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1922 | 2311 | ±250 | ahead | 9/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1591 | 1979 | ±250 | ahead | 9/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2895 | 561 | ±1774 | behind | 3/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_1mbps | `sync.av_offset_p95_ms` | 186 | 205 | ±18.3 | ahead | 8/10 | #466 |
| issue_466_undershoot_very_low_capacity | `video.e2e_delay_p95_ms` | 7556 | 9166 | ±1159 | ahead | 8/10 | #466 (closed, no note: #24, #37) |
| issue_466_undershoot_very_low_capacity | `video.time_to_first_frame_ms` | 1095 | 1015 | ±77.0 | behind | 1/10 | #466 (closed, no note: #24, #61) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.6 | 4.4 | ±0.8 | behind | 5/10 | #466 (closed, no note: #23) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6527 | 7576 | ±787 | ahead | 8/10 | #466 |
| issue_466_undershoot_very_low_capacity | `net.loss_pct` | 17.4 | 21.0 | ±2.9 | ahead | 7/10 | #466 (closed, no note: #21, #61, #12) |
| issue_466_undershoot_very_low_capacity | `repair.overhead_pct` | 30.0 | 36.3 | ±6.1 | ahead | 7/10 | #466 (closed, no note: #21, #7, #29) |
| issue_5_burst15_freeze | `audio.longest_interruption_ms` | 188 | 144 | ±41.2 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24, #20) |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24, #20) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 23.0 | 26.2 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.6 | 2.8 (further) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 108 | 64.3 (closer) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 105 | 49.4 (closer) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 416 | 487 (further) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 10.1 | 12.1 (further) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.8 | 3.2 (further) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1327 | 1153 (closer) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 23.0 | 26.2 (further) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.8 | 3.2 (further) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1327 | 1153 (closer) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 108 | 64.3 (closer) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 105 | 49.4 (closer) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 69.7 | 89.1 (further) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 23.0 | 26.2 (further) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 23.0 | 26.2 (further) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 1710 | 2640 (further) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 212 | 102 (closer) |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 104 | 88.5 (closer) |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 109 | 86.6 (closer) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 1.7 | 1.3 (closer) |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 2.9 | 2.5 (closer) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 5.2 | 4.6 (closer) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 23.0 | 26.4 (further) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 108 | 64.3 (closer) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 105 | 49.4 (closer) |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 23.0 | 26.2 (further) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 23.0 | 26.2 (further) |
| very_low_capacity | `video.render_fps` | 2.6 | 2.8 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.6 | 2.8 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.render_fps` | 11.1 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 7/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 422 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 761 | 1004 | ±208 | ahead | 7/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 803 | 1045 | ±208 | ahead | 7/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2896 | 3383 | ±381 | ahead | 7/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3395 | 3904 | ±462 | ahead | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 82 | 94 | ±10 | ahead | 5/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 137 | 149 | ±10 | ahead | 7/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 6.6 | 7.2 | ±0.6 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 239 | 225 | ±12.0 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3 | 6.2 | ±2.7 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 2457 | 6296 | ±2952 | ahead | 9/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1520 | 2673 | ±969 | ahead | 7/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3 | 6.2 | ±2.7 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 2457 | 6296 | ±2952 | ahead | 9/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1520 | 2673 | ±969 | ahead | 7/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.8 | ±2.4 | ahead | 6/10 | #411 (closed, no note: #37, #24) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 7/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 422 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_23_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_low_bandwidth | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.5 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.5 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 39.8 | 47.1 | ±6.4 | ahead | 8/10 | **none** (closed, no note: #21, #7, #29) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1057 | 967 | ±52.8 | ahead | 7/10 | **none** (closed, no note: #37, #61) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24465 | 19722 | ±4494 | behind | 8/10 | #410 (closed, no note: #5, #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 8/10 | #411 (closed, no note: #37, #24) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2580 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 3430 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3805 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `audio.longest_interruption_ms` | 121 | 74.6 | ±34.8 | behind | 5/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 619 | 726 | ±62.6 | ahead | 8/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 143 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9037740000000000 | 70.5 | ±451887000000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.8 | 64.8 | ±3.2 | behind | 9/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 282 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1225 | 1308 | ±65.4 | behind | 9/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 352 | 297 | ±17.6 | behind | 10/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3518 | 1216 | ±192 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2977 | 683 | ±212 | behind | 10/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.1 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 392 | 306 | ±73.9 | behind | 7/10 | **none** |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 850 | 762 | ±82.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 543 | 441 | ±100 | behind | 2/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1591 | 1979 | ±250 | ahead | 9/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1922 | 2311 | ±250 | ahead | 9/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2895 | 561 | ±1774 | behind | 3/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.3 | 8.0 | ±1.1 | ahead | 7/10 | #466 (closed, no note: #37, #24) |
| issue_466_scene_change_1mbps | `sync.av_offset_p95_ms` | 186 | 205 | ±18.3 | ahead | 8/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 9.7 | 7.2 | ±1.5 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `net.loss_pct` | 17.4 | 21.0 | ±2.9 | ahead | 7/10 | #466 (closed, no note: #21, #61, #12) |
| issue_466_undershoot_very_low_capacity | `repair.overhead_pct` | 30.0 | 36.3 | ±6.1 | ahead | 7/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6527 | 7576 | ±787 | ahead | 8/10 | #466 |
| issue_466_undershoot_very_low_capacity | `video.e2e_delay_p95_ms` | 7556 | 9166 | ±1159 | ahead | 8/10 | #466 (closed, no note: #24, #37) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.3 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.6 | 4.4 | ±0.8 | behind | 5/10 | #466 (closed, no note: #23) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.6 | 9.4 | ±1.7 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.time_to_first_frame_ms` | 1095 | 1015 | ±77.0 | behind | 1/10 | #466 (closed, no note: #24, #61) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.6 | 28.9 | ±2.4 | behind | 8/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 885 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_5_burst15_freeze | `audio.longest_interruption_ms` | 188 | 144 | ±41.2 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 422 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| low_bandwidth | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| very_low_capacity | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.render_fps` | 11.1 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.1 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24, #20) |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24, #20) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 422 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 7/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3395 | 3904 | ±462 | ahead | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2896 | 3383 | ±381 | ahead | 7/10 | **none** |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 6.6 | 7.2 | ±0.6 | ahead | 7/10 | **none** (closed, no note: #21, #61, #12) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 239 | 225 | ±12.0 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 422 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 7/10 | **none** |
| issue_23_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.5 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.5 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1057 | 967 | ±52.8 | ahead | 7/10 | **none** (closed, no note: #37, #61) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 39.8 | 47.1 | ±6.4 | ahead | 8/10 | **none** (closed, no note: #21, #7, #29) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2580 | 780 | ±1448 | behind | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 3430 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3805 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.7 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 543 | 441 | ±100 | behind | 2/10 | **none** (closed, no note: #5, #37, #24) |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 850 | 762 | ±82.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 392 | 306 | ±73.9 | behind | 7/10 | **none** |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 422 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 7/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 252 | 226 | ±12.6 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| very_low_capacity | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3746 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.5 | 3.1 | ±1.0 | behind | 8/10 | **none** (closed, no note: #5, #37, #24, #20) |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4602 | 3597 | ±849 | behind | 6/10 | **none** (closed, no note: #5, #37, #24, #20) |

