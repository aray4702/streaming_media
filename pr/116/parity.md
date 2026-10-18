## Parity with libwebrtc M157.8103@{#0}

53 scenarios, mean of 10 seeds. **316 gaps**: 160 behind libwebrtc, 156 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

462 gaps on `main`, 316 here: **218 closed, 72 opened**, 49 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.longest_interruption_ms` | 61 | 30 | ±20 | behind | 6/10 | #62, #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 93.3 | 170 | ±20.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 46.4 | 110 | ±17.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `video.avg_kbps` | 743 | 819 | ±41.0 | behind | 10/10 | #37, #61 |
| bufferbloat | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| bufferbloat | `cc.utilization_pct` | 86.6 | 92.3 | ±4.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 139 | 242 | ±52.3 | ahead | 9/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 71.2 | 154 | ±43.8 | ahead | 9/10 | #62, #63 |
| burst_loss | `video.freeze_total_ms` | 215 | 1432 | ±500 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.longest_freeze_ms` | 160 | 298 | ±100 | ahead | 5/10 | #5, #37, #24 |
| burst_loss | `video.avg_kbps` | 997 | 1461 | ±89.1 | behind | 10/10 | #37, #61 |
| burst_loss | `video.keyframe_requests` | 1.2 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `cc.utilization_pct` | 57.7 | 80.4 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.longest_interruption_ms` | 121 | 144 | ±20.1 | ahead | 6/10 | #62, #63 |
| capacity_drop | `video.freeze_count` | 3.6 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.avg_kbps` | 816 | 895 | ±44.8 | behind | 8/10 | #37, #61 |
| capacity_drop | `video.keyframe_requests` | 8.3 | 0 | ±0.5 | behind | 10/10 | #23 |
| capacity_drop | `cc.utilization_pct` | 54.4 | 57.5 | ±2.9 | behind | 5/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `net.loss_pct` | 4.1 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| capacity_drop | `repair.overhead_pct` | 7.2 | 3.1 | ±2 | behind | 9/10 | #21, #7, #29 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 89.3 | 238 | ±40.5 | ahead | 10/10 | #62, #63 |
| clean_link | `video.avg_kbps` | 1875 | 2142 | ±107 | behind | 10/10 | #37, #61 |
| clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| clean_link | `cc.utilization_pct` | 81.9 | 91.6 | ±4.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.avg_kbps` | 1546 | 1779 | ±88.9 | behind | 10/10 | #37, #61 |
| cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `cc.utilization_pct` | 66.1 | 74.2 | ±3.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| high_jitter | `video.render_fps` | 25.7 | 29.8 | ±1.5 | behind | 10/10 | #37, #24, #19 |
| high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `repair.overhead_pct` | 9.0 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `cc.max_settle_ms` | 1350 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 3310 | 2538 | ±362 | behind | 6/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.pause_total_ms` | 2799 | 0 | ±140 | behind | 3/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.render_fps` | 24.9 | 28.8 | ±1.4 | behind | 6/10 | #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 4.6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.max_settle_ms` | 13310 | 14830 | ±742 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 86.6 | 73.5 | ±10 | behind | 8/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `net.loss_pct` | 2.1 | 1.5 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 360 | 1432 | ±500 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1347 | 1461 | ±89.1 | behind | 7/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1798 | 695 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 748 | 471 | ±115 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1076 | 1182 | ±59.1 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 35.6 | 0 | ±1.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 86.0 | 91.4 | ±4.6 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.5 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.4 | 8.3 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 22.4 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.8 | 13.6 | ±3.0 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_random_loss | `video.keyframe_requests` | 3.4 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 556 | 315 | ±34.0 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 806 | 746 | ±40.3 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_shallow | `video.keyframe_requests` | 16 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow | `cc.utilization_pct` | 64.6 | 56.7 | ±3.2 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 850 | 746 | ±42.5 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.keyframe_requests` | 15.3 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow_step | `cc.rtt_p50_ms` | 74.1 | 62.5 | ±10 | behind | 10/10 | #61 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 100 | 238 | ±40.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_random_loss | `video.keyframe_requests` | 3.4 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_18_random_loss_keyframes | `audio.mouth_to_ear_p95_ms` | 112 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 54 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1043 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 2.6 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 60.8 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 8.9 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `cc.rtt_p50_ms` | 72.9 | 62.2 | ±10 | behind | 8/10 | #61 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.6 | 8.2 | ±1.1 | behind | 10/10 | #21, #61, #12 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 4576 | 2538 | ±362 | behind | 9/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3180 | 1875 | ±554 | behind | 9/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 26.5 | 28.8 | ±1.4 | behind | 9/10 | #37, #24 |
| issue_23_capacity_drop | `video.avg_kbps` | 826 | 895 | ±44.8 | behind | 7/10 | #37, #61 |
| issue_23_capacity_drop | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| issue_23_capacity_drop | `net.loss_pct` | 3.5 | 1.5 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.3 | 3.1 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_23_handover | `video.avg_kbps` | 886 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_handover | `cc.utilization_pct` | 72.3 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `cc.queue_p95_ms` | 193 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 20.2 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 60.3 | 33.4 | ±3.0 | behind | 10/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 250 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `video.freeze_count` | 2.6 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.avg_kbps` | 47.4 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| issue_23_low_bandwidth | `cc.max_settle_ms` | 900 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 183 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_23_outage_3s | `audio.concealed_pct` | 11.1 | 10.2 | ±0.6 | behind | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 225 | 170 | ±28.5 | behind | 8/10 | #62, #63 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 172 | 108 | ±24.9 | behind | 9/10 | #62, #63 |
| issue_23_outage_3s | `cc.utilization_pct` | 69.1 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 1.9 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 261 | 137 | ±27.9 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1336 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 390 | 192 | ±37.8 | behind | 8/10 | #24, #37 |
| issue_23_outage_500ms | `video.avg_kbps` | 972 | 1589 | ±79.4 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 59.7 | 87.6 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.max_settle_ms` | 100 | 700 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_23_outage_500ms | `net.loss_pct` | 2.1 | 1.6 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_23_outage_500ms | `repair.overhead_pct` | 9.1 | 2.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 555 | 869 | ±45.8 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 0 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 19.5 | 3279 | ±1648 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.avg_kbps` | 51.0 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `video.keyframe_requests` | 3 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 155 | 484 | ±24.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 106 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 60 | 24 | ±20 | behind | 7/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 744 | 919 | ±45.9 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 16.5 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 77.2 | 90.4 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1300 | 3140 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `repair.overhead_pct` | 15.2 | 11.0 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_5_adjuster_default | `video.avg_kbps` | 1546 | 1779 | ±88.9 | behind | 10/10 | #37, #61 |
| issue_5_adjuster_default | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_adjuster_default | `cc.utilization_pct` | 66.1 | 74.2 | ±3.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 20.9 | 37.4 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.9 | 1.7 | ±0.7 | behind | 9/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.interruptions` | 1.2 | 0.6 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_5_burst15_freeze | `video.avg_kbps` | 991 | 1518 | ±75.9 | behind | 10/10 | #37, #61 |
| issue_5_burst15_freeze | `video.keyframe_requests` | 2.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_burst15_freeze | `cc.utilization_pct` | 58.8 | 84.5 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 20.6 | 46.6 | ±13.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 122 | 149 | ±22.3 | ahead | 6/10 | #62, #63 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2108 | 2570 | ±359 | ahead | 7/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1240 | 1895 | ±545 | ahead | 7/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.avg_kbps` | 831 | 893 | ±44.7 | behind | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 5.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_76_packetization_v2 | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 102 | 156 | ±24.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `video.avg_kbps` | 674 | 1599 | ±184 | behind | 10/10 | #37, #61 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 36.9 | 87.0 | ±10.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 6.4 | 28.0 | ±10.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 15490 | 1290 | ±2439 | behind | 9/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 21.5 | 32.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 913 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 74.7 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `net.loss_pct` | 8.0 | 6.0 | ±1.3 | behind | 8/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 778 | 834 | ±41.7 | behind | 6/10 | #37, #61 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 328 | 1447 | ±109 | behind | 10/10 | #37, #61 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 19.1 | 83.6 | ±6.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.3 | 24.7 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 30000 | 2920 | ±2909 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 14.8 | 40.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 11.5 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_7_no_fec_beside_nack | `audio.mouth_to_ear_p95_ms` | 112 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 54 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1043 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 2.6 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 60.8 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 46.4 | 153 | ±37.4 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `video.avg_kbps` | 1068 | 1304 | ±65.2 | behind | 10/10 | #37, #61 |
| issue_85_video_pause | `cc.utilization_pct` | 76.5 | 88.7 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 250 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `video.freeze_count` | 0.6 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.longest_freeze_ms` | 304 | 3279 | ±1648 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.avg_kbps` | 48.2 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| low_bandwidth | `video.keyframe_requests` | 14.7 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| low_bandwidth | `cc.max_settle_ms` | 910 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| low_bandwidth | `cc.rtt_p50_ms` | 197 | 505 | ±33.7 | ahead | 10/10 | #61 |
| outage_3s | `audio.concealed_pct` | 11.0 | 10.2 | ±0.6 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 214 | 170 | ±28.5 | behind | 6/10 | #62, #63, #20 |
| outage_3s | `audio.jb_delay_p95_ms` | 161 | 108 | ±24.9 | behind | 7/10 | #62, #63, #20 |
| outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| outage_3s | `cc.utilization_pct` | 68.5 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 123 | 144 | ±20.1 | ahead | 5/10 | #62, #63 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2110 | 2538 | ±362 | ahead | 6/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 828 | 895 | ±44.8 | behind | 8/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 5.8 | 0 | ±0.5 | behind | 10/10 | #23 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3188 | 2538 | ±362 | behind | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.render_fps` | 27.4 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 10.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 13750 | 14830 | ±742 | ahead | 5/10 | #15, #48, #61, #20 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 85.9 | 73.5 | ±10 | behind | 8/10 | #61 |
| probe_v2_capacity_drop | `net.loss_pct` | 4.0 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `repair.overhead_pct` | 6.8 | 3.1 | ±2 | behind | 7/10 | #21, #7, #29 |
| probe_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 83.3 | 66.6 | ±10 | behind | 9/10 | #61 |
| probe_v2_cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| random_loss_5pct | `audio.mouth_to_ear_p95_ms` | 112 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 54 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `video.avg_kbps` | 1043 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| random_loss_5pct | `video.keyframe_requests` | 2.6 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| random_loss_5pct | `cc.utilization_pct` | 60.8 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| satellite | `audio.mouth_to_ear_p95_ms` | 374 | 508 | ±93.9 | ahead | 10/10 | #62, #63 |
| satellite | `audio.jb_delay_p95_ms` | 48.4 | 160 | ±68.5 | ahead | 10/10 | #62, #63 |
| satellite | `video.e2e_delay_p95_ms` | 809 | 932 | ±46.6 | ahead | 10/10 | #24, #37 |
| satellite | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `cc.utilization_pct` | 53.8 | 82.4 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.queue_p95_ms` | 13.9 | 43.9 | ±28.2 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.max_settle_ms` | 6450 | 710 | ±500 | behind | 2/10 | #15, #48, #61, #20 |
| satellite | `repair.overhead_pct` | 15.0 | 18.0 | ±2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_total_ms` | 12605 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 1.1 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| very_low_capacity | `cc.max_settle_ms` | 1340 | 400 | ±500 | behind | 9/10 | #15, #48, #61, #20 |
| very_low_capacity | `cc.rtt_p50_ms` | 351 | 328 | ±17.6 | behind | 7/10 | #61 |
| very_low_capacity | `net.loss_pct` | 34.6 | 29.6 | ±3.6 | behind | 8/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 15449 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.4 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `cc.max_settle_ms` | 1560 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 897 | 1299 | ±64.9 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 16.7 | 0 | ±0.8 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 73.1 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 192 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 19.8 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 58.0 | 33.4 | ±2.9 | behind | 10/10 | #21, #7, #29, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `video.e2e_delay_p95_ms` | 236 | 213 | ±14.2 | behind | 6/10 | #24, #37 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| high_jitter | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 236 | 213 | ±14.2 | behind | 6/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `cc.queue_p95_ms` | 78.8 | 46.8 | ±15.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 9.7 | 6.2 | ±2.7 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61 |
| issue_12_lossbwe_random_loss | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 60.8 | 77.5 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 62.7 | 75.6 | ±10 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 68.5 | 569 | ±156 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 31 | 42 | ±10 | ahead | 3/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.8 | 11.9 | ±4.1 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 1711 | 7143 | ±3468 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 545 | 2281 | ±1098 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 27.9 | 24.5 | ±2.2 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| issue_23_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_23_handover | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_23_handover | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61 |
| issue_23_low_bandwidth | `audio.concealed_pct` | 4.3 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 9/10 | #37, #24 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 179 | 105 | ±13.7 | behind | 10/10 | #24, #37 |
| issue_23_outage_3s | `net.loss_pct` | 5.0 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_23_outage_3s | `repair.overhead_pct` | 2.7 | 0.5 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.concealed_pct` | 4.3 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.render_fps` | 18.2 | 27.5 | ±1.4 | behind | 9/10 | #37, #24 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 88.8 | 128 | ±28.6 | ahead | 8/10 | #62, #63 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 520 | 271 | ±226 | behind | 8/10 | #24, #37 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 172 | 147 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 7853 | 4528 | ±592 | behind | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.render_fps` | 23.6 | 27.9 | ±1.4 | behind | 8/10 | #37, #24 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3547 | 2816 | ±513 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 2098 | 0 | ±105 | behind | 2/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 25.3 | 27.7 | ±1.4 | behind | 5/10 | #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 759 | 1107 | ±218 | ahead | 9/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.9 | 3.2 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_76_session_layer_v2 | `video.time_to_first_frame_ms` | 139 | 80.8 | ±50 | behind | 5/10 | #24, #61 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 198 | 153 | ±26.0 | behind | 9/10 | #24, #37 |
| low_bandwidth | `audio.concealed_pct` | 4.3 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 9/10 | #37, #24 |
| outage_3s | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24, #20 |
| outage_3s | `video.freeze_total_ms` | 3991 | 3222 | ±200 | behind | 10/10 | #5, #37, #24, #20 |
| outage_3s | `net.loss_pct` | 5.0 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12, #20 |
| outage_3s | `repair.overhead_pct` | 2.7 | 0.5 | ±2 | behind | 10/10 | #21, #7, #29, #20 |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| very_low_capacity | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 2602 | 354 | ±130 | behind | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 2602 | 354 | ±130 | behind | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 28.3 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| wifi_to_lte_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 7853 | 4528 | ±592 | behind | 8/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `video.e2e_delay_p95_ms` | 106 | 90.6 | ±10 | behind | 8/10 | #24, #37 |
| bufferbloat | `cc.queue_p95_ms` | 72.7 | 47.3 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.queue_p95_ms` | 78.8 | 46.8 | ±15.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 157 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| clean_link | `cc.queue_p95_ms` | 119 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| high_jitter | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61, #19, #24 |
| high_jitter | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 0 | 3.1 | ±1.5 | ahead | 10/10 | #23 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 157 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 119 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0 | 0.9 | ±0.6 | ahead | 8/10 | #23 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 15.7 | 20.3 | ±3.2 | ahead | 8/10 | #21, #7, #29 |
| issue_23_handover | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 16864 | 12936 | ±3677 | behind | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `net.loss_pct` | 10.5 | 6.9 | ±2.0 | behind | 7/10 | #21, #61, #12 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 63.6 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 109 | 54.3 | ±15.1 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 16864 | 12936 | ±3677 | behind | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 9/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 10.5 | 6.9 | ±2.0 | behind | 7/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `video.freeze_count` | 14.4 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 7207 | 3190 | ±491 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 358 | 264 | ±27.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 191 | 157 | ±10 | behind | 8/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 354 | 252 | ±56.0 | behind | 9/10 | #62, #63 |
| issue_85_video_pause | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 10/10 | #23 |
| issue_85_video_pause | `cc.queue_p95_ms` | 127 | 56.9 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 328 | 212 | ±40.8 | behind | 9/10 | #62, #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 176 | 140 | ±27.1 | behind | 8/10 | #62, #63 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 171 | 117 | ±15.6 | behind | 9/10 | #24, #37 |
| lossy_feedback | `cc.queue_p95_ms` | 126 | 49.1 | ±17.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `video.freeze_total_ms` | 16864 | 12936 | ±3677 | behind | 9/10 | #5, #37, #24 |
| low_bandwidth | `net.loss_pct` | 10.5 | 6.9 | ±2.0 | behind | 7/10 | #21, #61, #12 |
| outage_3s | `cc.queue_p95_ms` | 63.6 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| random_loss_5pct | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `audio.longest_interruption_ms` | 140 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `video.freeze_count` | 49.7 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 140 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 49.7 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| wifi_to_lte_handover | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.3 | 1.1 (closer) |
| burst_loss | `audio.concealed_pct` | 1.2 | 0.8 (closer) |
| burst_loss | `video.freeze_count` | 5.2 | 2.8 (closer) |
| capacity_drop | `audio.jb_delay_p95_ms` | 153 | 179 (further) |
| clean_link | `audio.jb_delay_p95_ms` | 101 | 40 (closer) |
| cross_traffic | `cc.queue_p95_ms` | 116 | 157 (further) |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 27.8 | 24.3 (closer) |
| high_jitter | `video.e2e_delay_p95_ms` | 1401 | 50.4 (closer) |
| issue_120_rtx_payload_padding | `audio.concealed_pct` | 0.6 | 0.5 (closer) |
| issue_120_rtx_payload_padding | `video.freeze_count` | 2.8 | 4.7 (further) |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 1007 | 3325 (further) |
| issue_120_rtx_payload_padding | `video.render_fps` | 1.9 | 4.3 (further) |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 11.9 | 30.8 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 491 | 340 (closer) |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 1.3 | 0.8 (closer) |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 4.7 | 2.8 (closer) |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 942 | 1372 (further) |
| issue_12_lossbwe_congestive | `video.render_fps` | 3.1 | 4.9 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 27.3 | 24.3 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 165 | 50.4 (closer) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 117 | 176 (further) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 11.2 | 15.5 (further) |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 159 | 104 (closer) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 14.9 | 24 (further) |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 262 | 588 (further) |
| issue_12_lossbwe_shallow | `net.loss_pct` | 4.0 | 2.0 (closer) |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 9.1 | 2.8 (closer) |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 13.6 | 20.4 (further) |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 18.8 | 21.2 (further) |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 4929 | 5714 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 316 | 178 (closer) |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 11.1 | 4.3 (closer) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 2.9 | 1.3 (closer) |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 7.6 | 3.1 (closer) |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 96 | 40 (closer) |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 143 | 157 (further) |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 159 | 104 (closer) |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.2 | 0.6 (closer) |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 325 | 273 (closer) |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 118 | 104 (closer) |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 31.0 | 24.3 (closer) |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 39.6 | 34.8 (closer) |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 23.3 | 50.4 (further) |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 1575 | 1979 (further) |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 97.3 | 77.5 (closer) |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 14.1 | 10.5 (closer) |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 140 | 179 (further) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 84.2 | 105 (further) |
| issue_23_handover | `audio.jb_delay_p95_ms` | 118 | 141 (further) |
| issue_23_handover | `video.freeze_total_ms` | 999 | 3325 (further) |
| issue_23_handover | `video.longest_freeze_ms` | 1429 | 435 (closer) |
| issue_23_handover | `video.render_fps` | 2.4 | 4.3 (further) |
| issue_23_handover | `video.e2e_delay_p95_ms` | 2064 | 1560 (closer) |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 3845 | 1824 (closer) |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.1 | 9.1 (further) |
| issue_23_outage_3s | `video.freeze_count` | 0.9 | 1 (further) |
| issue_23_outage_3s | `video.freeze_total_ms` | 201 | 770 (further) |
| issue_23_outage_3s | `video.avg_kbps` | 312 | 60.3 (closer) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 107 | 75.8 (closer) |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 0.9 (closer) |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1359 | 223 (closer) |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2476 | 1824 (closer) |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 16.2 | 9.1 (closer) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 11.8 | 15 (further) |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 449 | 332 (closer) |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1408 | 319 (closer) |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 50.9 | 16.3 (closer) |
| issue_53_cwnd_pushback | `net.loss_pct` | 2.1 | 1.4 (closer) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 116 | 157 (further) |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 434 | 214 (closer) |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 28.4 | 13.1 (closer) |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 107 | 90.5 (closer) |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 152 | 184 (further) |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 37.3 | 80.7 (further) |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 44.2 | 28.2 (closer) |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 128 | 141 (further) |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 0.8 | 0.6 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 160 | 53.6 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 132 | 158 (further) |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 68.5 | 37.3 (closer) |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 62 | 45.6 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 31.3 | 24.3 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 39.6 | 34.8 (closer) |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 90.5 | 50.4 (closer) |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.2 | 0.6 (closer) |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 325 | 273 (closer) |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 118 | 104 (closer) |
| issue_85_video_pause | `net.loss_pct` | 2.5 | 3.7 (further) |
| low_bandwidth | `video.e2e_delay_p95_ms` | 3765 | 1824 (closer) |
| low_bandwidth | `repair.overhead_pct` | 7.4 | 9.1 (further) |
| outage_3s | `video.e2e_delay_p95_ms` | 37.1 | 74.6 (further) |
| outage_3s | `video.avg_kbps` | 322 | 60.3 (closer) |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 99.9 | 75.2 (closer) |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 147 | 179 (further) |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 40.9 | 91.2 (further) |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 61.1 | 75.2 (further) |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 159 | 179 (further) |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 20.0 | 32.2 (further) |
| probe_v2_clean_link | `cc.queue_p95_ms` | 28.9 | 42.6 (further) |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 143 | 157 (further) |
| random_loss_5pct | `audio.concealed_pct` | 1.2 | 0.6 (closer) |
| random_loss_5pct | `video.longest_freeze_ms` | 325 | 273 (closer) |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 118 | 104 (closer) |
| satellite | `video.freeze_count` | 10.7 | 13.7 (further) |
| satellite | `video.render_fps` | 4.5 | 5.2 (further) |
| satellite | `video.avg_kbps` | 366 | 72.0 (closer) |
| very_low_capacity | `audio.concealed_pct` | 12.5 | 10.3 (closer) |
| very_low_capacity | `video.longest_freeze_ms` | 975 | 3057 (further) |
| very_low_capacity | `video.e2e_delay_p95_ms` | 6208 | 5035 (closer) |
| very_low_capacity | `video.keyframe_requests` | 210 | 9.2 (closer) |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 0.9 | 10.3 (further) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 633 | 3057 (further) |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 6218 | 5035 (closer) |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 124 | 9.2 (closer) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 85.9 | 105 (further) |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 122 | 141 (further) |
| wifi_to_lte_handover | `video.render_fps` | 1.7 | 4.3 (further) |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 2111 | 1560 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 80 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 107 | 146 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `cc.utilization_pct` | 93.7 | 77.5 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.queue_p95_ms` | 72.7 | 47.3 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `video.e2e_delay_p95_ms` | 106 | 90.6 | ±10 | behind | 8/10 | #24, #37 |
| burst_loss | `audio.concealed_pct` | 2.3 | 1.5 | ±0.5 | behind | 7/10 | #62, #63 |
| burst_loss | `cc.queue_p95_ms` | 78.8 | 46.8 | ±15.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| burst_loss | `video.e2e_delay_p95_ms` | 236 | 213 | ±14.2 | behind | 6/10 | #24, #37 |
| burst_loss | `video.freeze_count` | 3.4 | 6.2 | ±2.0 | ahead | 9/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 320 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| capacity_drop | `cc.queue_p95_ms` | 374 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| clean_link | `audio.jb_delay_p95_ms` | 104 | 144 | ±20.7 | ahead | 9/10 | #62, #63 |
| clean_link | `cc.queue_p95_ms` | 119 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 157 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 285 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 106 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.longest_freeze_ms` | 10.4 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 193 | 218 | ±13.7 | ahead | 9/10 | #62, #63, #19, #24 |
| high_jitter | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 187 | 136 | ±10 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 176 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 759 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 7853 | 4528 | ±592 | behind | 8/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.render_fps` | 23.6 | 27.9 | ±1.4 | behind | 8/10 | #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 320 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 374 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.3 | 1.5 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `cc.queue_p95_ms` | 78.8 | 46.8 | ±15.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 236 | 213 | ±14.2 | behind | 6/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 3.4 | 6.2 | ±2.0 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 9.7 | 6.2 | ±2.7 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3520 | 2148 | ±850 | behind | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.render_fps` | 25.0 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 193 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 187 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 11.5 | 38 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 45.2 | 59.2 | ±10 | ahead | 6/10 | #62, #63 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 28.2 | 12.7 | ±2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 23.9 | 89.0 | ±23.8 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 311 | 135 | ±20 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 332 | 3397 | ±562 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 0.4 | 9.3 | ±4.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 114 | 11683 | ±6940 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 0 | 3.1 | ±1.5 | ahead | 10/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 125 | 2904 | ±604 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.6 | ±1.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 9637 | ±6521 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 29.4 | 17.8 | ±3.6 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 313 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.5 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 112 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 84.1 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 18 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 60.8 | 77.5 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 8.4 | 6.5 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 15.0 | 12.2 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 0.3 | 26.9 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 72 | 7677 | ±1185 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 60.6 | 648 | ±203 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 19.6 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 62.7 | 75.6 | ±10 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 61.0 | 56.7 | ±3.1 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.6 | 7.2 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 16.0 | 12.9 | ±2 | behind | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 447 | 269 | ±22.4 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.2 | 21.4 | ±4.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 54.5 | 5769 | ±1258 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 68.5 | 569 | ±156 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 104 | 144 | ±20.7 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 119 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 157 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 285 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 106 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 10.4 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 313 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.5 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 112 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 84.1 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 313 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.5 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 112 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 84.1 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 193 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 187 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 31 | 42 | ±10 | ahead | 3/10 | #62, #63 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 60.2 | 49.7 | ±3.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 15.7 | 20.3 | ±3.2 | ahead | 8/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 550 | 472 | ±27.5 | ahead | 10/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 438 | 2417 | ±1281 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.8 | 11.9 | ±4.1 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 1711 | 7143 | ±3468 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0 | 0.9 | ±0.6 | ahead | 8/10 | #23 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 545 | 2281 | ±1098 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 27.9 | 24.5 | ±2.2 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 320 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 374 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| issue_23_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 176 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 759 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 7853 | 4528 | ±592 | behind | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24 |
| issue_23_handover | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 23.6 | 27.9 | ±1.4 | behind | 8/10 | #37, #24 |
| issue_23_low_bandwidth | `audio.concealed_pct` | 4.3 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `net.loss_pct` | 10.5 | 6.9 | ±2.0 | behind | 7/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 7.8 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 2894 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 16864 | 12936 | ±3677 | behind | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 9/10 | #37, #24 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 63.6 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `net.loss_pct` | 5.0 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_23_outage_3s | `repair.overhead_pct` | 2.7 | 0.5 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_outage_3s | `video.avg_kbps` | 1081 | 1142 | ±57.1 | behind | 5/10 | #37, #61 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 179 | 105 | ±13.7 | behind | 10/10 | #24, #37 |
| issue_23_outage_3s | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3991 | 3222 | ±200 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 280 | 204 | ±38.3 | behind | 9/10 | #62, #63 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 109 | 54.3 | ±15.1 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `video.freeze_count` | 1.9 | 1 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 806 | 583 | ±100 | behind | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.concealed_pct` | 4.3 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 10.5 | 6.9 | ±2.0 | behind | 7/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 7.8 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2894 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 16864 | 12936 | ±3677 | behind | 9/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 88 | 103 | ±10 | ahead | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 115 | 131 | ±10 | ahead | 4/10 | #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 7.7 | 6.2 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1565 | 1884 | ±218 | ahead | 7/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 14.4 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 7207 | 3190 | ±491 | behind | 9/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1471 | 1140 | ±256 | behind | 6/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 18.2 | 27.5 | ±1.4 | behind | 9/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 285 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 106 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 10.4 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 63.6 | 117 | ±14.9 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 118 | 172 | ±14.7 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 73.3 | 86.3 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 965 | 1179 | ±59.0 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 159 | 141 | ±10 | behind | 10/10 | #24, #37 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 88.8 | 128 | ±28.6 | ahead | 8/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 320 | 504 | ±34.7 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 720 | 811 | ±45.5 | ahead | 9/10 | #62, #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 369 | 289 | ±23.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 520 | 271 | ±226 | behind | 8/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 9.7 | 6.2 | ±2.7 | behind | 7/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 3520 | 2148 | ±850 | behind | 9/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 25.0 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 56.8 | 85 | ±15.1 | ahead | 8/10 | #62, #63 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 172 | 147 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 176 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 759 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 7853 | 4528 | ±592 | behind | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.render_fps` | 23.6 | 27.9 | ±1.4 | behind | 8/10 | #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.5 | 0.9 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 333 | 490 | ±36.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 719 | 773 | ±50.9 | ahead | 6/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 358 | 264 | ±27.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.9 | 3.2 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 759 | 1107 | ±218 | ahead | 9/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3547 | 2816 | ±513 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 2098 | 0 | ±105 | behind | 2/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 25.3 | 27.7 | ±1.4 | behind | 5/10 | #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 60.4 | 106 | ±22.2 | ahead | 9/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 145 | 182 | ±23.3 | ahead | 8/10 | #62, #63 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 191 | 157 | ±10 | behind | 8/10 | #24, #37 |
| issue_76_session_layer_v2 | `video.time_to_first_frame_ms` | 139 | 80.8 | ±50 | behind | 5/10 | #24, #61 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 193 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 47.1 | 37.1 | ±7.6 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 714 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 187 | 136 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 243 | 92.7 | ±50 | behind | 10/10 | #24, #61 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 313 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.5 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 112 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 84.1 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 354 | 252 | ±56.0 | behind | 9/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 110 | 2560 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 127 | 56.9 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `net.loss_pct` | 8.1 | 11.8 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 198 | 153 | ±26.0 | behind | 9/10 | #24, #37 |
| issue_85_video_pause | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 10/10 | #23 |
| issue_85_video_pause | `video.pause_total_ms` | 10217 | 12273 | ±614 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.4 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 176 | 140 | ±27.1 | behind | 8/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 328 | 212 | ±40.8 | behind | 9/10 | #62, #63 |
| lossy_feedback | `cc.queue_p95_ms` | 126 | 49.1 | ±17.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 171 | 117 | ±15.6 | behind | 9/10 | #24, #37 |
| low_bandwidth | `audio.concealed_pct` | 4.3 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `net.loss_pct` | 10.5 | 6.9 | ±2.0 | behind | 7/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 7.8 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 2894 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.freeze_total_ms` | 16864 | 12936 | ±3677 | behind | 9/10 | #5, #37, #24 |
| low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 9/10 | #37, #24 |
| outage_3s | `cc.queue_p95_ms` | 63.6 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `net.loss_pct` | 5.0 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12, #20 |
| outage_3s | `repair.overhead_pct` | 2.7 | 0.5 | ±2 | behind | 10/10 | #21, #7, #29, #20 |
| outage_3s | `video.avg_kbps` | 1081 | 1142 | ±57.1 | behind | 5/10 | #37, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 179 | 105 | ±13.7 | behind | 10/10 | #24, #37, #20 |
| outage_3s | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24, #20 |
| outage_3s | `video.freeze_total_ms` | 3991 | 3222 | ±200 | behind | 10/10 | #5, #37, #24, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 320 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 374 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 320 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 725 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 374 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 590 | 249 | ±227 | behind | 9/10 | #24, #37 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 104 | 144 | ±20.7 | ahead | 9/10 | #62, #63 |
| probe_v2_clean_link | `cc.queue_p95_ms` | 119 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 157 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 285 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 106 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 10.4 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| random_loss_5pct | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| random_loss_5pct | `cc.queue_p95_ms` | 42.9 | 29.5 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 313 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.5 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 112 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.longest_freeze_ms` | 84.1 | 357 | ±100 | ahead | 9/10 | #5, #37, #24 |
| satellite | `video.avg_kbps` | 892 | 964 | ±48.2 | behind | 2/10 | #37, #61 |
| satellite | `video.freeze_count` | 18.9 | 32.6 | ±5.6 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6258 | 17933 | ±2588 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.3 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.concealed_pct` | 10.9 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 140 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `repair.overhead_pct` | 28.3 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 3481 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 49.7 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 12.5 | 3.3 | ±0.7 | behind | 10/10 | #23 |
| very_low_capacity | `video.longest_freeze_ms` | 1398 | 4455 | ±406 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 2602 | 354 | ±130 | behind | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 10.9 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 140 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 28.3 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 3481 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 49.7 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 12.5 | 3.3 | ±0.7 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 1398 | 4455 | ±406 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 2602 | 354 | ±130 | behind | 10/10 | #24, #61 |
| wifi_to_lte_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 4/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 176 | 317 | ±21.6 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 165 | 135 | ±10 | behind | 3/10 | #61, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 759 | 2319 | ±478 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 9 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 7853 | 4528 | ±592 | behind | 8/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.longest_freeze_ms` | 2352 | 1917 | ±313 | behind | 5/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.pause_total_ms` | 507 | 0 | ±100 | behind | 1/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.render_fps` | 23.6 | 27.9 | ±1.4 | behind | 8/10 | #37, #24, #20 |

