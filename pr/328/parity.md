## Parity with libwebrtc M157.8103@{#0}

97 scenarios, mean of 10 seeds. **153 gaps**: 69 behind libwebrtc, 84 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

152 gaps on `main`, 153 here: **21 closed, 22 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 588 | 661 | ±39.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 333 | 377 | ±21.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 221 | 248 | ±22.5 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37 |
| issue_120_rtx_payload_padding | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 159 | 171 | ±11.6 | ahead | 5/10 | #63 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 159 | 171 | ±11.6 | ahead | 5/10 | #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37 |
| issue_23_handover | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 47.3 | 64.0 | ±16.1 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 159 | 171 | ±11.6 | ahead | 5/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37 |
| issue_76_rtx_v2_handover | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37, #20 |
| wifi_to_lte_handover | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `cc.rtt_p50_ms` | 279 | 317 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 279 | 317 (further) |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 1250 | 1090 (closer) |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 1176 | 1016 (closer) |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1513 | 1010 (closer) |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 136 | 101 (closer) |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 78.4 | 135 (further) |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 1250 | 1090 (closer) |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 1176 | 1016 (closer) |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1513 | 1010 (closer) |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 136 | 101 (closer) |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 78.4 | 135 (further) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 279 | 317 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1100 | 1258 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.1 | 3.7 (further) |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 1250 | 1090 (closer) |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 1176 | 1016 (closer) |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1513 | 1010 (closer) |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 136 | 101 (closer) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 279 | 317 (further) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 279 | 317 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `cc.rtt_p50_ms` | 390 | 72.2 | ±19.5 | behind | 10/10 | #61 |
| cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.8 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 390 | 72.2 | ±19.5 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.6 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.7 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 159 | 171 | ±11.6 | ahead | 5/10 | #63 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 100 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.4 | 84.4 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.8 | 73.4 | ±23.1 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2693 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2758 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 135 | 149 | ±10 | ahead | 6/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 226 | ±11.3 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.2 | 8.9 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 670 | 1740 | ±635 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 113 | 130 | ±13.0 | ahead | 5/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 209 | ±13.5 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.6 | 3.6 | ±2.4 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 139 | 742 | ±482 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 134 | 238 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 193 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2947 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2948 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 230 | 94.2 | ±11.5 | behind | 8/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 193 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2947 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2948 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 230 | 94.2 | ±11.5 | behind | 8/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 40.3 | 43.7 | ±3.2 | ahead | 23/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 41.2 | 44.9 | ±3.3 | ahead | 4/10 | #21, #7, #29 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_204_cross_traffic_join | `cc.rtt_p50_ms` | 365 | 105 | ±21.5 | behind | 10/10 | #61 |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 159 | 171 | ±11.6 | ahead | 5/10 | #63 |
| issue_224_congestive_e2e_flip | `cc.rtt_p50_ms` | 100 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 390 | 72.2 | ±19.5 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.8 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 29.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_handover | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 325 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 151 | ±19.7 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 189 | 247 | ±33.5 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 47.3 | 64.0 | ±16.1 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 95.2 | ±18.4 | ahead | 7/10 | #77 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 4/10 | #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.2 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 4/10 | #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.2 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 316 | 119 | ±19.7 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 58 | 21 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 631 | 201 | ±31.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 340 | 65.3 | ±17.0 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 12.7 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.7 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2451 | 72.9 | ±123 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1526 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2986 | 113 | ±149 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.4 | 0 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2002 | 0 | ±100 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.7 | 0 | ±0.5 | behind | 5/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1284 | 26.0 | ±100 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.8 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 193 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2947 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2948 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1877 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 325 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 4/10 | #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.2 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.3 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 387 | 72.5 | ±19.3 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 159 | 171 | ±11.6 | ahead | 5/10 | #63 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 100 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.1 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.8 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1614 | 1268 | ±223 | behind | 7/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.8 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 225 | 282 | ±32.0 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 92.8 | 131 | ±22.5 | ahead | 10/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 131 | 156 | ±17.1 | ahead | 8/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 325 | 232 | ±72.2 | behind | 8/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 390 | 72.2 | ±19.5 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 296 | 279 | ±16.3 | behind | 6/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 390 | 72.2 | ±19.5 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 88.1 | 64.5 | ±10 | behind | 1/10 | #61 |
| very_low_capacity | `repair.overhead_pct` | 35.6 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity | `video.render_fps` | 10.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.6 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.0 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.8 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `sync.av_offset_p95_ms` | 1357 | 1832 | ±467 | ahead | 6/10 | #77, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 1728 | 2259 | ±508 | ahead | 6/10 | #24, #37, #20 |

