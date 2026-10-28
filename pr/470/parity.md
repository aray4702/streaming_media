## Parity with libwebrtc M157.8103@{#0}

130 scenarios, mean of 10 seeds. **87 gaps**: 39 behind libwebrtc, 48 ahead of it; 5 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

73 gaps on `main`, 87 here: **0 closed, 14 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1919 | 2311 | ±250 | ahead | 8/10 | #466, #63 |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1587 | 1979 | ±250 | ahead | 8/10 | #466, #63 |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 25.1 | 21 | ±2.1 | behind | 6/10 | #466, #5, #37, #24 |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2406 | 561 | ±1774 | behind | 4/10 | #466, #5, #37, #24 |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.6 | 8.0 | ±1.1 | ahead | 9/10 | #466, #37, #24 |
| issue_466_overshoot_very_low_capacity | `repair.overhead_pct` | 31.6 | 50.3 | ±8.9 | ahead | 10/10 | #466, #21, #7, #29 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.0 | 7.2 | ±1.5 | ahead | 8/10 | #466, #37, #24 |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 27.1 | 39.8 | ±7.6 | ahead | 8/10 | #466, #21, #7, #29 |
| issue_466_undershoot_very_low_capacity | `video.freeze_total_ms` | 25464 | 30000 | ±3003 | ahead | 9/10 | #466, #5, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.pause_count` | 1 | 0.2 | ±0.5 | behind | 7/10 | #466, #5, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 5564 | 1245 | ±2661 | behind | 7/10 | #466, #5, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.5 | 9.4 | ±1.7 | ahead | 10/10 | #466, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 6.4 | 4.4 | ±0.8 | behind | 8/10 | #466, #23 |
| issue_466_undershoot_very_low_capacity | `repair.overhead_pct` | 32.1 | 57.2 | ±9.7 | ahead | 10/10 | #466, #21, #7, #29 |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 153 | 164 | ±10.5 | ahead | 6/10 | #63 |
| cross_traffic | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24, #411 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #19, #411 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4672 | 3597 | ±849 | behind | 7/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #21, #7, #29, #411 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #37, #24, #411 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 296 | 181 | ±42.6 | behind | 9/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 465 | 358 | ±53.9 | behind | 8/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 610 | 1004 | ±208 | ahead | 9/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 651 | 1045 | ±208 | ahead | 9/10 | #63, #217 |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.6 | 12.7 | ±2.0 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 55.6 | 79.5 | ±17.5 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2628 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2816 | 3904 | ±462 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4 | 2.3 | ±1.6 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 4373 | 2925 | ±1123 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 199 | 219 | ±19.5 | ahead | 8/10 | #24, #37, #411 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.3 | 45.2 | ±5.3 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 349 | ±113 | ahead | 6/10 | #24, #61, #411 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 239 | 225 | ±11.9 | behind | 5/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 296 | 181 | ±42.6 | behind | 9/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 465 | 358 | ±53.9 | behind | 8/10 | #24, #37 |
| issue_224_congestive_e2e_flip | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 4672 | 3597 | ±849 | behind | 7/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #37, #24, #411 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 250 | 207 | ±34.0 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 413 | 337 | ±53.5 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 127 | 93.9 | ±20.8 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 161 | 119 | ±37.2 | behind | 5/10 | **none** |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.7 | 25.7 | ±1.4 | ahead | 7/10 | #37, #24, #411 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| issue_372_outage_interruption_count | `cc.queue_p95_ms` | 62.5 | 46.3 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 7/10 | #21, #61, #12, #411 |
| issue_415_cross_80pct_standing_queue | `net.loss_pct` | 7.9 | 7.2 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1898 | 1602 | ±296 | behind | 7/10 | #5, #37, #24 |
| issue_437_render_smoothing_very_low_capacity | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #21, #7, #29 |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 10.7 | 8.3 | ±1.3 | ahead | 8/10 | #37, #24 |
| issue_457_libopus_audio_rate | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1587 | 1979 | ±250 | ahead | 8/10 | #466, #63 |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1919 | 2311 | ±250 | ahead | 8/10 | #466, #63 |
| issue_466_overshoot_very_low_capacity | `repair.overhead_pct` | 31.6 | 50.3 | ±8.9 | ahead | 10/10 | #466, #21, #7, #29 |
| issue_466_overshoot_very_low_capacity | `video.freeze_count` | 25.1 | 21 | ±2.1 | behind | 6/10 | #466, #5, #37, #24 |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2406 | 561 | ±1774 | behind | 4/10 | #466, #5, #37, #24 |
| issue_466_overshoot_very_low_capacity | `video.render_fps` | 9.6 | 8.0 | ±1.1 | ahead | 9/10 | #466, #37, #24 |
| issue_466_scene_change_very_low_capacity | `repair.overhead_pct` | 27.1 | 39.8 | ±7.6 | ahead | 8/10 | #466, #21, #7, #29 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 10.0 | 7.2 | ±1.5 | ahead | 8/10 | #466, #37, #24 |
| issue_466_undershoot_very_low_capacity | `repair.overhead_pct` | 32.1 | 57.2 | ±9.7 | ahead | 10/10 | #466, #21, #7, #29 |
| issue_466_undershoot_very_low_capacity | `video.freeze_total_ms` | 25464 | 30000 | ±3003 | ahead | 9/10 | #466, #5, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 6.4 | 4.4 | ±0.8 | behind | 8/10 | #466, #23 |
| issue_466_undershoot_very_low_capacity | `video.pause_count` | 1 | 0.2 | ±0.5 | behind | 7/10 | #466, #5, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 5564 | 1245 | ±2661 | behind | 7/10 | #466, #5, #37, #24 |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.5 | 9.4 | ±1.7 | ahead | 10/10 | #466, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #37, #24, #411 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.2 | 3.4 | ±0.7 | behind | 5/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 296 | 181 | ±42.6 | behind | 9/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 465 | 358 | ±53.9 | behind | 8/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1554 | 1125 | ±364 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 153 | 164 | ±10.5 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 153 | 164 | ±10.5 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4672 | 3597 | ±849 | behind | 7/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 453 | ±166 | ahead | 8/10 | #24, #61, #411 |
| issue_85_video_pause | `cc.max_settle_ms` | 4140 | 5390 | ±952 | ahead | 4/10 | #15, #48, #61, #20 |
| low_bandwidth | `video.render_fps` | 27.8 | 25.9 | ±1.4 | ahead | 7/10 | #37, #24, #411 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 226 | ±12.5 | behind | 4/10 | #24, #37 |
| very_low_capacity | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #21, #7, #29, #411 |
| very_low_capacity | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #37, #24, #411 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 29.7 | 49.8 | ±9.4 | ahead | 10/10 | #21, #7, #29, #411 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.7 | 8.3 | ±1.3 | ahead | 9/10 | #37, #24, #411 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4672 | 3597 | ±849 | behind | 7/10 | #5, #37, #24, #20 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 296 | 181 | ±42.6 | behind | 9/10 | **none** |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2628 | 3383 | ±381 | ahead | 8/10 | **none** |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 296 | 181 | ±42.6 | behind | 9/10 | **none** |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 161 | 119 | ±37.2 | behind | 5/10 | **none** |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 296 | 181 | ±42.6 | behind | 9/10 | **none** |

