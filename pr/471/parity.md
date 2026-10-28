## Parity with libwebrtc M157.8103@{#0}

126 scenarios, mean of 10 seeds. **85 gaps**: 38 behind libwebrtc, 47 ahead of it; 41 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

80 gaps on `main`, 85 here: **13 closed, 18 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4673 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 199 | 219 | ±19.5 | ahead | 8/10 | #411 (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | **none** (closed, no note: #21, #61, #12) |
| issue_23_handover | `video.freeze_total_ms` | 4673 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_372_outage_interruption_count | `cc.queue_p95_ms` | 62.5 | 46.3 | ±10 | behind | 6/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 7/10 | #411 (closed, no note: #21, #61, #12) |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | **none** (closed, no note: #21, #61, #12) |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4673 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24) |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4673 | 3597 | ±849 | behind | 7/10 | **none** (closed, no note: #5, #37, #24, #20) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 44.5 | 48.4 | ±3.2 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 149 | ±10 | ahead | 6/10 | #477 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 94 | ±10 | ahead | 5/10 | #477 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 248 | 232 | ±12.4 | behind | 6/10 | #477 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 488 | 724 | ±225 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 435 | 671 | ±226 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 264 | 223 | ±39.8 | behind | 5/10 | **none** |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 488 | 724 | ±225 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 435 | 671 | ±226 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_29_ulpfec_over_red | `audio.mouth_to_ear_p95_ms` | 180 | 207 | ±25.9 | ahead | 7/10 | #477 (closed, no note: #63, #476) |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 97 | 124 | ±24.6 | ahead | 7/10 | #477 (closed, no note: #63, #476) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1036 | 967 | ±52.5 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 264 | 223 | ±39.8 | behind | 5/10 | **none** |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2440 | 780 | ±1448 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2840 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| satellite | `audio.jb_delay_p95_ms` | 206 | 219 | ±11.0 | ahead | 6/10 | #477 (closed, no note: #63, #476) |
| satellite | `video.freeze_count` | 31.7 | 28.3 | ±3.3 | behind | 6/10 | #420 (closed, no note: #5, #37, #24) |
| satellite | `video.freeze_total_ms` | 13884 | 12284 | ±1547 | behind | 8/10 | #420 (closed, no note: #5, #37, #24) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 107 | 86.2 (closer) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 113 | 90.1 (closer) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 11 | 16.5 (further) |
| issue_204_cross_traffic_join | `video.freeze_count` | 1.9 | 1.5 (closer) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 717 | 636 (closer) |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 107 | 86.2 (closer) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 113 | 90.1 (closer) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 1.8 | 2.0 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 1.9 | 1.5 (closer) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 717 | 636 (closer) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 296 | 343 (further) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 107 | 86.2 (closer) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 114 | 90.1 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 1250 | 1040 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #411 (closed, no note: #23, #19, #24) |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #411 (closed, no note: #24, #61, #19) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #411 (closed, no note: #21, #7, #29) |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 44.5 | 48.4 | ±3.2 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #411 (closed, no note: #23) |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #411 (closed, no note: #24, #61) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 611 | 1004 | ±208 | ahead | 9/10 | #477, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 652 | 1045 | ±208 | ahead | 9/10 | #477, #217 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.6 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 55.6 | 79.5 | ±17.5 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2627 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2816 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4 | 2.3 | ±1.6 | behind | 7/10 | **none** (closed, no note: #23) |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 4373 | 2925 | ±1123 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 94 | ±10 | ahead | 5/10 | #477 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 149 | ±10 | ahead | 6/10 | #477 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | **none** (closed, no note: #23) |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | **none** (closed, no note: #24, #61) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 128 | 112 | ±10 | behind | 6/10 | #477 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 248 | 232 | ±12.4 | behind | 6/10 | #477 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.8 | 45.2 | ±5.3 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 349 | ±113 | ahead | 6/10 | #411 (closed, no note: #24, #61) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 239 | 224 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 435 | 671 | ±226 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 488 | 724 | ±225 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #411 (closed, no note: #23) |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #411 (closed, no note: #24, #61) |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 264 | 223 | ±39.8 | behind | 5/10 | **none** |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.freeze_count` | 4.2 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 2151 | 1514 | ±402 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 435 | 671 | ±226 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 488 | 724 | ±225 | ahead | 8/10 | #477 (closed, no note: #63, #476) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #411 (closed, no note: #23) |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #411 (closed, no note: #24, #61) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 248 | 207 | ±34.0 | behind | 5/10 | #477 (closed, no note: #63, #476) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 411 | 337 | ±53.5 | behind | 6/10 | #477 (closed, no note: #63, #476) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 127 | 93.9 | ±20.8 | behind | 6/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 159 | 119 | ±37.2 | behind | 5/10 | **none** |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 97 | 124 | ±24.6 | ahead | 7/10 | #477 (closed, no note: #63, #476) |
| issue_29_ulpfec_over_red | `audio.mouth_to_ear_p95_ms` | 180 | 207 | ±25.9 | ahead | 7/10 | #477 (closed, no note: #63, #476) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1036 | 967 | ±52.5 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.7 | 25.7 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 264 | 223 | ±39.8 | behind | 5/10 | **none** |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.2 | 2.7 | ±1.1 | behind | 5/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2151 | 1514 | ±402 | behind | 4/10 | #408 (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2440 | 780 | ±1448 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2840 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `net.loss_pct` | 7.9 | 7.2 | ±0.7 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1946 | 1602 | ±296 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.7 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #411 (closed, no note: #23) |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #411 (closed, no note: #24, #61) |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #411 (closed, no note: #23) |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #411 (closed, no note: #24, #61) |
| issue_85_video_pause | `cc.max_settle_ms` | 4350 | 5390 | ±952 | ahead | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #411 (closed, no note: #37, #24) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| satellite | `audio.jb_delay_p95_ms` | 206 | 219 | ±11.0 | ahead | 6/10 | #477 (closed, no note: #63, #476) |
| satellite | `video.freeze_count` | 31.7 | 28.3 | ±3.3 | behind | 6/10 | #420 (closed, no note: #5, #37, #24) |
| satellite | `video.freeze_total_ms` | 13884 | 12284 | ±1547 | behind | 8/10 | #420 (closed, no note: #5, #37, #24) |
| very_low_capacity | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #411 (closed, no note: #21, #7, #29) |
| very_low_capacity | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #411 (closed, no note: #21, #7, #29) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #411 (closed, no note: #37, #24) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 44.5 | 48.4 | ±3.2 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 4373 | 2925 | ±1123 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2816 | 3904 | ±462 | ahead | 9/10 | **none** (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4 | 2.3 | ±1.6 | behind | 7/10 | **none** (closed, no note: #23) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2627 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.6 | 12.7 | ±2.0 | ahead | 9/10 | **none** (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 55.6 | 79.5 | ±17.5 | ahead | 9/10 | **none** (closed, no note: #21, #7, #29) |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | **none** (closed, no note: #24, #61) |
| issue_150_gcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | **none** (closed, no note: #23) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.8 | 45.2 | ±5.3 | ahead | 6/10 | **none** (closed, no note: #21, #7, #29) |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 239 | 224 | ±12.0 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 264 | 223 | ±39.8 | behind | 5/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 159 | 119 | ±37.2 | behind | 5/10 | **none** |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 127 | 93.9 | ±20.8 | behind | 6/10 | **none** (closed, no note: #19, #15, #48, #12, #61) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 6.7 | ±1.8 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1036 | 967 | ±52.5 | ahead | 6/10 | **none** (closed, no note: #37, #61) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 580 | 0 | ±100 | behind | 1/10 | **none** (closed, no note: #5, #37, #24) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 533 | 422 | ±73.2 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 264 | 223 | ±39.8 | behind | 5/10 | **none** |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_372_outage_interruption_count | `cc.max_settle_ms` | 2440 | 780 | ±1448 | behind | 5/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_411_reconfigure_clears_cwnd_drop | `cc.max_settle_ms` | 2840 | 790 | ±1445 | behind | 6/10 | **none** (closed, no note: #15, #48, #61, #20) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1946 | 1602 | ±296 | behind | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `net.loss_pct` | 7.9 | 7.2 | ±0.7 | behind | 6/10 | **none** (closed, no note: #21, #61, #12) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.7 | 8.3 | ±1.3 | ahead | 8/10 | **none** (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | **none** (closed, no note: #21, #7, #29) |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | **none** (closed, no note: #37, #24) |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 444 | 358 | ±53.9 | behind | 6/10 | **none** (closed, no note: #24, #37) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 271 | 181 | ±42.6 | behind | 6/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |
| issue_85_video_pause | `cc.max_settle_ms` | 4350 | 5390 | ±952 | ahead | 4/10 | **none** (closed, no note: #15, #48, #61, #20) |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 249 | 226 | ±12.5 | behind | 5/10 | **none** (closed, no note: #24, #37) |

