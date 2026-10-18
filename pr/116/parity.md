## Parity with libwebrtc M157.8103@{#0}

51 scenarios, mean of 10 seeds. **311 gaps**: 162 behind libwebrtc, 149 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

442 gaps on `main`, 311 here: **197 closed, 66 opened**, 42 changed direction.

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
| burst_loss | `video.longest_freeze_ms` | 160 | 298 | ±100 | ahead | 5/10 | #5, #37, #24 |
| burst_loss | `video.avg_kbps` | 997 | 1461 | ±89.1 | behind | 10/10 | #37, #61 |
| burst_loss | `video.keyframe_requests` | 1.2 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `cc.utilization_pct` | 57.7 | 80.4 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.longest_interruption_ms` | 121 | 144 | ±20.1 | ahead | 6/10 | #62, #63 |
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
| high_jitter | `video.avg_kbps` | 68.7 | 584 | ±117 | behind | 10/10 | #37, #61, #19, #24 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `cc.utilization_pct` | 6.8 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20, #19, #24 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1347 | 1461 | ±89.1 | behind | 7/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1798 | 695 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 748 | 471 | ±115 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1076 | 1182 | ±59.1 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 35.6 | 0 | ±1.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 86.0 | 91.4 | ±4.6 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.5 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.4 | 8.3 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 251 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 16.8 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
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
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 77.2 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 7.3 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `cc.rtt_p50_ms` | 72.9 | 62.2 | ±10 | behind | 8/10 | #61 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.6 | 8.2 | ±1.1 | behind | 10/10 | #21, #61, #12 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3180 | 1875 | ±554 | behind | 9/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.avg_kbps` | 826 | 895 | ±44.8 | behind | 7/10 | #37, #61 |
| issue_23_capacity_drop | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| issue_23_capacity_drop | `net.loss_pct` | 3.5 | 1.5 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.3 | 3.1 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_23_handover | `video.longest_freeze_ms` | 3346 | 1917 | ±313 | behind | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
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
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 172 | 108 | ±24.9 | behind | 9/10 | #62, #63 |
| issue_23_outage_3s | `video.avg_kbps` | 829 | 1142 | ±57.1 | behind | 10/10 | #37, #61 |
| issue_23_outage_3s | `cc.utilization_pct` | 69.1 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 1.9 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 261 | 137 | ±27.9 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1336 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 390 | 192 | ±37.8 | behind | 8/10 | #24, #37 |
| issue_23_outage_500ms | `video.avg_kbps` | 972 | 1589 | ±79.4 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 59.7 | 87.6 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
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
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 476 | 1884 | ±218 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 744 | 919 | ±45.9 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 16.5 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 77.2 | 90.4 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 79.9 | 131 | ±10 | ahead | 10/10 | #61 |
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
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1240 | 1895 | ±545 | ahead | 7/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.avg_kbps` | 831 | 893 | ±44.7 | behind | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 5.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 36.9 | 87.0 | ±10.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 6.4 | 28.0 | ±10.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 15490 | 1290 | ±2439 | behind | 9/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 21.5 | 32.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `net.loss_pct` | 8.0 | 6.0 | ±1.3 | behind | 8/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.8 | 3.2 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.3 | 24.7 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 117 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 9.2 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
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
| outage_3s | `audio.jb_delay_p95_ms` | 161 | 108 | ±24.9 | behind | 7/10 | #62, #63, #20 |
| outage_3s | `video.avg_kbps` | 820 | 1142 | ±57.1 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| outage_3s | `cc.utilization_pct` | 68.5 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 123 | 144 | ±20.1 | ahead | 5/10 | #62, #63 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 828 | 895 | ±44.8 | behind | 8/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 5.8 | 0 | ±0.5 | behind | 10/10 | #23 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
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
| satellite | `video.avg_kbps` | 599 | 964 | ±48.2 | behind | 10/10 | #37, #61 |
| satellite | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `cc.utilization_pct` | 53.8 | 82.4 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.queue_p95_ms` | 13.9 | 43.9 | ±28.2 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.max_settle_ms` | 6450 | 710 | ±500 | behind | 2/10 | #15, #48, #61, #20 |
| satellite | `repair.overhead_pct` | 15.0 | 18.0 | ±2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `audio.longest_interruption_ms` | 310 | 203 | ±20 | behind | 10/10 | #62, #63 |
| very_low_capacity | `video.freeze_total_ms` | 12605 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `cc.max_settle_ms` | 1340 | 400 | ±500 | behind | 9/10 | #15, #48, #61, #20 |
| very_low_capacity | `net.loss_pct` | 34.6 | 29.6 | ±3.6 | behind | 8/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 300 | 203 | ±20 | behind | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 15449 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `cc.max_settle_ms` | 1560 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 16.7 | 0 | ±0.8 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 192 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 19.8 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 58.0 | 33.4 | ±2.9 | behind | 10/10 | #21, #7, #29, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 729 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| capacity_drop | `video.freeze_total_ms` | 3436 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.render_fps` | 27.1 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| high_jitter | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61, #19 |
| issue_12_lossbwe_burst_loss | `cc.queue_p95_ms` | 74.3 | 46.8 | ±15.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 10 | 6.2 | ±2.7 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61 |
| issue_12_lossbwe_random_loss | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 60.8 | 77.5 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 61.9 | 75.6 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 51.1 | 569 | ±156 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 30.2 | 42 | ±10 | ahead | 3/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.7 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 1486 | 7143 | ±3468 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 452 | 2281 | ±1098 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 28.2 | 24.5 | ±2.2 | ahead | 7/10 | #37, #24 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 729 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| issue_23_capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| issue_23_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_23_handover | `cc.max_settle_ms` | 2110 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `audio.concealed_pct` | 4.0 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 8/10 | #37, #24 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 173 | 105 | ±13.7 | behind | 10/10 | #24, #37 |
| issue_23_outage_3s | `net.loss_pct` | 4.9 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_23_outage_3s | `repair.overhead_pct` | 2.5 | 0.5 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.concealed_pct` | 4.0 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `video.render_fps` | 16.7 | 27.5 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 86.8 | 128 | ±28.6 | ahead | 7/10 | #62, #63 |
| issue_5_keyframe_flush | `video.render_fps` | 27.2 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 512 | 271 | ±226 | behind | 8/10 | #24, #37 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 172 | 147 | ±10 | behind | 9/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 8.4 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 7297 | 4528 | ±592 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.render_fps` | 24.4 | 27.9 | ±1.4 | behind | 6/10 | #37, #24 |
| issue_76_rtx_v2_handover | `cc.max_settle_ms` | 2110 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3348 | 2816 | ±513 | behind | 5/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 25.3 | 27.7 | ±1.4 | behind | 2/10 | #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 694 | 1107 | ±218 | ahead | 8/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 359 | 264 | ±27.3 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `video.time_to_first_frame_ms` | 134 | 80.8 | ±50 | behind | 5/10 | #24, #61 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 196 | 153 | ±26.0 | behind | 10/10 | #24, #37 |
| low_bandwidth | `audio.concealed_pct` | 4.0 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 8/10 | #37, #24 |
| outage_3s | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24, #20 |
| outage_3s | `video.freeze_total_ms` | 3868 | 3222 | ±193 | behind | 10/10 | #5, #37, #24, #20 |
| outage_3s | `net.loss_pct` | 4.9 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12, #20 |
| outage_3s | `repair.overhead_pct` | 2.5 | 0.5 | ±2 | behind | 5/10 | #21, #7, #29, #20 |
| pacer_v2_capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.render_fps` | 27.1 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| probe_v2_capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| very_low_capacity | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `cc.rtt_p50_ms` | 346 | 328 | ±17.3 | behind | 3/10 | #61 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 13.0 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| wifi_to_lte_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 7297 | 4528 | ±592 | behind | 6/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `cc.max_settle_ms` | 2110 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `video.e2e_delay_p95_ms` | 106 | 90.6 | ±10 | behind | 9/10 | #24, #37 |
| bufferbloat | `cc.queue_p95_ms` | 73.0 | 47.3 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.queue_p95_ms` | 74.3 | 46.8 | ±15.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 154 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| clean_link | `cc.queue_p95_ms` | 116 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 0 | 3.1 | ±1.5 | ahead | 10/10 | #23 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 154 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 116 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0 | 0.9 | ±0.6 | ahead | 8/10 | #23 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 15.6 | 20.3 | ±3.2 | ahead | 7/10 | #21, #7, #29 |
| issue_23_handover | `video.freeze_count` | 8.4 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 17337 | 12936 | ±3677 | behind | 8/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `net.loss_pct` | 10.4 | 6.9 | ±2.0 | behind | 8/10 | #21, #61, #12 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 66.4 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 112 | 54.3 | ±15.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 17337 | 12936 | ±3677 | behind | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 8/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 10.4 | 6.9 | ±2.0 | behind | 8/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `video.freeze_count` | 16.7 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 8315 | 3190 | ±491 | behind | 8/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 3396 | 2570 | ±359 | behind | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 173 | 157 | ±10 | behind | 8/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 348 | 252 | ±56.0 | behind | 10/10 | #62, #63 |
| issue_85_video_pause | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 10/10 | #23 |
| issue_85_video_pause | `cc.queue_p95_ms` | 129 | 56.9 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 325 | 212 | ±40.8 | behind | 9/10 | #62, #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 170 | 140 | ±27.1 | behind | 8/10 | #62, #63 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 174 | 117 | ±15.6 | behind | 10/10 | #24, #37 |
| lossy_feedback | `cc.queue_p95_ms` | 130 | 49.1 | ±17.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `video.freeze_total_ms` | 17337 | 12936 | ±3677 | behind | 8/10 | #5, #37, #24 |
| low_bandwidth | `net.loss_pct` | 10.4 | 6.9 | ±2.0 | behind | 8/10 | #21, #61, #12 |
| outage_3s | `cc.queue_p95_ms` | 66.4 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 3436 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| random_loss_5pct | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `video.freeze_count` | 45.6 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 1 | 3.3 | ±0.7 | ahead | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 45.6 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 1 | 3.3 | ±0.7 | ahead | 10/10 | #23 |
| wifi_to_lte_handover | `video.freeze_count` | 8.4 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.3 | 1.1 (closer) |
| burst_loss | `audio.concealed_pct` | 1.2 | 0.7 (closer) |
| burst_loss | `video.freeze_count` | 5.2 | 3.8 (closer) |
| burst_loss | `video.freeze_total_ms` | 1217 | 824 (closer) |
| capacity_drop | `audio.jb_delay_p95_ms` | 153 | 172 (further) |
| capacity_drop | `video.freeze_count` | 1.4 | 1 (closer) |
| capacity_drop | `cc.queue_p95_ms` | 101 | 89.5 (closer) |
| clean_link | `audio.jb_delay_p95_ms` | 101 | 42.4 (closer) |
| cross_traffic | `cc.queue_p95_ms` | 116 | 157 (further) |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 27.8 | 22.1 (closer) |
| high_jitter | `video.e2e_delay_p95_ms` | 1401 | 109 (closer) |
| high_jitter | `repair.overhead_pct` | 34.4 | 4.1 (closer) |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 1.3 | 0.7 (closer) |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 4.7 | 3.8 (closer) |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 1073 | 824 (closer) |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 942 | 1172 (further) |
| issue_12_lossbwe_congestive | `video.render_fps` | 3.1 | 4.5 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 27.3 | 22.1 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 165 | 109 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 21.0 | 4.1 (closer) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 117 | 173 (further) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 11.2 | 15.1 (further) |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 159 | 114 (closer) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 14.9 | 24 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 6924 | 7617 (further) |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 262 | 573 (further) |
| issue_12_lossbwe_shallow | `net.loss_pct` | 4.0 | 1.8 (closer) |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 9.1 | 2.5 (closer) |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 13.6 | 21.2 (further) |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 18.8 | 21.3 (further) |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 4929 | 5732 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 316 | 175 (closer) |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 11.1 | 4.0 (closer) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 2.9 | 1.3 (closer) |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 7.6 | 2.7 (closer) |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 96 | 42.4 (closer) |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 143 | 157 (further) |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 159 | 114 (closer) |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.2 | 0.6 (closer) |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 325 | 285 (closer) |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 31.0 | 22.1 (closer) |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 39.6 | 33.2 (closer) |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 23.3 | 109 (further) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.5 | 4.1 (closer) |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 1575 | 1990 (further) |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 97.3 | 74.7 (closer) |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 14.1 | 10.1 (closer) |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 140 | 172 (further) |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2038 | 898 (closer) |
| issue_23_capacity_drop | `video.render_fps` | 2.4 | 1.7 (closer) |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 99.8 | 89.5 (closer) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 84.2 | 105 (further) |
| issue_23_handover | `audio.jb_delay_p95_ms` | 118 | 138 (further) |
| issue_23_handover | `video.freeze_total_ms` | 999 | 2769 (further) |
| issue_23_handover | `video.render_fps` | 2.4 | 3.5 (further) |
| issue_23_handover | `video.e2e_delay_p95_ms` | 2064 | 1429 (closer) |
| issue_23_handover | `video.avg_kbps` | 413 | 110 (closer) |
| issue_23_handover | `cc.utilization_pct` | 32.4 | 7.3 (closer) |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 3845 | 1993 (closer) |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.1 | 9.5 (further) |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 54.7 | 33.7 (closer) |
| issue_23_outage_3s | `video.freeze_count` | 0.9 | 1 (further) |
| issue_23_outage_3s | `video.freeze_total_ms` | 201 | 646 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 107 | 84.2 (closer) |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 0.8 (closer) |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1359 | 213 (closer) |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2476 | 1993 (closer) |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 16.2 | 9.5 (closer) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 11.8 | 15 (further) |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 449 | 285 (closer) |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1840 | 530 (closer) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 116 | 157 (further) |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 434 | 205 (closer) |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 28.4 | 12.5 (closer) |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 107 | 94.1 (closer) |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 152 | 184 (further) |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 37.3 | 78.3 (further) |
| issue_76_packetization_v2 | `audio.concealed_pct` | 1.0 | 0.6 (closer) |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 54.4 | 31.1 (closer) |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 44.2 | 35.4 (closer) |
| issue_76_packetization_v2 | `video.avg_kbps` | 925 | 184 (closer) |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 386 | 110 (closer) |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 30.0 | 7.3 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 0.8 | 0.7 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 156 | 57.8 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 138 | 164 (further) |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 807 | 2305 (further) |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 68.5 | 47.6 (closer) |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 62 | 51.6 (closer) |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1119 | 210 (closer) |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 64.5 | 10.8 (closer) |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 27080 | 5310 (closer) |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 25.1 | 2.2 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.1 | 1.2 (further) |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 31.3 | 22.1 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 39.6 | 33.2 (closer) |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 90.5 | 109 (further) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 31.9 | 4.1 (closer) |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.2 | 0.6 (closer) |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 325 | 285 (closer) |
| issue_85_video_pause | `net.loss_pct` | 2.5 | 3.7 (further) |
| low_bandwidth | `video.e2e_delay_p95_ms` | 3765 | 1993 (closer) |
| low_bandwidth | `repair.overhead_pct` | 7.4 | 9.5 (further) |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 43.6 | 33.7 (closer) |
| outage_3s | `video.e2e_delay_p95_ms` | 37.1 | 68.0 (further) |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 99.9 | 71.4 (closer) |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 147 | 172 (further) |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 40.9 | 89.5 (further) |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 61.1 | 71.4 (further) |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 650 | 898 (further) |
| probe_v2_capacity_drop | `video.render_fps` | 1.5 | 1.7 (further) |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 20.0 | 28.9 (further) |
| probe_v2_clean_link | `cc.queue_p95_ms` | 28.9 | 39.4 (further) |
| random_loss_5pct | `audio.concealed_pct` | 1.2 | 0.6 (closer) |
| random_loss_5pct | `video.longest_freeze_ms` | 325 | 285 (closer) |
| satellite | `video.freeze_count` | 10.7 | 13.4 (further) |
| satellite | `video.render_fps` | 4.5 | 5.3 (further) |
| very_low_capacity | `audio.concealed_pct` | 12.5 | 15.0 (further) |
| very_low_capacity | `video.longest_freeze_ms` | 975 | 2635 (further) |
| very_low_capacity | `video.render_fps` | 4.9 | 2.2 (closer) |
| very_low_capacity | `video.e2e_delay_p95_ms` | 6208 | 5259 (closer) |
| very_low_capacity | `cc.rtt_p50_ms` | 23.2 | 17.8 (closer) |
| very_low_capacity | `repair.overhead_pct` | 67.0 | 84.5 (further) |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 0.9 | 15.0 (further) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 633 | 2635 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.6 | 2.2 (closer) |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 6218 | 5259 (closer) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 85.9 | 105 (further) |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 122 | 138 (further) |
| wifi_to_lte_handover | `video.render_fps` | 1.7 | 3.5 (further) |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 2111 | 1429 (closer) |
| wifi_to_lte_handover | `video.avg_kbps` | 402 | 110 (closer) |
| wifi_to_lte_handover | `cc.utilization_pct` | 31.6 | 7.3 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 80 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 107 | 146 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `cc.utilization_pct` | 93.7 | 77.5 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.queue_p95_ms` | 73.0 | 47.3 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `video.e2e_delay_p95_ms` | 106 | 90.6 | ±10 | behind | 9/10 | #24, #37 |
| burst_loss | `audio.concealed_pct` | 2.2 | 1.5 | ±0.5 | behind | 8/10 | #62, #63 |
| burst_loss | `cc.queue_p95_ms` | 74.3 | 46.8 | ±15.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| burst_loss | `video.freeze_count` | 2.4 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.freeze_total_ms` | 608 | 1432 | ±500 | ahead | 8/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 326 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 729 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| capacity_drop | `cc.queue_p95_ms` | 373 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.freeze_total_ms` | 3436 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.render_fps` | 27.1 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| clean_link | `audio.jb_delay_p95_ms` | 102 | 144 | ±20.7 | ahead | 9/10 | #62, #63 |
| clean_link | `cc.queue_p95_ms` | 116 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 154 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 286 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 107 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.longest_freeze_ms` | 10.2 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 84.8 | 118 | ±13.2 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 196 | 218 | ±13.7 | ahead | 9/10 | #62, #63, #19, #24 |
| high_jitter | `repair.overhead_pct` | 39.3 | 43.4 | ±4.0 | ahead | 6/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 246 | 136 | ±12.3 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61, #19 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.2 | 1.5 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `cc.queue_p95_ms` | 74.3 | 46.8 | ±15.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 2.4 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 608 | 1432 | ±500 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 60 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 10 | 6.2 | ±2.7 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3319 | 2148 | ±850 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.render_fps` | 25.4 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 84.8 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 196 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 39.3 | 43.4 | ±4.0 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 246 | 136 | ±12.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 12.4 | 38 | ±10 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 46.1 | 59.2 | ±10 | ahead | 6/10 | #62, #63 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 27.8 | 12.7 | ±2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 23.6 | 89.0 | ±23.8 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 307 | 135 | ±20 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 346 | 3397 | ±562 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 0.4 | 9.3 | ±4.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 97.4 | 11683 | ±6940 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 0 | 3.1 | ±1.5 | ahead | 10/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 87.7 | 2904 | ±604 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.6 | ±1.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 9637 | ±6521 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 29.4 | 17.8 | ±3.6 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 324 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 52 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 72 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 18 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 60.8 | 77.5 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 8.3 | 6.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 14.7 | 12.2 | ±2 | behind | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 0.2 | 26.9 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 59.4 | 7677 | ±1185 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 75.5 | 648 | ±203 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 18.8 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 61.9 | 75.6 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 60.7 | 56.7 | ±3.0 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.5 | 7.2 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.7 | 12.9 | ±2 | behind | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 444 | 269 | ±22.2 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.1 | 21.4 | ±4.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 36.8 | 5769 | ±1258 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 51.1 | 569 | ±156 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 102 | 144 | ±20.7 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 116 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 154 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 286 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 107 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 10.2 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 324 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 52 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 72 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 324 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 52 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 72 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 84.8 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 196 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 39.3 | 43.4 | ±4.0 | ahead | 6/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 246 | 136 | ±12.3 | behind | 10/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 30.2 | 42 | ±10 | ahead | 3/10 | #62, #63 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 59.8 | 49.7 | ±3.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 15.6 | 20.3 | ±3.2 | ahead | 7/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 547 | 472 | ±27.5 | ahead | 9/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 427 | 2417 | ±1281 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.7 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 1486 | 7143 | ±3468 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0 | 0.9 | ±0.6 | ahead | 8/10 | #23 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 452 | 2281 | ±1098 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 28.2 | 24.5 | ±2.2 | ahead | 7/10 | #37, #24 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 326 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 729 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 373 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| issue_23_capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 3436 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 27.1 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| issue_23_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 179 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63 |
| issue_23_handover | `cc.max_settle_ms` | 2110 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |
| issue_23_handover | `cc.utilization_pct` | 97.4 | 105 | ±5.2 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `video.avg_kbps` | 1189 | 1299 | ±64.9 | behind | 4/10 | #37, #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 890 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 8.4 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 7297 | 4528 | ±592 | behind | 6/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 24.4 | 27.9 | ±1.4 | behind | 6/10 | #37, #24 |
| issue_23_low_bandwidth | `audio.concealed_pct` | 4.0 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `net.loss_pct` | 10.4 | 6.9 | ±2.0 | behind | 8/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 7.4 | 16.9 | ±4.3 | ahead | 8/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 2724 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 17337 | 12936 | ±3677 | behind | 8/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 8/10 | #37, #24 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 204 | 170 | ±28.5 | behind | 9/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 66.4 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `net.loss_pct` | 4.9 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_23_outage_3s | `repair.overhead_pct` | 2.5 | 0.5 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 173 | 105 | ±13.7 | behind | 10/10 | #24, #37 |
| issue_23_outage_3s | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3868 | 3222 | ±193 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 288 | 204 | ±38.3 | behind | 9/10 | #62, #63 |
| issue_23_outage_500ms | `cc.max_settle_ms` | 100 | 700 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 112 | 54.3 | ±15.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `video.freeze_count` | 1.8 | 1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 796 | 583 | ±100 | behind | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.concealed_pct` | 4.0 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 10.4 | 6.9 | ±2.0 | behind | 8/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 7.4 | 16.9 | ±4.3 | ahead | 8/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2724 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 17337 | 12936 | ±3677 | behind | 8/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 8/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 88 | 103 | ±10 | ahead | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 2610 | 3140 | ±500 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `net.loss_pct` | 8.3 | 6.2 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `video.freeze_count` | 16.7 | 8.3 | ±2.6 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 8315 | 3190 | ±491 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1424 | 1140 | ±256 | behind | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 16.7 | 27.5 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 286 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 107 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 10.2 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 1.0 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 62.4 | 117 | ±14.9 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 117 | 172 | ±14.7 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 73.8 | 86.3 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 974 | 1179 | ±59.0 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 159 | 141 | ±10 | behind | 10/10 | #24, #37 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 86.8 | 128 | ±28.6 | ahead | 7/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 320 | 504 | ±34.7 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 716 | 811 | ±45.5 | ahead | 9/10 | #62, #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 367 | 289 | ±23.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 512 | 271 | ±226 | behind | 8/10 | #24, #37 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 3396 | 2570 | ±359 | behind | 6/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.render_fps` | 27.2 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 60 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 10 | 6.2 | ±2.7 | behind | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 3319 | 2148 | ±850 | behind | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 25.4 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 49.6 | 85 | ±15.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 125 | 156 | ±24.1 | ahead | 9/10 | #62, #63 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1415 | 1599 | ±184 | behind | 6/10 | #37, #61 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 172 | 147 | ±10 | behind | 9/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 179 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.max_settle_ms` | 2110 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 97.4 | 105 | ±5.2 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 1189 | 1299 | ±64.9 | behind | 4/10 | #37, #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 890 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 8.4 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 7297 | 4528 | ±592 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.render_fps` | 24.4 | 27.9 | ±1.4 | behind | 6/10 | #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.6 | 0.9 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 326 | 490 | ±36.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 715 | 773 | ±50.9 | ahead | 7/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 359 | 264 | ±27.3 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 694 | 1107 | ±218 | ahead | 8/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3348 | 2816 | ±513 | behind | 5/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 2305 | 0 | ±115 | behind | 2/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 25.3 | 27.7 | ±1.4 | behind | 2/10 | #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 54.4 | 106 | ±22.2 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 135 | 182 | ±23.3 | ahead | 9/10 | #62, #63 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 8230 | 2920 | ±2909 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 72.8 | 83.6 | ±6.6 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.7 | 40.0 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1237 | 1447 | ±109 | behind | 10/10 | #37, #61 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 173 | 157 | ±10 | behind | 8/10 | #24, #37 |
| issue_76_session_layer_v2 | `video.time_to_first_frame_ms` | 134 | 80.8 | ±50 | behind | 5/10 | #24, #61 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 84.8 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 196 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 39.3 | 43.4 | ±4.0 | ahead | 6/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 246 | 136 | ±12.3 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 221 | 92.7 | ±50 | behind | 8/10 | #24, #61 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 324 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 52 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 72 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 348 | 252 | ±56.0 | behind | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 200 | 2560 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 129 | 56.9 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `net.loss_pct` | 8.1 | 11.8 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 196 | 153 | ±26.0 | behind | 10/10 | #24, #37 |
| issue_85_video_pause | `video.keyframe_requests` | 0 | 1 | ±0.5 | ahead | 10/10 | #23 |
| issue_85_video_pause | `video.pause_total_ms` | 10215 | 12273 | ±614 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.4 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 170 | 140 | ±27.1 | behind | 8/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 325 | 212 | ±40.8 | behind | 9/10 | #62, #63 |
| lossy_feedback | `cc.queue_p95_ms` | 130 | 49.1 | ±17.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 174 | 117 | ±15.6 | behind | 10/10 | #24, #37 |
| low_bandwidth | `audio.concealed_pct` | 4.0 | 1.6 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `net.loss_pct` | 10.4 | 6.9 | ±2.0 | behind | 8/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 7.4 | 16.9 | ±4.3 | ahead | 8/10 | #21, #7, #29 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 2724 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.freeze_total_ms` | 17337 | 12936 | ±3677 | behind | 8/10 | #5, #37, #24 |
| low_bandwidth | `video.render_fps` | 14.4 | 21.3 | ±3.1 | behind | 8/10 | #37, #24 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 204 | 170 | ±28.5 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 66.4 | 38.6 | ±10 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `net.loss_pct` | 4.9 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12, #20 |
| outage_3s | `repair.overhead_pct` | 2.5 | 0.5 | ±2 | behind | 5/10 | #21, #7, #29, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 173 | 105 | ±13.7 | behind | 10/10 | #24, #37, #20 |
| outage_3s | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24, #20 |
| outage_3s | `video.freeze_total_ms` | 3868 | 3222 | ±193 | behind | 10/10 | #5, #37, #24, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 326 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 729 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 373 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 3436 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.render_fps` | 27.1 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 326 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 729 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 373 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 524 | 249 | ±227 | behind | 9/10 | #24, #37 |
| probe_v2_capacity_drop | `video.freeze_count` | 3.2 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3436 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.render_fps` | 27.1 | 28.8 | ±1.4 | behind | 4/10 | #37, #24 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 102 | 144 | ±20.7 | ahead | 9/10 | #62, #63 |
| probe_v2_clean_link | `cc.queue_p95_ms` | 116 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 154 | 125 | ±15.4 | behind | 7/10 | #24, #37 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 286 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 107 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 10.2 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| random_loss_5pct | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| random_loss_5pct | `cc.queue_p95_ms` | 41.0 | 29.5 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 324 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.2 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 52 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.longest_freeze_ms` | 72 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.freeze_count` | 19.2 | 32.6 | ±5.6 | ahead | 9/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6565 | 17933 | ±2588 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.2 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.concealed_pct` | 15.5 | 0.5 | ±0.8 | behind | 10/10 | #62, #63 |
| very_low_capacity | `cc.rtt_p50_ms` | 346 | 328 | ±17.3 | behind | 3/10 | #61 |
| very_low_capacity | `repair.overhead_pct` | 13.0 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 3257 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 45.6 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 1 | 3.3 | ±0.7 | ahead | 10/10 | #23 |
| very_low_capacity | `video.longest_freeze_ms` | 1820 | 4455 | ±406 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 3.8 | 6.0 | ±1.2 | behind | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 15.5 | 0.5 | ±0.8 | behind | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `cc.rtt_p50_ms` | 346 | 328 | ±17.3 | behind | 3/10 | #61 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 13.0 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 3257 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 45.6 | 18.4 | ±3.7 | behind | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 1 | 3.3 | ±0.7 | ahead | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 1820 | 4455 | ±406 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_count` | 0 | 1.3 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.pause_total_ms` | 0 | 8289 | ±5083 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 3.8 | 6.0 | ±1.2 | behind | 8/10 | #37, #24 |
| wifi_to_lte_handover | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 179 | 317 | ±21.6 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 9/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.max_settle_ms` | 2110 | 740 | ±500 | behind | 6/10 | #15, #48, #61, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 97.4 | 105 | ±5.2 | behind | 4/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 1189 | 1299 | ±64.9 | behind | 4/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 890 | 2319 | ±478 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 8.4 | 4.3 | ±0.7 | behind | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 7297 | 4528 | ±592 | behind | 6/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.render_fps` | 24.4 | 27.9 | ±1.4 | behind | 6/10 | #37, #24, #20 |

