## Parity with libwebrtc M157.8103@{#0}

137 scenarios, mean of 10 seeds. **78 gaps**: 49 behind libwebrtc, 29 ahead of it; 33 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

78 gaps on `main`, 78 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_123_v2_allocation_min | `video.render_fps` | 10.9 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504, #224 (closed, no note: #63, #476) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2967 | 3383 | ±381 | ahead | 7/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3381 | 3904 | ±462 | ahead | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 139 | 149 | ±10 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 240 | 225 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.4 | 6.2 | ±2.7 | ahead | 6/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 2488 | 6296 | ±2952 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1346 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.freeze_count` | 4.3 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2097 | 1514 | ±402 | behind | 6/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.4 | 6.2 | ±2.7 | ahead | 6/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 2488 | 6296 | ±2952 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1346 | 2673 | ±969 | ahead | 8/10 | #411 (closed, no note: #5, #37, #24) |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 91.7 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 91.7 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1037 | 967 | ±52.5 | ahead | 5/10 | **none** (closed, no note: #37, #61) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.3 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2097 | 1514 | ±402 | behind | 6/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2500 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1272 | 1051 | ±151 | behind | 6/10 | **none** |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1793 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2205 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.7 | 8.3 | ±1.3 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.longest_interruption_ms` | 125 | 74.6 | ±34.8 | behind | 5/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 621 | 726 | ±62.6 | ahead | 9/10 | #504, #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 145 | 272 | ±51.8 | ahead | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.rtt_p50_ms` | 9054030000000000 | 70.5 | ±452701500000000 | behind | 10/10 | #451 (closed, no note: #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.8 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1223 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 348 | 297 | ±17.4 | behind | 9/10 | #451 (closed, no note: #24, #37) |
| issue_451_host_rtt_near_limit | `video.freeze_total_ms` | 3355 | 1216 | ±192 | behind | 9/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #451 (closed, no note: #23) |
| issue_451_host_rtt_near_limit | `video.longest_freeze_ms` | 2968 | 683 | ±212 | behind | 10/10 | #451 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `video.render_fps` | 26.2 | 28.9 | ±1.4 | behind | 9/10 | #451 (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 414 | 306 | ±73.9 | behind | 9/10 | **none** |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 866 | 762 | ±82.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_464_start_rate_above_int_bps | `video.freeze_total_ms` | 697 | 486 | ±208 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 653 | 441 | ±100 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 24.7 | 21 | ±2.1 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.7 | 8.0 | ±1.1 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 6514 | 4941 | ±962 | behind | 6/10 | #466 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.2 | 7.2 | ±1.5 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.9 | 20.7 | ±2.9 | behind | 9/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.longest_freeze_ms` | 3555 | 4711 | ±928 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.8 | 9.4 | ±1.7 | ahead | 9/10 | #466 (closed, no note: #37, #24) |
| issue_477_opus_rtp_clock | `audio.jb_delay_p95_ms` | 85 | 74 | ±10 | behind | 5/10 | #504 (closed, no note: #63, #476) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_533_opus_decoder_frame_older_than_window | `audio.concealed_pct` | 33.6 | 28.9 | ±2.4 | behind | 9/10 | #504 (closed, no note: #63, #476) |
| issue_533_opus_decoder_frame_older_than_window | `audio.jb_delay_p95_ms` | 885 | 996 | ±107 | ahead | 6/10 | #504 (closed, no note: #63, #476) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 314 | 284 | ±29.6 | behind | 5/10 | #504, #224 (closed, no note: #63, #476) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 906 | 1029 | ±90.7 | ahead | 5/10 | #411 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1393 | 1527 | ±96.8 | ahead | 5/10 | #411 (closed, no note: #24, #37) |
| low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| very_low_capacity | `video.render_fps` | 10.9 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.9 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3381 | 3904 | ±462 | ahead | 7/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2967 | 3383 | ±381 | ahead | 7/10 | **none** |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 240 | 225 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 91.7 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 91.7 | 78.6 | ±11.1 | behind | 5/10 | **none** |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1037 | 967 | ±52.5 | ahead | 5/10 | **none** (closed, no note: #37, #61) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 517 | 422 | ±73.2 | behind | 4/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2500 | 790 | ±1445 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2205 | 1602 | ±296 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1793 | 1577 | ±150 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1272 | 1051 | ±151 | behind | 6/10 | **none** |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.7 | 8.3 | ±1.3 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_464_start_rate_above_int_bps | `video.freeze_total_ms` | 697 | 486 | ±208 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 653 | 441 | ±100 | behind | 4/10 | **none** (closed, no note: #5, #37, #24) |
| issue_464_start_rate_above_int_bps | `video.e2e_delay_p95_ms` | 866 | 762 | ±82.2 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_464_start_rate_above_int_bps | `sync.av_offset_p95_ms` | 414 | 306 | ±73.9 | behind | 9/10 | **none** |
| issue_464_start_rate_above_int_bps | `repair.overhead_pct` | 311 | 336 | ±16.8 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 466 | 358 | ±53.9 | behind | 7/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 285 | 181 | ±42.6 | behind | 8/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.4 | behind | 5/10 | **none** (closed, no note: #24, #37) |

