## Parity with libwebrtc M157.8103@{#0}

69 scenarios, mean of 10 seeds. **568 gaps**: 270 behind libwebrtc, 298 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

559 gaps on `main`, 568 here: **42 closed, 51 opened**, 4 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.longest_interruption_ms` | 61 | 30 | ±20 | behind | 6/10 | #62, #63 |
| capacity_drop | `cc.utilization_pct` | 54.4 | 57.5 | ±2.9 | behind | 5/10 | #19, #15, #48, #12, #61 |
| high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 147 | 135 | ±10 | behind | 7/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.render_fps` | 27.4 | 28.8 | ±1.4 | behind | 2/10 | #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 349 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.max_settle_ms` | 13830 | 14830 | ±742 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 991 | 354 | ±50 | behind | 6/10 | #24, #61 |
| issue_123_v2_allocation_min | `cc.rtt_p50_ms` | 347 | 328 | ±17.3 | behind | 5/10 | #61 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 102 | 90.6 | ±10 | behind | 4/10 | #24, #37 |
| issue_124_bufferbloat_v2 | `cc.queue_p95_ms` | 69.0 | 47.3 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_124_burst_loss_v2 | `cc.queue_p95_ms` | 73.5 | 46.8 | ±15.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_126_audio_v2 | `audio.mouth_to_ear_p95_ms` | 59.8 | 71.1 | ±10 | ahead | 6/10 | #62, #63 |
| issue_127_all_v2_no_stall | `video.time_to_first_frame_ms` | 268 | 181 | ±50 | behind | 7/10 | #24, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.mouth_to_ear_p95_ms` | 170 | 190 | ±10 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 45.3 | 59.2 | ±10 | ahead | 6/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 195 | 218 | ±13.7 | ahead | 9/10 | #62, #63 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 141 | 105 | ±13.7 | behind | 9/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 58.4 | 75.6 | ±10 | ahead | 9/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 412 | 472 | ±27.5 | behind | 9/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.7 | 0.9 | ±0.6 | behind | 5/10 | #23 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 44.0 | 49.7 | ±2.8 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `cc.max_settle_ms` | 8970 | 5130 | ±1729 | behind | 8/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `cc.rtt_p50_ms` | 72.9 | 62.2 | ±10 | behind | 8/10 | #61 |
| issue_23_outage_3s | `audio.concealed_pct` | 10.8 | 10.2 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1240 | 1895 | ±545 | ahead | 7/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 57.7 | 46.5 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 94.4 | 82.0 | ±10 | behind | 7/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 7.2 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 12.7 | 8.3 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `audio.concealed_pct` | 1.0 | 0.2 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_76_rtx_v2_handover | `net.loss_pct` | 8.0 | 6.0 | ±1.3 | behind | 8/10 | #21, #61, #12 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 145 | 157 | ±10 | ahead | 6/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.render_fps` | 27.5 | 29.8 | ±1.5 | behind | 8/10 | #37, #24 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| probe_v2_capacity_drop | `repair.overhead_pct` | 6.6 | 3.1 | ±2 | behind | 7/10 | #21, #7, #29 |
| probe_v2_clean_link | `cc.queue_p95_ms` | 106 | 76.7 | ±22.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.max_settle_ms` | 6450 | 710 | ±500 | behind | 2/10 | #15, #48, #61, #20 |
| very_low_capacity | `video.pause_total_ms` | 13392 | 8289 | ±5083 | behind | 9/10 | #5, #37, #24 |
| very_low_capacity | `cc.rtt_p50_ms` | 351 | 328 | ±17.6 | behind | 7/10 | #61 |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 1.5 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 15449 | 24870 | ±4734 | ahead | 10/10 | #5, #37, #24 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_120_rtx_payload_padding | `video.render_fps` | 26.3 | 27.9 | ±1.4 | behind | 8/10 | #37, #24 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 139 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `video.freeze_count` | 10.2 | 18.4 | ±3.7 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 5556 | 4455 | ±406 | behind | 6/10 | #5, #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 78.8 | 110 | ±17.6 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.mouth_to_ear_p95_ms` | 179 | 242 | ±52.3 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 78.8 | 154 | ±43.8 | ahead | 10/10 | #62, #63 |
| issue_124_clean_link_v2 | `audio.mouth_to_ear_p95_ms` | 186 | 229 | ±34.3 | ahead | 30/40 | #62, #63 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 361 | 566 | ±142 | ahead | 7/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 431 | 247 | ±114 | behind | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `cc.utilization_pct` | 75.2 | 80.4 | ±4.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 37.7 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 80 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 329 | 569 | ±156 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.render_fps` | 28.3 | 29.9 | ±1.5 | behind | 7/10 | #37, #24 |
| issue_150_gcc_v2_high_jitter | `video.avg_kbps` | 813 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter | `cc.utilization_pct` | 51.7 | 37.1 | ±7.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter | `cc.queue_p95_ms` | 16.7 | 4.6 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 80 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 94.4 | 75.6 | ±10 | behind | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 121 | 144 | ±20.1 | ahead | 5/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 805 | 869 | ±45.8 | ahead | 4/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 286 | 352 | ±46.2 | ahead | 7/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 222 | 204 | ±11.1 | behind | 9/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.mouth_to_ear_p95_ms` | 172 | 218 | ±38.3 | ahead | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 91.2 | 128 | ±28.6 | ahead | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `video.freeze_count` | 2.7 | 4.9 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.freeze_total_ms` | 654 | 1269 | ±553 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1010 | 1930 | ±888 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `video.pause_total_ms` | 500 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.render_fps` | 26.3 | 27.9 | ±1.4 | behind | 4/10 | #37, #24 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 31.0 | 33.4 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±32.5 | ahead | 5/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1270 | 1698 | ±289 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 600 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.8 | 3.2 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 791 | 869 | ±45.8 | ahead | 6/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 269 | 352 | ±46.2 | ahead | 9/10 | #62, #63 |
| probe_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 175 | 238 | ±40.5 | ahead | 9/10 | #62, #63 |
| satellite | `video.longest_freeze_ms` | 1108 | 838 | ±262 | behind | 5/10 | #5, #37, #24 |
| very_low_capacity | `audio.interruptions` | 0.1 | 1 | ±0.5 | ahead | 9/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 400 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 62.4 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 407 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 72 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 25.8 | 29.6 | ±3.6 | ahead | 8/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 76.9 | 97.5 | ±17.6 | ahead | 8/10 | #21, #7, #29 |
| wifi_to_lte_handover | `video.longest_freeze_ms` | 2413 | 1917 | ±313 | behind | 7/10 | #5, #37, #24, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1938 | 2538 | ±362 | ahead | 9/10 | #5, #37, #24 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 101 | 125 | ±15.4 | ahead | 9/10 | #24, #37 |
| very_low_capacity | `audio.longest_interruption_ms` | 118 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 60 | 203 | ±20 | ahead | 10/10 | #62, #63 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.3 | 1.1 (closer) |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 37.7 | 28.4 (closer) |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 76.2 | 67.7 (closer) |
| bufferbloat | `video.e2e_delay_p95_ms` | 26.6 | 17.4 (closer) |
| burst_loss | `audio.jb_delay_p95_ms` | 82.8 | 92.8 (further) |
| burst_loss | `video.longest_freeze_ms` | 138 | 115 (closer) |
| capacity_drop | `audio.longest_interruption_ms` | 23 | 28 (further) |
| capacity_drop | `video.avg_kbps` | 78.7 | 59.1 (closer) |
| capacity_drop | `video.keyframe_requests` | 8.3 | 9.7 (further) |
| capacity_drop | `net.loss_pct` | 2.6 | 2.9 (further) |
| clean_link | `video.avg_kbps` | 267 | 170 (closer) |
| clean_link | `cc.utilization_pct` | 9.6 | 5.6 (closer) |
| cross_traffic | `video.avg_kbps` | 233 | 182 (closer) |
| cross_traffic | `cc.utilization_pct` | 8.2 | 6.1 (closer) |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 27.8 | 17.0 (closer) |
| high_jitter | `video.render_fps` | 4.1 | 3.7 (closer) |
| high_jitter | `repair.overhead_pct` | 34.4 | 30.9 (closer) |
| issue_120_rtx_payload_padding | `video.freeze_count` | 1 | 1.7 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 110 | 150 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 179 | 147 (closer) |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 4.7 | 9.3 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 14.2 | 10.8 (closer) |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 2550 | 2114 (closer) |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.4 | 1.3 (closer) |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 0.8 | 0.7 (closer) |
| issue_124_burst_loss_v2 | `video.freeze_total_ms` | 793 | 898 (further) |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 48.0 | 59.3 (further) |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 51.6 | 39.6 (closer) |
| issue_127_all_v2_no_stall | `video.e2e_delay_p95_ms` | 45.4 | 40.8 (closer) |
| issue_127_all_v2_no_stall | `cc.queue_p95_ms` | 12.4 | 10.7 (closer) |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 0.8 | 0.9 (further) |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_count` | 3.1 | 2.3 (closer) |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 724 | 816 (further) |
| issue_127_pacer_v2_jitter_reorder_loss | `video.keyframe_requests` | 1.1 | 1.6 (further) |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.max_settle_ms` | 24090 | 16130 (closer) |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 1.3 | 1.1 (closer) |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 114 | 132 (further) |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.6 | 1.4 (closer) |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 942 | 1237 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 21.2 | 15.1 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 31.6 | 34.8 (further) |
| issue_12_lossbwe_policer | `video.avg_kbps` | 117 | 148 (further) |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 8.4 | 16.3 (further) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 11.2 | 13.7 (further) |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 159 | 137 (closer) |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 262 | 334 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 240 | 299 (further) |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 60.0 | 71.2 (further) |
| issue_12_lossbwe_shallow | `net.loss_pct` | 4.0 | 4.6 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 316 | 284 (closer) |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 104 | 62.7 (closer) |
| issue_12_lossbwe_shallow_step | `video.keyframe_requests` | 15.3 | 18.4 (further) |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 11.1 | 8.2 (closer) |
| issue_12_lossbwe_shallow_step | `cc.rtt_p50_ms` | 11.6 | 10.3 (closer) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 2.9 | 4.4 (further) |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 7.6 | 10.8 (further) |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 40.1 | 67.3 (further) |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 72.9 | 93.6 (further) |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 36.0 | 13.8 (closer) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 23.2 | 19.6 (closer) |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 159 | 137 (closer) |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.2 | 1.0 (closer) |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 64 | 73.2 (further) |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 118 | 77.6 (closer) |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 339 | 263 (closer) |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 2.4 | 1.6 (closer) |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 16.7 | 12.5 (closer) |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 31.0 | 22.5 (closer) |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 23.3 | 20.9 (closer) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 52.6 | 58.2 (further) |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2038 | 2657 (further) |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1305 | 1462 (further) |
| issue_23_capacity_drop | `video.render_fps` | 2.4 | 2.9 (further) |
| issue_23_capacity_drop | `video.avg_kbps` | 69.1 | 51.9 (closer) |
| issue_23_capacity_drop | `video.keyframe_requests` | 0.9 | 1 (further) |
| issue_23_capacity_drop | `net.loss_pct` | 2.0 | 2.5 (further) |
| issue_23_capacity_drop | `repair.overhead_pct` | 3.2 | 3.8 (further) |
| issue_23_handover | `video.freeze_total_ms` | 999 | 869 (closer) |
| issue_23_handover | `net.loss_pct` | 14.2 | 12.5 (closer) |
| issue_23_handover | `repair.overhead_pct` | 26.8 | 22.9 (closer) |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 100 | 40 (closer) |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 54.4 | 41.3 (closer) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 107 | 129 (further) |
| issue_23_outage_500ms | `net.loss_pct` | 0.6 | 0.6 (further) |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 326 | 288 (closer) |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.6 | 0.7 (further) |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 36 | 40 (further) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 11.8 | 13.4 (further) |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 449 | 636 (further) |
| issue_53_cwnd_pushback | `video.avg_kbps` | 175 | 136 (closer) |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 13.2 | 9.7 (closer) |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1840 | 1640 (closer) |
| issue_53_cwnd_pushback | `net.loss_pct` | 2.1 | 2.4 (further) |
| issue_5_adjuster_default | `video.avg_kbps` | 233 | 182 (closer) |
| issue_5_adjuster_default | `cc.utilization_pct` | 8.2 | 6.1 (closer) |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 57.6 | 39.6 (closer) |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 52.6 | 43.8 (closer) |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 19.6 | 26.3 (further) |
| issue_5_burst15_freeze | `audio.concealed_pct` | 1.2 | 0.9 (closer) |
| issue_5_burst15_freeze | `video.keyframe_requests` | 2.1 | 1.6 (closer) |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 107 | 90.5 (closer) |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 462 | 413 (closer) |
| issue_5_keyframe_flush | `video.avg_kbps` | 62.3 | 52.3 (closer) |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 37.3 | 48.0 (further) |
| issue_5_keyframe_flush | `net.loss_pct` | 0.7 | 0.8 (further) |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 21.0 | 11.8 (closer) |
| issue_61_googcc_glue_congestive | `video.render_fps` | 5.2 | 3.8 (closer) |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 52.2 | 39.2 (closer) |
| issue_76_packetization_v2 | `video.avg_kbps` | 818 | 634 (closer) |
| issue_76_packetization_v2 | `cc.utilization_pct` | 45.7 | 36.3 (closer) |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 11870 | 6090 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 12.4 | 10.6 (closer) |
| issue_76_rtx_v2_handover | `video.freeze_count` | 0.7 | 0.9 (further) |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 384 | 343 (closer) |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 29.8 | 26.4 (closer) |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 0.8 | 0.6 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 56.2 | 48.5 (closer) |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 68.5 | 57.9 (closer) |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1119 | 991 (closer) |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 64.5 | 57.3 (closer) |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 25.1 | 19.1 (closer) |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.1 | 1.2 (further) |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 27.7 | 19.0 (closer) |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 1051 | 559 (closer) |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 499 | 442 (closer) |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 29.5 | 26.2 (closer) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 33.6 | 27.1 (closer) |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.2 | 1.0 (closer) |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 64 | 73.2 (further) |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 118 | 77.6 (closer) |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 339 | 263 (closer) |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 2.4 | 1.6 (closer) |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 16.7 | 12.5 (closer) |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 118 | 101 (closer) |
| low_bandwidth | `audio.longest_interruption_ms` | 100 | 40 (closer) |
| low_bandwidth | `cc.max_settle_ms` | 610 | 700 (further) |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 43.6 | 73.6 (further) |
| outage_3s | `audio.jb_delay_p95_ms` | 52.8 | 71.2 (further) |
| outage_3s | `video.e2e_delay_p95_ms` | 37.1 | 42.3 (further) |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 21 | 25 (further) |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 99.9 | 77.1 (closer) |
| pacer_v2_capacity_drop | `video.avg_kbps` | 66.9 | 60.2 (closer) |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 40.9 | 50.9 (further) |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 166 | 147 (closer) |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 609 | 401 (closer) |
| probe_v2_capacity_drop | `video.keyframe_requests` | 9.3 | 10.5 (further) |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 83.4 | 56.8 (closer) |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 1350 | 890 (closer) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 13.5 | 11.9 (closer) |
| probe_v2_capacity_drop | `net.loss_pct` | 2.4 | 1.4 (closer) |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 43.2 | 64 (further) |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 16.7 | 14.7 (closer) |
| random_loss_5pct | `audio.concealed_pct` | 1.2 | 1.0 (closer) |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 64 | 73.2 (further) |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 118 | 77.6 (closer) |
| random_loss_5pct | `video.avg_kbps` | 339 | 263 (closer) |
| random_loss_5pct | `video.keyframe_requests` | 2.4 | 1.6 (closer) |
| random_loss_5pct | `cc.utilization_pct` | 16.7 | 12.5 (closer) |
| satellite | `audio.mouth_to_ear_p95_ms` | 134 | 117 (closer) |
| satellite | `video.freeze_count` | 10.7 | 13 (further) |
| satellite | `video.e2e_delay_p95_ms` | 123 | 103 (closer) |
| satellite | `video.avg_kbps` | 366 | 317 (closer) |
| satellite | `video.keyframe_requests` | 2.2 | 2.8 (further) |
| satellite | `cc.utilization_pct` | 28.6 | 24.3 (closer) |
| satellite | `repair.overhead_pct` | 3.0 | 2.3 (closer) |
| very_low_capacity | `video.freeze_count` | 12.5 | 7.4 (closer) |
| very_low_capacity | `video.freeze_total_ms` | 12265 | 8473 (closer) |
| very_low_capacity | `cc.max_settle_ms` | 940 | 1170 (further) |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 10.5 | 6.7 (closer) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 633 | 746 (further) |
| wifi_to_lte_handover | `video.avg_kbps` | 402 | 459 (further) |
| wifi_to_lte_handover | `video.keyframe_requests` | 16.7 | 14.3 (closer) |
| wifi_to_lte_handover | `cc.utilization_pct` | 31.6 | 35.5 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 80 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 117 | 146 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `cc.utilization_pct` | 93.5 | 77.5 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `audio.jb_delay_p95_ms` | 44.8 | 110 | ±17.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 102 | 170 | ±20.6 | ahead | 10/10 | #62, #63 |
| bufferbloat | `cc.queue_p95_ms` | 27.2 | 47.3 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.utilization_pct` | 86.5 | 92.3 | ±4.6 | behind | 8/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `video.avg_kbps` | 742 | 819 | ±41.0 | behind | 10/10 | #37, #61 |
| bufferbloat | `video.e2e_delay_p95_ms` | 73.2 | 90.6 | ±10 | ahead | 8/10 | #24, #37 |
| bufferbloat | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `audio.concealed_pct` | 2.7 | 1.5 | ±0.5 | behind | 9/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 61.2 | 154 | ±43.8 | ahead | 10/10 | #62, #63 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 139 | 242 | ±52.3 | ahead | 10/10 | #62, #63 |
| burst_loss | `cc.queue_p95_ms` | 18.0 | 46.8 | ±15.9 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.utilization_pct` | 56.9 | 80.4 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `video.avg_kbps` | 982 | 1461 | ±89.1 | behind | 10/10 | #37, #61 |
| burst_loss | `video.freeze_count` | 1.3 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.freeze_total_ms` | 282 | 1432 | ±500 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.keyframe_requests` | 1.3 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `video.longest_freeze_ms` | 182 | 298 | ±100 | ahead | 4/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 352 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.longest_interruption_ms` | 116 | 144 | ±20.1 | ahead | 4/10 | #62, #63 |
| capacity_drop | `cc.queue_p95_ms` | 385 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `net.loss_pct` | 4.4 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| capacity_drop | `repair.overhead_pct` | 7.6 | 3.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| capacity_drop | `video.avg_kbps` | 836 | 895 | ±44.8 | behind | 6/10 | #37, #61 |
| capacity_drop | `video.freeze_count` | 3.5 | 2.2 | ±0.9 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.keyframe_requests` | 9.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| clean_link | `audio.jb_delay_p95_ms` | 44 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 102 | 238 | ±40.5 | ahead | 10/10 | #62, #63 |
| clean_link | `cc.queue_p95_ms` | 23.3 | 76.7 | ±22.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| clean_link | `cc.utilization_pct` | 86.0 | 91.6 | ±4.6 | behind | 9/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.avg_kbps` | 1972 | 2142 | ±107 | behind | 10/10 | #37, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 68.3 | 125 | ±15.4 | ahead | 10/10 | #24, #37 |
| clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `audio.jb_delay_p95_ms` | 152 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 324 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 138 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `cc.utilization_pct` | 68.1 | 74.2 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.avg_kbps` | 1597 | 1779 | ±88.9 | behind | 8/10 | #37, #61 |
| cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `video.longest_freeze_ms` | 5.1 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 118 | ±13.2 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 201 | 218 | ±13.7 | ahead | 8/10 | #62, #63, #19, #24 |
| high_jitter | `cc.utilization_pct` | 8.0 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `repair.overhead_pct` | 12.6 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.avg_kbps` | 89.0 | 584 | ±117 | behind | 10/10 | #37, #61, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 1532 | 136 | ±76.6 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `video.render_fps` | 26.1 | 29.8 | ±1.5 | behind | 10/10 | #37, #24, #19 |
| issue_120_rtx_payload_padding | `audio.concealed_pct` | 4.7 | 4.2 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 176 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 384 | 536 | ±41.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 722 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 6 | 4.3 | ±0.7 | behind | 8/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.render_fps` | 26.3 | 27.9 | ±1.4 | behind | 8/10 | #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 650 | 800 | ±50.4 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 84.3 | 73.5 | ±10 | behind | 6/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1938 | 2538 | ±362 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 9.3 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 139 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 19.4 | 29.6 | ±3.6 | ahead | 9/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 38.4 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 6401 | 8516 | ±961 | ahead | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.freeze_count` | 10.2 | 18.4 | ±3.7 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.6 | 3.3 | ±0.7 | behind | 7/10 | #23 |
| issue_123_v2_allocation_min | `video.longest_freeze_ms` | 5556 | 4455 | ±406 | behind | 6/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.2 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 78.8 | 110 | ±17.6 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.1 | 1.5 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 78.8 | 154 | ±43.8 | ahead | 10/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.mouth_to_ear_p95_ms` | 179 | 242 | ±52.3 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `video.freeze_count` | 2.2 | 6.2 | ±2.0 | ahead | 9/10 | #5, #37, #24 |
| issue_124_burst_loss_v2 | `video.freeze_total_ms` | 534 | 1432 | ±500 | ahead | 9/10 | #5, #37, #24 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 81.3 | 141 | ±17.5 | ahead | 40/40 | #62, #63 |
| issue_124_clean_link_v2 | `audio.mouth_to_ear_p95_ms` | 186 | 229 | ±34.3 | ahead | 30/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_126_audio_v2 | `cc.rtt_p50_ms` | 73.7 | 62.8 | ±10 | behind | 8/10 | #61 |
| issue_126_audio_v2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_127_all_v2_no_stall | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 95.2 | 158 | ±28.6 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 307 | 347 | ±30.3 | ahead | 8/10 | #62, #63 |
| issue_127_all_v2_no_stall | `cc.queue_p95_ms` | 42.4 | 31.7 | ±10 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_127_all_v2_no_stall | `video.e2e_delay_p95_ms` | 483 | 443 | ±24.2 | behind | 7/10 | #24, #37 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 4.2 | 65.8 | ±6.5 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 1191 | 19132 | ±1879 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 361 | 566 | ±142 | ahead | 7/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 1.0 | 0.1 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.jb_delay_p95_ms` | 72 | 99 | ±10 | ahead | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.max_settle_ms` | 32040 | 15910 | ±9723 | behind | 8/10 | #15, #48, #61, #20 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.utilization_pct` | 7.6 | 56.8 | ±12.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 9.7 | 47.9 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.avg_kbps` | 89.4 | 917 | ±186 | behind | 10/10 | #37, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 2302 | 174 | ±115 | behind | 10/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_count` | 4.1 | 1.8 | ±1.6 | behind | 8/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 1241 | 425 | ±357 | behind | 8/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.keyframe_requests` | 2.2 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 431 | 247 | ±114 | behind | 5/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.render_fps` | 26.4 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.6 | 1.5 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `cc.utilization_pct` | 75.2 | 80.4 | ±4.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1328 | 1461 | ±89.1 | behind | 7/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 1.5 | 6.2 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 333 | 1432 | ±500 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 85.9 | 91.4 | ±4.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.7 | 4.5 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.6 | 8.3 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1074 | 1182 | ±59.1 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 755 | 471 | ±115 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3384 | 2148 | ±850 | behind | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 37.4 | 0 | ±1.9 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1813 | 695 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.render_fps` | 26.8 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 203 | 218 | ±13.7 | ahead | 6/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 37.7 | 43.4 | ±4.0 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 165 | 136 | ±10 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 14.5 | 38 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 26.4 | 12.7 | ±2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `net.loss_pct` | 9.2 | 13.6 | ±3.0 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 21.4 | 89.0 | ±23.8 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 283 | 135 | ±20 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 519 | 3397 | ±562 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 0.9 | 9.3 | ±4.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 331 | 11683 | ±6940 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 19.4 | 3.1 | ±1.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 241 | 2904 | ±604 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.6 | ±1.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 9637 | ±6521 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 28.9 | 17.8 | ±3.6 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 80 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 347 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.4 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 82 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.keyframe_requests` | 3.5 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 91.6 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 26 | 42 | ±10 | ahead | 7/10 | #62, #63 |
| issue_12_lossbwe_shallow | `cc.utilization_pct` | 65.2 | 56.7 | ±3.3 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 11.0 | 6.5 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 21.9 | 12.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 818 | 746 | ±40.9 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 614 | 315 | ±34.0 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.4 | 26.9 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 674 | 7677 | ±1185 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.keyframe_requests` | 16.5 | 0 | ±0.8 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 315 | 648 | ±203 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 27.2 | 40 | ±10 | ahead | 7/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.rtt_p50_ms` | 72.8 | 62.5 | ±10 | behind | 5/10 | #61 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 64.9 | 56.7 | ±3.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 11.6 | 7.2 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 23.7 | 12.9 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 808 | 746 | ±40.4 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 553 | 269 | ±27.7 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 3 | 21.4 | ±4.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 819 | 5769 | ±1258 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.keyframe_requests` | 18.4 | 0 | ±0.9 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 329 | 569 | ±156 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.render_fps` | 28.3 | 29.9 | ±1.5 | behind | 7/10 | #37, #24 |
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
| issue_150_gcc_v2_high_jitter | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 84.8 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `cc.queue_p95_ms` | 16.7 | 4.6 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter | `cc.utilization_pct` | 51.7 | 37.1 | ±7.6 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter | `video.avg_kbps` | 813 | 584 | ±117 | ahead | 7/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 204 | 136 | ±10.2 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 186 | 92.7 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `audio.concealed_pct` | 12.1 | 10.2 | ±0.6 | behind | 10/10 | #62, #63 |
| issue_151_outage_3s_v2 | `audio.jb_delay_p95_ms` | 1229 | 108 | ±61.4 | behind | 10/10 | #62, #63 |
| issue_151_outage_3s_v2 | `audio.longest_interruption_ms` | 3549 | 2977 | ±177 | behind | 10/10 | #62, #63 |
| issue_151_outage_3s_v2 | `audio.mouth_to_ear_p95_ms` | 1531 | 170 | ±76.5 | behind | 10/10 | #62, #63 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 52.4 | 38.6 | ±10 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_151_outage_3s_v2 | `net.loss_pct` | 4.4 | 6.0 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_151_outage_3s_v2 | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.freeze_total_ms` | 4081 | 3222 | ±204 | behind | 10/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.longest_freeze_ms` | 3723 | 3222 | ±186 | behind | 10/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 22.4 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.1 | 11.9 | ±4.1 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 47.2 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 110 | 238 | ±40.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 28.2 | 76.7 | ±22.3 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 73.9 | 125 | ±15.4 | ahead | 10/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 151 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 319 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 134 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 10.4 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.0 | 0.4 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 80 | 118 | ±36.8 | ahead | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 347 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.4 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 82 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.keyframe_requests` | 3.5 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 91.6 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.4 | 0.4 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 44.8 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.mouth_to_ear_p95_ms` | 114 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 16.6 | 29.5 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 65.0 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1120 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 287 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 0 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 13 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 76.8 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 195 | 218 | ±13.7 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 30000 | 23150 | ±6355 | behind | 6/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 7.9 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 9.4 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 90.5 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 157 | 136 | ±10 | behind | 9/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 94.4 | 75.6 | ±10 | behind | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 63.8 | 49.7 | ±3.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.2 | 8.2 | ±1.1 | behind | 10/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 39.1 | 20.3 | ±3.2 | behind | 10/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 571 | 472 | ±28.6 | ahead | 10/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 812 | 2417 | ±1281 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 59.1 | 0.9 | ±3.0 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 358 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 121 | 144 | ±20.1 | ahead | 5/10 | #62, #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 384 | 283 | ±26.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `net.loss_pct` | 4.0 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.9 | 3.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_capacity_drop | `video.avg_kbps` | 843 | 895 | ±44.8 | behind | 7/10 | #37, #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 5195 | 2538 | ±362 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3337 | 1875 | ±554 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 26.0 | 28.8 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 198 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 458 | 536 | ±41.7 | ahead | 8/10 | #62, #63 |
| issue_23_handover | `cc.queue_p95_ms` | 191 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `cc.utilization_pct` | 74.9 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 18.4 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 56.3 | 33.4 | ±2.8 | behind | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.avg_kbps` | 912 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 290 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 2.2 | 4.3 | ±0.7 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 5398 | 4528 | ±592 | behind | 9/10 | #5, #37, #24 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_handover | `video.longest_freeze_ms` | 3387 | 1917 | ±313 | behind | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 25.7 | 27.9 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 286 | 352 | ±46.2 | ahead | 7/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 190 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 805 | 869 | ±45.8 | ahead | 4/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.max_settle_ms` | 960 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 208 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_23_low_bandwidth | `net.loss_pct` | 2.2 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.2 | 16.9 | ±4.3 | ahead | 9/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.avg_kbps` | 48.4 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 884 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_count` | 2.8 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 4984 | 12936 | ±3677 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 64.8 | 108 | ±24.9 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 129 | 170 | ±28.5 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 23.5 | 38.6 | ±10 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `cc.utilization_pct` | 77.7 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `net.loss_pct` | 5.2 | 6.0 | ±0.5 | ahead | 9/10 | #21, #61, #12 |
| issue_23_outage_3s | `video.avg_kbps` | 951 | 1142 | ±57.1 | behind | 10/10 | #37, #61 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 89 | 105 | ±13.7 | ahead | 5/10 | #24, #37 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 1.9 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 272 | 137 | ±27.9 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 333 | 204 | ±38.3 | behind | 9/10 | #62, #63 |
| issue_23_outage_500ms | `cc.max_settle_ms` | 100 | 700 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 29.6 | 54.3 | ±15.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 60.2 | 87.6 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `net.loss_pct` | 2.2 | 1.6 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_23_outage_500ms | `repair.overhead_pct` | 8.9 | 2.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_outage_500ms | `video.avg_kbps` | 984 | 1589 | ±79.4 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 373 | 192 | ±37.8 | behind | 8/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1922 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1318 | 583 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 587 | 869 | ±45.8 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 196 | 484 | ±24.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 101 | 505 | ±33.7 | ahead | 10/10 | #61 |
| issue_48_prober_low_bandwidth | `net.loss_pct` | 0.1 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `repair.overhead_pct` | 0.7 | 16.9 | ±4.3 | ahead | 10/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.avg_kbps` | 50.7 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2250 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.freeze_count` | 0 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.freeze_total_ms` | 0 | 12936 | ±3677 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.keyframe_requests` | 3 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 10.3 | 3279 | ±1648 | ahead | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 26.4 | 21.3 | ±3.1 | ahead | 10/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 89.6 | 103 | ±10 | ahead | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 64 | 24 | ±20 | behind | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 222 | 204 | ±11.1 | behind | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1500 | 3140 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 77.5 | 131 | ±10 | ahead | 10/10 | #61 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 80.7 | 90.4 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 8.7 | 6.2 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `repair.overhead_pct` | 15.4 | 11.0 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 783 | 919 | ±45.9 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 480 | 1884 | ±218 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 2.5 | 8.3 | ±2.6 | ahead | 10/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 2626 | 3190 | ±491 | ahead | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 17.8 | 0 | ±0.9 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1775 | 1140 | ±256 | behind | 9/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 152 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 324 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 138 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `cc.utilization_pct` | 68.1 | 74.2 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.avg_kbps` | 1597 | 1779 | ±88.9 | behind | 8/10 | #37, #61 |
| issue_5_adjuster_default | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 5.1 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 73.2 | 117 | ±14.9 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 133 | 172 | ±14.7 | ahead | 8/10 | #62, #63 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 19.8 | 37.4 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 56.3 | 86.3 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 722 | 1179 | ±59.0 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 167 | 141 | ±10 | behind | 8/10 | #24, #37 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.6 | 1.7 | ±0.7 | behind | 8/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.interruptions` | 1.2 | 0.6 | ±0.5 | behind | 3/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 91.2 | 128 | ±28.6 | ahead | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.mouth_to_ear_p95_ms` | 172 | 218 | ±38.3 | ahead | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 18.6 | 46.6 | ±13.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `cc.utilization_pct` | 57.4 | 84.5 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `video.avg_kbps` | 969 | 1518 | ±75.9 | behind | 10/10 | #37, #61 |
| issue_5_burst15_freeze | `video.freeze_count` | 2.7 | 4.9 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.freeze_total_ms` | 654 | 1269 | ±553 | ahead | 8/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.keyframe_requests` | 1.6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 346 | 504 | ±34.7 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 120 | 149 | ±22.3 | ahead | 6/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 720 | 811 | ±45.5 | ahead | 9/10 | #62, #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 337 | 289 | ±23.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.3 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_5_keyframe_flush | `video.avg_kbps` | 841 | 893 | ±44.7 | behind | 5/10 | #37, #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2156 | 2570 | ±359 | ahead | 6/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 6.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 59.2 | 87 | ±11.6 | ahead | 9/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 142 | 154 | ±11.8 | ahead | 4/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1010 | 1930 | ±888 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 695 | 471 | ±115 | behind | 9/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 26.1 | 29.9 | ±1.5 | behind | 9/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 43.2 | 85 | ±15.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 117 | 156 | ±24.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 7380 | 1290 | ±2439 | behind | 8/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 9.5 | 28.0 | ±10.3 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 50.7 | 87.0 | ±10.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 21.7 | 32.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.avg_kbps` | 965 | 1599 | ±184 | behind | 10/10 | #37, #61 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 186 | 317 | ±21.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 431 | 536 | ±41.7 | ahead | 8/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 78.3 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 31.0 | 33.4 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 956 | 1299 | ±64.9 | behind | 10/10 | #37, #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 725 | 2319 | ±478 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 3.4 | 4.3 | ±0.7 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.pause_total_ms` | 500 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.render_fps` | 26.3 | 27.9 | ±1.4 | behind | 4/10 | #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.5 | 0.9 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 490 | ±36.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 116 | 149 | ±32.5 | ahead | 5/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 603 | 773 | ±50.9 | ahead | 9/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 230 | 264 | ±27.3 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.8 | 3.2 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `video.avg_kbps` | 786 | 834 | ±41.7 | behind | 7/10 | #37, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1270 | 1698 | ±289 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 600 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 44 | 106 | ±22.2 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 125 | 182 | ±23.3 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 28930 | 2920 | ±2909 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.7 | 24.7 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 26.3 | 83.6 | ±6.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 20.9 | 40.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 456 | 1447 | ±109 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 80 | 118 | ±13.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 199 | 218 | ±13.7 | ahead | 8/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 10.9 | 37.1 | ±7.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 16.3 | 43.4 | ±4.0 | ahead | 10/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 142 | 584 | ±117 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 695 | 136 | ±34.8 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.4 | 0.4 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 44.8 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.mouth_to_ear_p95_ms` | 114 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 16.6 | 29.5 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 65.0 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1120 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 287 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 0 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 13 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 48 | 153 | ±37.4 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 120 | 252 | ±56.0 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 220 | 2560 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 20.9 | 56.9 | ±15.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `cc.utilization_pct` | 76.6 | 88.7 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `net.loss_pct` | 9.4 | 11.8 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_85_video_pause | `video.avg_kbps` | 1070 | 1304 | ±65.2 | behind | 10/10 | #37, #61 |
| issue_85_video_pause | `video.keyframe_requests` | 2 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_85_video_pause | `video.pause_total_ms` | 10206 | 12273 | ±614 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.3 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 47.2 | 140 | ±27.1 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 111 | 212 | ±40.8 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `cc.queue_p95_ms` | 23.7 | 49.1 | ±17.9 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 76.0 | 117 | ±15.6 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 269 | 352 | ±46.2 | ahead | 9/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 190 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 791 | 869 | ±45.8 | ahead | 6/10 | #62, #63 |
| low_bandwidth | `cc.max_settle_ms` | 1000 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| low_bandwidth | `cc.rtt_p50_ms` | 174 | 505 | ±33.7 | ahead | 10/10 | #61 |
| low_bandwidth | `net.loss_pct` | 2.2 | 6.9 | ±2.0 | ahead | 10/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 9.1 | 16.9 | ±4.3 | ahead | 8/10 | #21, #7, #29 |
| low_bandwidth | `video.avg_kbps` | 46.4 | 73.4 | ±20 | behind | 10/10 | #37, #61 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 1121 | 4718 | ±872 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.freeze_count` | 1.1 | 17.8 | ±4.3 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.freeze_total_ms` | 576 | 12936 | ±3677 | ahead | 10/10 | #5, #37, #24 |
| low_bandwidth | `video.keyframe_requests` | 15.2 | 0.7 | ±0.9 | behind | 10/10 | #23 |
| low_bandwidth | `video.longest_freeze_ms` | 443 | 3279 | ±1648 | ahead | 10/10 | #5, #37, #24 |
| outage_3s | `audio.concealed_pct` | 11.1 | 10.2 | ±0.6 | behind | 10/10 | #62, #63, #20 |
| outage_3s | `audio.jb_delay_p95_ms` | 179 | 108 | ±24.9 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 244 | 170 | ±28.5 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 24.0 | 38.6 | ±10 | ahead | 9/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `cc.utilization_pct` | 69.5 | 89.1 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `video.avg_kbps` | 833 | 1142 | ±57.1 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 147 | 105 | ±13.7 | behind | 7/10 | #24, #37, #20 |
| outage_3s | `video.keyframe_requests` | 5.2 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 119 | 144 | ±20.1 | ahead | 5/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 723 | 800 | ±50.4 | ahead | 8/10 | #62, #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 334 | 283 | ±26.9 | behind | 8/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 835 | 895 | ±44.8 | behind | 6/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2131 | 2538 | ±362 | ahead | 5/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 6.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 499 | ±35.6 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 724 | 800 | ±50.4 | ahead | 7/10 | #62, #63 |
| probe_v2_capacity_drop | `cc.max_settle_ms` | 13940 | 14830 | ±742 | ahead | 4/10 | #15, #48, #61, #20 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 340 | 283 | ±26.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 85.4 | 73.5 | ±10 | behind | 8/10 | #61 |
| probe_v2_capacity_drop | `net.loss_pct` | 2.9 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2940 | 2538 | ±362 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 10.5 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 80 | 144 | ±20.7 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 175 | 238 | ±40.5 | ahead | 9/10 | #62, #63 |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 81.3 | 66.6 | ±10 | behind | 9/10 | #61 |
| probe_v2_clean_link | `video.e2e_delay_p95_ms` | 101 | 125 | ±15.4 | ahead | 9/10 | #24, #37 |
| probe_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 157 | 404 | ±52.4 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 305 | 684 | ±70.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 114 | 264 | ±21.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 10.3 | 588 | ±424 | ahead | 9/10 | #5, #37, #24 |
| random_loss_5pct | `audio.concealed_pct` | 1.4 | 0.4 | ±0.5 | behind | 9/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 44.8 | 118 | ±36.8 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.mouth_to_ear_p95_ms` | 114 | 182 | ±42.7 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `cc.queue_p95_ms` | 16.6 | 29.5 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `cc.utilization_pct` | 65.0 | 77.5 | ±3.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `video.avg_kbps` | 1120 | 1382 | ±69.1 | behind | 9/10 | #37, #61 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 287 | 209 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0 | 7.5 | ±1.7 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 0 | 1863 | ±436 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 8/10 | #23 |
| random_loss_5pct | `video.longest_freeze_ms` | 13 | 357 | ±100 | ahead | 10/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 52.8 | 160 | ±68.5 | ahead | 9/10 | #62, #63 |
| satellite | `audio.mouth_to_ear_p95_ms` | 391 | 508 | ±93.9 | ahead | 9/10 | #62, #63 |
| satellite | `cc.queue_p95_ms` | 13.7 | 43.9 | ±28.2 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.utilization_pct` | 58.1 | 82.4 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `repair.overhead_pct` | 15.7 | 18.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| satellite | `video.avg_kbps` | 648 | 964 | ±48.2 | behind | 10/10 | #37, #61 |
| satellite | `video.e2e_delay_p95_ms` | 829 | 932 | ±46.6 | ahead | 7/10 | #24, #37 |
| satellite | `video.freeze_count` | 19.6 | 32.6 | ±5.6 | ahead | 9/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6648 | 17933 | ±2588 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.keyframe_requests` | 2.9 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `video.longest_freeze_ms` | 1108 | 838 | ±262 | behind | 5/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.7 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.concealed_pct` | 11.9 | 0.5 | ±0.6 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.interruptions` | 0.1 | 1 | ±0.5 | ahead | 9/10 | #62, #63 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 62.4 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 118 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 400 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `cc.max_settle_ms` | 1570 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| very_low_capacity | `net.loss_pct` | 34.4 | 29.6 | ±3.6 | behind | 8/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 30.7 | 97.5 | ±17.6 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 2303 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 11 | 18.4 | ±3.7 | ahead | 7/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_total_ms` | 16397 | 24870 | ±4734 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 217 | 3.3 | ±10.9 | behind | 10/10 | #23 |
| very_low_capacity | `video.longest_freeze_ms` | 3471 | 4455 | ±406 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 1.2 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 72 | 207 | ±51.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 60 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 407 | 520 | ±51.1 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `cc.max_settle_ms` | 1460 | 400 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 25.8 | 29.6 | ±3.6 | ahead | 8/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 76.9 | 97.5 | ±17.6 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 2138 | 8516 | ±961 | ahead | 10/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 11.7 | 18.4 | ±3.7 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 125 | 3.3 | ±6.3 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3709 | 4455 | ±406 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.6 | 6.0 | ±1.2 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 198 | 317 | ±21.6 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 459 | 536 | ±41.7 | ahead | 9/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 192 | 162 | ±28.1 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 69.2 | 105 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 20.0 | 6.0 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 60.3 | 33.4 | ±3.0 | behind | 10/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 839 | 1299 | ±64.9 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 238 | 2319 | ±478 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.3 | 4.3 | ±0.7 | ahead | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 14.3 | 0 | ±0.7 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `video.longest_freeze_ms` | 2413 | 1917 | ±313 | behind | 7/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.render_fps` | 26.2 | 27.9 | ±1.4 | behind | 6/10 | #37, #24, #20 |

