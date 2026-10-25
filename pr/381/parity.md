## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **108 gaps**: 56 behind libwebrtc, 52 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

81 gaps on `main`, 108 here: **6 closed, 33 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_85_video_pause | `cc.max_settle_ms` | 4370 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 198 | 236 | ±20.0 | ahead | 10/10 | #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 54.2 | 81.5 | ±19.1 | ahead | 9/10 | #77 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1290 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.render_fps` | 17.8 | 19.9 | ±2.0 | behind | 7/10 | #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.7 | 2.7 | ±0.9 | behind | 8/10 | #23 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 107 | 118 | ±10 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.6 | 3.5 | ±2.8 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 121 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 105 | 236 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 149 | 139 | ±10 | behind | 3/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.4 | 43.8 | ±4.1 | ahead | 8/10 | #21, #7, #29 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1290 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 57.1 | 45.2 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 248 | 206 | ±35.5 | behind | 7/10 | #63 |
| issue_256_audio_24khz | `audio.jb_delay_p95_ms` | 150 | 119 | ±28.1 | behind | 6/10 | #63 |
| issue_256_audio_24khz | `cc.queue_p95_ms` | 51.1 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_298_audio_24khz_plays | `audio.mouth_to_ear_p95_ms` | 248 | 206 | ±35.5 | behind | 7/10 | #63 |
| issue_298_audio_24khz_plays | `audio.jb_delay_p95_ms` | 150 | 119 | ±28.1 | behind | 6/10 | #63 |
| issue_298_audio_24khz_plays | `cc.queue_p95_ms` | 51.1 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 622 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 0.8 | 0.1 | ±0.5 | behind | 5/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.longest_interruption_ms` | 342 | 77.7 | ±240 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 0.7 | 0 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_348_scaling_outage_500ms | `cc.queue_p95_ms` | 57.1 | 45.2 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_372_outage_interruption_count | `cc.queue_p95_ms` | 66.7 | 51.3 | ±10.9 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 302 | 286 | ±15.4 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1290 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 2420 | 1932 | ±486 | behind | 7/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.0 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_77_av_sync_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| satellite | `video.freeze_count` | 31.8 | 28 | ±3.2 | behind | 7/10 | #5, #37, #24 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.6 | 2.1 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.9 | 4.8 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 787 | 950 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 11.1 | 17.2 (further) |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 790 | 980 (further) |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1 | 0.7 (closer) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1 | 0.7 (closer) |
| issue_257_zero_max_payload | `audio.concealed_pct` | 0.7 | 0.8 (further) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 22.8 | 31.1 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.1 | 1.5 (further) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 1.7 | 1.3 (closer) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 3721 | 3136 (closer) |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 3.2 | 3.6 (further) |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 706 | 877 (further) |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 277 | 399 (further) |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 123 | 281 (further) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 3170 | 4420 (further) |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 198 | 229 (further) |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 0.9 | 1 (further) |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 10.2 | 11.8 (further) |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 439 | 489 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 3.4 | 3.0 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 151 | 167 | ±10 | ahead | 8/10 | #63 |
| clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.7 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8326 | 6028 | ±1493 | behind | 8/10 | #77 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9389 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 8/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 36.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1290 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.7 | 2.7 | ±0.9 | behind | 8/10 | #23 |
| issue_12_lossbwe_policer | `video.render_fps` | 17.8 | 19.9 | ±2.0 | behind | 7/10 | #37, #24 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 107 | 118 | ±10 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 205 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.2 | 7 | ±3.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 451 | 1401 | ±595 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 81.0 | 117 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 188 | 214 | ±10.7 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.6 | 3.5 | ±2.8 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 121 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 105 | 236 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.4 | 43.8 | ±4.1 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 149 | 139 | ±10 | behind | 3/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3290 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.9 | 6.8 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.9 | 6.8 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #23 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1290 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 57.1 | 45.2 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_256_audio_24khz | `audio.jb_delay_p95_ms` | 150 | 119 | ±28.1 | behind | 6/10 | #63 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 248 | 206 | ±35.5 | behind | 7/10 | #63 |
| issue_256_audio_24khz | `cc.queue_p95_ms` | 51.1 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 375 | 125 | ±18.8 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 60.1 | 29 | ±20 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 648 | 205 | ±32.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 262 | 43.5 | ±13.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.3 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1127 | 57.0 | ±56.4 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1584 | 2002 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1464 | 122 | ±73.2 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.7 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3452 | 0 | ±173 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.5 | 0 | ±0.5 | behind | 9/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1291 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 22.0 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_298_audio_24khz_plays | `audio.jb_delay_p95_ms` | 150 | 119 | ±28.1 | behind | 6/10 | #63 |
| issue_298_audio_24khz_plays | `audio.mouth_to_ear_p95_ms` | 248 | 206 | ±35.5 | behind | 7/10 | #63 |
| issue_298_audio_24khz_plays | `cc.queue_p95_ms` | 51.1 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.7 | 7.4 | ±1.3 | behind | 8/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24102 | 20966 | ±2845 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 622 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.3 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.7 | 25.5 | ±1.4 | ahead | 10/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 4.1 | 0.5 | ±1.5 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 0.8 | 0.1 | ±0.5 | behind | 5/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1205 | 806 | ±225 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.longest_interruption_ms` | 342 | 77.7 | ±240 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1856 | 979 | ±426 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 1870 | 9440 | ±3638 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 616 | 127 | ±202 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 39.8 | 51.6 | ±8.0 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 701 | 930 | ±156 | behind | 8/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2392 | 1441 | ±394 | behind | 9/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 1.1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 508 | 226 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 0.7 | 0 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 4420 | 0 | ±221 | behind | 7/10 | #5, #37, #24 |
| issue_348_scaling_outage_500ms | `cc.queue_p95_ms` | 57.1 | 45.2 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_372_outage_interruption_count | `cc.queue_p95_ms` | 66.7 | 51.3 | ±10.9 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 302 | 286 | ±15.4 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1290 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 151 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 151 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.2 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.6 | 32.6 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 193 | 218 | ±23.6 | ahead | 5/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 2420 | 1932 | ±486 | behind | 7/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.0 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_77_av_sync_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 128 | 142 | ±12.1 | ahead | 5/10 | #24, #37 |
| satellite | `video.freeze_count` | 31.8 | 28 | ±3.2 | behind | 7/10 | #5, #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 34.7 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8326 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9389 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.7 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8326 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9389 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 8/10 | #37, #24 |

