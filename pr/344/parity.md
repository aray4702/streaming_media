## Parity with libwebrtc M157.8103@{#0}

112 scenarios, mean of 10 seeds. **204 gaps**: 99 behind libwebrtc, 105 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

103 gaps on `main`, 204 here: **0 closed, 101 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_334_scaling_capacity_drop_seed1 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `audio.longest_interruption_ms` | 120 | 190 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `audio.jb_delay_p95_ms` | 530 | 500 | ±26.5 | behind | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `video.freeze_count` | 3 | 2 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed1 | `video.longest_freeze_ms` | 1464 | 3031 | ±152 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed1 | `video.e2e_delay_p95_ms` | 599 | 345 | ±30.0 | behind | 1/1 | #24, #37 |
| issue_334_scaling_capacity_drop_seed1 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_capacity_drop_seed1 | `sync.av_offset_p95_ms` | 179 | 333 | ±16.7 | ahead | 1/1 | #77 |
| issue_334_scaling_capacity_drop_seed2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed2 | `audio.longest_interruption_ms` | 130 | 200 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed2 | `video.freeze_count` | 4 | 1 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed2 | `video.longest_freeze_ms` | 1393 | 3103 | ±155 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed2 | `video.e2e_delay_p95_ms` | 638 | 350 | ±31.9 | behind | 1/1 | #24, #37 |
| issue_334_scaling_capacity_drop_seed2 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_capacity_drop_seed2 | `sync.av_offset_p95_ms` | 158 | 336 | ±16.8 | ahead | 1/1 | #77 |
| issue_334_scaling_capacity_drop_seed3 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed3 | `audio.longest_interruption_ms` | 120 | 210 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed3 | `audio.jb_delay_p95_ms` | 444 | 470 | ±23.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed3 | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed3 | `video.longest_freeze_ms` | 3168 | 2859 | ±158 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed3 | `video.e2e_delay_p95_ms` | 334 | 537 | ±26.8 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_capacity_drop_seed3 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 1/1 | #23 |
| issue_334_scaling_capacity_drop_seed3 | `sync.av_offset_p95_ms` | 260 | 173 | ±13.0 | behind | 1/1 | #77 |
| issue_334_scaling_cross_join_seed1 | `audio.longest_interruption_ms` | 30 | 80 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_cross_join_seed1 | `video.freeze_count` | 5 | 2 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed1 | `video.freeze_total_ms` | 2193 | 1338 | ±110 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed1 | `video.longest_freeze_ms` | 736 | 1127 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed1 | `video.e2e_delay_p95_ms` | 560 | 491 | ±28 | behind | 1/1 | #24, #37 |
| issue_334_scaling_cross_join_seed1 | `video.avg_kbps` | 1304 | 1423 | ±71.2 | behind | 1/1 | #37, #61 |
| issue_334_scaling_cross_join_seed1 | `cc.utilization_pct` | 55.1 | 59.8 | ±3.0 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_join_seed1 | `cc.max_settle_ms` | 12000 | 6900 | ±600 | behind | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_join_seed1 | `cc.rtt_p50_ms` | 85.1 | 104 | ±10 | ahead | 1/1 | #61 |
| issue_334_scaling_cross_join_seed2 | `audio.longest_interruption_ms` | 130 | 40 | ±20 | behind | 1/1 | #63 |
| issue_334_scaling_cross_join_seed2 | `audio.mouth_to_ear_p95_ms` | 656 | 722 | ±36.1 | ahead | 1/1 | #63 |
| issue_334_scaling_cross_join_seed2 | `audio.jb_delay_p95_ms` | 360 | 410 | ±20.5 | ahead | 1/1 | #63 |
| issue_334_scaling_cross_join_seed2 | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed2 | `video.freeze_total_ms` | 1297 | 1153 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed2 | `video.longest_freeze_ms` | 1297 | 728 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed2 | `video.e2e_delay_p95_ms` | 438 | 343 | ±21.9 | behind | 1/1 | #24, #37 |
| issue_334_scaling_cross_join_seed2 | `sync.av_offset_p95_ms` | 186 | 212 | ±10.6 | ahead | 1/1 | #77 |
| issue_334_scaling_cross_join_seed2 | `cc.queue_p95_ms` | 247 | 261 | ±13.1 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_join_seed2 | `cc.max_settle_ms` | 12800 | 11700 | ±640 | behind | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_join_seed3 | `video.freeze_total_ms` | 2181 | 1821 | ±109 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed3 | `video.longest_freeze_ms` | 911 | 709 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed3 | `video.e2e_delay_p95_ms` | 512 | 546 | ±27.3 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_cross_join_seed3 | `cc.queue_p95_ms` | 288 | 272 | ±14.4 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_join_seed3 | `cc.max_settle_ms` | 15000 | 10200 | ±750 | behind | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_traffic_seed1 | `audio.mouth_to_ear_p95_ms` | 778 | 566 | ±38.9 | behind | 1/1 | #63 |
| issue_334_scaling_cross_traffic_seed1 | `audio.jb_delay_p95_ms` | 450 | 330 | ±22.5 | behind | 1/1 | #63 |
| issue_334_scaling_cross_traffic_seed1 | `video.freeze_count` | 14 | 0 | ±0.7 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_traffic_seed1 | `video.freeze_total_ms` | 6634 | 0 | ±332 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_traffic_seed1 | `video.longest_freeze_ms` | 982 | 18 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_traffic_seed1 | `video.e2e_delay_p95_ms` | 1008 | 249 | ±50.4 | behind | 1/1 | #24, #37 |
| issue_334_scaling_cross_traffic_seed1 | `sync.av_offset_p95_ms` | 409 | 292 | ±20.5 | behind | 1/1 | #77 |
| issue_334_scaling_cross_traffic_seed1 | `cc.queue_p95_ms` | 295 | 201 | ±14.7 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_traffic_seed1 | `cc.max_settle_ms` | 1300 | 2800 | ±500 | ahead | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_traffic_seed1 | `net.loss_pct` | 1.6 | 0 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_334_scaling_cross_traffic_seed1 | `repair.overhead_pct` | 2.7 | 0.0 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_handover_seed1 | `audio.mouth_to_ear_p95_ms` | 385 | 464 | ±23.2 | ahead | 1/1 | #63 |
| issue_334_scaling_handover_seed1 | `audio.jb_delay_p95_ms` | 260 | 300 | ±15 | ahead | 1/1 | #63 |
| issue_334_scaling_handover_seed1 | `video.freeze_total_ms` | 3766 | 4192 | ±210 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_handover_seed1 | `video.e2e_delay_p95_ms` | 1229 | 1381 | ±69.1 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_handover_seed1 | `video.time_to_first_frame_ms` | 175 | 277 | ±50 | ahead | 1/1 | #24, #61 |
| issue_334_scaling_handover_seed1 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_handover_seed1 | `sync.av_offset_p95_ms` | 900 | 1009 | ±50.5 | ahead | 1/1 | #77 |
| issue_334_scaling_handover_seed1 | `cc.queue_p95_ms` | 42.8 | 85.7 | ±10 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_handover_seed1 | `repair.overhead_pct` | 24.5 | 28.4 | ±2 | ahead | 1/1 | #21, #7, #29 |
| issue_334_scaling_high_jitter_seed1 | `audio.mouth_to_ear_p95_ms` | 270 | 219 | ±13.5 | behind | 1/1 | #63 |
| issue_334_scaling_high_jitter_seed1 | `audio.jb_delay_p95_ms` | 150 | 100 | ±10 | behind | 1/1 | #63 |
| issue_334_scaling_high_jitter_seed1 | `video.e2e_delay_p95_ms` | 215 | 263 | ±13.1 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_high_jitter_seed1 | `video.time_to_first_frame_ms` | 200 | 665 | ±50 | ahead | 1/1 | #24, #61 |
| issue_334_scaling_high_jitter_seed1 | `video.keyframe_requests` | 0 | 2 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_high_jitter_seed1 | `sync.av_offset_p95_ms` | 85 | 126 | ±10 | ahead | 1/1 | #77 |
| issue_334_scaling_high_jitter_seed1 | `repair.overhead_pct` | 36.6 | 41.6 | ±2.1 | ahead | 1/1 | #21, #7, #29 |
| issue_334_scaling_outage_8s_seed1 | `video.e2e_delay_p95_ms` | 257 | 294 | ±14.7 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_outage_8s_seed1 | `video.avg_kbps` | 1897 | 2054 | ±103 | behind | 1/1 | #37, #61 |
| issue_334_scaling_outage_8s_seed1 | `cc.utilization_pct` | 92.0 | 100 | ±5.0 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_outage_8s_seed1 | `net.loss_pct` | 9.2 | 8.0 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_334_scaling_outage_8s_seed1 | `repair.overhead_pct` | 7.7 | 5.0 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_outage_8s_seed2 | `video.freeze_total_ms` | 301 | 190 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed2 | `video.longest_freeze_ms` | 301 | 190 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed2 | `video.e2e_delay_p95_ms` | 361 | 274 | ±18.1 | behind | 1/1 | #24, #37 |
| issue_334_scaling_outage_8s_seed2 | `cc.queue_p95_ms` | 213 | 183 | ±10.7 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_outage_8s_seed2 | `net.loss_pct` | 8.9 | 7.9 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_334_scaling_outage_8s_seed2 | `repair.overhead_pct` | 8.7 | 4.8 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_outage_8s_seed3 | `video.freeze_total_ms` | 447 | 233 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed3 | `video.longest_freeze_ms` | 447 | 233 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed3 | `video.e2e_delay_p95_ms` | 314 | 289 | ±15.7 | behind | 1/1 | #24, #37 |
| issue_334_scaling_outage_8s_seed3 | `cc.queue_p95_ms` | 253 | 202 | ±12.7 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_outage_8s_seed3 | `cc.rtt_p50_ms` | 89.4 | 101 | ±10 | ahead | 1/1 | #61 |
| issue_334_scaling_outage_8s_seed3 | `repair.overhead_pct` | 9.0 | 5.9 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_policer_seed1 | `audio.mouth_to_ear_p95_ms` | 412 | 223 | ±20.6 | behind | 1/1 | #63 |
| issue_334_scaling_policer_seed1 | `audio.jb_delay_p95_ms` | 370 | 180 | ±18.5 | behind | 1/1 | #63 |
| issue_334_scaling_policer_seed1 | `video.freeze_count` | 7 | 8 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_policer_seed1 | `video.freeze_total_ms` | 12457 | 11265 | ±623 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_policer_seed1 | `video.render_fps` | 21.9 | 23.8 | ±1.2 | behind | 1/1 | #37, #24 |
| issue_334_scaling_policer_seed1 | `video.e2e_delay_p95_ms` | 643 | 1762 | ±88.1 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_policer_seed1 | `video.avg_kbps` | 176 | 202 | ±20 | behind | 1/1 | #37, #61 |
| issue_334_scaling_policer_seed1 | `video.keyframe_requests` | 3 | 2 | ±0.5 | behind | 1/1 | #23 |
| issue_334_scaling_policer_seed1 | `sync.av_offset_p95_ms` | 507 | 1434 | ±71.7 | ahead | 1/1 | #77 |
| issue_335_fps1_keyframe_packets | `video.time_to_first_frame_ms` | 558 | 610 | ±50 | ahead | 7/10 | #24, #61 |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| high_jitter | `sync.av_offset_p95_ms` | 72.0 | 51.3 | ±12.7 | behind | 5/10 | #77, #19, #24 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.3 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 191 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 36.1 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 2.9 | 4.2 | ±0.8 | ahead | 9/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.0 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 321 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.1 | 37.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 72.0 | 51.3 | ±12.7 | behind | 5/10 | #77 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 548 | 885 | ±269 | ahead | 8/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 589 | 926 | ±269 | ahead | 8/10 | #63, #217 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.8 | 79.5 | ±25.8 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 111 | 130 | ±10.7 | ahead | 6/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 222 | ±11.1 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.2 | 9.6 | ±2.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 670 | 1902 | ±522 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 84.8 | 114 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 214 | ±11.9 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.6 | 4.6 | ±1.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 139 | 970 | ±353 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 132 | 253 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.5 | 45.8 | ±2.9 | ahead | 6/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.1 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 72.0 | 51.3 | ±12.7 | behind | 5/10 | #77 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.1 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 29.3 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 145 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1807 | 1576 | ±188 | behind | 5/10 | #77 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 159 | ±28.1 | ahead | 10/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 190 | 260 | ±43.1 | ahead | 10/10 | #63, #218 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 47.6 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 102 | ±16.2 | ahead | 10/10 | #77 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 72.0 | 51.3 | ±12.7 | behind | 5/10 | #77 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 727 | 123 | ±36.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 46 | 22 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1043 | 205 | ±52.1 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 272 | 64.4 | ±13.6 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 12.0 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.9 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2150 | 71.9 | ±107 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1535 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2954 | 123 | ±148 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.8 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2391 | 0 | ±120 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 0 | ±0.5 | behind | 6/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1500 | 13.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.2 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1973 | ±98.7 | ahead | 1/10 | #63 |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 89 | 153 | ±55.6 | ahead | 9/10 | #63 |
| issue_29_ulpfec_over_red | `audio.mouth_to_ear_p95_ms` | 173 | 252 | ±73.3 | ahead | 9/10 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `audio.jb_delay_p95_ms` | 530 | 500 | ±26.5 | behind | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `audio.longest_interruption_ms` | 120 | 190 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed1 | `sync.av_offset_p95_ms` | 179 | 333 | ±16.7 | ahead | 1/1 | #77 |
| issue_334_scaling_capacity_drop_seed1 | `video.e2e_delay_p95_ms` | 599 | 345 | ±30.0 | behind | 1/1 | #24, #37 |
| issue_334_scaling_capacity_drop_seed1 | `video.freeze_count` | 3 | 2 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed1 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_capacity_drop_seed1 | `video.longest_freeze_ms` | 1464 | 3031 | ±152 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed2 | `audio.longest_interruption_ms` | 130 | 200 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed2 | `sync.av_offset_p95_ms` | 158 | 336 | ±16.8 | ahead | 1/1 | #77 |
| issue_334_scaling_capacity_drop_seed2 | `video.e2e_delay_p95_ms` | 638 | 350 | ±31.9 | behind | 1/1 | #24, #37 |
| issue_334_scaling_capacity_drop_seed2 | `video.freeze_count` | 4 | 1 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed2 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_capacity_drop_seed2 | `video.longest_freeze_ms` | 1393 | 3103 | ±155 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed3 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed3 | `audio.jb_delay_p95_ms` | 444 | 470 | ±23.5 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed3 | `audio.longest_interruption_ms` | 120 | 210 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_capacity_drop_seed3 | `sync.av_offset_p95_ms` | 260 | 173 | ±13.0 | behind | 1/1 | #77 |
| issue_334_scaling_capacity_drop_seed3 | `video.e2e_delay_p95_ms` | 334 | 537 | ±26.8 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_capacity_drop_seed3 | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_capacity_drop_seed3 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 1/1 | #23 |
| issue_334_scaling_capacity_drop_seed3 | `video.longest_freeze_ms` | 3168 | 2859 | ±158 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed1 | `audio.longest_interruption_ms` | 30 | 80 | ±20 | ahead | 1/1 | #63 |
| issue_334_scaling_cross_join_seed1 | `cc.max_settle_ms` | 12000 | 6900 | ±600 | behind | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_join_seed1 | `cc.rtt_p50_ms` | 85.1 | 104 | ±10 | ahead | 1/1 | #61 |
| issue_334_scaling_cross_join_seed1 | `cc.utilization_pct` | 55.1 | 59.8 | ±3.0 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_join_seed1 | `video.avg_kbps` | 1304 | 1423 | ±71.2 | behind | 1/1 | #37, #61 |
| issue_334_scaling_cross_join_seed1 | `video.e2e_delay_p95_ms` | 560 | 491 | ±28 | behind | 1/1 | #24, #37 |
| issue_334_scaling_cross_join_seed1 | `video.freeze_count` | 5 | 2 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed1 | `video.freeze_total_ms` | 2193 | 1338 | ±110 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed1 | `video.longest_freeze_ms` | 736 | 1127 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed2 | `audio.jb_delay_p95_ms` | 360 | 410 | ±20.5 | ahead | 1/1 | #63 |
| issue_334_scaling_cross_join_seed2 | `audio.longest_interruption_ms` | 130 | 40 | ±20 | behind | 1/1 | #63 |
| issue_334_scaling_cross_join_seed2 | `audio.mouth_to_ear_p95_ms` | 656 | 722 | ±36.1 | ahead | 1/1 | #63 |
| issue_334_scaling_cross_join_seed2 | `cc.max_settle_ms` | 12800 | 11700 | ±640 | behind | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_join_seed2 | `cc.queue_p95_ms` | 247 | 261 | ±13.1 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_join_seed2 | `sync.av_offset_p95_ms` | 186 | 212 | ±10.6 | ahead | 1/1 | #77 |
| issue_334_scaling_cross_join_seed2 | `video.e2e_delay_p95_ms` | 438 | 343 | ±21.9 | behind | 1/1 | #24, #37 |
| issue_334_scaling_cross_join_seed2 | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed2 | `video.freeze_total_ms` | 1297 | 1153 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed2 | `video.longest_freeze_ms` | 1297 | 728 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed3 | `cc.max_settle_ms` | 15000 | 10200 | ±750 | behind | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_join_seed3 | `cc.queue_p95_ms` | 288 | 272 | ±14.4 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_join_seed3 | `video.e2e_delay_p95_ms` | 512 | 546 | ±27.3 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_cross_join_seed3 | `video.freeze_total_ms` | 2181 | 1821 | ±109 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_join_seed3 | `video.longest_freeze_ms` | 911 | 709 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_traffic_seed1 | `audio.jb_delay_p95_ms` | 450 | 330 | ±22.5 | behind | 1/1 | #63 |
| issue_334_scaling_cross_traffic_seed1 | `audio.mouth_to_ear_p95_ms` | 778 | 566 | ±38.9 | behind | 1/1 | #63 |
| issue_334_scaling_cross_traffic_seed1 | `cc.max_settle_ms` | 1300 | 2800 | ±500 | ahead | 1/1 | #15, #48, #61, #20 |
| issue_334_scaling_cross_traffic_seed1 | `cc.queue_p95_ms` | 295 | 201 | ±14.7 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_cross_traffic_seed1 | `net.loss_pct` | 1.6 | 0 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_334_scaling_cross_traffic_seed1 | `repair.overhead_pct` | 2.7 | 0.0 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_cross_traffic_seed1 | `sync.av_offset_p95_ms` | 409 | 292 | ±20.5 | behind | 1/1 | #77 |
| issue_334_scaling_cross_traffic_seed1 | `video.e2e_delay_p95_ms` | 1008 | 249 | ±50.4 | behind | 1/1 | #24, #37 |
| issue_334_scaling_cross_traffic_seed1 | `video.freeze_count` | 14 | 0 | ±0.7 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_traffic_seed1 | `video.freeze_total_ms` | 6634 | 0 | ±332 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_cross_traffic_seed1 | `video.longest_freeze_ms` | 982 | 18 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_handover_seed1 | `audio.jb_delay_p95_ms` | 260 | 300 | ±15 | ahead | 1/1 | #63 |
| issue_334_scaling_handover_seed1 | `audio.mouth_to_ear_p95_ms` | 385 | 464 | ±23.2 | ahead | 1/1 | #63 |
| issue_334_scaling_handover_seed1 | `cc.queue_p95_ms` | 42.8 | 85.7 | ±10 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_handover_seed1 | `repair.overhead_pct` | 24.5 | 28.4 | ±2 | ahead | 1/1 | #21, #7, #29 |
| issue_334_scaling_handover_seed1 | `sync.av_offset_p95_ms` | 900 | 1009 | ±50.5 | ahead | 1/1 | #77 |
| issue_334_scaling_handover_seed1 | `video.e2e_delay_p95_ms` | 1229 | 1381 | ±69.1 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_handover_seed1 | `video.freeze_total_ms` | 3766 | 4192 | ±210 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_handover_seed1 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_handover_seed1 | `video.time_to_first_frame_ms` | 175 | 277 | ±50 | ahead | 1/1 | #24, #61 |
| issue_334_scaling_high_jitter_seed1 | `audio.jb_delay_p95_ms` | 150 | 100 | ±10 | behind | 1/1 | #63 |
| issue_334_scaling_high_jitter_seed1 | `audio.mouth_to_ear_p95_ms` | 270 | 219 | ±13.5 | behind | 1/1 | #63 |
| issue_334_scaling_high_jitter_seed1 | `repair.overhead_pct` | 36.6 | 41.6 | ±2.1 | ahead | 1/1 | #21, #7, #29 |
| issue_334_scaling_high_jitter_seed1 | `sync.av_offset_p95_ms` | 85 | 126 | ±10 | ahead | 1/1 | #77 |
| issue_334_scaling_high_jitter_seed1 | `video.e2e_delay_p95_ms` | 215 | 263 | ±13.1 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_high_jitter_seed1 | `video.keyframe_requests` | 0 | 2 | ±0.5 | ahead | 1/1 | #23 |
| issue_334_scaling_high_jitter_seed1 | `video.time_to_first_frame_ms` | 200 | 665 | ±50 | ahead | 1/1 | #24, #61 |
| issue_334_scaling_outage_8s_seed1 | `cc.utilization_pct` | 92.0 | 100 | ±5.0 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_outage_8s_seed1 | `net.loss_pct` | 9.2 | 8.0 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_334_scaling_outage_8s_seed1 | `repair.overhead_pct` | 7.7 | 5.0 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_outage_8s_seed1 | `video.avg_kbps` | 1897 | 2054 | ±103 | behind | 1/1 | #37, #61 |
| issue_334_scaling_outage_8s_seed1 | `video.e2e_delay_p95_ms` | 257 | 294 | ±14.7 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_outage_8s_seed2 | `cc.queue_p95_ms` | 213 | 183 | ±10.7 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_outage_8s_seed2 | `net.loss_pct` | 8.9 | 7.9 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_334_scaling_outage_8s_seed2 | `repair.overhead_pct` | 8.7 | 4.8 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_outage_8s_seed2 | `video.e2e_delay_p95_ms` | 361 | 274 | ±18.1 | behind | 1/1 | #24, #37 |
| issue_334_scaling_outage_8s_seed2 | `video.freeze_total_ms` | 301 | 190 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed2 | `video.longest_freeze_ms` | 301 | 190 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed3 | `cc.queue_p95_ms` | 253 | 202 | ±12.7 | behind | 1/1 | #19, #15, #48, #12, #61 |
| issue_334_scaling_outage_8s_seed3 | `cc.rtt_p50_ms` | 89.4 | 101 | ±10 | ahead | 1/1 | #61 |
| issue_334_scaling_outage_8s_seed3 | `repair.overhead_pct` | 9.0 | 5.9 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_334_scaling_outage_8s_seed3 | `video.e2e_delay_p95_ms` | 314 | 289 | ±15.7 | behind | 1/1 | #24, #37 |
| issue_334_scaling_outage_8s_seed3 | `video.freeze_total_ms` | 447 | 233 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_outage_8s_seed3 | `video.longest_freeze_ms` | 447 | 233 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_policer_seed1 | `audio.jb_delay_p95_ms` | 370 | 180 | ±18.5 | behind | 1/1 | #63 |
| issue_334_scaling_policer_seed1 | `audio.mouth_to_ear_p95_ms` | 412 | 223 | ±20.6 | behind | 1/1 | #63 |
| issue_334_scaling_policer_seed1 | `sync.av_offset_p95_ms` | 507 | 1434 | ±71.7 | ahead | 1/1 | #77 |
| issue_334_scaling_policer_seed1 | `video.avg_kbps` | 176 | 202 | ±20 | behind | 1/1 | #37, #61 |
| issue_334_scaling_policer_seed1 | `video.e2e_delay_p95_ms` | 643 | 1762 | ±88.1 | ahead | 1/1 | #24, #37 |
| issue_334_scaling_policer_seed1 | `video.freeze_count` | 7 | 8 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_334_scaling_policer_seed1 | `video.freeze_total_ms` | 12457 | 11265 | ±623 | behind | 1/1 | #5, #37, #24 |
| issue_334_scaling_policer_seed1 | `video.keyframe_requests` | 3 | 2 | ±0.5 | behind | 1/1 | #23 |
| issue_334_scaling_policer_seed1 | `video.render_fps` | 21.9 | 23.8 | ±1.2 | behind | 1/1 | #37, #24 |
| issue_335_fps1_keyframe_packets | `video.time_to_first_frame_ms` | 558 | 610 | ±50 | ahead | 7/10 | #24, #61 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 145 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1807 | 1576 | ±188 | behind | 5/10 | #77 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.2 | 37.1 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 72.0 | 51.3 | ±12.7 | behind | 5/10 | #77 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.3 | 32.4 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.3 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 72.0 | 51.3 | ±12.7 | behind | 5/10 | #77 |
| issue_84_encoder_qp_360p_low_rate | `audio.jb_delay_p95_ms` | 126 | 146 | ±15.8 | ahead | 5/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 6/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 114 | ±10 | ahead | 8/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `audio.jb_delay_p95_ms` | 126 | 146 | ±15.8 | ahead | 5/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 6/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 114 | ±10 | ahead | 8/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 55.0 | 43.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 159 | ±23.3 | ahead | 8/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 225 | 279 | ±32.9 | ahead | 8/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2690 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 45.3 | 55.6 | ±10 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.7 | 116 | ±16.9 | ahead | 6/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 135 | 164 | ±25.6 | ahead | 8/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 145 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1807 | 1576 | ±188 | behind | 5/10 | #77 |
| low_fps_clean_link | `cc.queue_p95_ms` | 110 | 98.6 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 191 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `repair.overhead_pct` | 36.1 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 2.9 | 4.2 | ±0.8 | ahead | 9/10 | #23 |
| very_low_capacity | `video.render_fps` | 11.0 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 321 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 191 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 36.1 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 2.9 | 4.2 | ±0.8 | ahead | 9/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.0 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 321 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.3 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29, #20 |

