## Parity with libwebrtc M157.8103@{#0}

134 scenarios, mean of 10 seeds. **100 gaps**: 51 behind libwebrtc, 49 ahead of it; 45 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

111 gaps on `main`, 100 here: **19 closed, 8 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `video.freeze_count` | 4.1 | 3.1 | ±1.0 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4547 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.1 | ±2 | ahead | 5/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 199 | 219 | ±19.5 | ahead | 8/10 | #411 (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | **none** (closed, no note: #21, #61, #12) |
| issue_23_handover | `video.freeze_count` | 4.1 | 3.1 | ±1.0 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_23_handover | `video.freeze_total_ms` | 4547 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24520 | 19722 | ±4494 | behind | 7/10 | #410 (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.queue_p95_ms` | 62.5 | 46.3 | ±10 | behind | 6/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 7/10 | #411 (closed, no note: #21, #61, #12) |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | **none** (closed, no note: #21, #61, #12) |
| issue_76_rtx_v2_handover | `video.freeze_count` | 4.1 | 3.1 | ±1.0 | behind | 6/10 | **none** (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4547 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| wifi_to_lte_handover | `video.freeze_count` | 4.1 | 3.1 | ±1.0 | behind | 6/10 | **none** (closed, no note: #5, #37, #24, #20) |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4547 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24, #20) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 43.7 | 48.4 | ±3.2 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 136 | 149 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 81 | 94 | ±10 | ahead | 5/10 | #504 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 432 | 671 | ±226 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 432 | 671 | ±226 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 41.1 | 48.8 | ±6.7 | ahead | 7/10 | **none** (closed, no note: #21, #7, #29) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2440 | 780 | ±1448 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2840 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 107 | 86.2 (closer) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 114 | 90.5 (closer) |
| issue_204_cross_traffic_join | `video.freeze_count` | 1.9 | 1.5 (closer) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 717 | 636 (closer) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 107 | 86.2 (closer) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 114 | 90.5 (closer) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 62.4 | 82.3 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 1.9 | 1.5 (closer) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 717 | 636 (closer) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 296 | 343 (further) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 107 | 86.2 (closer) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 114 | 90.5 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 1250 | 1040 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 152 | 164 | ±10.5 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_120_rtx_payload_padding | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.3 | 49.8 | ±9.4 | ahead | 9/10 | #411 (closed, no note: #21, #7, #29) |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3722 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.render_fps` | 11.2 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 43.7 | 48.4 | ±3.2 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 611 | 1004 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 652 | 1045 | ±208 | ahead | 9/10 | #504, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.6 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 55.6 | 79.5 | ±17.5 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2627 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2816 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4 | 2.3 | ±1.6 | behind | 7/10 | **none** (closed, no note: #23) |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 4373 | 2925 | ±1123 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 81 | 94 | ±10 | ahead | 5/10 | #504 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 136 | 149 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 240 | 225 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 432 | 671 | ±226 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 485 | 724 | ±225 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.freeze_count` | 4.2 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2151 | 1514 | ±402 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 432 | 671 | ±226 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 485 | 724 | ±225 | ahead | 8/10 | #504 (closed, no note: #63, #476) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_23_handover | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 247 | 207 | ±34.0 | behind | 4/10 | #504 (closed, no note: #63, #476) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 410 | 337 | ±53.5 | behind | 6/10 | #504 (closed, no note: #63, #476) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 127 | 93.9 | ±20.8 | behind | 6/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 160 | 119 | ±37.2 | behind | 4/10 | **none** |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 41.1 | 48.8 | ±6.7 | ahead | 7/10 | **none** (closed, no note: #21, #7, #29) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1050 | 967 | ±52.5 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.7 | 25.7 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.2 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2151 | 1514 | ±402 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2440 | 780 | ±1448 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2840 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `net.loss_pct` | 7.9 | 7.2 | ±0.7 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1946 | 1602 | ±296 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 34.3 | 49.8 | ±9.4 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3731 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.8 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.longest_interruption_ms` | 125 | 74.6 | ±34.8 | behind | 5/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 619 | 726 | ±62.6 | ahead | 9/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 145 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9034580000000000 | 70.5 | ±451729000000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.7 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1222 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 349 | 297 | ±17.4 | behind | 9/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3355 | 1216 | ±192 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2968 | 683 | ±212 | behind | 10/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.2 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1618 | 1979 | ±250 | ahead | 7/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1950 | 2311 | ±250 | ahead | 7/10 | #466, #504 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `repair.overhead_pct` | 34.4 | 50.3 | ±8.9 | ahead | 9/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 23.8 | 21 | ±2.1 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2397 | 561 | ±1774 | behind | 3/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.1 | 8.0 | ±1.1 | ahead | 6/10 | #466 (closed, no note: #37, #24) |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 28.4 | 39.8 | ±7.6 | ahead | 9/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5953 | 4941 | ±962 | behind | 4/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.2 | 7.2 | ±1.5 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `net.loss_pct` | 18.0 | 21.0 | ±2.9 | ahead | 7/10 | #466 (closed, no note: #21, #61, #12) |
| issue_466_undershoot_very_low_capacity | `repair.overhead_pct` | 31.2 | 57.2 | ±9.7 | ahead | 10/10 | #466 (closed, no note: #21, #7, #29) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6787 | 7576 | ±787 | ahead | 7/10 | #466 |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.9 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.5 | 4.4 | ±0.8 | behind | 5/10 | #466 (closed, no note: #23) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 12.2 | 9.4 | ±1.7 | ahead | 10/10 | #466 (closed, no note: #37, #24) |
| issue_476_libopus_decode | `audio.mouth_to_ear_p95_ms` | 152 | 164 | ±10.5 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 152 | 164 | ±10.5 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 152 | 164 | ±10.5 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_76_rtx_v2_handover | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_85_video_pause | `cc.max_settle_ms` | 4350 | 5390 | ±952 | ahead | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| very_low_capacity | `repair.overhead_pct` | 34.3 | 49.8 | ±9.4 | ahead | 9/10 | #411 (closed, no note: #21, #7, #29) |
| very_low_capacity | `video.longest_freeze_ms` | 3722 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.render_fps` | 11.2 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.3 | 49.8 | ±9.4 | ahead | 9/10 | #411 (closed, no note: #21, #7, #29) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3722 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.2 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| wifi_to_lte_handover | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12, #20) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_120_rtx_payload_padding | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 3722 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 43.7 | 48.4 | ±3.2 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 4373 | 2925 | ±1123 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2816 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4 | 2.3 | ±1.6 | behind | 7/10 | **none** (closed, no note: #23) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2627 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.6 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 55.6 | 79.5 | ±17.5 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 240 | 225 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_23_handover | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 160 | 119 | ±37.2 | behind | 4/10 | **none** |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 127 | 93.9 | ±20.8 | behind | 6/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.5 | 6.7 | ±1.8 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1050 | 967 | ±52.5 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 41.1 | 48.8 | ±6.7 | ahead | 7/10 | **none** (closed, no note: #21, #7, #29) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2440 | 780 | ±1448 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2840 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1946 | 1602 | ±296 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `net.loss_pct` | 7.9 | 7.2 | ±0.7 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_437_render_smoothing_very_low_capacity | `video.longest_freeze_ms` | 3731 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.8 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 34.3 | 49.8 | ±9.4 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_76_rtx_v2_handover | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_85_video_pause | `cc.max_settle_ms` | 4350 | 5390 | ±952 | ahead | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| very_low_capacity | `video.longest_freeze_ms` | 3722 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3722 | 4431 | ±465 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| wifi_to_lte_handover | `net.loss_pct` | 5.3 | 4.7 | ±0.5 | behind | 6/10 | **none** (closed, no note: #21, #61, #12, #20) |

