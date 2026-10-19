## Parity with libwebrtc M157.8103@{#0}

64 scenarios, mean of 10 seeds. **520 gaps**: 269 behind libwebrtc, 251 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

520 gaps on `main`, 520 here: **0 closed, 0 opened**, 0 changed direction.

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 54.4 | 48.0 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.8 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44.8 | 80 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.longest_interruption_ms` | 61 | 30 | ±20 | behind | 6/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 108 | 146 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `cc.utilization_pct` | 93.5 | 77.5 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `audio.jb_delay_p95_ms` | 46.4 | 110 | ±17.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 93.3 | 170 | ±20.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `cc.queue_p95_ms` | 27.0 | 47.3 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.utilization_pct` | 86.6 | 92.3 | ±4.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `video.avg_kbps` | 743 | 819 | ±41.0 | behind | 10/10 | #37, #61 |
| bufferbloat | `video.e2e_delay_p95_ms` | 64.0 | 90.6 | ±10 | ahead | 10/10 | #24, #37 |
| bufferbloat | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `audio.concealed_pct` | 2.6 | 1.5 | ±0.5 | behind | 9/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 71.2 | 154 | ±43.8 | ahead | 9/10 | #62, #63 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 139 | 242 | ±52.3 | ahead | 9/10 | #62, #63 |
| burst_loss | `cc.queue_p95_ms` | 18.6 | 46.8 | ±15.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.utilization_pct` | 57.7 | 80.4 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `video.avg_kbps` | 997 | 1461 | ±89.1 | behind | 10/10 | #37, #61 |
| burst_loss | `video.freeze_count` | 1 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.freeze_total_ms` | 215 | 1432 | ±500 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.keyframe_requests` | 1.2 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `video.longest_freeze_ms` | 160 | 298 | ±100 | ahead | 5/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.longest_interruption_ms` | 121 | 144 | ±20.1 | ahead | 6/10 | #62, #63 |
| capacity_drop | `cc.queue_p95_ms` | 384 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `cc.utilization_pct` | 54.4 | 57.5 | ±2.9 | behind | 5/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `net.loss_pct` | 4.1 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| capacity_drop | `repair.overhead_pct` | 7.2 | 3.1 | ±2 | behind | 9/10 | #21, #7, #29 |
| capacity_drop | `video.avg_kbps` | 816 | 895 | ±44.8 | behind | 8/10 | #37, #61 |
| capacity_drop | `video.freeze_count` | 3.6 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.keyframe_requests` | 8.3 | 0 | ±0.5 | behind | 10/10 | #23 |
| clean_link | `audio.jb_delay_p95_ms` | 43.2 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 89.3 | 238 | ±40.5 | ahead | 10/10 | #62, #63 |
| clean_link | `cc.queue_p95_ms` | 22.0 | 76.7 | ±22.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| clean_link | `cc.utilization_pct` | 81.9 | 91.6 | ±4.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.avg_kbps` | 1875 | 2142 | ±107 | behind | 10/10 | #37, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 67.7 | 125 | ±15.4 | ahead | 10/10 | #24, #37 |
| clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `audio.jb_delay_p95_ms` | 148 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 319 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 148 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `cc.utilization_pct` | 66.1 | 74.2 | ±3.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.avg_kbps` | 1546 | 1779 | ±88.9 | behind | 10/10 | #37, #61 |
| cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `video.longest_freeze_ms` | 8.2 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 118 | ±13.2 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 190 | 218 | ±13.7 | ahead | 8/10 | #62, #63, #19, #24 |
| high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `cc.utilization_pct` | 6.8 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `repair.overhead_pct` | 9.0 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.avg_kbps` | 68.7 | 584 | ±117 | behind | 10/10 | #37, #61, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 1538 | 136 | ±76.9 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `video.render_fps` | 25.7 | 29.8 | ±1.5 | behind | 10/10 | #37, #24, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 173 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 392 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 147 | 135 | ±10 | behind | 7/10 | #61 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 660 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 5.3 | 4.3 | ±0.7 | behind | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 320 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 702 | 800 | ±50.4 | ahead | 9/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.max_settle_ms` | 13600 | 14830 | ±742 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 361 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 86.9 | 73.5 | ±10 | behind | 9/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 511 | 249 | ±227 | behind | 9/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 3046 | 2538 | ±362 | behind | 3/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 4.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_120_rtx_payload_padding_capacity_drop | `video.render_fps` | 27.2 | 28.8 | ±1.4 | behind | 3/10 | #37, #24 |
| issue_123_v2_allocation_min | `net.loss_pct` | 18.1 | 29.6 | ±3.6 | ahead | 10/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 37.8 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 6957 | 8516 | ±961 | ahead | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.freeze_count` | 13 | 18.4 | ±3.7 | ahead | 7/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 9.7 | 3.3 | ±0.7 | behind | 8/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.5 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 1457 | 354 | ±72.9 | behind | 6/10 | #24, #61 |
| issue_124_bufferbloat_v2 | `cc.queue_p95_ms` | 69.0 | 47.3 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 102 | 90.6 | ±10 | behind | 4/10 | #24, #37 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.3 | 1.5 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `cc.queue_p95_ms` | 73.5 | 46.8 | ±15.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_124_burst_loss_v2 | `video.freeze_count` | 2.4 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_124_burst_loss_v2 | `video.freeze_total_ms` | 640 | 1432 | ±500 | ahead | 8/10 | #5, #37, #24 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 92.6 | 141 | ±17.5 | ahead | 38/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_126_audio_v2 | `audio.mouth_to_ear_p95_ms` | 59.8 | 71.1 | ±10 | ahead | 6/10 | #62, #63 |
| issue_126_audio_v2 | `cc.rtt_p50_ms` | 73.4 | 62.8 | ±10 | behind | 8/10 | #61 |
| issue_126_audio_v2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.7 | 1.5 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1347 | 1461 | ±89.1 | behind | 7/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 1.5 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 360 | 1432 | ±500 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 60 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 86.0 | 91.4 | ±4.6 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.5 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.4 | 8.3 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1076 | 1182 | ±59.1 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 748 | 471 | ±115 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3090 | 2148 | ±850 | behind | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 35.6 | 0 | ±1.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1798 | 695 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.render_fps` | 26.9 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 190 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 16.8 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 22.4 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 251 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 301 | 136 | ±15.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 12.9 | 38 | ±10 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 45.3 | 59.2 | ±10 | ahead | 6/10 | #62, #63 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 24.0 | 12.7 | ±2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.8 | 13.6 | ±3.0 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 21.3 | 89.0 | ±23.8 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 251 | 135 | ±20 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 539 | 3397 | ±562 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 0.5 | 9.3 | ±4.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 186 | 11683 | ±6940 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 11.5 | 3.1 | ±1.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 160 | 2904 | ±604 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.6 | ±1.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 9637 | ±6521 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 29.0 | 17.8 | ±3.6 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 369 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 51.4 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.keyframe_requests` | 3.4 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 66.3 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 27.1 | 42 | ±10 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_shallow | `cc.utilization_pct` | 64.6 | 56.7 | ±3.2 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 10.5 | 6.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 21.3 | 12.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 806 | 746 | ±40.3 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 556 | 315 | ±34.0 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.5 | 26.9 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 753 | 7677 | ±1185 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.keyframe_requests` | 16 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 386 | 648 | ±203 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 26.4 | 40 | ±10 | ahead | 7/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.rtt_p50_ms` | 74.1 | 62.5 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 67.8 | 56.7 | ±3.4 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 10.2 | 7.2 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 20.5 | 12.9 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 850 | 746 | ±42.5 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 585 | 269 | ±29.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 2.6 | 21.4 | ±4.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 840 | 5769 | ±1258 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.keyframe_requests` | 15.3 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_133_all_v2_outage_15s | `cc.max_settle_ms` | 260 | 15000 | ±750 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_15s | `cc.rtt_p50_ms` | 108 | 90.2 | ±10 | behind | 7/10 | #61 |
| issue_133_all_v2_outage_15s | `net.loss_pct` | 5.0 | 11.3 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_133_all_v2_outage_15s | `video.avg_kbps` | 1503 | 1690 | ±84.5 | behind | 10/10 | #37, #61 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 123 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_15s | `video.keyframe_requests` | 1 | 2 | ±0.5 | ahead | 10/10 | #23 |
| issue_133_all_v2_outage_3s | `cc.max_settle_ms` | 260 | 14300 | ±13387 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 108 | 93.2 | ±10 | behind | 10/10 | #61 |
| issue_133_all_v2_outage_3s | `net.loss_pct` | 3.0 | 4.5 | ±0.9 | ahead | 10/10 | #21, #61, #12 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 260 | 3070 | ±1029 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.queue_p95_ms` | 27.7 | 229 | ±30.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 108 | 93.6 | ±10 | behind | 8/10 | #61 |
| issue_133_all_v2_outage_8s | `cc.utilization_pct` | 97.2 | 103 | ±5.1 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_133_all_v2_outage_8s | `net.loss_pct` | 3.7 | 8.6 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_133_all_v2_outage_8s | `repair.overhead_pct` | 0.1 | 5.9 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_133_all_v2_outage_8s | `video.avg_kbps` | 1926 | 2040 | ±102 | behind | 7/10 | #37, #61 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 123 | 325 | ±80.2 | ahead | 10/10 | #24, #37 |
| issue_133_all_v2_outage_8s | `video.freeze_count` | 0 | 1.9 | ±1.0 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `video.freeze_total_ms` | 0 | 769 | ±498 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `video.keyframe_requests` | 1 | 2 | ±0.5 | ahead | 10/10 | #23 |
| issue_133_all_v2_outage_8s | `video.longest_freeze_ms` | 22.7 | 436 | ±313 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `video.pause_total_ms` | 8298 | 9780 | ±489 | ahead | 10/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_15s | `cc.max_settle_ms` | 220 | 15000 | ±750 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `cc.utilization_pct` | 88.5 | 101 | ±5.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_15s | `net.loss_pct` | 5.1 | 11.3 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_15s | `video.avg_kbps` | 1391 | 1690 | ±84.5 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 134 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_3s | `cc.max_settle_ms` | 220 | 14300 | ±13387 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_3s | `cc.utilization_pct` | 85.2 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_3s | `net.loss_pct` | 3.3 | 4.5 | ±0.9 | ahead | 6/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_3s | `video.avg_kbps` | 1923 | 2301 | ±115 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 340 | 3070 | ±1029 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.queue_p95_ms` | 23.6 | 229 | ±30.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `cc.utilization_pct` | 91.1 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 3.7 | 8.6 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 0 | 5.9 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `video.avg_kbps` | 1811 | 2040 | ±102 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 129 | 325 | ±80.2 | ahead | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `video.freeze_count` | 0 | 1.9 | ±1.0 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.freeze_total_ms` | 0 | 769 | ±498 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.longest_freeze_ms` | 18 | 436 | ±313 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.pause_total_ms` | 8263 | 9780 | ±489 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 48 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 100 | 238 | ±40.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 28.3 | 76.7 | ±22.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 72.1 | 125 | ±15.4 | ahead | 10/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 148 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 293 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 121 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 11.3 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 369 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 51.4 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.keyframe_requests` | 3.4 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 66.3 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.6 | 0.4 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 54 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.mouth_to_ear_p95_ms` | 112 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 16.1 | 29.5 | ±10 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 60.8 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1043 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 327 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 19.2 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 2.6 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 32 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 78.4 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 187 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 7.3 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 8.9 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 77.2 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 160 | 136 | ±10 | behind | 9/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_21_shallow_drop_repair | `cc.rtt_p50_ms` | 72.9 | 62.2 | ±10 | behind | 8/10 | #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 63.8 | 49.7 | ±3.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.6 | 8.2 | ±1.1 | behind | 10/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 39.7 | 20.3 | ±3.2 | behind | 9/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 570 | 472 | ±28.5 | ahead | 9/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 842 | 2417 | ±1281 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 53.5 | 0.9 | ±2.7 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 358 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 383 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `net.loss_pct` | 3.5 | 1.5 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.3 | 3.1 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_23_capacity_drop | `video.avg_kbps` | 826 | 895 | ±44.8 | behind | 7/10 | #37, #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 4576 | 2538 | ±362 | behind | 9/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3180 | 1875 | ±554 | behind | 9/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 26.5 | 28.8 | ±1.4 | behind | 9/10 | #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 198 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 452 | 536 | ±41.7 | ahead | 8/10 | #62, #63 |
| issue_23_handover | `cc.queue_p95_ms` | 193 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `cc.utilization_pct` | 72.3 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 20.2 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 60.3 | 33.4 | ±3.0 | behind | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.avg_kbps` | 886 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 255 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 2.3 | 4.3 | ±0.7 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 5528 | 4528 | ±592 | behind | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_handover | `video.longest_freeze_ms` | 3346 | 1917 | ±313 | behind | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 25.6 | 27.9 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 250 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.max_settle_ms` | 900 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 183 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_23_low_bandwidth | `net.loss_pct` | 2.4 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.8 | 16.9 | ±4.3 | ahead | 9/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.avg_kbps` | 47.4 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 873 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_count` | 2.6 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 5142 | 12936 | ±3677 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_3s | `audio.concealed_pct` | 11.1 | 10.2 | ±0.6 | behind | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 172 | 108 | ±24.9 | behind | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 225 | 170 | ±28.5 | behind | 8/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 22.5 | 38.6 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `cc.utilization_pct` | 69.1 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `video.avg_kbps` | 829 | 1142 | ±57.1 | behind | 10/10 | #37, #61 |
| issue_23_outage_3s | `video.freeze_count` | 1.9 | 1 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3423 | 3222 | ±171 | behind | 6/10 | #5, #37, #24 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 1.9 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 261 | 137 | ±27.9 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 311 | 204 | ±38.3 | behind | 9/10 | #62, #63 |
| issue_23_outage_500ms | `cc.max_settle_ms` | 100 | 700 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 29.5 | 54.3 | ±15.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 59.7 | 87.6 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `net.loss_pct` | 2.1 | 1.6 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_23_outage_500ms | `repair.overhead_pct` | 9.1 | 2.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_outage_500ms | `video.avg_kbps` | 972 | 1589 | ±79.4 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 390 | 192 | ±37.8 | behind | 8/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1942 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1336 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 566 | 869 | ±45.8 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 158 | 484 | ±24.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 110 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 0.1 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 0.6 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.avg_kbps` | 51.0 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2245 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 0 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 0 | 12936 | ±3677 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.keyframe_requests` | 3 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 26.1 | 3279 | ±1648 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 26.5 | 21.3 | ±3.1 | ahead | 10/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 91.2 | 103 | ±10 | ahead | 8/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 60 | 24 | ±20 | behind | 7/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1300 | 3140 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 79.9 | 131 | ±10 | ahead | 10/10 | #61 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 77.2 | 90.4 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 8.3 | 6.2 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `repair.overhead_pct` | 15.2 | 11.0 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 744 | 919 | ±45.9 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 476 | 1884 | ±218 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 2.8 | 8.3 | ±2.6 | ahead | 10/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 2568 | 3190 | ±491 | ahead | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 16.5 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1589 | 1140 | ±256 | behind | 9/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 148 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 319 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 148 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `cc.utilization_pct` | 66.1 | 74.2 | ±3.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.avg_kbps` | 1546 | 1779 | ±88.9 | behind | 10/10 | #37, #61 |
| issue_5_adjuster_default | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 8.2 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 64.4 | 117 | ±14.9 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 115 | 172 | ±14.7 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 20.9 | 37.4 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 57.9 | 86.3 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 745 | 1179 | ±59.0 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 161 | 141 | ±10 | behind | 8/10 | #24, #37 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.9 | 1.7 | ±0.7 | behind | 9/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.interruptions` | 1.2 | 0.6 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 20.6 | 46.6 | ±13.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `cc.utilization_pct` | 58.8 | 84.5 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `video.avg_kbps` | 991 | 1518 | ±75.9 | behind | 10/10 | #37, #61 |
| issue_5_burst15_freeze | `video.keyframe_requests` | 2.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 504 | ±34.7 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 122 | 149 | ±22.3 | ahead | 6/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 703 | 811 | ±45.5 | ahead | 8/10 | #62, #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 326 | 289 | ±23.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_5_keyframe_flush | `video.avg_kbps` | 831 | 893 | ±44.7 | behind | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2108 | 2570 | ±359 | ahead | 7/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 5.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1240 | 1895 | ±545 | ahead | 7/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 133 | 154 | ±11.8 | ahead | 8/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 57.7 | 46.5 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 94.4 | 82.0 | ±10 | behind | 7/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 7.2 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 12.7 | 8.3 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 709 | 471 | ±115 | behind | 10/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 24.7 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.concealed_pct` | 1.0 | 0.2 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 42.4 | 85 | ±15.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 104 | 156 | ±24.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 13160 | 1290 | ±2439 | behind | 9/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 8.1 | 28.0 | ±10.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 41.3 | 87.0 | ±10.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 19.8 | 32.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.avg_kbps` | 781 | 1599 | ±184 | behind | 10/10 | #37, #61 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 189 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 425 | 536 | ±41.7 | ahead | 9/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 74.7 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `net.loss_pct` | 8.0 | 6.0 | ±1.3 | behind | 8/10 | #21, #61, #12 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 913 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 790 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.7 | 0.9 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 358 | 490 | ±36.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 613 | 773 | ±50.9 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 231 | 264 | ±27.3 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 778 | 834 | ±41.7 | behind | 6/10 | #37, #61 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 44 | 106 | ±22.2 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 114 | 182 | ±23.3 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 30000 | 2920 | ±2909 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.3 | 24.7 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 19.1 | 83.6 | ±6.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 14.8 | 40.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 328 | 1447 | ±109 | behind | 10/10 | #37, #61 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 145 | 157 | ±10 | ahead | 6/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 78.4 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 186 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 9.2 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 11.5 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 117 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 227 | 136 | ±11.3 | behind | 9/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.6 | 0.4 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 54 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.mouth_to_ear_p95_ms` | 112 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 16.1 | 29.5 | ±10 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 60.8 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1043 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 327 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 19.2 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 2.6 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 32 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 46.4 | 153 | ±37.4 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 107 | 252 | ±56.0 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 200 | 2560 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 20.6 | 56.9 | ±15.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `cc.utilization_pct` | 76.5 | 88.7 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `net.loss_pct` | 9.4 | 11.8 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_85_video_pause | `video.avg_kbps` | 1068 | 1304 | ±65.2 | behind | 10/10 | #37, #61 |
| issue_85_video_pause | `video.keyframe_requests` | 2 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_85_video_pause | `video.pause_total_ms` | 10170 | 12273 | ±614 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.3 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 43.2 | 140 | ±27.1 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 93.7 | 212 | ±40.8 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `cc.queue_p95_ms` | 21.5 | 49.1 | ±17.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 72.8 | 117 | ±15.6 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 250 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.max_settle_ms` | 910 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| low_bandwidth | `cc.rtt_p50_ms` | 197 | 505 | ±33.7 | ahead | 10/10 | #61 |
| low_bandwidth | `net.loss_pct` | 2.3 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 9.5 | 16.9 | ±4.3 | ahead | 9/10 | #21, #7, #29 |
| low_bandwidth | `video.avg_kbps` | 48.2 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 953 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.freeze_count` | 0.6 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.freeze_total_ms` | 373 | 12936 | ±3677 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.keyframe_requests` | 14.7 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| low_bandwidth | `video.longest_freeze_ms` | 304 | 3279 | ±1648 | ahead | 10/10 | #5, #37, #24 |
| outage_3s | `audio.concealed_pct` | 11.0 | 10.2 | ±0.6 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `audio.jb_delay_p95_ms` | 161 | 108 | ±24.9 | behind | 7/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 214 | 170 | ±28.5 | behind | 6/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 22.7 | 38.6 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `cc.utilization_pct` | 68.5 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `video.avg_kbps` | 820 | 1142 | ±57.1 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 142 | 105 | ±13.7 | behind | 6/10 | #24, #37, #20 |
| outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 123 | 144 | ±20.1 | ahead | 5/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 700 | 800 | ±50.4 | ahead | 7/10 | #62, #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 324 | 283 | ±26.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 828 | 895 | ±44.8 | behind | 8/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2110 | 2538 | ±362 | ahead | 6/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 5.8 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 333 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 719 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 13480 | 14830 | ±742 | ahead | 6/10 | #15, #48, #61, #20 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 367 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 87.0 | 73.5 | ±10 | behind | 8/10 | #61 |
| probe_v2_capacity_drop | `net.loss_pct` | 3.9 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `repair.overhead_pct` | 6.6 | 3.1 | ±2 | behind | 7/10 | #21, #7, #29 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3148 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 9.3 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 101 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `cc.queue_p95_ms` | 106 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 83.3 | 66.6 | ±10 | behind | 9/10 | #61 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 145 | 125 | ±15.4 | behind | 6/10 | #24, #37 |
| probe_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 163 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 306 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 121 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 6.8 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| random_loss_5pct | `audio.concealed_pct` | 1.6 | 0.4 | ±0.5 | behind | 10/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 54 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.mouth_to_ear_p95_ms` | 112 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `cc.queue_p95_ms` | 16.1 | 29.5 | ±10 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `cc.utilization_pct` | 60.8 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `video.avg_kbps` | 1043 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 327 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 19.2 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.keyframe_requests` | 2.6 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| random_loss_5pct | `video.longest_freeze_ms` | 32 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 48.4 | 160 | ±68.5 | ahead | 10/10 | #62, #63 |
| satellite | `audio.mouth_to_ear_p95_ms` | 374 | 508 | ±93.9 | ahead | 10/10 | #62, #63 |
| satellite | `cc.max_settle_ms` | 6450 | 710 | ±500 | behind | 2/10 | #15, #48, #61, #20 |
| satellite | `cc.queue_p95_ms` | 13.9 | 43.9 | ±28.2 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.utilization_pct` | 53.8 | 82.4 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `repair.overhead_pct` | 15.0 | 18.0 | ±2 | ahead | 8/10 | #21, #7, #29 |
| satellite | `video.avg_kbps` | 599 | 964 | ±48.2 | behind | 10/10 | #37, #61 |
| satellite | `video.e2e_delay_p95_ms` | 809 | 932 | ±46.6 | ahead | 10/10 | #24, #37 |
| satellite | `video.freeze_count` | 21.9 | 32.6 | ±5.6 | ahead | 9/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6371 | 17933 | ±2588 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `video.render_fps` | 25.0 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.concealed_pct` | 13.0 | 0.5 | ±0.7 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 310 | 203 | ±20 | behind | 10/10 | #62, #63 |
| very_low_capacity | `cc.max_settle_ms` | 1340 | 400 | ±500 | behind | 9/10 | #15, #48, #61, #20 |
| very_low_capacity | `cc.rtt_p50_ms` | 351 | 328 | ±17.6 | behind | 7/10 | #61 |
| very_low_capacity | `net.loss_pct` | 34.6 | 29.6 | ±3.6 | behind | 8/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 30.5 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 2308 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 5.9 | 18.4 | ±3.7 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_total_ms` | 12605 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 214 | 3.3 | ±10.7 | behind | 10/10 | #23 |
| very_low_capacity | `video.longest_freeze_ms` | 3480 | 4455 | ±406 | ahead | 6/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_total_ms` | 13392 | 8289 | ±5083 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 1.1 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 1.5 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 300 | 203 | ±20 | behind | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `cc.max_settle_ms` | 1560 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 2298 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 7.9 | 18.4 | ±3.7 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 15449 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 127 | 3.3 | ±6.4 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3822 | 4455 | ±406 | ahead | 7/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.4 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 195 | 317 | ±21.6 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 450 | 536 | ±41.7 | ahead | 8/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 192 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 73.1 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 19.8 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 58.0 | 33.4 | ±2.9 | behind | 10/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 897 | 1299 | ±64.9 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 208 | 2319 | ±478 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.4 | 4.3 | ±0.7 | ahead | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 16.7 | 0 | ±0.8 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `video.render_fps` | 26.2 | 27.9 | ±1.4 | behind | 5/10 | #37, #24, #20 |

