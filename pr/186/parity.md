## Parity with libwebrtc M157.8103@{#0}

71 scenarios, mean of 10 seeds. **294 gaps**: 74 behind libwebrtc, 220 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

546 gaps on `main`, 294 here: **279 closed, 27 opened**, 19 changed direction.

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
| issue_120_rtx_payload_padding | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 5.3 | 4.3 | ±0.7 | behind | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.avg_kbps` | 1233 | 1299 | ±64.9 | behind | 5/10 | #37, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1987 | 2538 | ±362 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 8 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 251 | 283 | ±26.9 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 84.4 | 73.5 | ±10 | behind | 6/10 | #61 |
| issue_123_v2_allocation_min | `video.freeze_count` | 14.2 | 18.4 | ±3.7 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.2 | 3.3 | ±0.7 | behind | 7/10 | #23 |
| issue_126_audio_v2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_126_audio_v2 | `cc.rtt_p50_ms` | 73.7 | 62.8 | ±10 | behind | 8/10 | #61 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 412 | 566 | ±142 | ahead | 6/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.e2e_delay_p95_ms` | 485 | 443 | ±24.3 | behind | 7/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.render_fps` | 26.6 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.avg_kbps` | 102 | 917 | ±186 | behind | 10/10 | #37, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.keyframe_requests` | 3.6 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.utilization_pct` | 8.3 | 56.8 | ±12.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.max_settle_ms` | 32040 | 15910 | ±9723 | behind | 8/10 | #15, #48, #61, #20 |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 11.2 | 47.9 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
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
| issue_21_shallow_drop_repair | `video.avg_kbps` | 562 | 472 | ±28.1 | ahead | 9/10 | #37, #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 62.7 | 49.7 | ±3.1 | ahead | 10/10 | #19, #15, #48, #12, #61 |
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
| issue_23_handover | `net.loss_pct` | 17.9 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12 |
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
| issue_48_prober_low_bandwidth | `video.avg_kbps` | 52.9 | 73.4 | ±20 | behind | 6/10 | #37, #61 |
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
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 173 | 141 | ±10 | behind | 9/10 | #24, #37 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 727 | 1179 | ±59.0 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 56.6 | 86.3 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 20.4 | 37.4 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.5 | 1.7 | ±0.7 | behind | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.interruptions` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #62, #63 |
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
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 91.7 | 153 | ±26.0 | ahead | 10/10 | #24, #37 |
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
| outage_3s | `video.e2e_delay_p95_ms` | 146 | 105 | ±13.7 | behind | 8/10 | #24, #37, #20 |
| outage_3s | `video.avg_kbps` | 826 | 1142 | ±57.1 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.keyframe_requests` | 5.3 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| outage_3s | `cc.utilization_pct` | 69.0 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 119 | 144 | ±20.1 | ahead | 4/10 | #62, #63 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2133 | 2538 | ±362 | ahead | 6/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 833 | 895 | ±44.8 | behind | 7/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 6.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 325 | 283 | ±26.9 | behind | 8/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3118 | 2538 | ±362 | behind | 9/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 7.9 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 283 | ±26.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 84.5 | 73.5 | ±10 | behind | 6/10 | #61 |
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
| very_low_capacity | `video.freeze_count` | 10.4 | 18.4 | ±3.7 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_total_ms` | 17081 | 24870 | ±4734 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity | `video.longest_freeze_ms` | 3684 | 4455 | ±406 | ahead | 7/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 219 | 3.3 | ±10.9 | behind | 10/10 | #23 |
| very_low_capacity | `cc.max_settle_ms` | 1420 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 407 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 10.6 | 18.4 | ±3.7 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3780 | 4455 | ±406 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 123 | 3.3 | ±6.1 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `cc.max_settle_ms` | 1600 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.5 | 4.3 | ±0.7 | ahead | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.render_fps` | 26.5 | 27.9 | ±1.4 | behind | 4/10 | #37, #24, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 850 | 1299 | ±64.9 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 14.5 | 0 | ±0.7 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 69.8 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 19.2 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 32.4 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 434 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 22.3 | 42 | ±10 | ahead | 9/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.9 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 3053 | 7143 | ±3468 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 6337 | 2959 | ±3121 | behind | 6/10 | #5, #37, #24 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 1008 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 24.7 | 21.3 | ±3.1 | ahead | 7/10 | #37, #24 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 435 | 484 | ±24.2 | ahead | 2/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.render_fps` | 17.9 | 27.5 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 174 | 147 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±32.5 | ahead | 6/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| low_bandwidth | `cc.queue_p95_ms` | 435 | 484 | ±24.2 | ahead | 2/10 | #19, #15, #48, #12, #61 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 38.8 | 425 | ±357 | ahead | 8/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 60.8 | 247 | ±114 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3609 | 2904 | ±604 | behind | 8/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 434 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_23_handover | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 51.3 | 38.6 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 144 | 204 | ±38.3 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 71.2 | 137 | ±27.9 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 154 | 192 | ±37.8 | ahead | 6/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 12.7 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 5165 | 3190 | ±491 | behind | 10/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 176 | 157 | ±10 | behind | 7/10 | #24, #37 |
| outage_3s | `cc.queue_p95_ms` | 51.3 | 38.6 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61, #20 |
| very_low_capacity | `net.loss_pct` | 20.8 | 29.6 | ±3.6 | ahead | 10/10 | #21, #61, #12 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| bufferbloat | `audio.jb_delay_p95_ms` | 65.2 | 33.2 (closer) |
| burst_loss | `audio.concealed_pct` | 1.1 | 0.6 (closer) |
| burst_loss | `audio.jb_delay_p95_ms` | 93.6 | 61.6 (closer) |
| burst_loss | `video.freeze_count` | 5.4 | 4.8 (closer) |
| burst_loss | `video.freeze_total_ms` | 1252 | 1048 (closer) |
| clean_link | `audio.mouth_to_ear_p95_ms` | 136 | 45.8 (closer) |
| clean_link | `audio.jb_delay_p95_ms` | 100 | 59.2 (closer) |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 382 | 258 (closer) |
| cross_traffic | `audio.jb_delay_p95_ms` | 265 | 222 (closer) |
| cross_traffic | `cc.queue_p95_ms` | 135 | 53.4 (closer) |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 22.9 | 15.4 (closer) |
| high_jitter | `audio.jb_delay_p95_ms` | 41.2 | 34.8 (closer) |
| high_jitter | `video.e2e_delay_p95_ms` | 1395 | 70.4 (closer) |
| high_jitter | `repair.overhead_pct` | 30.0 | 6.6 (closer) |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 50.8 | 35.5 (closer) |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 163 | 142 (closer) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.9 | 3.4 (further) |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 3098 | 2415 (closer) |
| issue_123_v2_allocation_min | `net.loss_pct` | 9.9 | 8.8 (closer) |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 0.9 | 0.8 (closer) |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 1889 | 43.3 (closer) |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 1.1 | 0.6 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 26.1 | 70.4 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 5.4 | 6.6 (further) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 26.1 | 29 (further) |
| issue_12_lossbwe_policer | `video.freeze_count` | 9.1 | 5.0 (closer) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2900 | 1248 (closer) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 154 | 24.7 (closer) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 72.2 | 39.2 (closer) |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 273 | 316 (further) |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 111 | 79.9 (closer) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 15.2 | 24 (further) |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 361 | 553 (further) |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 43.0 | 76.1 (further) |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 133 | 45.8 (closer) |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 100 | 59.2 (closer) |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 273 | 316 (further) |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 111 | 79.9 (closer) |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 0.9 | 0.6 (closer) |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 68.4 | 45.6 (closer) |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 23.7 | 15.4 (closer) |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 42.8 | 34.8 (closer) |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 21.4 | 70.4 (further) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.1 | 6.6 (closer) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 55.5 | 1.0 (closer) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 70.7 | 175 (further) |
| issue_23_handover | `audio.jb_delay_p95_ms` | 112 | 154 (further) |
| issue_23_handover | `video.e2e_delay_p95_ms` | 2041 | 1517 (closer) |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 40 | 48 (further) |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 66.4 | 167 (further) |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 67.1 | 128 (further) |
| issue_23_low_bandwidth | `video.freeze_count` | 14.4 | 11.1 (closer) |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 7027 | 8523 (further) |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 3808 | 2281 (closer) |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 292 | 218 (closer) |
| issue_23_low_bandwidth | `net.loss_pct` | 4.5 | 5.9 (further) |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.3 | 14.9 (further) |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 0.6 (closer) |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1342 | 155 (closer) |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 430 | 167 (closer) |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 17.8 | 11.1 (closer) |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 12936 | 8523 (closer) |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 3259 | 2271 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 6.7 | 3.4 (closer) |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 321 | 49.3 (closer) |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 402 | 218 (closer) |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 6.9 | 5.9 (closer) |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 16.9 | 14.9 (closer) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 13.4 | 19.8 (further) |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1335 | 404 (closer) |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 382 | 258 (closer) |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 265 | 222 (closer) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 135 | 53.4 (closer) |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 37.7 | 20.6 (closer) |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 31.2 | 48.4 (further) |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 98.5 | 160 (further) |
| issue_61_googcc_glue_congestive | `video.render_fps` | 4.4 | 2.8 (closer) |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 42.6 | 30.2 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 9.8 | 3.1 (closer) |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 85.7 | 175 (further) |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 125 | 154 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 2.1 | 5.4 (further) |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 0.8 | 0.7 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 144 | 123 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 490 | 363 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 238 | 642 (further) |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 58.4 | 30.3 (closer) |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 62 | 46.8 (closer) |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 20.8 | 3.5 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 24.6 | 15.4 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 42.8 | 34.8 (closer) |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 601 | 70.4 (closer) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 26.6 | 6.6 (closer) |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 0.9 | 0.6 (closer) |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 68.4 | 45.6 (closer) |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 136 | 56.1 (closer) |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 106 | 59 (closer) |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 100 | 49.9 (closer) |
| lossy_feedback | `audio.jb_delay_p95_ms` | 92 | 62.8 (closer) |
| lossy_feedback | `video.e2e_delay_p95_ms` | 41.6 | 27.1 (closer) |
| low_bandwidth | `audio.longest_interruption_ms` | 40 | 48 (further) |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 70.1 | 167 (further) |
| low_bandwidth | `audio.jb_delay_p95_ms` | 76.7 | 128 (further) |
| low_bandwidth | `video.freeze_count` | 17.4 | 11.1 (closer) |
| low_bandwidth | `video.freeze_total_ms` | 12570 | 8523 (closer) |
| low_bandwidth | `video.longest_freeze_ms` | 2957 | 2271 (closer) |
| low_bandwidth | `video.render_fps` | 3.8 | 3.4 (closer) |
| low_bandwidth | `video.e2e_delay_p95_ms` | 3773 | 2281 (closer) |
| low_bandwidth | `cc.rtt_p50_ms` | 291 | 218 (closer) |
| low_bandwidth | `net.loss_pct` | 4.7 | 5.9 (further) |
| low_bandwidth | `repair.overhead_pct` | 7.9 | 14.9 (further) |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 86.6 | 142 (further) |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 89.3 | 142 (further) |
| random_loss_5pct | `audio.concealed_pct` | 0.9 | 0.6 (closer) |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 68.4 | 45.6 (closer) |
| satellite | `audio.jb_delay_p95_ms` | 112 | 70 (closer) |
| satellite | `video.e2e_delay_p95_ms` | 103 | 87.5 (closer) |
| satellite | `repair.overhead_pct` | 2.5 | 3.2 (further) |
| very_low_capacity | `audio.longest_interruption_ms` | 77 | 44 (closer) |
| very_low_capacity | `audio.jb_delay_p95_ms` | 137 | 56.7 (closer) |
| very_low_capacity | `video.render_fps` | 4.9 | 3.4 (closer) |
| very_low_capacity | `video.e2e_delay_p95_ms` | 6242 | 2415 (closer) |
| very_low_capacity | `repair.overhead_pct` | 68.2 | 58.7 (closer) |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 143 | 44 (closer) |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 135 | 56.7 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.5 | 3.4 (closer) |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 6420 | 2415 (closer) |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 3.7 | 8.8 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 19.2 | 58.7 (further) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 74.6 | 175 (further) |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 115 | 154 (further) |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 2109 | 1517 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 80 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 117 | 146 | ±10 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 76.8 | 110 | ±17.6 | ahead | 8/10 | #62, #63 |
| burst_loss | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 6/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 92.4 | 154 | ±43.8 | ahead | 9/10 | #62, #63 |
| burst_loss | `video.freeze_count` | 1.4 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.freeze_total_ms` | 385 | 1432 | ±500 | ahead | 9/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.jb_delay_p95_ms` | 84.8 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 192 | 238 | ±40.5 | ahead | 7/10 | #62, #63 |
| cross_traffic | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.longest_freeze_ms` | 4.5 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 202 | 218 | ±13.7 | ahead | 6/10 | #62, #63, #19, #24 |
| high_jitter | `repair.overhead_pct` | 36.8 | 43.4 | ±4.0 | ahead | 8/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 207 | 136 | ±10.3 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 361 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 802 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 150 | 207 | ±51.4 | ahead | 9/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 20.8 | 29.6 | ±3.6 | ahead | 10/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 38.8 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 6101 | 8516 | ±961 | ahead | 8/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.7 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 76.8 | 110 | ±17.6 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 92.4 | 154 | ±43.8 | ahead | 9/10 | #62, #63 |
| issue_124_burst_loss_v2 | `video.freeze_count` | 1.4 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_124_burst_loss_v2 | `video.freeze_total_ms` | 385 | 1432 | ±500 | ahead | 9/10 | #5, #37, #24 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 82.8 | 141 | ±17.5 | ahead | 40/40 | #62, #63 |
| issue_124_clean_link_v2 | `audio.mouth_to_ear_p95_ms` | 189 | 229 | ±34.3 | ahead | 31/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 89.6 | 158 | ±28.6 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 297 | 347 | ±30.3 | ahead | 9/10 | #62, #63 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 32.4 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 4.2 | 65.8 | ±6.5 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 1332 | 19132 | ±1879 | ahead | 10/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 0.9 | 0.1 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.jb_delay_p95_ms` | 72 | 99 | ±10 | ahead | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 217 | 174 | ±10.8 | behind | 10/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 38.8 | 425 | ±357 | ahead | 8/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 60.8 | 247 | ±114 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 92.4 | 154 | ±43.8 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 1.4 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 385 | 1432 | ±500 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_congestive | `video.render_fps` | 27.1 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 202 | 218 | ±13.7 | ahead | 6/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 36.8 | 43.4 | ±4.0 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 207 | 136 | ±10.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 9 | 38 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.8 | 89.0 | ±23.8 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 160 | 135 | ±20 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2149 | 3397 | ±562 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.3 | 9.3 | ±4.0 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3609 | 2904 | ±604 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 72.4 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 289 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 24.1 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 41 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 18 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 0.4 | 26.9 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 105 | 7677 | ±1185 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 95.4 | 648 | ±203 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 20.7 | 40 | ±10 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 7.8 | 7.2 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 437 | 269 | ±21.8 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.4 | 21.4 | ±4.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 120 | 5769 | ±1258 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 130 | 569 | ±156 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 169 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 434 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 169 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4550 | 3070 | ±1029 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 434 | 325 | ±80.2 | behind | 7/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 202 | 218 | ±13.7 | ahead | 6/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 36.8 | 43.4 | ±4.0 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 207 | 136 | ±10.3 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 80 | 107 | ±13.4 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.2 | 43.2 | ±3.4 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 191 | 143 | ±15.8 | behind | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.time_to_first_frame_ms` | 179 | 88.6 | ±50 | behind | 6/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 78.4 | 113 | ±14.2 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 198 | 213 | ±14.4 | ahead | 5/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 35.8 | 43.8 | ±2.9 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.e2e_delay_p95_ms` | 210 | 134 | ±10.5 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 188 | 89.4 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 51.3 | 38.6 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 22.3 | 42 | ±10 | ahead | 9/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.9 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 3053 | 7143 | ±3468 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.9 | 0.9 | ±0.6 | behind | 8/10 | #23 |
| issue_154_shallow_drop_repair_v2 | `video.pause_total_ms` | 6337 | 2959 | ±3121 | behind | 6/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 192 | 238 | ±40.5 | ahead | 7/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 4.5 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 72.4 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 289 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 24.1 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 41 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 72.4 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 289 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 24.1 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 41 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 202 | 218 | ±13.7 | ahead | 6/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 36.8 | 43.4 | ±4.0 | ahead | 8/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 207 | 136 | ±10.3 | behind | 10/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 22.3 | 42 | ±10 | ahead | 9/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.9 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 3053 | 7143 | ±3468 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.9 | 0.9 | ±0.6 | behind | 8/10 | #23 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 6337 | 2959 | ±3121 | behind | 6/10 | #5, #37, #24 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 361 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 802 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 224 | 352 | ±46.2 | ahead | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 702 | 869 | ±45.8 | ahead | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 435 | 484 | ±24.2 | ahead | 2/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 287 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_23_low_bandwidth | `net.loss_pct` | 0.9 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 2.0 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 2437 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_count` | 6.7 | 17.8 | ±4.3 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 4413 | 12936 | ±3677 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 1008 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 24.7 | 21.3 | ±3.1 | ahead | 7/10 | #37, #24 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 51.3 | 38.6 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 71.2 | 137 | ±27.9 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 144 | 204 | ±38.3 | ahead | 9/10 | #62, #63 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 154 | 192 | ±37.8 | ahead | 6/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 1.6 | 1 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 737 | 583 | ±100 | behind | 6/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 224 | 352 | ±46.2 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 702 | 869 | ±45.8 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 435 | 484 | ±24.2 | ahead | 2/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 287 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 0.9 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 2.0 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2437 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 6.7 | 17.8 | ±4.3 | ahead | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 4413 | 12936 | ±3677 | ahead | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 1008 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 24.7 | 21.3 | ±3.1 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 83.2 | 103 | ±10 | ahead | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1480 | 1884 | ±218 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 12.7 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 5165 | 3190 | ±491 | behind | 10/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 17.9 | 27.5 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 4.5 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 75.6 | 117 | ±14.9 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 152 | 172 | ±14.7 | ahead | 8/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 79.6 | 128 | ±28.6 | ahead | 9/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.mouth_to_ear_p95_ms` | 179 | 218 | ±38.3 | ahead | 7/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 339 | 504 | ±34.7 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 650 | 811 | ±45.5 | ahead | 10/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 9/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 27.1 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 54.8 | 85 | ±15.1 | ahead | 9/10 | #62, #63 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.2 | 32.3 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 174 | 147 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 361 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 802 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.6 | 0.9 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 490 | ±36.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±32.5 | ahead | 6/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 649 | 773 | ±50.9 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 466 | 1107 | ±218 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1335 | 1698 | ±289 | ahead | 7/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 59.2 | 106 | ±22.2 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 152 | 182 | ±23.3 | ahead | 8/10 | #62, #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.5 | 40.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 176 | 157 | ±10 | behind | 7/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 202 | 218 | ±13.7 | ahead | 6/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 36.8 | 43.4 | ±4.0 | ahead | 8/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 207 | 136 | ±10.3 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 201 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 72.4 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 289 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 24.1 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 41 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 94 | 153 | ±37.4 | ahead | 9/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 196 | 252 | ±56.0 | ahead | 9/10 | #62, #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 77.2 | 140 | ±27.1 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 162 | 212 | ±40.8 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 90.1 | 117 | ±15.6 | ahead | 9/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 224 | 352 | ±46.2 | ahead | 10/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 198 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 702 | 869 | ±45.8 | ahead | 10/10 | #62, #63 |
| low_bandwidth | `cc.queue_p95_ms` | 435 | 484 | ±24.2 | ahead | 2/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `cc.rtt_p50_ms` | 287 | 505 | ±33.7 | ahead | 10/10 | #61 |
| low_bandwidth | `net.loss_pct` | 0.9 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 2.0 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 2437 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.freeze_count` | 6.7 | 17.8 | ±4.3 | ahead | 9/10 | #5, #37, #24 |
| low_bandwidth | `video.freeze_total_ms` | 4413 | 12936 | ±3677 | ahead | 9/10 | #5, #37, #24 |
| low_bandwidth | `video.longest_freeze_ms` | 1008 | 3279 | ±1648 | ahead | 9/10 | #5, #37, #24 |
| low_bandwidth | `video.render_fps` | 24.7 | 21.3 | ±3.1 | ahead | 7/10 | #37, #24 |
| outage_3s | `cc.queue_p95_ms` | 51.3 | 38.6 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 346 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 658 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 192 | 238 | ±40.5 | ahead | 7/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 182 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 426 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 210 | 264 | ±21.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 4.5 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| random_loss_5pct | `audio.concealed_pct` | 0.9 | 0.4 | ±0.5 | behind | 6/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 72.4 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 289 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.1 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 24.1 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.longest_freeze_ms` | 41 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 90 | 160 | ±68.5 | ahead | 8/10 | #62, #63 |
| satellite | `repair.overhead_pct` | 14.8 | 18.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| satellite | `video.e2e_delay_p95_ms` | 844 | 932 | ±46.6 | ahead | 8/10 | #24, #37 |
| satellite | `video.freeze_count` | 21.5 | 32.6 | ±5.6 | ahead | 9/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6288 | 17933 | ±2588 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.8 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 150 | 207 | ±51.4 | ahead | 9/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `net.loss_pct` | 20.8 | 29.6 | ±3.6 | ahead | 10/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 38.8 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 6101 | 8516 | ±961 | ahead | 8/10 | #24, #37 |
| very_low_capacity | `video.render_fps` | 2.7 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 150 | 207 | ±51.4 | ahead | 9/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 20.8 | 29.6 | ±3.6 | ahead | 10/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 38.8 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 6101 | 8516 | ±961 | ahead | 8/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.7 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 163 | 317 | ±21.6 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 361 | 536 | ±41.7 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 127 | 162 | ±28.1 | ahead | 6/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.0 | 33.4 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 802 | 2319 | ±478 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3913 | 4528 | ±592 | ahead | 7/10 | #5, #37, #24, #20 |

