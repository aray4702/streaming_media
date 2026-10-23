## Parity with libwebrtc M157.8103@{#0}

94 scenarios, mean of 10 seeds. **182 gaps**: 94 behind libwebrtc, 88 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

176 gaps on `main`, 182 here: **28 closed, 34 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `video.freeze_total_ms` | 1802 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1802 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.4 | 8.9 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 713 | 1740 | ±635 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 59.4 | 56.3 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.1 | 12.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.2 | 5.8 | ±2.6 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.6 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.2 | 5.8 | ±2.6 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.6 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 1802 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_count` | 1.2 | 1.8 | ±0.5 | ahead | 6/10 | #5, #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2136 | 2274 | ±114 | ahead | 6/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.longest_freeze_ms` | 3486 | 3283 | ±174 | behind | 1/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 1788 | 2524 | ±299 | ahead | 10/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1015 | 1221 | ±198 | ahead | 8/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 534 | 737 | ±198 | ahead | 9/10 | #77 |
| lossy_feedback | `sync.av_offset_p95_ms` | 79.1 | 110 | ±27.3 | ahead | 8/10 | #77 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 1802 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 1802 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9711 | 7658 | ±1520 | behind | 9/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 9259 | 7211 | ±1537 | behind | 9/10 | #77 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1764 | 1301 | ±388 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2611 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2546 | 3204 | ±494 | ahead | 6/10 | #77 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 46.5 | 73.4 | ±23.1 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 8.2 | 3.6 | ±2.4 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 1653 | 742 | ±482 | behind | 7/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 3140 | 2291 | ±544 | behind | 9/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_default | `net.loss_pct` | 6.3 | 5.7 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 3140 | 2291 | ±544 | behind | 9/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_render_fps | `net.loss_pct` | 6.3 | 5.7 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 3140 | 2291 | ±544 | behind | 9/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 6.3 | 5.7 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.3 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1764 | 1301 | ±388 | behind | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 171 | 142 | ±20.4 | behind | 4/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1536 | 1268 | ±223 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9711 | 7658 | ±1520 | behind | 9/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 9259 | 7211 | ±1537 | behind | 9/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9711 | 7658 | ±1520 | behind | 9/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 9259 | 7211 | ±1537 | behind | 9/10 | #77 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `cc.rtt_p50_ms` | 227 | 281 (further) |
| cross_traffic | `cc.queue_p95_ms` | 25.2 | 30.3 (further) |
| high_jitter | `repair.overhead_pct` | 9.5 | 7.3 (closer) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 5.7 | 5.0 (closer) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 227 | 281 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.7 | 3.5 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 19.6 | 17.5 (closer) |
| issue_12_lossbwe_congestive | `video.freeze_count` | 1 | 1.8 (further) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 277 | 358 (further) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 281 | 364 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 9.5 | 7.3 (closer) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 18.0 | 36.2 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 21.5 | 39.5 (further) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 1.3 | 1.5 (further) |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1011 | 1514 (further) |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 32.6 | 107 (further) |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 19.7 | 83.9 (further) |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1011 | 1514 (further) |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 32.6 | 107 (further) |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 19.7 | 83.9 (further) |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 5.7 | 4.9 (closer) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 9.2 | 7.1 (closer) |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 83.2 | 73.0 (closer) |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 51.4 | 43.4 (closer) |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 25.2 | 30.3 (further) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 9.5 | 7.3 (closer) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 227 | 281 (further) |
| issue_23_handover | `repair.overhead_pct` | 5.7 | 5.0 (closer) |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 23.5 | 26.5 (further) |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 23 | 26 (further) |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 216 | 377 (further) |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 439 | 604 (further) |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 441 | 610 (further) |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 9.5 | 7.3 (closer) |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 23.5 | 26.5 (further) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 23 | 26 (further) |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 216 | 377 (further) |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 439 | 604 (further) |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 441 | 610 (further) |
| issue_257_zero_max_payload | `video.freeze_count` | 3 | 1.7 (closer) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3591 | 2137 (closer) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 2424 | 1708 (closer) |
| issue_257_zero_max_payload | `video.render_fps` | 9.8 | 8.4 (closer) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2513 | 2925 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 5.5 | 2.8 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2052 | 2446 (further) |
| issue_257_zero_max_payload | `net.loss_pct` | 9.6 | 12.6 (further) |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.5 | 14.7 (further) |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1011 | 1514 (further) |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 32.6 | 107 (further) |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1769 | 2166 (further) |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 23.5 | 26.5 (further) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 23 | 26 (further) |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 216 | 377 (further) |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 439 | 604 (further) |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 441 | 610 (further) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 25.2 | 30.3 (further) |
| issue_5_keyframe_flush | `video.freeze_count` | 0.9 | 0.7 (closer) |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 223 | 287 (further) |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 1 | 1.8 (further) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 277 | 358 (further) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 281 | 364 (further) |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 25.2 | 30.3 (further) |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 9.5 | 7.3 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 3.1 | 3.7 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 5.7 | 5.0 (closer) |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 3.2 | 3.6 (further) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 9.5 | 7.3 (closer) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 227 | 281 (further) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 227 | 281 (further) |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 25.2 | 30.3 (further) |
| very_low_capacity | `video.render_fps` | 2.7 | 3.5 (further) |
| very_low_capacity | `repair.overhead_pct` | 19.6 | 17.5 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.7 | 3.5 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 19.6 | 17.5 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 5.7 | 5.0 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| capacity_drop | `cc.rtt_p50_ms` | 353 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| high_jitter | `repair.overhead_pct` | 36.4 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1243 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.1 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 353 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 38.1 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 9259 | 7211 | ±1537 | behind | 9/10 | #77 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9711 | 7658 | ±1520 | behind | 9/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.7 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.8 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 103 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.1 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 672 | 307 | ±100 | behind | 10/10 | #77 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 778 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 5.3 | 3.5 | ±1.0 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1764 | 1301 | ±388 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 36.4 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.6 | 84.4 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 46.5 | 73.4 | ±23.1 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2546 | 3204 | ±494 | ahead | 6/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2611 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 6.9 | 6.3 | ±0.5 | behind | 4/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.2 | 6.7 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 169 | 130 | ±13.0 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 245 | 209 | ±13.5 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 8.2 | 3.6 | ±2.4 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 1653 | 742 | ±482 | behind | 7/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 199 | 92.3 | ±10 | behind | 6/10 | #61 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 178 | 94.2 | ±10 | behind | 7/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 199 | 92.3 | ±10 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 178 | 94.2 | ±10 | behind | 7/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.8 | 43.7 | ±3.2 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.7 | 43.6 | ±3.8 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 37.8 | 44.9 | ±3.3 | ahead | 8/10 | #21, #7, #29 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 333 | 377 | ±21.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 588 | 661 | ±39.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 221 | 248 | ±22.5 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 9.2 | 8.0 | ±1.0 | behind | 6/10 | #21, #61, #12 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 36.4 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 9.2 | 8.0 | ±1.0 | behind | 6/10 | #21, #61, #12 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 353 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1243 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 28.1 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 7/10 | #63, #224 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 319 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 117 | 151 | ±19.7 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 192 | 247 | ±33.5 | ahead | 9/10 | #63, #218 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 102 | 128 | ±23.0 | ahead | 6/10 | #63, #224 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 223 | 249 | ±22.9 | ahead | 7/10 | #63, #224 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_default | `net.loss_pct` | 6.3 | 5.7 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 2368 | 1759 | ±118 | behind | 10/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 2553 | 1949 | ±128 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 3140 | 2291 | ±544 | behind | 9/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1252 | 876 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 36.4 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 102 | 128 | ±23.0 | ahead | 6/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 223 | 249 | ±22.9 | ahead | 7/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_render_fps | `net.loss_pct` | 6.3 | 5.7 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2368 | 1759 | ±118 | behind | 10/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2553 | 1949 | ±128 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 3140 | 2291 | ±544 | behind | 9/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1252 | 876 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 348 | 119 | ±19.7 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 58 | 21 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 664 | 201 | ±33.2 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 340 | 65.3 | ±17.0 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 12.6 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.8 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2519 | 72.9 | ±126 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1501 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 3038 | 113 | ±152 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 1.7 | 0 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2137 | 0 | ±107 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 2.8 | 0 | ±0.5 | behind | 5/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1734 | 26.0 | ±100 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.5 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 199 | 92.3 | ±10 | behind | 6/10 | #61 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 33.9 | 38.9 | ±3.3 | ahead | 9/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 2166 | 0 | ±108 | behind | 3/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 7/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 319 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 102 | 128 | ±23.0 | ahead | 6/10 | #63, #224 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 223 | 249 | ±22.9 | ahead | 7/10 | #63, #224 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 6.3 | 5.7 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 2368 | 1759 | ±118 | behind | 10/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 2553 | 1949 | ±128 | behind | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 3140 | 2291 | ±544 | behind | 9/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1252 | 876 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.3 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 359 | 72.5 | ±18.0 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `video.freeze_count` | 1.6 | 2.3 | ±0.7 | ahead | 6/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 103 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.1 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 672 | 307 | ±100 | behind | 10/10 | #77 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 778 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 5.3 | 3.5 | ±1.0 | behind | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1764 | 1301 | ±388 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 36.4 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 28.4 | 32.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1243 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.1 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 171 | 142 | ±20.4 | behind | 4/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1536 | 1268 | ±223 | behind | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.2 | 39.8 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 36.4 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.4 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 129 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 227 | 282 | ±32.0 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 95.2 | 131 | ±22.5 | ahead | 10/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 132 | 156 | ±17.1 | ahead | 8/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 7/10 | #63, #224 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 319 | 232 | ±72.2 | behind | 7/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 353 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 353 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 593 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 221 | 251 | ±23.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| satellite | `repair.overhead_pct` | 14.6 | 18.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `repair.overhead_pct` | 38.1 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `sync.av_offset_p95_ms` | 9259 | 7211 | ±1537 | behind | 9/10 | #77 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9711 | 7658 | ±1520 | behind | 9/10 | #24, #37 |
| very_low_capacity | `video.render_fps` | 10.7 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 38.1 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 9259 | 7211 | ±1537 | behind | 9/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9711 | 7658 | ±1520 | behind | 9/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.7 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1243 | 1422 | ±165 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.9 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.1 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |

