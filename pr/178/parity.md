## Parity with libwebrtc M157.8103@{#0}

76 scenarios, mean of 10 seeds. **536 gaps**: 268 behind libwebrtc, 268 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

536 gaps on `main`, 536 here: **6 closed, 6 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 222 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 85.4 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 222 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 85.4 | 64.5 | ±10 | behind | 1/10 | #61 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 467 | 641 | ±39.5 | ahead | 10/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 189 | 377 | ±21.5 | ahead | 10/10 | #62, #63 |
| issue_152_link_allocation_v2 | `audio.mouth_to_ear_p95_ms` | 83.7 | 98.9 | ±10 | ahead | 9/10 | #62, #63 |
| issue_152_link_allocation_v2 | `audio.jb_delay_p95_ms` | 29 | 64 | ±10 | ahead | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.longest_interruption_ms` | 48 | 27 | ±20 | behind | 5/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.5 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 41.2 | 47.1 (further) |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 208 | 181 (closer) |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 235 | 313 (further) |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 405 | 524 (further) |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 1.6 | 1.3 (closer) |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 208 | 181 (closer) |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 8422 | 6974 (closer) |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 585 | 687 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 10/10 | #62, #63 |
| audio_only_lossy | `audio.jb_delay_p95_ms` | 44 | 81 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 117 | 147 | ±10 | ahead | 10/10 | #62, #63 |
| audio_only_lossy | `cc.utilization_pct` | 93.5 | 77.3 | ±4.7 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `audio.jb_delay_p95_ms` | 44.8 | 115 | ±17.8 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 102 | 176 | ±20.4 | ahead | 10/10 | #62, #63 |
| bufferbloat | `cc.queue_p95_ms` | 27.3 | 48.4 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `cc.utilization_pct` | 86.6 | 91.6 | ±4.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| bufferbloat | `video.avg_kbps` | 743 | 812 | ±40.6 | behind | 10/10 | #37, #61 |
| bufferbloat | `video.e2e_delay_p95_ms` | 73.2 | 91.2 | ±10 | ahead | 7/10 | #24, #37 |
| bufferbloat | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `audio.concealed_pct` | 2.6 | 1.3 | ±0.5 | behind | 10/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 60.4 | 157 | ±45.7 | ahead | 10/10 | #62, #63 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 138 | 248 | ±56.2 | ahead | 10/10 | #62, #63 |
| burst_loss | `cc.queue_p95_ms` | 17.8 | 51.1 | ±12.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `cc.utilization_pct` | 57.0 | 81.4 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| burst_loss | `video.avg_kbps` | 985 | 1477 | ±73.8 | behind | 10/10 | #37, #61 |
| burst_loss | `video.e2e_delay_p95_ms` | 199 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| burst_loss | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| burst_loss | `video.longest_freeze_ms` | 146 | 292 | ±125 | ahead | 5/10 | #5, #37, #24 |
| capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.longest_interruption_ms` | 117 | 140 | ±20 | ahead | 4/10 | #62, #63 |
| capacity_drop | `cc.queue_p95_ms` | 385 | 279 | ±19.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `net.loss_pct` | 4.4 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| capacity_drop | `repair.overhead_pct` | 7.6 | 3.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| capacity_drop | `video.keyframe_requests` | 10.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| clean_link | `audio.jb_delay_p95_ms` | 44 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.mouth_to_ear_p95_ms` | 102 | 221 | ±37.3 | ahead | 10/10 | #62, #63 |
| clean_link | `cc.queue_p95_ms` | 23.2 | 74.4 | ±25.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| clean_link | `cc.utilization_pct` | 86.0 | 91.4 | ±4.6 | behind | 8/10 | #19, #15, #48, #12, #61 |
| clean_link | `video.avg_kbps` | 1972 | 2137 | ±107 | behind | 9/10 | #37, #61 |
| clean_link | `video.e2e_delay_p95_ms` | 68.1 | 123 | ±19.8 | ahead | 10/10 | #24, #37 |
| clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| cross_traffic | `audio.jb_delay_p95_ms` | 139 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.longest_interruption_ms` | 18 | 46 | ±28.0 | ahead | 4/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 302 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 128 | 251 | ±23.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `cc.utilization_pct` | 67.6 | 74.5 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.avg_kbps` | 1584 | 1785 | ±89.3 | behind | 9/10 | #37, #61 |
| cross_traffic | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 76.8 | 111 | ±11.0 | ahead | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 195 | 212 | ±11.1 | ahead | 7/10 | #62, #63, #19, #24 |
| high_jitter | `cc.utilization_pct` | 8.3 | 35.3 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `repair.overhead_pct` | 13.4 | 43.7 | ±2.5 | ahead | 10/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.avg_kbps` | 94.8 | 554 | ±64.5 | behind | 10/10 | #37, #61, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 1532 | 150 | ±76.6 | behind | 10/10 | #24, #37, #19 |
| high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23, #19, #24 |
| high_jitter | `video.render_fps` | 26.3 | 29.8 | ±1.5 | behind | 10/10 | #37, #24, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 166 | 314 | ±23.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 350 | 525 | ±63.8 | ahead | 9/10 | #62, #63 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 802 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 637 | 794 | ±39.7 | ahead | 9/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 251 | 279 | ±16.3 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 84.4 | 72.2 | ±10 | behind | 8/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 196 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1987 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.keyframe_requests` | 8 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 151 | 186 | ±24.5 | ahead | 8/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 36.8 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 15 | 21.9 | ±5.4 | ahead | 7/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.2 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.6 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 75.2 | 115 | ±17.8 | ahead | 10/10 | #62, #63 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 155 | 176 | ±20.4 | ahead | 6/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.0 | 1.3 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 83.6 | 157 | ±45.7 | ahead | 10/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.mouth_to_ear_p95_ms` | 185 | 248 | ±56.2 | ahead | 9/10 | #62, #63 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 86 | 133 | ±21.9 | ahead | 40/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_126_audio_v2 | `cc.rtt_p50_ms` | 73.7 | 63.0 | ±10 | behind | 7/10 | #61 |
| issue_126_audio_v2 | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_127_all_v2_no_stall | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 89.6 | 144 | ±29.5 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 299 | 332 | ±33.5 | ahead | 5/10 | #62, #63 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 3 | 11.8 | ±6.3 | ahead | 9/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 852 | 3072 | ±1628 | ahead | 9/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 1.0 | 0.1 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.jb_delay_p95_ms` | 72 | 99 | ±10 | ahead | 9/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.max_settle_ms` | 32040 | 15870 | ±8707 | behind | 8/10 | #15, #48, #61, #20 |
| issue_127_pacer_v2_jitter_reorder_loss | `cc.utilization_pct` | 8.3 | 59.0 | ±12.8 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `repair.overhead_pct` | 11.2 | 48.4 | ±4.2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.avg_kbps` | 102 | 952 | ±192 | behind | 10/10 | #37, #61 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 2063 | 185 | ±103 | behind | 10/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_count` | 3.4 | 1.1 | ±0.7 | behind | 5/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 1262 | 240 | ±150 | behind | 7/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.keyframe_requests` | 3.6 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 681 | 195 | ±100 | behind | 5/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.render_fps` | 26.6 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.6 | 1.3 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 90.8 | 157 | ±45.7 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.mouth_to_ear_p95_ms` | 182 | 248 | ±56.2 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `cc.queue_p95_ms` | 31.7 | 51.1 | ±12.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `cc.utilization_pct` | 72.4 | 81.4 | ±4.1 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1275 | 1477 | ±73.8 | behind | 9/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 208 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 57.6 | 85 | ±10.8 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 10.6 | 3.8 | ±0.7 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 17.3 | 7.5 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1071 | 1146 | ±57.3 | behind | 8/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 782 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 5 | 3.7 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 3280 | 1340 | ±401 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.keyframe_requests` | 34.7 | 0 | ±1.7 | behind | 10/10 | #23 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 1926 | 577 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.render_fps` | 26.9 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 83.2 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 38.0 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 11.9 | 43 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 26.4 | 13.3 | ±2.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.1 | 11.5 | ±2.0 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 16.8 | 73.4 | ±23.1 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 289 | 138 | ±27.4 | ahead | 10/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 497 | 3282 | ±495 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 0.2 | 7 | ±2.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 73.1 | 10663 | ±3593 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 13.6 | 2.9 | ±1.4 | behind | 10/10 | #23 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 86.6 | 3230 | ±749 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 0 | 1.3 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 0 | 8280 | ±5343 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.render_fps` | 29.1 | 19.2 | ±3.8 | ahead | 10/10 | #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 69.2 | 101 | ±20.2 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.mouth_to_ear_p95_ms` | 145 | 168 | ±21.9 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `cc.utilization_pct` | 74.1 | 79.6 | ±4.0 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_random_loss | `video.avg_kbps` | 1286 | 1422 | ±71.1 | behind | 8/10 | #37, #61 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 320 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 64.5 | 415 | ±318 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.keyframe_requests` | 2.2 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 84 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 26.8 | 40 | ±10 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_shallow | `cc.utilization_pct` | 69.5 | 56.9 | ±3.5 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 8.5 | 6.3 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `repair.overhead_pct` | 15.5 | 12.8 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `video.avg_kbps` | 889 | 744 | ±44.5 | ahead | 9/10 | #37, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 495 | 226 | ±24.7 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 1.8 | 14.8 | ±5.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 519 | 2820 | ±1084 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.keyframe_requests` | 9.1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 18.4 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 59.4 | 56.3 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.3 | 6.7 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.1 | 12.7 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 775 | 736 | ±38.8 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 436 | 209 | ±21.8 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0 | 4.3 | ±2.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 0 | 842 | ±556 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 16.2 | 211 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `cc.rtt_p50_ms` | 109 | 91.2 | ±10 | behind | 7/10 | #61 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 173 | 92.4 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 109 | 92.3 | ±10 | behind | 10/10 | #61 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 109 | 94.2 | ±10 | behind | 9/10 | #61 |
| issue_133_pacer_v2_outage_15s | `cc.max_settle_ms` | 200 | 15000 | ±750 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `cc.utilization_pct` | 89.4 | 101 | ±5.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_15s | `net.loss_pct` | 5.1 | 11.3 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_15s | `video.avg_kbps` | 1405 | 1690 | ±84.5 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 136 | 92.4 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_3s | `cc.max_settle_ms` | 200 | 14300 | ±13387 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_3s | `cc.utilization_pct` | 86.2 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_3s | `video.avg_kbps` | 1944 | 2301 | ±115 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_3s | `video.keyframe_requests` | 5 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 200 | 3170 | ±1172 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.queue_p95_ms` | 23.6 | 231 | ±30.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `cc.utilization_pct` | 91.6 | 103 | ±5.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 3.7 | 8.5 | ±0.5 | ahead | 10/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 0 | 5.9 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `video.avg_kbps` | 1821 | 2044 | ±102 | behind | 10/10 | #37, #61 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 136 | 430 | ±108 | ahead | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `video.freeze_count` | 0 | 2.1 | ±0.9 | ahead | 10/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.freeze_total_ms` | 0 | 819 | ±416 | ahead | 10/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.longest_freeze_ms` | 15.3 | 438 | ±131 | ahead | 10/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.pause_total_ms` | 8276 | 9766 | ±488 | ahead | 10/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 40/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.6 | 106 | ±10.3 | ahead | 39/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.5 | 43.7 | ±3.2 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 189 | 148 | ±15.6 | behind | 34/40 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 180 | 96.5 | ±50 | behind | 23/40 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 83.2 | 112 | ±14.8 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 36.3 | 43.6 | ±3.8 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 193 | 144 | ±10 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.time_to_first_frame_ms` | 179 | 88.6 | ±50 | behind | 6/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 78.4 | 108 | ±11.4 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 34.4 | 44.9 | ±3.3 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.e2e_delay_p95_ms` | 204 | 147 | ±26.1 | behind | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 196 | 89.4 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `audio.jb_delay_p95_ms` | 89.6 | 119 | ±19.1 | ahead | 8/10 | #62, #63 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 63.8 | 40.2 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 160 | 113 | ±10 | behind | 4/10 | #24, #37 |
| issue_151_outage_3s_v2 | `video.freeze_total_ms` | 3422 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.longest_freeze_ms` | 3422 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 189 | 377 | ±21.5 | ahead | 10/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 467 | 641 | ±39.5 | ahead | 10/10 | #62, #63 |
| issue_152_link_allocation_v2 | `audio.jb_delay_p95_ms` | 29 | 64 | ±10 | ahead | 10/10 | #62, #63 |
| issue_152_link_allocation_v2 | `audio.mouth_to_ear_p95_ms` | 83.7 | 98.9 | ±10 | ahead | 9/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 22.4 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.e2e_delay_p95_ms` | 1484 | 2624 | ±988 | ahead | 8/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.9 | 7.1 | ±3.3 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 44 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `audio.mouth_to_ear_p95_ms` | 105 | 221 | ±37.3 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_clean_link | `cc.queue_p95_ms` | 26.1 | 74.4 | ±25.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_clean_link | `video.e2e_delay_p95_ms` | 71.8 | 123 | ±19.8 | ahead | 10/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 192 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 468 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 69.2 | 101 | ±20.2 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.mouth_to_ear_p95_ms` | 145 | 168 | ±21.9 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `cc.utilization_pct` | 74.1 | 79.6 | ±4.0 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_random_loss | `video.avg_kbps` | 1286 | 1422 | ±71.1 | behind | 8/10 | #37, #61 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 320 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 64.5 | 415 | ±318 | ahead | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.keyframe_requests` | 2.2 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 84 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 49.6 | 101 | ±20.2 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.mouth_to_ear_p95_ms` | 119 | 168 | ±21.9 | ahead | 10/10 | #62, #63 |
| issue_18_random_loss_keyframes | `cc.queue_p95_ms` | 16.9 | 32.4 | ±10 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `cc.utilization_pct` | 64.8 | 79.6 | ±4.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1117 | 1422 | ±71.1 | behind | 10/10 | #37, #61 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 9/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.1 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 18.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 34.5 | 209 | ±110 | ahead | 7/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.4 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 75.2 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 194 | 212 | ±11.1 | ahead | 6/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 7.5 | 35.3 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 9.4 | 43.7 | ±2.5 | ahead | 10/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 81.9 | 554 | ±64.5 | behind | 10/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 94.0 | 73.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 62.7 | 47.5 | ±3.1 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 17.7 | 8.0 | ±1.0 | behind | 10/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 40.5 | 21.2 | ±3.2 | behind | 9/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 562 | 447 | ±28.1 | ahead | 10/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 874 | 2624 | ±988 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 56.4 | 1.4 | ±2.8 | behind | 10/10 | #23 |
| issue_21_shallow_drop_repair | `video.pause_count` | 0 | 0.8 | ±0.5 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 0 | 4831 | ±2576 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.6 | 23.8 | ±1.6 | ahead | 7/10 | #37, #24 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 358 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 385 | 279 | ±19.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `net.loss_pct` | 4.0 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_capacity_drop | `repair.overhead_pct` | 6.9 | 3.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_capacity_drop | `video.avg_kbps` | 837 | 890 | ±44.5 | behind | 5/10 | #37, #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 5206 | 2613 | ±382 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 3348 | 1810 | ±580 | behind | 10/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.render_fps` | 26.0 | 28.7 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 205 | 314 | ±23.7 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `cc.utilization_pct` | 73.4 | 103 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `net.loss_pct` | 17.9 | 6.2 | ±1.3 | behind | 10/10 | #21, #61, #12 |
| issue_23_handover | `repair.overhead_pct` | 55.0 | 33.1 | ±2.8 | behind | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.avg_kbps` | 894 | 1289 | ±64.4 | behind | 10/10 | #37, #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 278 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 2.2 | 4.1 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_handover | `video.longest_freeze_ms` | 3364 | 2025 | ±350 | behind | 10/10 | #5, #37, #24 |
| issue_23_handover | `video.render_fps` | 25.6 | 27.9 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 285 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 190 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.max_settle_ms` | 960 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 468 | 395 | ±68.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `net.loss_pct` | 2.4 | 0.4 | ±0.9 | behind | 9/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `repair.overhead_pct` | 8.6 | 1.6 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 909 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `video.freeze_total_ms` | 5909 | 1169 | ±2113 | behind | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 3824 | 519 | ±608 | behind | 10/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 24.2 | 26.2 | ±1.3 | behind | 6/10 | #37, #24 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 71.2 | 119 | ±19.1 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 135 | 182 | ±20.3 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 23.3 | 40.2 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `cc.utilization_pct` | 76.3 | 88.9 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `net.loss_pct` | 5.2 | 6.0 | ±0.5 | ahead | 8/10 | #21, #61, #12 |
| issue_23_outage_3s | `video.avg_kbps` | 934 | 1139 | ±56.9 | behind | 10/10 | #37, #61 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 91.6 | 113 | ±10 | ahead | 9/10 | #24, #37 |
| issue_23_outage_500ms | `audio.concealed_pct` | 2.7 | 2.0 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 269 | 151 | ±19.7 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 329 | 227 | ±33.5 | behind | 10/10 | #62, #63 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 29.5 | 64.0 | ±16.1 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `cc.utilization_pct` | 60.5 | 89.5 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `net.loss_pct` | 2.3 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_23_outage_500ms | `repair.overhead_pct` | 9.1 | 2.2 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_23_outage_500ms | `video.avg_kbps` | 988 | 1621 | ±81.1 | behind | 10/10 | #37, #61 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 389 | 180 | ±27.1 | behind | 8/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 2 | 1.3 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_total_ms` | 1925 | 655 | ±135 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 1321 | 577 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 81.6 | 128 | ±23.0 | ahead | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.longest_interruption_ms` | 48 | 27 | ±20 | behind | 5/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 203 | 229 | ±22.9 | ahead | 7/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.5 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2473 | 1949 | ±124 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1189 | 875 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 227 | 353 | ±29.5 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 439 | 778 | ±68.6 | ahead | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 163 | 395 | ±68.9 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 103 | 232 | ±72.2 | ahead | 10/10 | #61 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2247 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 28.0 | 26.2 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 8/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 89.6 | 128 | ±23.0 | ahead | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.longest_interruption_ms` | 64 | 27 | ±20 | behind | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.max_settle_ms` | 1500 | 2670 | ±839 | ahead | 8/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 77.6 | 126 | ±11.9 | ahead | 10/10 | #61 |
| issue_53_cwnd_pushback | `cc.utilization_pct` | 80.9 | 89.9 | ±4.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 8.4 | 5.7 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `repair.overhead_pct` | 14.9 | 10.6 | ±2 | behind | 9/10 | #21, #7, #29 |
| issue_53_cwnd_pushback | `video.avg_kbps` | 784 | 908 | ±45.4 | behind | 10/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 549 | 1949 | ±107 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 2.4 | 7.3 | ±2.1 | ahead | 9/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.keyframe_requests` | 17.5 | 0 | ±0.9 | behind | 10/10 | #23 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1613 | 875 | ±186 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 139 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.longest_interruption_ms` | 18 | 46 | ±28.0 | ahead | 4/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 302 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 128 | 251 | ±23.2 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `cc.utilization_pct` | 67.6 | 74.5 | ±3.7 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.avg_kbps` | 1584 | 1785 | ±89.3 | behind | 9/10 | #37, #61 |
| issue_5_adjuster_default | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_bitrate_adjuster | `audio.concealed_pct` | 0.9 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 74.4 | 116 | ±11.7 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 134 | 171 | ±12.4 | ahead | 9/10 | #62, #63 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 20.4 | 36.7 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.utilization_pct` | 56.6 | 85.7 | ±4.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `video.avg_kbps` | 727 | 1170 | ±58.5 | behind | 10/10 | #37, #61 |
| issue_5_bitrate_adjuster | `video.e2e_delay_p95_ms` | 173 | 140 | ±10 | behind | 9/10 | #24, #37 |
| issue_5_bitrate_adjuster | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.5 | 1.6 | ±0.6 | behind | 7/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.interruptions` | 1.3 | 0.6 | ±0.5 | behind | 5/10 | #62, #63 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 19.6 | 40.4 | ±10.6 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `cc.utilization_pct` | 58.9 | 84.1 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_burst15_freeze | `video.avg_kbps` | 997 | 1508 | ±75.4 | behind | 10/10 | #37, #61 |
| issue_5_burst15_freeze | `video.e2e_delay_p95_ms` | 179 | 263 | ±36.6 | ahead | 10/10 | #24, #37 |
| issue_5_burst15_freeze | `video.keyframe_requests` | 1.7 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 495 | ±33.8 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 116 | 144 | ±20.7 | ahead | 3/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 712 | 795 | ±39.8 | ahead | 7/10 | #62, #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 324 | 282 | ±17.8 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `net.loss_pct` | 2.1 | 1.5 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_5_keyframe_flush | `video.avg_kbps` | 827 | 883 | ±44.1 | behind | 6/10 | #37, #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2077 | 2643 | ±377 | ahead | 8/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.keyframe_requests` | 6 | 0 | ±0.5 | behind | 10/10 | #23 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 60 | 85 | ±10.8 | ahead | 9/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 101 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.1 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 25.8 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 8.3 | 35.3 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 13.4 | 43.7 | ±2.5 | ahead | 10/10 | #21, #7, #29 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 94.8 | 554 | ±64.5 | behind | 10/10 | #37, #61 |
| issue_62_neteq_v2_high_jitter | `video.e2e_delay_p95_ms` | 1532 | 150 | ±76.6 | behind | 10/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.render_fps` | 26.3 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 42.4 | 88 | ±24.9 | ahead | 10/10 | #62, #63 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 117 | 157 | ±29.0 | ahead | 8/10 | #62, #63 |
| issue_76_packetization_v2 | `cc.max_settle_ms` | 8890 | 1130 | ±2665 | behind | 8/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 10.8 | 25.6 | ±10 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 54.2 | 87.2 | ±8.8 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 22.5 | 32.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1030 | 1604 | ±156 | behind | 10/10 | #37, #61 |
| issue_76_packetization_v2 | `video.freeze_count` | 0 | 0.6 | ±0.5 | ahead | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.freeze_total_ms` | 0 | 136 | ±125 | ahead | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 9/10 | #23 |
| issue_76_packetization_v2 | `video.longest_freeze_ms` | 13.9 | 145 | ±115 | ahead | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 192 | 314 | ±23.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 451 | 525 | ±63.8 | ahead | 8/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.utilization_pct` | 78.5 | 103 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 961 | 1289 | ±64.4 | behind | 10/10 | #37, #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 795 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.7 | 0.9 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 484 | ±38.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 118 | 142 | ±20.4 | ahead | 4/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 629 | 765 | ±48.5 | ahead | 8/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `net.loss_pct` | 3.8 | 3.2 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 870 | 1221 | ±198 | ahead | 9/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.pause_total_ms` | 1799 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 44 | 112 | ±26.2 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 125 | 188 | ±27.9 | ahead | 10/10 | #62, #63 |
| issue_76_session_layer_v2 | `cc.max_settle_ms` | 30000 | 3160 | ±2997 | behind | 10/10 | #15, #48, #61, #20 |
| issue_76_session_layer_v2 | `cc.queue_p95_ms` | 4.6 | 24.0 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `cc.utilization_pct` | 24.3 | 82.5 | ±6.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 19.0 | 39.8 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 424 | 1429 | ±107 | behind | 10/10 | #37, #61 |
| issue_76_session_layer_v2 | `video.freeze_count` | 0 | 0.6 | ±0.5 | ahead | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.freeze_total_ms` | 0 | 166 | ±154 | ahead | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.longest_freeze_ms` | 12.2 | 173 | ±146 | ahead | 6/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 75.2 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 193 | 212 | ±11.1 | ahead | 7/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 11.2 | 35.3 | ±4.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 16.9 | 43.7 | ±2.5 | ahead | 10/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 148 | 554 | ±64.5 | behind | 10/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 737 | 150 | ±36.9 | behind | 8/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 1 | 0.3 | ±0.5 | behind | 7/10 | #23 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 49.6 | 101 | ±20.2 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.mouth_to_ear_p95_ms` | 119 | 168 | ±21.9 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `cc.queue_p95_ms` | 16.9 | 32.4 | ±10 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `cc.utilization_pct` | 64.8 | 79.6 | ±4.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1117 | 1422 | ±71.1 | behind | 10/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 9/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.1 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 18.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 34.5 | 209 | ±110 | ahead | 7/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 46.9 | 163 | ±24.5 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 117 | 262 | ±32.0 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 200 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 19.4 | 55.2 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `cc.utilization_pct` | 71.5 | 88.4 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `net.loss_pct` | 9.8 | 11.7 | ±0.6 | ahead | 10/10 | #21, #61, #12 |
| issue_85_video_pause | `video.avg_kbps` | 997 | 1297 | ±64.8 | behind | 10/10 | #37, #61 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 91.7 | 156 | ±17.1 | ahead | 10/10 | #24, #37 |
| issue_85_video_pause | `video.keyframe_requests` | 2 | 1 | ±0.5 | behind | 10/10 | #23 |
| issue_85_video_pause | `video.pause_total_ms` | 10246 | 12271 | ±614 | ahead | 10/10 | #5, #37, #24 |
| issue_85_video_pause | `video.render_fps` | 22.3 | 20.8 | ±1.1 | ahead | 10/10 | #37, #24 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 48 | 152 | ±33.3 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 111 | 224 | ±46.2 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `cc.queue_p95_ms` | 23.8 | 49.8 | ±15.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 75.6 | 119 | ±15.5 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `video.keyframe_requests` | 0.9 | 0 | ±0.5 | behind | 9/10 | #23 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 275 | 353 | ±29.5 | ahead | 9/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 190 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.max_settle_ms` | 1000 | 300 | ±500 | behind | 10/10 | #15, #48, #61, #20 |
| low_bandwidth | `cc.queue_p95_ms` | 472 | 395 | ±68.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `net.loss_pct` | 2.1 | 0.4 | ±0.9 | behind | 9/10 | #21, #61, #12 |
| low_bandwidth | `repair.overhead_pct` | 9.0 | 1.6 | ±2 | behind | 9/10 | #21, #7, #29 |
| low_bandwidth | `video.avg_kbps` | 50.3 | 70.5 | ±20 | behind | 5/10 | #37, #61 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 944 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| low_bandwidth | `video.keyframe_requests` | 15.3 | 0 | ±0.8 | behind | 10/10 | #23 |
| outage_3s | `audio.concealed_pct` | 11.1 | 10.2 | ±0.6 | behind | 10/10 | #62, #63, #20 |
| outage_3s | `audio.jb_delay_p95_ms` | 186 | 119 | ±19.1 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 250 | 182 | ±20.3 | behind | 9/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 24.0 | 40.2 | ±10 | ahead | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `cc.utilization_pct` | 69.0 | 88.9 | ±4.4 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `video.avg_kbps` | 826 | 1139 | ±56.9 | behind | 10/10 | #37, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 146 | 113 | ±10 | behind | 6/10 | #24, #37, #20 |
| outage_3s | `video.keyframe_requests` | 5.3 | 1 | ±0.5 | behind | 10/10 | #23, #20 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 119 | 140 | ±20 | ahead | 2/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 713 | 794 | ±39.7 | ahead | 7/10 | #62, #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 325 | 279 | ±16.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `net.loss_pct` | 2.2 | 1.5 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| pacer_v2_capacity_drop | `video.avg_kbps` | 833 | 890 | ±44.5 | behind | 6/10 | #37, #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2133 | 2613 | ±382 | ahead | 7/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.keyframe_requests` | 6.4 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 339 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 711 | 794 | ±39.7 | ahead | 9/10 | #62, #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 279 | ±17.1 | behind | 9/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 84.5 | 72.2 | ±10 | behind | 8/10 | #61 |
| probe_v2_capacity_drop | `net.loss_pct` | 3.3 | 1.5 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| probe_v2_capacity_drop | `repair.overhead_pct` | 6.0 | 3.2 | ±2 | behind | 3/10 | #21, #7, #29 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 255 | 387 | ±125 | ahead | 9/10 | #24, #37 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 3118 | 2613 | ±382 | behind | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.keyframe_requests` | 7.9 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `cc.rtt_p50_ms` | 81.9 | 66.2 | ±10 | behind | 9/10 | #61 |
| probe_v2_clean_link | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 10/10 | #23 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 192 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 468 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 49.6 | 101 | ±20.2 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.mouth_to_ear_p95_ms` | 119 | 168 | ±21.9 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `cc.queue_p95_ms` | 16.9 | 32.4 | ±10 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `cc.utilization_pct` | 64.8 | 79.6 | ±4.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| random_loss_5pct | `video.avg_kbps` | 1117 | 1422 | ±71.1 | behind | 10/10 | #37, #61 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 9/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.1 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 18.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| random_loss_5pct | `video.keyframe_requests` | 1.8 | 0.2 | ±0.5 | behind | 9/10 | #23 |
| random_loss_5pct | `video.longest_freeze_ms` | 34.5 | 209 | ±110 | ahead | 7/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 48 | 164 | ±57.6 | ahead | 10/10 | #62, #63 |
| satellite | `audio.mouth_to_ear_p95_ms` | 386 | 506 | ±64.0 | ahead | 10/10 | #62, #63 |
| satellite | `cc.queue_p95_ms` | 13.8 | 38.2 | ±11.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `cc.utilization_pct` | 58.6 | 82.6 | ±4.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| satellite | `repair.overhead_pct` | 15.5 | 18.3 | ±2 | ahead | 8/10 | #21, #7, #29 |
| satellite | `video.avg_kbps` | 655 | 965 | ±48.2 | behind | 10/10 | #37, #61 |
| satellite | `video.e2e_delay_p95_ms` | 829 | 927 | ±46.4 | ahead | 8/10 | #24, #37 |
| satellite | `video.freeze_count` | 22.4 | 30.6 | ±4.7 | ahead | 8/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6667 | 13227 | ±2008 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 10/10 | #23 |
| satellite | `video.render_fps` | 24.8 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.concealed_pct` | 12.2 | 0.5 | ±0.6 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.interruptions` | 0.2 | 1 | ±0.5 | ahead | 8/10 | #62, #63 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 69.6 | 186 | ±24.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 126 | 203 | ±20 | ahead | 9/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 407 | 499 | ±25.0 | ahead | 10/10 | #62, #63 |
| very_low_capacity | `cc.max_settle_ms` | 1420 | 40000 | ±2000 | ahead | 10/10 | #15, #48, #61, #20 |
| very_low_capacity | `net.loss_pct` | 34.2 | 18.0 | ±3.8 | behind | 10/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 29.3 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 2274 | 7658 | ±1520 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 10.4 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.freeze_total_ms` | 17081 | 25415 | ±5970 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 219 | 3.2 | ±10.9 | behind | 10/10 | #23 |
| very_low_capacity | `video.render_fps` | 1.1 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.concealed_pct` | 1.1 | 0.5 | ±0.5 | behind | 6/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 0 | 1 | ±0.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 72 | 186 | ±24.5 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 62 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 407 | 499 | ±25.0 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 26.9 | 18.0 | ±3.8 | behind | 10/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 81.4 | 55.6 | ±11.1 | behind | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 2235 | 7658 | ±1520 | ahead | 10/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 9.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_total_ms` | 18441 | 25415 | ±5970 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 126 | 3.2 | ±6.3 | behind | 10/10 | #23 |
| very_low_capacity_allocator_v2 | `video.longest_freeze_ms` | 3522 | 4209 | ±557 | ahead | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 202 | 314 | ±23.7 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.utilization_pct` | 69.8 | 103 | ±5.2 | behind | 10/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `net.loss_pct` | 19.2 | 6.2 | ±1.3 | behind | 10/10 | #21, #61, #12, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 57.9 | 33.1 | ±2.9 | behind | 10/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 850 | 1289 | ±64.4 | behind | 10/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 210 | 2259 | ±508 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.5 | 4.1 | ±1.3 | ahead | 7/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.keyframe_requests` | 14.5 | 0 | ±0.7 | behind | 10/10 | #23, #20 |

