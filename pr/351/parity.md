## Parity with libwebrtc M157.8103@{#0}

108 scenarios, mean of 10 seeds. **142 gaps**: 56 behind libwebrtc, 86 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

112 gaps on `main`, 142 here: **0 closed, 30 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_347_scaling_high_jitter_seed1 | `audio.mouth_to_ear_p95_ms` | 270 | 219 | ±13.5 | behind | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed1 | `audio.jb_delay_p95_ms` | 150 | 100 | ±10 | behind | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed1 | `video.e2e_delay_p95_ms` | 215 | 263 | ±13.1 | ahead | 1/1 | #24, #37 |
| issue_347_scaling_high_jitter_seed1 | `video.time_to_first_frame_ms` | 200 | 665 | ±50 | ahead | 1/1 | #24, #61 |
| issue_347_scaling_high_jitter_seed1 | `video.keyframe_requests` | 0 | 2 | ±0.5 | ahead | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed1 | `sync.av_offset_p95_ms` | 85 | 126 | ±10 | ahead | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed1 | `repair.overhead_pct` | 35.7 | 41.6 | ±2.1 | ahead | 1/1 | #21, #7, #29 |
| issue_347_scaling_high_jitter_seed10 | `audio.mouth_to_ear_p95_ms` | 222 | 270 | ±13.5 | ahead | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 100 | 150 | ±10 | ahead | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed10 | `video.avg_kbps` | 697 | 438 | ±34.9 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed10 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed10 | `cc.utilization_pct` | 30.9 | 20.5 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed10 | `cc.max_settle_ms` | 16800 | 30000 | ±1500 | ahead | 1/1 | #15, #48, #61, #20 |
| issue_347_scaling_high_jitter_seed10 | `repair.overhead_pct` | 47.4 | 38.7 | ±2.4 | behind | 1/1 | #21, #7, #29 |
| issue_347_scaling_high_jitter_seed2 | `video.e2e_delay_p95_ms` | 176 | 187 | ±10 | ahead | 1/1 | #24, #37 |
| issue_347_scaling_high_jitter_seed2 | `video.avg_kbps` | 585 | 485 | ±29.2 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed2 | `sync.av_offset_p95_ms` | 58.9 | 45.4 | ±10 | behind | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed2 | `cc.utilization_pct` | 26.2 | 22.2 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed3 | `video.e2e_delay_p95_ms` | 182 | 156 | ±10 | behind | 1/1 | #24, #37 |
| issue_347_scaling_high_jitter_seed3 | `video.time_to_first_frame_ms` | 209 | 469 | ±50 | ahead | 1/1 | #24, #61 |
| issue_347_scaling_high_jitter_seed3 | `video.avg_kbps` | 358 | 288 | ±20 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed3 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed3 | `sync.av_offset_p95_ms` | 67.2 | 43.8 | ±10 | behind | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed3 | `cc.utilization_pct` | 17.1 | 14.3 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed9 | `video.time_to_first_frame_ms` | 230 | 382 | ±50 | ahead | 1/1 | #24, #61 |
| issue_347_scaling_high_jitter_seed9 | `video.avg_kbps` | 780 | 619 | ±39.0 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed9 | `sync.av_offset_p95_ms` | 54.5 | 33.8 | ±10 | behind | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed9 | `cc.utilization_pct` | 34.1 | 27.6 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed9 | `repair.overhead_pct` | 49.5 | 46.8 | ±2.5 | behind | 1/1 | #21, #7, #29 |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.4 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.4 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.6 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 615 | 885 | ±269 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 657 | 926 | ±269 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 112 | 130 | ±10.7 | ahead | 8/10 | #77 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 5.2 | 9.6 | ±2.7 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 1087 | 1902 | ±522 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 79.3 | 114 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 185 | 214 | ±11.9 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.9 | 4.6 | ±1.8 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 174 | 970 | ±353 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 108 | 253 | ±100 | ahead | 7/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.7 | 45.8 | ±2.9 | ahead | 4/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.4 | 6.7 | ±1.7 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.8 | 0.9 | ±0.7 | behind | 6/10 | #23 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 748 | 706 | ±41.1 | behind | 5/10 | #63 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 289 | 266 | ±19.8 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.4 | 6.7 | ±1.7 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.8 | 0.9 | ±0.7 | behind | 6/10 | #23 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_23_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1576 | ±188 | behind | 6/10 | #77 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 118 | 159 | ±28.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 193 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 48.9 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 81.0 | 102 | ±16.2 | ahead | 9/10 | #77 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 720 | 123 | ±36 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 50 | 22 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1036 | 205 | ±51.8 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 267 | 64.4 | ±13.3 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 11.8 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.4 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2115 | 71.9 | ±106 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1524 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2944 | 123 | ±147 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.9 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2478 | 0 | ±124 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.6 | 0 | ±0.5 | behind | 4/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1353 | 13.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.4 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 149 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1973 | ±98.7 | ahead | 1/10 | #63 |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 97 | 153 | ±55.6 | ahead | 7/10 | #63 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 34.4 | 37.2 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_346_reconfigure_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_346_reconfigure_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_346_reconfigure_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 415 | 380 | ±20.8 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `audio.mouth_to_ear_p95_ms` | 750 | 698 | ±37.5 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `cc.max_settle_ms` | 12030 | 8570 | ±1897 | behind | 7/10 | #15, #48, #61, #20 |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 287 | 268 | ±14.4 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_cross_join | `net.loss_pct` | 0.9 | 0.4 | ±0.5 | behind | 3/10 | #21, #61, #12 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 299 | 231 | ±21.4 | behind | 7/10 | #77 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 692 | 451 | ±72.8 | behind | 9/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 7.7 | 2.3 | ±1.3 | behind | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 3210 | 1349 | ±247 | behind | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 3680 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_347_scaling_high_jitter_seed1 | `audio.jb_delay_p95_ms` | 150 | 100 | ±10 | behind | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed1 | `audio.mouth_to_ear_p95_ms` | 270 | 219 | ±13.5 | behind | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed1 | `repair.overhead_pct` | 35.7 | 41.6 | ±2.1 | ahead | 1/1 | #21, #7, #29 |
| issue_347_scaling_high_jitter_seed1 | `sync.av_offset_p95_ms` | 85 | 126 | ±10 | ahead | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed1 | `video.e2e_delay_p95_ms` | 215 | 263 | ±13.1 | ahead | 1/1 | #24, #37 |
| issue_347_scaling_high_jitter_seed1 | `video.keyframe_requests` | 0 | 2 | ±0.5 | ahead | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed1 | `video.time_to_first_frame_ms` | 200 | 665 | ±50 | ahead | 1/1 | #24, #61 |
| issue_347_scaling_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 100 | 150 | ±10 | ahead | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed10 | `audio.mouth_to_ear_p95_ms` | 222 | 270 | ±13.5 | ahead | 1/1 | #63 |
| issue_347_scaling_high_jitter_seed10 | `cc.max_settle_ms` | 16800 | 30000 | ±1500 | ahead | 1/1 | #15, #48, #61, #20 |
| issue_347_scaling_high_jitter_seed10 | `cc.utilization_pct` | 30.9 | 20.5 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed10 | `repair.overhead_pct` | 47.4 | 38.7 | ±2.4 | behind | 1/1 | #21, #7, #29 |
| issue_347_scaling_high_jitter_seed10 | `video.avg_kbps` | 697 | 438 | ±34.9 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed10 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed2 | `cc.utilization_pct` | 26.2 | 22.2 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed2 | `sync.av_offset_p95_ms` | 58.9 | 45.4 | ±10 | behind | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed2 | `video.avg_kbps` | 585 | 485 | ±29.2 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed2 | `video.e2e_delay_p95_ms` | 176 | 187 | ±10 | ahead | 1/1 | #24, #37 |
| issue_347_scaling_high_jitter_seed2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed3 | `cc.utilization_pct` | 17.1 | 14.3 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed3 | `sync.av_offset_p95_ms` | 67.2 | 43.8 | ±10 | behind | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed3 | `video.avg_kbps` | 358 | 288 | ±20 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed3 | `video.e2e_delay_p95_ms` | 182 | 156 | ±10 | behind | 1/1 | #24, #37 |
| issue_347_scaling_high_jitter_seed3 | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 1/1 | #23 |
| issue_347_scaling_high_jitter_seed3 | `video.time_to_first_frame_ms` | 209 | 469 | ±50 | ahead | 1/1 | #24, #61 |
| issue_347_scaling_high_jitter_seed9 | `cc.utilization_pct` | 34.1 | 27.6 | ±2 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_347_scaling_high_jitter_seed9 | `repair.overhead_pct` | 49.5 | 46.8 | ±2.5 | behind | 1/1 | #21, #7, #29 |
| issue_347_scaling_high_jitter_seed9 | `sync.av_offset_p95_ms` | 54.5 | 33.8 | ±10 | behind | 1/1 | #77 |
| issue_347_scaling_high_jitter_seed9 | `video.avg_kbps` | 780 | 619 | ±39.0 | ahead | 1/1 | #37, #61 |
| issue_347_scaling_high_jitter_seed9 | `video.time_to_first_frame_ms` | 230 | 382 | ±50 | ahead | 1/1 | #24, #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1576 | ±188 | behind | 6/10 | #77 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.0 | 32.4 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `cc.max_settle_ms` | 13810 | 14740 | ±737 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 126 | 159 | ±23.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 222 | 279 | ±32.9 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 2690 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 44.0 | 55.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.9 | 116 | ±16.9 | ahead | 7/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 132 | 164 | ±25.6 | ahead | 9/10 | #24, #37 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1576 | ±188 | behind | 6/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| low_fps_clean_link | `cc.queue_p95_ms` | 109 | 98.6 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `repair.overhead_pct` | 35.4 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 10.4 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.4 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.4 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29, #20 |

