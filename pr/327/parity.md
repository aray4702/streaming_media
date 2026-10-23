## Parity with libwebrtc M157.8103@{#0}

101 scenarios, mean of 10 seeds. **188 gaps**: 89 behind libwebrtc, 99 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

152 gaps on `main`, 188 here: **0 closed, 36 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_326_congestive_sync_seed2 | `audio.mouth_to_ear_p95_ms` | 377 | 304 | ±18.8 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed2 | `audio.jb_delay_p95_ms` | 290 | 220 | ±14.5 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed2 | `video.freeze_count` | 4 | 5 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed2 | `video.freeze_total_ms` | 1531 | 1711 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed2 | `video.longest_freeze_ms` | 794 | 446 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed2 | `video.e2e_delay_p95_ms` | 559 | 490 | ±27.9 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed2 | `sync.av_offset_p95_ms` | 346 | 299 | ±17.3 | behind | 1/1 | #77 |
| issue_326_congestive_sync_seed2 | `cc.rtt_p50_ms` | 104 | 83 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed2 | `net.loss_pct` | 5.2 | 3.9 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_326_congestive_sync_seed2 | `repair.overhead_pct` | 9.7 | 7.4 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_326_congestive_sync_seed3 | `audio.mouth_to_ear_p95_ms` | 407 | 240 | ±20.4 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed3 | `audio.jb_delay_p95_ms` | 320 | 150 | ±16 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed3 | `video.freeze_count` | 6 | 4 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed3 | `video.freeze_total_ms` | 1959 | 1529 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed3 | `video.e2e_delay_p95_ms` | 651 | 390 | ±32.6 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed3 | `sync.av_offset_p95_ms` | 427 | 264 | ±21.4 | behind | 1/1 | #77 |
| issue_326_congestive_sync_seed3 | `cc.max_settle_ms` | 1400 | 3200 | ±500 | ahead | 1/1 | #15, #48, #61, #20 |
| issue_326_congestive_sync_seed3 | `cc.rtt_p50_ms` | 103 | 87.5 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed4 | `audio.mouth_to_ear_p95_ms` | 361 | 306 | ±18.0 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed4 | `audio.jb_delay_p95_ms` | 280 | 220 | ±14 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed4 | `video.freeze_count` | 3 | 4 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed4 | `video.freeze_total_ms` | 1135 | 1711 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed4 | `video.longest_freeze_ms` | 606 | 778 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed4 | `video.e2e_delay_p95_ms` | 536 | 486 | ±26.8 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed4 | `cc.rtt_p50_ms` | 103 | 83 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed4 | `repair.overhead_pct` | 6.4 | 9.0 | ±2 | ahead | 1/1 | #21, #7, #29 |
| issue_326_congestive_sync_seed5 | `audio.mouth_to_ear_p95_ms` | 315 | 232 | ±15.7 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed5 | `audio.jb_delay_p95_ms` | 230 | 150 | ±11.5 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed5 | `video.freeze_count` | 3 | 2 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed5 | `video.freeze_total_ms` | 1072 | 756 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed5 | `video.e2e_delay_p95_ms` | 332 | 285 | ±16.6 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed5 | `video.avg_kbps` | 1153 | 1084 | ±57.6 | ahead | 1/1 | #37, #61 |
| issue_326_congestive_sync_seed5 | `sync.av_offset_p95_ms` | 195 | 161 | ±10 | behind | 1/1 | #77 |
| issue_326_congestive_sync_seed5 | `cc.utilization_pct` | 89.3 | 84.4 | ±4.5 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_326_congestive_sync_seed5 | `cc.rtt_p50_ms` | 97.1 | 79 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed5 | `net.loss_pct` | 4.0 | 3.2 | ±0.5 | behind | 1/1 | #21, #61, #12 |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| capacity_drop | `cc.rtt_p50_ms` | 352 | 72.2 | ±17.6 | behind | 10/10 | #61 |
| cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1242 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.8 | 33.1 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 352 | 72.2 | ±17.6 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.6 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.7 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.4 | 84.4 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 48.0 | 73.4 | ±23.1 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2680 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2745 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 135 | 149 | ±10 | ahead | 6/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 226 | ±11.3 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.2 | 8.9 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 670 | 1740 | ±635 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 113 | 130 | ±13.0 | ahead | 5/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 209 | ±13.5 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.6 | 3.6 | ±2.4 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 139 | 742 | ±482 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 134 | 238 | ±100 | ahead | 5/10 | #5, #37, #24 |
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
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 40.3 | 43.7 | ±3.2 | ahead | 23/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 41.2 | 44.9 | ±3.3 | ahead | 4/10 | #21, #7, #29 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 333 | 377 | ±21.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 588 | 661 | ±39.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 221 | 248 | ±22.5 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_204_cross_traffic_join | `cc.rtt_p50_ms` | 353 | 105 | ±21.5 | behind | 10/10 | #61 |
| issue_224_congestive_e2e_flip | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 352 | 72.2 | ±17.6 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1242 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 28.8 | 33.1 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 151 | ±19.7 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 191 | 247 | ±33.5 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 95.2 | ±18.4 | ahead | 7/10 | #77 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 4/10 | #61 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 4/10 | #61 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 315 | 119 | ±19.7 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 58 | 21 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 631 | 201 | ±31.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 339 | 65.3 | ±16.9 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 12.6 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.6 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2505 | 72.9 | ±125 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1523 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 3030 | 113 | ±151 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.4 | 0 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2014 | 0 | ±101 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.1 | 0 | ±0.5 | behind | 4/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1126 | 26.0 | ±100 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.8 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1877 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_326_congestive_sync_seed2 | `audio.jb_delay_p95_ms` | 290 | 220 | ±14.5 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed2 | `audio.mouth_to_ear_p95_ms` | 377 | 304 | ±18.8 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed2 | `cc.rtt_p50_ms` | 104 | 83 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed2 | `net.loss_pct` | 5.2 | 3.9 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_326_congestive_sync_seed2 | `repair.overhead_pct` | 9.7 | 7.4 | ±2 | behind | 1/1 | #21, #7, #29 |
| issue_326_congestive_sync_seed2 | `sync.av_offset_p95_ms` | 346 | 299 | ±17.3 | behind | 1/1 | #77 |
| issue_326_congestive_sync_seed2 | `video.e2e_delay_p95_ms` | 559 | 490 | ±27.9 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed2 | `video.freeze_count` | 4 | 5 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed2 | `video.freeze_total_ms` | 1531 | 1711 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed2 | `video.longest_freeze_ms` | 794 | 446 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed3 | `audio.jb_delay_p95_ms` | 320 | 150 | ±16 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed3 | `audio.mouth_to_ear_p95_ms` | 407 | 240 | ±20.4 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed3 | `cc.max_settle_ms` | 1400 | 3200 | ±500 | ahead | 1/1 | #15, #48, #61, #20 |
| issue_326_congestive_sync_seed3 | `cc.rtt_p50_ms` | 103 | 87.5 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed3 | `sync.av_offset_p95_ms` | 427 | 264 | ±21.4 | behind | 1/1 | #77 |
| issue_326_congestive_sync_seed3 | `video.e2e_delay_p95_ms` | 651 | 390 | ±32.6 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed3 | `video.freeze_count` | 6 | 4 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed3 | `video.freeze_total_ms` | 1959 | 1529 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed4 | `audio.jb_delay_p95_ms` | 280 | 220 | ±14 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed4 | `audio.mouth_to_ear_p95_ms` | 361 | 306 | ±18.0 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed4 | `cc.rtt_p50_ms` | 103 | 83 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed4 | `repair.overhead_pct` | 6.4 | 9.0 | ±2 | ahead | 1/1 | #21, #7, #29 |
| issue_326_congestive_sync_seed4 | `video.e2e_delay_p95_ms` | 536 | 486 | ±26.8 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed4 | `video.freeze_count` | 3 | 4 | ±0.5 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed4 | `video.freeze_total_ms` | 1135 | 1711 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed4 | `video.longest_freeze_ms` | 606 | 778 | ±100 | ahead | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed5 | `audio.jb_delay_p95_ms` | 230 | 150 | ±11.5 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed5 | `audio.mouth_to_ear_p95_ms` | 315 | 232 | ±15.7 | behind | 1/1 | #63 |
| issue_326_congestive_sync_seed5 | `cc.rtt_p50_ms` | 97.1 | 79 | ±10 | behind | 1/1 | #61 |
| issue_326_congestive_sync_seed5 | `cc.utilization_pct` | 89.3 | 84.4 | ±4.5 | ahead | 1/1 | #19, #15, #48, #12, #61 |
| issue_326_congestive_sync_seed5 | `net.loss_pct` | 4.0 | 3.2 | ±0.5 | behind | 1/1 | #21, #61, #12 |
| issue_326_congestive_sync_seed5 | `sync.av_offset_p95_ms` | 195 | 161 | ±10 | behind | 1/1 | #77 |
| issue_326_congestive_sync_seed5 | `video.avg_kbps` | 1153 | 1084 | ±57.6 | ahead | 1/1 | #37, #61 |
| issue_326_congestive_sync_seed5 | `video.e2e_delay_p95_ms` | 332 | 285 | ±16.6 | behind | 1/1 | #24, #37 |
| issue_326_congestive_sync_seed5 | `video.freeze_count` | 3 | 2 | ±0.5 | behind | 1/1 | #5, #37, #24 |
| issue_326_congestive_sync_seed5 | `video.freeze_total_ms` | 1072 | 756 | ±100 | behind | 1/1 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 4/10 | #61 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.3 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 360 | 72.5 | ±18.0 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.1 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1242 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.8 | 33.1 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1610 | 1268 | ±223 | behind | 6/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.4 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 225 | 282 | ±32.0 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 92.6 | 131 | ±22.5 | ahead | 10/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 131 | 156 | ±17.1 | ahead | 8/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 352 | 72.2 | ±17.6 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 352 | 72.2 | ±17.6 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `repair.overhead_pct` | 35.6 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity | `video.render_fps` | 10.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.6 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1242 | 1422 | ±165 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.8 | 33.1 | ±2 | ahead | 8/10 | #21, #7, #29, #20 |

