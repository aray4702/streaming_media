## Parity with libwebrtc M157.8103@{#0}

71 scenarios, mean of 10 seeds. **519 gaps**: 261 behind libwebrtc, 258 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

520 gaps on `main`, 519 here: **3 closed, 2 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_all_v2_no_stall | `video.e2e_delay_p95_ms` | 482 | 441 | ±24.1 | behind | 7/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 591 | 432 | ±93.5 | behind | 7/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 56.5 | 45.3 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.5 | 7.3 | ±2 | behind | 6/10 | #21, #7, #29 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 149 | 238 (further) |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 23.4 | 21.0 (closer) |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 1.8 | 1.5 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 81 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 117 | 147 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `cc.utilization_pct` | 93.5 | 77.3 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `audio.jb_delay_p95_ms` | 44.8 | 110 | ±17.0 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 102 | 169 | ±21.1 | ahead | 10/10 | #62, #63 |
| bufferbloat | `cc.queue_p95_ms` | 27.3 | 46.1 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.utilization_pct` | 86.6 | 91.6 | ±4.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `video.avg_kbps` | 743 | 813 | ±40.6 | behind | 10/10 | #37, #61 |
| bufferbloat | `video.e2e_delay_p95_ms` | 73.2 | 89.3 | ±10 | ahead | 7/10 | #24, #37 |
| bufferbloat | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `audio.concealed_pct` | 2.6 | 1.5 | ±0.5 | behind | 10/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 60.4 | 162 | ±44.2 | ahead | 10/10 | #62, #63 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 138 | 252 | ±50.8 | ahead | 10/10 | #62, #63 |
| burst_loss | `cc.queue_p95_ms` | 17.8 | 48.6 | ±11.7 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.utilization_pct` | 57.0 | 81.3 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `video.avg_kbps` | 985 | 1480 | ±76.5 | behind | 10/10 | #37, #61 |
| burst_loss | `video.freeze_count` | 0.8 | 6.5 | ±2.5 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.freeze_total_ms` | 180 | 1572 | ±637 | ahead | 10/10 | #5, #37, #24 |
| burst_loss | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `video.longest_freeze_ms` | 146 | 348 | ±100 | ahead | 6/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 352 | 507 | ±36.0 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.longest_interruption_ms` | 117 | 140 | ±20 | ahead | 4/10 | #62, #63 |
| capacity_drop | `cc.queue_p95_ms` | 385 | 281 | ±21.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `net.loss_pct` | 4.4 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| capacity_drop | `repair.overhead_pct` | 7.6 | 3.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| capacity_drop | `video.freeze_total_ms` | 2890 | 2511 | ±343 | behind | 8/10 | #5, #37, #24 |
| capacity_drop | `video.keyframe_requests` | 10.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| clean_link | `audio.jb_delay_p95_ms` | 44 | 132 | ±23.0 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 102 | 223 | ±34.2 | ahead | 10/10 | #62, #63 |
| clean_link | `cc.queue_p95_ms` | 23.2 | 73.9 | ±24.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| clean_link | `cc.utilization_pct` | 86.0 | 91.4 | ±4.6 | behind | 8/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.avg_kbps` | 1972 | 2138 | ±107 | behind | 9/10 | #37, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 68.1 | 122 | ±19.3 | ahead | 10/10 | #24, #37 |
| clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `audio.jb_delay_p95_ms` | 139 | 382 | ±27.3 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.longest_interruption_ms` | 18 | 46 | ±28.0 | ahead | 4/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 302 | 649 | ±49.6 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 128 | 251 | ±23.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `cc.utilization_pct` | 67.6 | 74.4 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.avg_kbps` | 1584 | 1785 | ±89.2 | behind | 9/10 | #37, #61 |
| cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 76.8 | 111 | ±10 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 195 | 211 | ±10.6 | ahead | 6/10 | #62, #63, #19, #24 |
| high_jitter | `cc.utilization_pct` | 8.3 | 35.9 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `repair.overhead_pct` | 13.4 | 43.8 | ±2.6 | ahead | 10/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.avg_kbps` | 94.8 | 564 | ±69.2 | behind | 10/10 | #37, #61, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 1532 | 135 | ±76.6 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `video.render_fps` | 26.3 | 29.8 | ±1.5 | behind | 10/10 | #37, #24, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 166 | 305 | ±19.5 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 350 | 508 | ±61.9 | ahead | 9/10 | #62, #63 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 772 | 2165 | ±517 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 507 | ±36.0 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 637 | 806 | ±40.3 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 251 | 281 | ±21.9 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 84.4 | 71.3 | ±10 | behind | 8/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 1.7 | 2.4 | ±0.7 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1987 | 2511 | ±343 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 8 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1505 | 1786 | ±249 | ahead | 7/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 19.1 | 26.8 | ±3.2 | ahead | 9/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 36.8 | 87.2 | ±12.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 6162 | 8447 | ±1167 | ahead | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.freeze_count` | 15 | 26.7 | ±7.5 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.2 | 1.7 | ±1.1 | behind | 9/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.6 | 7.8 | ±2.4 | behind | 10/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 75.2 | 110 | ±17.0 | ahead | 10/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.0 | 1.5 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 83.6 | 162 | ±44.2 | ahead | 9/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.mouth_to_ear_p95_ms` | 185 | 252 | ±50.8 | ahead | 8/10 | #62, #63 |
| issue_124_burst_loss_v2 | `video.freeze_count` | 1.5 | 6.5 | ±2.5 | ahead | 10/10 | #5, #37, #24 |
| issue_124_burst_loss_v2 | `video.freeze_total_ms` | 381 | 1572 | ±637 | ahead | 10/10 | #5, #37, #24 |
| issue_124_burst_loss_v2 | `video.longest_freeze_ms` | 247 | 348 | ±100 | ahead | 4/10 | #5, #37, #24 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 84.8 | 133 | ±20.9 | ahead | 39/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_126_audio_v2 | `cc.rtt_p50_ms` | 73.7 | 63.0 | ±10 | behind | 7/10 | #61 |
| issue_126_audio_v2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_127_all_v2_no_stall | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 89.6 | 158 | ±27.0 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 299 | 347 | ±29.2 | ahead | 8/10 | #62, #63 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 3 | 61.6 | ±6.1 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 852 | 17971 | ±1617 | ahead | 10/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 374 | 612 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 1.0 | 0.1 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.jb_delay_p95_ms` | 72 | 98 | ±10.3 | ahead | 8/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.max_settle_ms` | 32040 | 17290 | ±10994 | behind | 8/10 | #15, #48, #61, #20 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.utilization_pct` | 8.3 | 58.6 | ±15.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 11.2 | 47.8 | ±6.1 | ahead | 10/10 | #21, #7, #29 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.avg_kbps` | 102 | 944 | ±240 | behind | 10/10 | #37, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 2063 | 175 | ±103 | behind | 10/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.keyframe_requests` | 3.6 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 681 | 262 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.render_fps` | 26.6 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.6 | 1.5 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 90.8 | 162 | ±44.2 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.mouth_to_ear_p95_ms` | 182 | 252 | ±50.8 | ahead | 7/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `cc.queue_p95_ms` | 31.7 | 48.6 | ±11.7 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `cc.utilization_pct` | 72.4 | 81.3 | ±4.1 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1275 | 1480 | ±76.5 | behind | 9/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.freeze_count` | 1.3 | 6.5 | ±2.5 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.freeze_total_ms` | 291 | 1572 | ±637 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_burst_loss | `video.longest_freeze_ms` | 207 | 348 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 57.6 | 87 | ±10.6 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.6 | 3.9 | ±0.8 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.3 | 7.3 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1071 | 1160 | ±58.0 | behind | 10/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 782 | 432 | ±93.5 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3280 | 1823 | ±403 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 34.7 | 0 | ±1.7 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1926 | 624 | ±226 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.render_fps` | 26.9 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 111 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 38.0 | 43.8 | ±2.6 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 162 | 135 | ±10 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 11.9 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 26.4 | 13.7 | ±2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.1 | 11.0 | ±1.6 | ahead | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 16.8 | 65.0 | ±16.5 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 289 | 146 | ±21.1 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 497 | 3186 | ±781 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 0.2 | 7.4 | ±2.1 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 73.1 | 10660 | ±5828 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 13.6 | 3.3 | ±1.3 | behind | 10/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 86.6 | 3474 | ±1578 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.6 | ±1.0 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 10098 | ±5886 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 29.1 | 18.4 | ±3.2 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 69.2 | 109 | ±27.7 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `cc.utilization_pct` | 74.1 | 79.6 | ±4.0 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_random_loss | `video.avg_kbps` | 1286 | 1420 | ±71.0 | behind | 9/10 | #37, #61 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 320 | 213 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.3 | 7.9 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 64.5 | 1975 | ±511 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.keyframe_requests` | 2.2 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 84 | 368 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 26.8 | 40 | ±10 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_shallow | `cc.utilization_pct` | 69.5 | 55.9 | ±3.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 8.5 | 6.2 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 15.5 | 12.3 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 889 | 731 | ±44.5 | ahead | 9/10 | #37, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 495 | 317 | ±24.7 | behind | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 1.8 | 26.6 | ±4.6 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 519 | 7510 | ±1119 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.keyframe_requests` | 9.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 287 | 590 | ±172 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 18.4 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 59.4 | 56.3 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.3 | 6.8 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.1 | 12.6 | ±2 | behind | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 436 | 255 | ±21.8 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0 | 20.5 | ±3.5 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 0 | 5254 | ±833 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 16.2 | 451 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `cc.rtt_p50_ms` | 108 | 90.2 | ±10 | behind | 7/10 | #61 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 171 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 109 | 93.2 | ±10 | behind | 10/10 | #61 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 108 | 93.6 | ±10 | behind | 8/10 | #61 |
| issue_133_pacer_v2_outage_15s | `cc.max_settle_ms` | 200 | 15000 | ±750 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `cc.utilization_pct` | 89.4 | 101 | ±5.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_15s | `net.loss_pct` | 5.1 | 11.3 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_15s | `video.avg_kbps` | 1405 | 1690 | ±84.5 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 136 | 92.7 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_3s | `cc.max_settle_ms` | 200 | 14300 | ±13387 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_3s | `cc.utilization_pct` | 86.2 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_3s | `video.avg_kbps` | 1944 | 2301 | ±115 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 200 | 3070 | ±1029 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.queue_p95_ms` | 23.6 | 229 | ±30.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `cc.utilization_pct` | 91.6 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 3.7 | 8.6 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 0 | 5.9 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `video.avg_kbps` | 1821 | 2040 | ±102 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 136 | 325 | ±80.2 | ahead | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `video.freeze_count` | 0 | 1.9 | ±1.0 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.freeze_total_ms` | 0 | 769 | ±498 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.longest_freeze_ms` | 15.3 | 436 | ±313 | ahead | 9/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.pause_total_ms` | 8276 | 9780 | ±489 | ahead | 10/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 40/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.6 | 107 | ±10.6 | ahead | 39/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.5 | 43.6 | ±3.3 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 189 | 135 | ±10 | behind | 40/40 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 180 | 96.5 | ±50 | behind | 23/40 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 83.2 | 112 | ±14.8 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 36.3 | 43.1 | ±3.1 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 193 | 134 | ±10 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.time_to_first_frame_ms` | 179 | 88.6 | ±50 | behind | 6/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 78.4 | 107 | ±10.6 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 34.4 | 43.8 | ±2.7 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.e2e_delay_p95_ms` | 204 | 133 | ±10.2 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 196 | 89.4 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `audio.jb_delay_p95_ms` | 90.8 | 117 | ±17.7 | ahead | 8/10 | #62, #63 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 64.2 | 40.6 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 154 | 114 | ±10 | behind | 4/10 | #24, #37 |
| issue_151_outage_3s_v2 | `video.freeze_total_ms` | 3413 | 3227 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.longest_freeze_ms` | 3413 | 3227 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 22.4 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.e2e_delay_p95_ms` | 1530 | 2707 | ±1158 | ahead | 8/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3 | 11.3 | ±2.4 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 44 | 132 | ±23.0 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 105 | 223 | ±34.2 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 26.1 | 73.9 | ±24.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 71.8 | 122 | ±19.3 | ahead | 10/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 186 | 382 | ±27.3 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 441 | 649 | ±49.6 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 222 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 85.4 | 67.5 | ±11.5 | behind | 1/10 | #61 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 69.2 | 109 | ±27.7 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `cc.utilization_pct` | 74.1 | 79.6 | ±4.0 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_random_loss | `video.avg_kbps` | 1286 | 1420 | ±71.0 | behind | 9/10 | #37, #61 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 320 | 213 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.3 | 7.9 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 64.5 | 1975 | ±511 | ahead | 10/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.keyframe_requests` | 2.2 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 84 | 368 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 49.6 | 109 | ±27.7 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.mouth_to_ear_p95_ms` | 119 | 176 | ±31.3 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 16.9 | 32.4 | ±10 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 64.8 | 79.6 | ±4.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1117 | 1420 | ±71.0 | behind | 10/10 | #37, #61 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 292 | 213 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.1 | 7.9 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 18.9 | 1975 | ±511 | ahead | 10/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 34.5 | 368 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 75.2 | 111 | ±10 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 194 | 211 | ±10.6 | ahead | 6/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 7.5 | 35.9 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 9.4 | 43.8 | ±2.6 | ahead | 10/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 81.9 | 564 | ±69.2 | behind | 10/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 158 | 135 | ±10 | behind | 8/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 94.0 | 73.9 | ±10 | behind | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `cc.max_settle_ms` | 6900 | 4730 | ±1879 | behind | 5/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 62.7 | 48.9 | ±3.1 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.7 | 8.1 | ±1.1 | behind | 10/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 40.5 | 20.7 | ±2.8 | behind | 10/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 562 | 460 | ±30.9 | ahead | 9/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 874 | 2707 | ±1158 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 56.4 | 1 | ±2.8 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 358 | 507 | ±36.0 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 385 | 281 | ±21.9 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `net.loss_pct` | 4.0 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.9 | 3.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_capacity_drop | `video.avg_kbps` | 837 | 883 | ±44.2 | behind | 3/10 | #37, #61 |
| issue_23_capacity_drop | `video.freeze_count` | 3.3 | 2.4 | ±0.7 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 5206 | 2511 | ±343 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3348 | 1786 | ±249 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 26.0 | 28.9 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 205 | 305 | ±19.5 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `cc.utilization_pct` | 73.4 | 104 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 17.9 | 5.9 | ±1.3 | behind | 10/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 55.0 | 32.8 | ±2.8 | behind | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.avg_kbps` | 894 | 1293 | ±64.7 | behind | 10/10 | #37, #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 278 | 2165 | ±517 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 2.2 | 4.2 | ±1.1 | ahead | 9/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 5249 | 4354 | ±856 | behind | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_handover | `video.longest_freeze_ms` | 3364 | 1940 | ±353 | behind | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 25.6 | 28.0 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 285 | 355 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 190 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.max_settle_ms` | 960 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 468 | 385 | ±65.4 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `net.loss_pct` | 2.4 | 0.4 | ±1.1 | behind | 9/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.6 | 1.5 | ±2.1 | behind | 10/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 909 | 2956 | ±372 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 5909 | 1178 | ±2987 | behind | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 3824 | 365 | ±638 | behind | 10/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 24.2 | 26.2 | ±1.3 | behind | 6/10 | #37, #24 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 71.2 | 117 | ±17.7 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 135 | 181 | ±19.1 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 23.3 | 40.6 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `cc.utilization_pct` | 76.3 | 89.2 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `net.loss_pct` | 5.2 | 5.9 | ±0.5 | ahead | 8/10 | #21, #61, #12 |
| issue_23_outage_3s | `video.avg_kbps` | 934 | 1144 | ±57.2 | behind | 10/10 | #37, #61 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 91.6 | 114 | ±10 | ahead | 8/10 | #24, #37 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 1.9 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 269 | 142 | ±21.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 329 | 215 | ±30.7 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 29.5 | 60.9 | ±16.0 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 60.5 | 89.2 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `net.loss_pct` | 2.3 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_outage_500ms | `repair.overhead_pct` | 9.1 | 2.0 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_outage_500ms | `video.avg_kbps` | 988 | 1619 | ±81.0 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 389 | 158 | ±36.4 | behind | 8/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 2 | 1 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1925 | 577 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1321 | 577 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 227 | 355 | ±29.5 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 439 | 771 | ±65.9 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 163 | 385 | ±65.4 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 103 | 222 | ±66.0 | ahead | 10/10 | #61 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2247 | 2956 | ±372 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 28.0 | 26.2 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 89.6 | 121 | ±23.3 | ahead | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 64 | 27 | ±20 | behind | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1500 | 2860 | ±611 | ahead | 9/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 77.6 | 127 | ±12.7 | ahead | 10/10 | #61 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 80.9 | 90.0 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 8.4 | 5.9 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `repair.overhead_pct` | 14.9 | 10.4 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 784 | 911 | ±45.6 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 549 | 1868 | ±134 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 2.4 | 6.8 | ±2.1 | ahead | 9/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 17.5 | 0 | ±0.9 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1613 | 1160 | ±272 | behind | 6/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 139 | 382 | ±27.3 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.longest_interruption_ms` | 18 | 46 | ±28.0 | ahead | 4/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 302 | 649 | ±49.6 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 128 | 251 | ±23.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `cc.utilization_pct` | 67.6 | 74.4 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.avg_kbps` | 1584 | 1785 | ±89.2 | behind | 9/10 | #37, #61 |
| issue_5_adjuster_default | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 74.4 | 117 | ±14.2 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 134 | 172 | ±14.5 | ahead | 9/10 | #62, #63 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 20.4 | 36.8 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 56.6 | 85.7 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 727 | 1170 | ±58.5 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 173 | 140 | ±10 | behind | 9/10 | #24, #37 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.5 | 1.7 | ±0.7 | behind | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 19.6 | 41.4 | ±10.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `cc.utilization_pct` | 58.9 | 83.8 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `video.avg_kbps` | 997 | 1506 | ±75.3 | behind | 10/10 | #37, #61 |
| issue_5_burst15_freeze | `video.keyframe_requests` | 1.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 502 | ±38.8 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 116 | 144 | ±20.7 | ahead | 3/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 712 | 797 | ±44.4 | ahead | 7/10 | #62, #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 324 | 276 | ±23.7 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.1 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_5_keyframe_flush | `video.avg_kbps` | 827 | 883 | ±44.2 | behind | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2077 | 2559 | ±350 | ahead | 6/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1326 | 1795 | ±258 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 60 | 87 | ±10.6 | ahead | 9/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.3 | 3.9 | ±0.8 | behind | 8/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.5 | 7.3 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 25.9 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 42.4 | 86 | ±33.1 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 8890 | 1130 | ±2665 | behind | 8/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 10.8 | 27.6 | ±11.9 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 54.2 | 88.0 | ±9.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 22.5 | 32.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1030 | 1611 | ±158 | behind | 10/10 | #37, #61 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 192 | 305 | ±19.5 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 78.5 | 104 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 961 | 1293 | ±64.7 | behind | 10/10 | #37, #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 795 | 2165 | ±517 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.7 | 0.9 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 493 | ±37.4 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 118 | 150 | ±29.4 | ahead | 6/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 629 | 778 | ±45.6 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.8 | 3.2 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1208 | 1615 | ±202 | ahead | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 1799 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 44 | 105 | ±23.2 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 125 | 182 | ±25.4 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 30000 | 3270 | ±3245 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.6 | 25.9 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 24.3 | 83.5 | ±6.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 19.0 | 40.2 | ±2.0 | ahead | 10/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 424 | 1443 | ±104 | behind | 10/10 | #37, #61 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 147 | 158 | ±10 | ahead | 7/10 | #24, #37 |
| issue_76_session_layer_v2 | `video.freeze_count` | 0 | 0.9 | ±0.9 | ahead | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.longest_freeze_ms` | 12.2 | 161 | ±128 | ahead | 6/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 75.2 | 111 | ±10 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 193 | 211 | ±10.6 | ahead | 6/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 11.2 | 35.9 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 16.9 | 43.8 | ±2.6 | ahead | 10/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 148 | 564 | ±69.2 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 737 | 135 | ±36.9 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 49.6 | 109 | ±27.7 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.mouth_to_ear_p95_ms` | 119 | 176 | ±31.3 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 16.9 | 32.4 | ±10 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 64.8 | 79.6 | ±4.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1117 | 1420 | ±71.0 | behind | 10/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 292 | 213 | ±22.1 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.1 | 7.9 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 18.9 | 1975 | ±511 | ahead | 10/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 34.5 | 368 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 46.9 | 163 | ±18.3 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 117 | 265 | ±22.4 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 200 | 2660 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 19.4 | 56.8 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `cc.utilization_pct` | 71.5 | 88.5 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `net.loss_pct` | 9.8 | 11.7 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_85_video_pause | `video.avg_kbps` | 997 | 1299 | ±65.0 | behind | 10/10 | #37, #61 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 91.7 | 157 | ±12.6 | ahead | 10/10 | #24, #37 |
| issue_85_video_pause | `video.keyframe_requests` | 2 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_85_video_pause | `video.pause_total_ms` | 10246 | 12267 | ±613 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.3 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 48 | 155 | ±30.6 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 111 | 226 | ±41.0 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `cc.queue_p95_ms` | 23.8 | 49.0 | ±12.0 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 75.6 | 119 | ±14.9 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 275 | 355 | ±29.5 | ahead | 9/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 190 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.max_settle_ms` | 1000 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| low_bandwidth | `cc.queue_p95_ms` | 472 | 385 | ±65.4 | behind | 8/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `net.loss_pct` | 2.1 | 0.4 | ±1.1 | behind | 9/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 9.0 | 1.5 | ±2.1 | behind | 9/10 | #21, #7, #29 |
| low_bandwidth | `video.avg_kbps` | 50.3 | 70.8 | ±20 | behind | 5/10 | #37, #61 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 944 | 2956 | ±372 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.keyframe_requests` | 15.3 | 0 | ±0.8 | behind | 10/10 | #23 |
| outage_3s | `audio.concealed_pct` | 11.1 | 10.2 | ±0.6 | behind | 10/10 | #62, #63, #20 |
| outage_3s | `audio.jb_delay_p95_ms` | 186 | 117 | ±17.7 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 250 | 181 | ±19.1 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 24.0 | 40.6 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `cc.utilization_pct` | 69.0 | 89.2 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `video.avg_kbps` | 826 | 1144 | ±57.2 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 146 | 114 | ±10 | behind | 6/10 | #24, #37, #20 |
| outage_3s | `video.keyframe_requests` | 5.3 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 507 | ±36.0 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 119 | 140 | ±20 | ahead | 2/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 713 | 806 | ±40.3 | ahead | 8/10 | #62, #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 325 | 281 | ±21.9 | behind | 8/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 833 | 883 | ±44.2 | behind | 6/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2133 | 2511 | ±343 | ahead | 5/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 6.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1404 | 1786 | ±249 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 339 | 507 | ±36.0 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 711 | 806 | ±40.3 | ahead | 9/10 | #62, #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 281 | ±21.9 | behind | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 84.5 | 71.3 | ±10 | behind | 9/10 | #61 |
| probe_v2_capacity_drop | `net.loss_pct` | 3.3 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `repair.overhead_pct` | 6.0 | 3.2 | ±2 | behind | 3/10 | #21, #7, #29 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3118 | 2511 | ±343 | behind | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 7.9 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 132 | ±23.0 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 81.9 | 66.3 | ±10 | behind | 9/10 | #61 |
| probe_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 186 | 382 | ±27.3 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 441 | 649 | ±49.6 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 222 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 85.4 | 67.5 | ±11.5 | behind | 1/10 | #61 |
| random_loss_5pct | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 8/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 49.6 | 109 | ±27.7 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.mouth_to_ear_p95_ms` | 119 | 176 | ±31.3 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `cc.queue_p95_ms` | 16.9 | 32.4 | ±10 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `cc.utilization_pct` | 64.8 | 79.6 | ±4.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `video.avg_kbps` | 1117 | 1420 | ±71.0 | behind | 10/10 | #37, #61 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 292 | 213 | ±22.1 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.1 | 7.9 | ±2.0 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 18.9 | 1975 | ±511 | ahead | 10/10 | #5, #37, #24 |
| random_loss_5pct | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| random_loss_5pct | `video.longest_freeze_ms` | 34.5 | 368 | ±100 | ahead | 10/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 48 | 134 | ±55.6 | ahead | 10/10 | #62, #63 |
| satellite | `audio.mouth_to_ear_p95_ms` | 386 | 469 | ±58.4 | ahead | 10/10 | #62, #63 |
| satellite | `cc.queue_p95_ms` | 13.8 | 32.0 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.utilization_pct` | 58.6 | 82.0 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `repair.overhead_pct` | 15.5 | 17.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| satellite | `video.avg_kbps` | 655 | 960 | ±48.0 | behind | 10/10 | #37, #61 |
| satellite | `video.e2e_delay_p95_ms` | 829 | 929 | ±46.4 | ahead | 9/10 | #24, #37 |
| satellite | `video.freeze_count` | 22.4 | 30.1 | ±3.2 | ahead | 8/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6667 | 17335 | ±1735 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `video.render_fps` | 24.8 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.concealed_pct` | 12.2 | 0.5 | ±0.6 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.interruptions` | 0.2 | 1 | ±0.5 | ahead | 8/10 | #62, #63 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 69.6 | 164 | ±17.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 126 | 203 | ±20 | ahead | 9/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 407 | 478 | ±23.9 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `cc.max_settle_ms` | 1420 | 40000 | ±2000 | ahead | 10/10 | #15, #48, #61, #20 |
| very_low_capacity | `net.loss_pct` | 34.2 | 26.8 | ±3.2 | behind | 10/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 29.3 | 87.2 | ±12.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.avg_kbps` | 51.8 | 30.2 | ±20 | ahead | 9/10 | #37, #61 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 2274 | 8447 | ±1167 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 10.4 | 26.7 | ±7.5 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_total_ms` | 17081 | 26567 | ±6004 | ahead | 7/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 219 | 1.7 | ±10.9 | behind | 10/10 | #23 |
| very_low_capacity | `video.render_fps` | 1.1 | 7.8 | ±2.4 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 1.1 | 0.5 | ±0.5 | behind | 7/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 72 | 164 | ±17.4 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 62 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 407 | 478 | ±23.9 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 2227 | 8447 | ±1167 | ahead | 10/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 8.8 | 26.7 | ±7.5 | ahead | 10/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 16992 | 26567 | ±6004 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 125 | 1.7 | ±6.3 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.3 | 7.8 | ±2.4 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 202 | 305 | ±19.5 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 69.8 | 104 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 19.2 | 5.9 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 57.9 | 32.8 | ±2.9 | behind | 10/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 850 | 1293 | ±64.7 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 210 | 2165 | ±517 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.5 | 4.2 | ±1.1 | ahead | 8/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 14.5 | 0 | ±0.7 | behind | 10/10 | #23, #20 |
| wifi_to_lte_handover | `video.render_fps` | 26.5 | 28.0 | ±1.4 | behind | 4/10 | #37, #24, #20 |

