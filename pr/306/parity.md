## Parity with libwebrtc M157.8103@{#0}

97 scenarios, mean of 10 seeds. **157 gaps**: 66 behind libwebrtc, 91 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

152 gaps on `main`, 157 here: **13 closed, 18 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2680 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 41.2 | 44.9 | ±3.3 | ahead | 4/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1877 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.1 | ±2 | ahead | 5/10 | #21, #7, #29 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 319 | 278 | ±34.3 | behind | 8/10 | #63, #224 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 234 | 192 | ±33.9 | behind | 8/10 | #63, #224 |
| issue_204_cross_traffic_join | `video.avg_kbps` | 1293 | 1393 | ±73.5 | behind | 6/10 | #37, #61 |
| issue_204_cross_traffic_join | `cc.utilization_pct` | 54.9 | 58.8 | ±3.0 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 319 | 278 | ±34.3 | behind | 8/10 | #63 |
| issue_224_congestive_e2e_flip | `audio.jb_delay_p95_ms` | 234 | 192 | ±33.9 | behind | 8/10 | #63 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1723 | 1401 | ±199 | behind | 8/10 | #77 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1131 | 1073 | ±56.5 | ahead | 4/10 | #37, #61 |
| issue_29_ulpfec_over_red | `sync.av_offset_p95_ms` | 96.5 | 120 | ±21.0 | ahead | 8/10 | #77 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 33.7 | 39.8 | ±2.6 | ahead | 7/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1723 | 1401 | ±199 | behind | 8/10 | #77 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 319 | 278 | ±34.3 | behind | 8/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 234 | 192 | ±33.9 | behind | 8/10 | #63, #224 |
| issue_85_video_pause | `cc.queue_p95_ms` | 45.5 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1723 | 1401 | ±199 | behind | 8/10 | #77 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 4.2 | 3.0 (closer) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.8 | 3.8 (further) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 14.0 | 299 (further) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 14 | 299 (further) |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 13.8 | 15.4 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 16.8 | 28.3 (further) |
| issue_23_handover | `repair.overhead_pct` | 4.2 | 3.0 (closer) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 37 | 25 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 430 | 804 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 196 | 570 (further) |
| issue_257_zero_max_payload | `video.freeze_count` | 2.4 | 3 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2014 | 2622 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1100 | 1719 (further) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2917 | 2601 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.1 | 4.2 (further) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2432 | 1922 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 12.6 | 10.6 (closer) |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.5 | 12.3 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 4.2 | 3.0 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 342 | 459 (further) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 24.3 | 31.9 (further) |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 38.2 | 28.0 (closer) |
| very_low_capacity | `video.render_fps` | 2.8 | 3.8 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.8 | 3.8 (further) |
| wifi_to_lte_handover | `repair.overhead_pct` | 4.2 | 3.0 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| burst_loss | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| capacity_drop | `cc.rtt_p50_ms` | 356 | 72.2 | ±17.8 | behind | 10/10 | #61 |
| cross_traffic | `audio.jb_delay_p95_ms` | 339 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 594 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1236 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.2 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 30.1 | 33.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 356 | 72.2 | ±17.8 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 36.1 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.1 | 37.7 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 234 | 192 | ±33.9 | behind | 8/10 | #63, #224 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 319 | 278 | ±34.3 | behind | 8/10 | #63, #224 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 101 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 548 | 847 | ±208 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 589 | 888 | ±208 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 48.0 | 73.4 | ±23.1 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2745 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 111 | 127 | ±14.4 | ahead | 4/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 226 | ±11.3 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.2 | 8.9 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 670 | 1740 | ±635 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 84.8 | 113 | ±10 | ahead | 9/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 209 | ±13.5 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.6 | 3.6 | ±2.4 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 139 | 742 | ±482 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 132 | 238 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 173 | 94.2 | ±10 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 173 | 94.2 | ±10 | behind | 6/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 40.1 | 43.7 | ±3.2 | ahead | 24/40 | #21, #7, #29 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 335 | 377 | ±21.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 591 | 661 | ±39.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 222 | 248 | ±22.5 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 339 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 594 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_204_cross_traffic_join | `cc.rtt_p50_ms` | 354 | 105 | ±21.5 | behind | 10/10 | #61 |
| issue_204_cross_traffic_join | `cc.utilization_pct` | 54.9 | 58.8 | ±3.0 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `video.avg_kbps` | 1293 | 1393 | ±73.5 | behind | 6/10 | #37, #61 |
| issue_224_congestive_e2e_flip | `audio.jb_delay_p95_ms` | 234 | 192 | ±33.9 | behind | 8/10 | #63 |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 319 | 278 | ±34.3 | behind | 8/10 | #63 |
| issue_224_congestive_e2e_flip | `cc.rtt_p50_ms` | 101 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 356 | 72.2 | ±17.8 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1236 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.2 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 30.1 | 33.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1723 | 1401 | ±199 | behind | 8/10 | #77 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 151 | ±19.7 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 190 | 247 | ±33.5 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 94.0 | ±17.0 | ahead | 7/10 | #77 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 3/10 | #61 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 3/10 | #61 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 689 | 119 | ±34.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 46 | 21 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1005 | 201 | ±50.2 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 339 | 65.3 | ±17.0 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 10.6 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 12.4 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1992 | 69.5 | ±99.6 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1526 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2722 | 121 | ±136 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 3 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2622 | 0 | ±131 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.2 | 0 | ±0.5 | behind | 6/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1738 | 18.9 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.4 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 33.7 | 39.8 | ±2.6 | ahead | 7/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `sync.av_offset_p95_ms` | 96.5 | 120 | ±21.0 | ahead | 8/10 | #77 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1131 | 1073 | ±56.5 | ahead | 4/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1723 | 1401 | ±199 | behind | 8/10 | #77 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 3/10 | #61 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 339 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 594 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.2 | 36.7 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 362 | 72.5 | ±18.1 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 234 | 192 | ±33.9 | behind | 8/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 319 | 278 | ±34.3 | behind | 8/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 101 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 339 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 594 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1236 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.2 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 30.1 | 33.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1727 | 1268 | ±223 | behind | 7/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.6 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 163 | ±26.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 225 | 283 | ±33.5 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 45.5 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.1 | 118 | ±17.0 | ahead | 9/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 135 | 167 | ±20.6 | ahead | 9/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 8/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1723 | 1401 | ±199 | behind | 8/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 356 | 72.2 | ±17.8 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 356 | 72.2 | ±17.8 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 339 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 594 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `repair.overhead_pct` | 36.1 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 11.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 36.1 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1236 | 1422 | ±165 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.2 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 30.1 | 33.1 | ±2 | ahead | 7/10 | #21, #7, #29, #20 |

