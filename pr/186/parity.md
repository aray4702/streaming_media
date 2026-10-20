## Parity with libwebrtc M157.8103@{#0}

71 scenarios, mean of 10 seeds. **310 gaps**: 87 behind libwebrtc, 223 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

543 gaps on `main`, 310 here: **267 closed, 34 opened**, 23 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `cc.utilization_pct` | 93.5 | 77.5 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 102 | 170 | ±20.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `video.e2e_delay_p95_ms` | 73.2 | 90.6 | ±10 | ahead | 8/10 | #24, #37 |
| bufferbloat | `video.avg_kbps` | 743 | 819 | ±41.0 | behind | 10/10 | #37, #61 |
| bufferbloat | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| bufferbloat | `cc.utilization_pct` | 86.6 | 92.3 | ±4.6 | behind | 8/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.queue_p95_ms` | 27.3 | 47.3 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `audio.longest_interruption_ms` | 120 | 91 | ±25.1 | behind | 6/10 | #62, #63 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 138 | 242 | ±52.3 | ahead | 10/10 | #62, #63 |
| burst_loss | `video.longest_freeze_ms` | 146 | 298 | ±100 | ahead | 5/10 | #5, #37, #24 |
| burst_loss | `video.avg_kbps` | 985 | 1461 | ±89.1 | behind | 10/10 | #37, #61 |
| burst_loss | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `cc.utilization_pct` | 57.0 | 80.4 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.queue_p95_ms` | 17.8 | 46.8 | ±15.9 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.longest_interruption_ms` | 117 | 144 | ±20.1 | ahead | 4/10 | #62, #63 |
| capacity_drop | `video.keyframe_requests` | 10.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| capacity_drop | `cc.queue_p95_ms` | 385 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `net.loss_pct` | 4.4 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| capacity_drop | `repair.overhead_pct` | 7.6 | 3.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| clean_link | `video.e2e_delay_p95_ms` | 68.1 | 125 | ±15.4 | ahead | 10/10 | #24, #37 |
| clean_link | `video.avg_kbps` | 1972 | 2142 | ±107 | behind | 10/10 | #37, #61 |
| clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| clean_link | `cc.utilization_pct` | 86.0 | 91.6 | ±4.6 | behind | 9/10 | #19, #15, #48, #12, #61 |
| clean_link | `cc.queue_p95_ms` | 23.2 | 76.7 | ±22.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.avg_kbps` | 1584 | 1779 | ±88.9 | behind | 8/10 | #37, #61 |
| cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `cc.utilization_pct` | 67.6 | 74.2 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| high_jitter | `video.render_fps` | 26.3 | 29.8 | ±1.5 | behind | 10/10 | #37, #24, #19 |
| high_jitter | `video.avg_kbps` | 94.8 | 584 | ±117 | behind | 10/10 | #37, #61, #19, #24 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `cc.utilization_pct` | 8.3 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61, #24 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 5.2 | 4.3 | ±0.7 | behind | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1987 | 2538 | ±362 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 8 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 251 | 283 | ±26.9 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 84.4 | 73.5 | ±10 | behind | 6/10 | #61 |
| issue_126_audio_v2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_126_audio_v2 | `cc.rtt_p50_ms` | 73.7 | 62.8 | ±10 | behind | 8/10 | #61 |
| issue_127_all_v2_no_stall | `video.e2e_delay_p95_ms` | 470 | 443 | ±23.5 | behind | 6/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 1262 | 425 | ±357 | behind | 6/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.render_fps` | 26.6 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.avg_kbps` | 102 | 917 | ±186 | behind | 10/10 | #37, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.keyframe_requests` | 3.6 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.utilization_pct` | 8.3 | 56.8 | ±12.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.max_settle_ms` | 32040 | 15910 | ±9723 | behind | 8/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_burst_loss | `audio.mouth_to_ear_p95_ms` | 182 | 242 | ±52.3 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1275 | 1461 | ±89.1 | behind | 9/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_burst_loss | `cc.utilization_pct` | 72.4 | 80.4 | ±4.3 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3280 | 2148 | ±850 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1926 | 695 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 782 | 471 | ±115 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1071 | 1182 | ±59.1 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 34.7 | 0 | ±1.7 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 85.4 | 91.4 | ±4.6 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.6 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.3 | 8.3 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 73.1 | 11683 | ±6940 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.6 | ±1.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 9637 | ±6521 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 29.1 | 17.8 | ±3.6 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 289 | 135 | ±20 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 13.6 | 3.1 | ±1.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 26.4 | 12.7 | ±2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.1 | 13.6 | ±3.0 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_random_loss | `video.avg_kbps` | 1286 | 1382 | ±69.1 | behind | 6/10 | #37, #61 |
| issue_12_lossbwe_random_loss | `video.keyframe_requests` | 2.2 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 495 | 315 | ±34.0 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 889 | 746 | ±44.5 | ahead | 9/10 | #37, #61 |
| issue_12_lossbwe_shallow | `video.keyframe_requests` | 9.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow | `cc.utilization_pct` | 69.5 | 56.7 | ±3.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 8.5 | 6.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 15.5 | 12.2 | ±2 | behind | 7/10 | #21, #7, #29 |
| issue_133_all_v2_outage_15s | `cc.rtt_p50_ms` | 108 | 90.2 | ±10 | behind | 7/10 | #61 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 109 | 93.2 | ±10 | behind | 10/10 | #61 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 108 | 93.6 | ±10 | behind | 8/10 | #61 |
| issue_133_pacer_v2_outage_15s | `video.avg_kbps` | 1405 | 1690 | ±84.5 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_15s | `cc.utilization_pct` | 89.4 | 101 | ±5.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_15s | `cc.max_settle_ms` | 200 | 15000 | ±750 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `net.loss_pct` | 5.1 | 11.3 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_3s | `video.avg_kbps` | 1944 | 2301 | ±115 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_133_pacer_v2_outage_3s | `cc.utilization_pct` | 86.2 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_3s | `cc.max_settle_ms` | 200 | 14300 | ±13387 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `video.freeze_count` | 0 | 1.9 | ±1.0 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.freeze_total_ms` | 0 | 769 | ±498 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.longest_freeze_ms` | 15.3 | 436 | ±313 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.pause_total_ms` | 8276 | 9780 | ±489 | ahead | 10/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.avg_kbps` | 1821 | 2040 | ±102 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_8s | `cc.utilization_pct` | 91.6 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `cc.queue_p95_ms` | 23.6 | 229 | ±30.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 3.7 | 8.6 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 0 | 5.9 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 71.8 | 125 | ±15.4 | ahead | 10/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 26.1 | 76.7 | ±22.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_random_loss | `video.avg_kbps` | 1286 | 1382 | ±69.1 | behind | 6/10 | #37, #61 |
| issue_15_aimd_v2_random_loss | `video.keyframe_requests` | 2.2 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_18_random_loss_keyframes | `audio.mouth_to_ear_p95_ms` | 119 | 182 | ±42.7 | ahead | 8/10 | #62, #63 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1117 | 1382 | ±69.1 | behind | 10/10 | #37, #61 |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 64.8 | 77.5 | ±3.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 16.9 | 29.5 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 81.9 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 7.5 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 94.0 | 75.6 | ±10 | behind | 9/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 874 | 2417 | ±1281 | ahead | 8/10 | #24, #37 |
| issue_21_shallow_drop_repair | `cc.max_settle_ms` | 6900 | 5130 | ±1729 | behind | 6/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.7 | 8.2 | ±1.1 | behind | 10/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 40.5 | 20.3 | ±3.2 | behind | 10/10 | #21, #7, #29 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 122 | 144 | ±20.1 | ahead | 4/10 | #62, #63 |
| issue_23_capacity_drop | `video.freeze_count` | 3.3 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 5206 | 2538 | ±362 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3348 | 1875 | ±554 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 26.0 | 28.8 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_capacity_drop | `video.avg_kbps` | 837 | 895 | ±44.8 | behind | 6/10 | #37, #61 |
| issue_23_capacity_drop | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 385 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `net.loss_pct` | 4.0 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.9 | 3.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.freeze_count` | 2.2 | 4.3 | ±0.7 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.longest_freeze_ms` | 3364 | 1917 | ±313 | behind | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 25.6 | 27.9 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_handover | `video.avg_kbps` | 894 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_handover | `cc.utilization_pct` | 73.4 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `video.avg_kbps` | 51.3 | 73.4 | ±20 | behind | 8/10 | #37, #61 |
| issue_23_low_bandwidth | `cc.max_settle_ms` | 960 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 135 | 170 | ±28.5 | ahead | 7/10 | #62, #63 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 71.2 | 108 | ±24.9 | ahead | 7/10 | #62, #63 |
| issue_23_outage_3s | `video.avg_kbps` | 934 | 1142 | ±57.1 | behind | 10/10 | #37, #61 |
| issue_23_outage_3s | `cc.utilization_pct` | 76.3 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `net.loss_pct` | 5.2 | 6.0 | ±0.5 | ahead | 9/10 | #21, #61, #12 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 1.9 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1321 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.avg_kbps` | 988 | 1589 | ±79.4 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 60.5 | 87.6 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 29.5 | 54.3 | ±15.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.max_settle_ms` | 100 | 700 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_23_outage_500ms | `net.loss_pct` | 2.3 | 1.6 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_outage_500ms | `repair.overhead_pct` | 9.1 | 2.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.avg_kbps` | 51.3 | 73.4 | ±20 | behind | 9/10 | #37, #61 |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 64 | 24 | ±20 | behind | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 222 | 204 | ±11.1 | behind | 8/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1613 | 1140 | ±256 | behind | 6/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 784 | 919 | ±45.9 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 17.5 | 0 | ±0.9 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 80.9 | 90.4 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1500 | 3140 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 77.6 | 131 | ±10 | ahead | 10/10 | #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 8.4 | 6.2 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `repair.overhead_pct` | 14.9 | 11.0 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_5_adjuster_default | `video.avg_kbps` | 1584 | 1779 | ±88.9 | behind | 8/10 | #37, #61 |
| issue_5_adjuster_default | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_adjuster_default | `cc.utilization_pct` | 67.6 | 74.2 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 727 | 1179 | ±59.0 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 56.6 | 86.3 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 20.4 | 37.4 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.5 | 1.7 | ±0.7 | behind | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.interruptions` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.mouth_to_ear_p95_ms` | 179 | 218 | ±38.3 | ahead | 6/10 | #62, #63 |
| issue_5_burst15_freeze | `video.freeze_count` | 2.7 | 4.9 | ±2.1 | ahead | 9/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.freeze_total_ms` | 690 | 1269 | ±553 | ahead | 9/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.avg_kbps` | 997 | 1518 | ±75.9 | behind | 10/10 | #37, #61 |
| issue_5_burst15_freeze | `video.keyframe_requests` | 1.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_burst15_freeze | `cc.utilization_pct` | 58.9 | 84.5 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 19.6 | 46.6 | ±13.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±22.3 | ahead | 5/10 | #62, #63 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2077 | 2570 | ±359 | ahead | 7/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1326 | 1895 | ±545 | ahead | 6/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.avg_kbps` | 827 | 893 | ±44.7 | behind | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 324 | 289 | ±23.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.1 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 57.1 | 46.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.8 | 4.5 | ±1.0 | behind | 8/10 | #21, #61, #12 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 117 | 156 | ±24.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1030 | 1599 | ±184 | behind | 10/10 | #37, #61 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 54.2 | 87.0 | ±10.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 10.8 | 28.0 | ±10.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 8890 | 1290 | ±2439 | behind | 8/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 3.2 | 4.3 | ±0.7 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 961 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 78.5 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 2280 | 2816 | ±513 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 1799 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 790 | 834 | ±41.7 | behind | 5/10 | #37, #61 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.8 | 3.2 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 429 | 1447 | ±109 | behind | 10/10 | #37, #61 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 24.6 | 83.6 | ±6.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.6 | 24.7 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 30000 | 2920 | ±2909 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 148 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 11.2 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `audio.mouth_to_ear_p95_ms` | 119 | 182 | ±42.7 | ahead | 8/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1117 | 1382 | ±69.1 | behind | 10/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 64.8 | 77.5 | ±3.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 16.9 | 29.5 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `video.pause_total_ms` | 10246 | 12273 | ±614 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.3 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| issue_85_video_pause | `video.avg_kbps` | 997 | 1304 | ±65.2 | behind | 10/10 | #37, #61 |
| issue_85_video_pause | `video.keyframe_requests` | 2 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_85_video_pause | `cc.utilization_pct` | 71.5 | 88.7 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `cc.queue_p95_ms` | 19.4 | 56.9 | ±15.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `cc.max_settle_ms` | 200 | 2560 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `net.loss_pct` | 9.8 | 11.8 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| lossy_feedback | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| lossy_feedback | `cc.queue_p95_ms` | 23.8 | 49.1 | ±17.9 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `video.avg_kbps` | 50.3 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| low_bandwidth | `video.keyframe_requests` | 15.3 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| low_bandwidth | `cc.max_settle_ms` | 1000 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| outage_3s | `audio.concealed_pct` | 11.1 | 10.2 | ±0.6 | behind | 10/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 250 | 170 | ±28.5 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `audio.jb_delay_p95_ms` | 186 | 108 | ±24.9 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `video.avg_kbps` | 826 | 1142 | ±57.1 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.keyframe_requests` | 5.3 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| outage_3s | `cc.utilization_pct` | 69.0 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 119 | 144 | ±20.1 | ahead | 4/10 | #62, #63 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2133 | 2538 | ±362 | ahead | 6/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 833 | 895 | ±44.8 | behind | 7/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 6.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 325 | 283 | ±26.9 | behind | 8/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3120 | 2538 | ±362 | behind | 9/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 7.8 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 283 | ±26.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 84.8 | 73.5 | ±10 | behind | 6/10 | #61 |
| probe_v2_capacity_drop | `net.loss_pct` | 3.3 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `repair.overhead_pct` | 6.0 | 3.1 | ±2 | behind | 4/10 | #21, #7, #29 |
| probe_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 81.9 | 66.6 | ±10 | behind | 9/10 | #61 |
| random_loss_5pct | `audio.mouth_to_ear_p95_ms` | 119 | 182 | ±42.7 | ahead | 8/10 | #62, #63 |
| random_loss_5pct | `video.avg_kbps` | 1117 | 1382 | ±69.1 | behind | 10/10 | #37, #61 |
| random_loss_5pct | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| random_loss_5pct | `cc.utilization_pct` | 64.8 | 77.5 | ±3.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `cc.queue_p95_ms` | 16.9 | 29.5 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| satellite | `audio.mouth_to_ear_p95_ms` | 386 | 508 | ±93.9 | ahead | 10/10 | #62, #63 |
| satellite | `video.avg_kbps` | 655 | 964 | ±48.2 | behind | 10/10 | #37, #61 |
| satellite | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `cc.utilization_pct` | 58.6 | 82.4 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.queue_p95_ms` | 13.8 | 43.9 | ±28.2 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `audio.concealed_pct` | 12.2 | 0.5 | ±0.6 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.interruptions` | 0.2 | 1 | ±0.5 | ahead | 8/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 407 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `video.freeze_total_ms` | 17081 | 24870 | ±4734 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity | `video.longest_freeze_ms` | 3684 | 4455 | ±406 | ahead | 7/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 219 | 3.3 | ±10.9 | behind | 10/10 | #23 |
| very_low_capacity | `cc.max_settle_ms` | 1420 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 407 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3780 | 4455 | ±406 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 123 | 3.3 | ±6.1 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `cc.max_settle_ms` | 1600 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.5 | 4.3 | ±0.7 | ahead | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.render_fps` | 26.5 | 27.9 | ±1.4 | behind | 4/10 | #37, #24, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 850 | 1299 | ±64.9 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 14.5 | 0 | ±0.7 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 69.8 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 13.3 | 18.4 | ±3.7 | ahead | 8/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 429 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 22 | 42 | ±10 | ahead | 8/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.7 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 663 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 25.9 | 21.3 | ±3.1 | ahead | 10/10 | #37, #24 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 433 | 484 | ±24.2 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.longest_freeze_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 125 | 105 | ±13.7 | behind | 7/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 296 | 352 | ±46.2 | ahead | 8/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.render_fps` | 18.0 | 27.5 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 166 | 147 | ±10 | behind | 8/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±32.5 | ahead | 6/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| low_bandwidth | `cc.queue_p95_ms` | 433 | 484 | ±24.2 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| outage_3s | `video.freeze_total_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24, #20 |
| outage_3s | `video.longest_freeze_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 79.8 | 247 | ±114 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3651 | 2904 | ±604 | behind | 8/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 429 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 443 | 472 | ±27.5 | behind | 4/10 | #37, #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 46.8 | 49.7 | ±2.8 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 56.5 | 38.6 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 144 | 204 | ±38.3 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 71.2 | 137 | ±27.9 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 153 | 192 | ±37.8 | ahead | 6/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 12.9 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 5123 | 3190 | ±491 | behind | 10/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 174 | 157 | ±10 | behind | 9/10 | #24, #37 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 179 | 153 | ±26.0 | behind | 6/10 | #24, #37 |
| outage_3s | `cc.queue_p95_ms` | 56.5 | 38.6 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61, #20 |
| very_low_capacity | `net.loss_pct` | 20.4 | 29.6 | ±3.6 | ahead | 9/10 | #21, #61, #12 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| bufferbloat | `audio.jb_delay_p95_ms` | 65.2 | 33.2 (closer) |
| burst_loss | `audio.concealed_pct` | 1.1 | 0.6 (closer) |
| burst_loss | `audio.jb_delay_p95_ms` | 93.6 | 60.6 (closer) |
| burst_loss | `video.freeze_count` | 5.4 | 4 (closer) |
| burst_loss | `video.freeze_total_ms` | 1252 | 880 (closer) |
| clean_link | `audio.mouth_to_ear_p95_ms` | 136 | 45.8 (closer) |
| clean_link | `audio.jb_delay_p95_ms` | 100 | 59.2 (closer) |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 382 | 258 (closer) |
| cross_traffic | `audio.jb_delay_p95_ms` | 265 | 222 (closer) |
| cross_traffic | `cc.queue_p95_ms` | 135 | 53.4 (closer) |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 22.9 | 15.1 (closer) |
| high_jitter | `audio.jb_delay_p95_ms` | 41.2 | 34.8 (closer) |
| high_jitter | `video.e2e_delay_p95_ms` | 1395 | 55.4 (closer) |
| high_jitter | `repair.overhead_pct` | 30.0 | 7.1 (closer) |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 60.9 | 72.0 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 163 | 143 (closer) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.7 | 3.7 (further) |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 3244 | 1960 (closer) |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 1889 | 30.3 (closer) |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 36.6 | 4.4 (closer) |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 1.1 | 0.6 (closer) |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 4.9 | 4 (closer) |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 1142 | 880 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 26.1 | 55.4 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 5.4 | 7.1 (further) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 26.1 | 29 (further) |
| issue_12_lossbwe_policer | `video.freeze_count` | 9.1 | 4.7 (closer) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2900 | 1114 (closer) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 72.2 | 37.2 (closer) |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 111 | 73.9 (closer) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 15.2 | 24 (further) |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 361 | 554 (further) |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 43.0 | 83.0 (further) |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 133 | 45.8 (closer) |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 100 | 59.2 (closer) |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 111 | 73.9 (closer) |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 0.9 | 0.6 (closer) |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 68.4 | 47.6 (closer) |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 322 | 282 (closer) |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 83.0 | 73.9 (closer) |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 23.7 | 15.1 (closer) |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 42.8 | 34.8 (closer) |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 21.4 | 55.4 (further) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.1 | 7.1 (closer) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 55.5 | 1.1 (closer) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 70.7 | 208 (further) |
| issue_23_handover | `audio.jb_delay_p95_ms` | 112 | 154 (further) |
| issue_23_handover | `video.e2e_delay_p95_ms` | 2041 | 1535 (closer) |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 40 | 48 (further) |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 66.4 | 98.1 (further) |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 67.1 | 55.9 (closer) |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 7027 | 10871 (further) |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 3808 | 2300 (closer) |
| issue_23_low_bandwidth | `net.loss_pct` | 4.5 | 6.5 (further) |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.3 | 15.2 (further) |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 0.6 (closer) |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1342 | 160 (closer) |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 218 | 98.1 (closer) |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 17.8 | 14.3 (closer) |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 12936 | 10871 (closer) |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 3261 | 2616 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 5.4 | 4.6 (closer) |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 239 | 51.3 (closer) |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 404 | 266 (closer) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 13.4 | 19.8 (further) |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1335 | 375 (closer) |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 382 | 258 (closer) |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 265 | 222 (closer) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 135 | 53.4 (closer) |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 37.7 | 20.6 (closer) |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 31.8 | 19.3 (closer) |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 31.2 | 45.6 (further) |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 98.5 | 161 (further) |
| issue_61_googcc_glue_congestive | `video.render_fps` | 4.5 | 2.8 (closer) |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 42.6 | 34.2 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 9.8 | 2.9 (closer) |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 85.7 | 208 (further) |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 125 | 154 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 2.1 | 6.1 (further) |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 0.8 | 0.6 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 144 | 123 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 490 | 363 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 238 | 646 (further) |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 58.4 | 27.6 (closer) |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 62 | 45.6 (closer) |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 20.8 | 3.7 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 24.6 | 15.1 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 42.8 | 34.8 (closer) |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 601 | 55.4 (closer) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 26.6 | 7.1 (closer) |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 0.9 | 0.6 (closer) |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 68.4 | 47.6 (closer) |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 322 | 282 (closer) |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 83.0 | 73.9 (closer) |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 136 | 56.1 (closer) |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 106 | 59 (closer) |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 100 | 50.6 (closer) |
| lossy_feedback | `audio.jb_delay_p95_ms` | 92 | 62.8 (closer) |
| lossy_feedback | `video.e2e_delay_p95_ms` | 41.6 | 29.2 (closer) |
| low_bandwidth | `audio.longest_interruption_ms` | 40 | 48 (further) |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 70.1 | 98.1 (further) |
| low_bandwidth | `audio.jb_delay_p95_ms` | 76.7 | 55.9 (closer) |
| low_bandwidth | `video.freeze_count` | 17.4 | 14.3 (closer) |
| low_bandwidth | `video.freeze_total_ms` | 12570 | 10871 (closer) |
| low_bandwidth | `video.longest_freeze_ms` | 2957 | 2616 (closer) |
| low_bandwidth | `video.render_fps` | 3.8 | 4.6 (further) |
| low_bandwidth | `video.e2e_delay_p95_ms` | 3773 | 2300 (closer) |
| low_bandwidth | `net.loss_pct` | 4.7 | 6.5 (further) |
| low_bandwidth | `repair.overhead_pct` | 7.9 | 15.2 (further) |
| outage_3s | `video.e2e_delay_p95_ms` | 41.0 | 20.6 (closer) |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 86.6 | 143 (further) |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 89.3 | 143 (further) |
| random_loss_5pct | `audio.concealed_pct` | 0.9 | 0.6 (closer) |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 68.4 | 47.6 (closer) |
| random_loss_5pct | `video.longest_freeze_ms` | 322 | 282 (closer) |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 83.0 | 73.9 (closer) |
| satellite | `audio.jb_delay_p95_ms` | 112 | 85.2 (closer) |
| satellite | `repair.overhead_pct` | 2.5 | 3.6 (further) |
| very_low_capacity | `audio.longest_interruption_ms` | 77 | 44 (closer) |
| very_low_capacity | `audio.jb_delay_p95_ms` | 137 | 62.2 (closer) |
| very_low_capacity | `video.freeze_count` | 8.0 | 5.1 (closer) |
| very_low_capacity | `video.render_fps` | 4.9 | 3.7 (closer) |
| very_low_capacity | `video.e2e_delay_p95_ms` | 6242 | 1960 (closer) |
| very_low_capacity | `repair.overhead_pct` | 68.2 | 58.3 (closer) |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 143 | 44 (closer) |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 135 | 62.2 (closer) |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 7.8 | 5.1 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.5 | 3.7 (closer) |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 6420 | 1960 (closer) |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 3.7 | 9.2 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 19.2 | 58.3 (further) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 74.6 | 208 (further) |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 115 | 154 (further) |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 2109 | 1535 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 80 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 117 | 146 | ±10 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 76.8 | 110 | ±17.6 | ahead | 8/10 | #62, #63 |
| burst_loss | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 6/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 92.4 | 153 | ±44.7 | ahead | 9/10 | #62, #63 |
| burst_loss | `video.freeze_count` | 2.2 | 6.2 | ±2.0 | ahead | 9/10 | #5, #37, #24 |
| burst_loss | `video.freeze_total_ms` | 553 | 1432 | ±500 | ahead | 8/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±34.8 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.jb_delay_p95_ms` | 84.8 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 192 | 238 | ±40.5 | ahead | 7/10 | #62, #63 |
| cross_traffic | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.longest_freeze_ms` | 8.3 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 203 | 218 | ±13.7 | ahead | 5/10 | #62, #63, #19, #24 |
| high_jitter | `repair.overhead_pct` | 36.3 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 192 | 136 | ±10 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 329 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 785 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±34.8 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 145 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 20.4 | 29.6 | ±3.6 | ahead | 9/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 39.3 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 6556 | 8516 | ±961 | ahead | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.freeze_count` | 13.3 | 18.4 | ±3.7 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.3 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 76.8 | 110 | ±17.6 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 92.4 | 153 | ±44.7 | ahead | 9/10 | #62, #63 |
| issue_124_burst_loss_v2 | `video.freeze_count` | 2.2 | 6.2 | ±2.0 | ahead | 9/10 | #5, #37, #24 |
| issue_124_burst_loss_v2 | `video.freeze_total_ms` | 553 | 1432 | ±500 | ahead | 8/10 | #5, #37, #24 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 82.8 | 141 | ±17.5 | ahead | 40/40 | #62, #63 |
| issue_124_clean_link_v2 | `audio.mouth_to_ear_p95_ms` | 189 | 229 | ±34.3 | ahead | 31/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 83.2 | 158 | ±28.6 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 289 | 347 | ±30.3 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 3.2 | 65.8 | ±6.5 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 950 | 19132 | ±1879 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 368 | 566 | ±142 | ahead | 9/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 1.0 | 0.1 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.jb_delay_p95_ms` | 72 | 99 | ±10 | ahead | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 43.5 | 47.9 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 204 | 174 | ±10.2 | behind | 9/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 79.8 | 247 | ±114 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 92.4 | 153 | ±44.7 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 2.2 | 6.2 | ±2.0 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 553 | 1432 | ±500 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_congestive | `video.render_fps` | 27.1 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 203 | 218 | ±13.7 | ahead | 5/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 36.3 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 192 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 9 | 38 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 51.8 | 89.0 | ±23.8 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2283 | 3397 | ±562 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.6 | 9.3 | ±4.0 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3651 | 2904 | ±604 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 70.4 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 283 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.3 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 63.5 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 74.8 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 18 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 0.4 | 26.9 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 104 | 7677 | ±1185 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 94.2 | 648 | ±203 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 21.1 | 40 | ±10 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 7.9 | 7.2 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 430 | 269 | ±21.5 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.4 | 21.4 | ±4.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 120 | 5769 | ±1258 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 129 | 569 | ±156 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 176 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 429 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 176 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 429 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 203 | 218 | ±13.7 | ahead | 5/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 36.3 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 192 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 83.2 | 107 | ±13.4 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 37.3 | 43.2 | ±3.4 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 178 | 143 | ±15.8 | behind | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.time_to_first_frame_ms` | 166 | 88.6 | ±50 | behind | 6/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.concealed_pct` | 1.5 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 81.6 | 113 | ±14.2 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 37.0 | 43.8 | ±2.9 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.e2e_delay_p95_ms` | 189 | 134 | ±10 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 180 | 89.4 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 56.5 | 38.6 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 125 | 105 | ±13.7 | behind | 7/10 | #24, #37 |
| issue_151_outage_3s_v2 | `video.freeze_total_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.longest_freeze_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 22 | 42 | ±10 | ahead | 8/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 46.8 | 49.7 | ±2.8 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 443 | 472 | ±27.5 | behind | 4/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.7 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 2 | 0.9 | ±0.6 | behind | 8/10 | #23 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 192 | 238 | ±40.5 | ahead | 7/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 8.3 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 70.4 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 283 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.3 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 63.5 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 74.8 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 70.4 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 283 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.3 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 63.5 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 74.8 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 203 | 218 | ±13.7 | ahead | 5/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 36.3 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 192 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 22 | 42 | ±10 | ahead | 8/10 | #62, #63 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 46.8 | 49.7 | ±2.8 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 443 | 472 | ±27.5 | behind | 4/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.7 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 2 | 0.9 | ±0.6 | behind | 8/10 | #23 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±34.8 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 329 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 785 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 296 | 352 | ±46.2 | ahead | 8/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 771 | 869 | ±45.8 | ahead | 7/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 433 | 484 | ±24.2 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 239 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_23_low_bandwidth | `net.loss_pct` | 0.4 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 1.7 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 2418 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_count` | 3.5 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 2066 | 12936 | ±3677 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 663 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 25.9 | 21.3 | ±3.1 | ahead | 10/10 | #37, #24 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 56.5 | 38.6 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 125 | 105 | ±13.7 | behind | 7/10 | #24, #37 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.longest_freeze_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 71.2 | 137 | ±27.9 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 144 | 204 | ±38.3 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 153 | 192 | ±37.8 | ahead | 6/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 1.6 | 1 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 743 | 583 | ±100 | behind | 6/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 296 | 352 | ±46.2 | ahead | 8/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 771 | 869 | ±45.8 | ahead | 7/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 433 | 484 | ±24.2 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 239 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 0.4 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 1.7 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2418 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 3.5 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 2066 | 12936 | ±3677 | ahead | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 663 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 25.9 | 21.3 | ±3.1 | ahead | 10/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 83.2 | 103 | ±10 | ahead | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1509 | 1884 | ±218 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 12.9 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 5123 | 3190 | ±491 | behind | 10/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 18.0 | 27.5 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 8.3 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 75.6 | 117 | ±14.9 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 152 | 172 | ±14.7 | ahead | 8/10 | #62, #63 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 160 | 141 | ±10 | behind | 10/10 | #24, #37 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 82.4 | 128 | ±28.6 | ahead | 9/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 339 | 504 | ±33.7 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 650 | 811 | ±45.0 | ahead | 10/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 9/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 27.1 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 50.8 | 85 | ±15.1 | ahead | 9/10 | #62, #63 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.3 | 32.3 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 166 | 147 | ±10 | behind | 8/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 329 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 785 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.5 | 0.9 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 490 | ±36.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±32.5 | ahead | 6/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 649 | 773 | ±50.9 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 462 | 1107 | ±218 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1335 | 1698 | ±289 | ahead | 7/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 60.4 | 106 | ±22.2 | ahead | 9/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 155 | 182 | ±23.3 | ahead | 8/10 | #62, #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.3 | 40.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 174 | 157 | ±10 | behind | 9/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 203 | 218 | ±13.7 | ahead | 5/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 36.3 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 192 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 70.4 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 283 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.3 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 63.5 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 74.8 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 94 | 153 | ±37.4 | ahead | 9/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 196 | 252 | ±56.0 | ahead | 9/10 | #62, #63 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 179 | 153 | ±26.0 | behind | 6/10 | #24, #37 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 77.2 | 140 | ±27.1 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 161 | 212 | ±40.8 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 88.1 | 117 | ±15.6 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 296 | 352 | ±46.2 | ahead | 8/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 771 | 869 | ±45.8 | ahead | 7/10 | #62, #63 |
| low_bandwidth | `cc.queue_p95_ms` | 433 | 484 | ±24.2 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `cc.rtt_p50_ms` | 239 | 505 | ±33.7 | ahead | 10/10 | #61 |
| low_bandwidth | `net.loss_pct` | 0.4 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 1.7 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 2418 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.freeze_count` | 3.5 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.freeze_total_ms` | 2066 | 12936 | ±3677 | ahead | 9/10 | #5, #37, #24 |
| low_bandwidth | `video.longest_freeze_ms` | 663 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| low_bandwidth | `video.render_fps` | 25.9 | 21.3 | ±3.1 | ahead | 10/10 | #37, #24 |
| outage_3s | `cc.queue_p95_ms` | 56.5 | 38.6 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 125 | 105 | ±13.7 | behind | 7/10 | #24, #37, #20 |
| outage_3s | `video.freeze_total_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24, #20 |
| outage_3s | `video.longest_freeze_ms` | 3465 | 3222 | ±173 | behind | 2/10 | #5, #37, #24, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±34.8 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±34.8 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 801 | ±50.0 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 192 | 238 | ±40.5 | ahead | 7/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 8.3 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| random_loss_5pct | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 7/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 70.4 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 283 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.3 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 63.5 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.longest_freeze_ms` | 74.8 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 74.8 | 160 | ±68.5 | ahead | 8/10 | #62, #63 |
| satellite | `repair.overhead_pct` | 14.4 | 18.0 | ±2 | ahead | 9/10 | #21, #7, #29 |
| satellite | `video.e2e_delay_p95_ms` | 831 | 932 | ±46.6 | ahead | 9/10 | #24, #37 |
| satellite | `video.freeze_count` | 21.7 | 32.6 | ±5.6 | ahead | 9/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6699 | 17933 | ±2588 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.6 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 145 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `net.loss_pct` | 20.4 | 29.6 | ±3.6 | ahead | 9/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 39.3 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 6556 | 8516 | ±961 | ahead | 7/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 13.3 | 18.4 | ±3.7 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 2.3 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 145 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 20.4 | 29.6 | ±3.6 | ahead | 9/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 39.3 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 6556 | 8516 | ±961 | ahead | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 13.3 | 18.4 | ±3.7 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.3 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 329 | 536 | ±41.7 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 90.2 | 162 | ±28.1 | ahead | 8/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 4.7 | 6.0 | ±1.3 | ahead | 7/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.4 | 33.4 | ±2 | ahead | 10/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 785 | 2319 | ±478 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3735 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24, #20 |

