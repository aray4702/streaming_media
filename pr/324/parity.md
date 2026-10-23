## Parity with libwebrtc M157.8103@{#0}

95 scenarios, mean of 10 seeds. **244 gaps**: 103 behind libwebrtc, 141 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

176 gaps on `main`, 244 here: **52 closed, 120 opened**, 24 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 4.5 | 3.5 | ±1.0 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.1 | 12.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.2 | 5.8 | ±2.6 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 9.1 | 8.0 | ±1.0 | behind | 7/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.2 | 5.8 | ±2.6 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 9.1 | 8.0 | ±1.0 | behind | 7/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 226 | 249 | ±22.9 | ahead | 8/10 | #63, #224 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 105 | 128 | ±23.0 | ahead | 6/10 | #63, #224 |
| issue_24_timing_v2_default | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 226 | 249 | ±22.9 | ahead | 8/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 105 | 128 | ±23.0 | ahead | 6/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.6 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_283_longest_freeze_no_metrics | `video.freeze_count` | 1.2 | 1.8 | ±0.5 | ahead | 6/10 | #5, #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2136 | 2274 | ±114 | ahead | 6/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.longest_freeze_ms` | 3486 | 3283 | ±174 | behind | 1/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1769 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 33.7 | 38.9 | ±3.3 | ahead | 8/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 226 | 249 | ±22.9 | ahead | 8/10 | #63, #224 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 105 | 128 | ±23.0 | ahead | 6/10 | #63, #224 |
| issue_53_cwnd_pushback | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_5_keyframe_flush | `video.freeze_count` | 1.4 | 2.3 | ±0.7 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 4.5 | 3.5 | ±1.0 | behind | 7/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 229 | 282 | ±32.0 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 131 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 133 | 156 | ±17.1 | ahead | 9/10 | #24, #37 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 97.5 | 131 | ±22.5 | ahead | 10/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| very_low_capacity | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `video.avg_kbps` | 1496 | 1410 | ±74.8 | ahead | 6/10 | #37, #61 |
| capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61, #19, #24 |
| high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61 |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| issue_124_burst_loss_v2 | `video.avg_kbps` | 1496 | 1410 | ±74.8 | ahead | 6/10 | #37, #61 |
| issue_127_all_v2_no_stall | `video.avg_kbps` | 627 | 593 | ±31.3 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1496 | 1410 | ±74.8 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1182 | 1108 | ±59.1 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 91.5 | 86.6 | ±4.6 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.9 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 2 | 0.9 | ±0.7 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 11775 | 5460 | ±4644 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 136 | 245 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 779 | 732 | ±38.9 | ahead | 6/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 571 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 35.2 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.freeze_total_ms` | 370 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.longest_freeze_ms` | 346 | 52.5 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 318 | 231 | ±35.7 | behind | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3170 | 2300 | ±776 | behind | 4/10 | #15, #48, #61, #20 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_23_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63 |
| issue_23_handover | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24 |
| issue_23_handover | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61 |
| issue_23_handover | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 762 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1965 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 82.9 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1965 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 82.9 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1074 | 984 | ±77.3 | ahead | 6/10 | #37, #61 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1965 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 82.9 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_5_adjuster_default | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_5_burst15_freeze | `video.avg_kbps` | 1502 | 1414 | ±75.1 | ahead | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 148 | 204 | ±20 | ahead | 9/10 | #63 |
| issue_61_googcc_glue_congestive | `video.avg_kbps` | 1182 | 1108 | ±59.1 | ahead | 7/10 | #37, #61 |
| issue_61_googcc_glue_congestive | `cc.utilization_pct` | 91.5 | 86.6 | ±4.6 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 157 | 143 | ±10 | behind | 6/10 | #24, #37 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1536 | 1324 | ±138 | ahead | 8/10 | #37, #61 |
| issue_76_packetization_v2 | `sync.av_offset_p95_ms` | 60.0 | 49.9 | ±10 | behind | 5/10 | #77 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 81.9 | 72.4 | ±7.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.1 | 1.2 | ±0.5 | ahead | 9/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 134 | 184 | ±27.2 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 2509 | 2827 | ±262 | ahead | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1385 | 1270 | ±79.8 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.freeze_total_ms` | 159 | 57 | ±100 | behind | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 191 | 224 | ±28.4 | ahead | 8/10 | #63 |
| low_fps_clean_link | `video.e2e_delay_p95_ms` | 259 | 234 | ±12.9 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| probe_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| satellite | `video.avg_kbps` | 991 | 930 | ±49.6 | ahead | 6/10 | #37, #61 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 688 | 534 | ±34.4 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 397 | 311 | ±19.9 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 258 | 187 | ±16.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 243 | 309 | ±60.4 | ahead | 8/10 | #24, #37 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 93.9 | 154 | ±56.5 | ahead | 8/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 243 | 309 | ±60.4 | ahead | 8/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 93.9 | 154 | ±56.5 | ahead | 8/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 243 | 309 | ±60.4 | ahead | 8/10 | #24, #37 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 93.9 | 154 | ±56.5 | ahead | 8/10 | #77 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `video.freeze_total_ms` | 647 | 350 (closer) |
| capacity_drop | `cc.rtt_p50_ms` | 227 | 293 (further) |
| high_jitter | `repair.overhead_pct` | 9.5 | 5.4 (closer) |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 84.5 | 75.4 (closer) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 5.7 | 6.9 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 647 | 350 (closer) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 227 | 293 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.7 | 2.9 (further) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 277 | 387 (further) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 281 | 387 (further) |
| issue_12_lossbwe_congestive | `net.loss_pct` | 1.3 | 1.6 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 9.5 | 5.4 (closer) |
| issue_12_lossbwe_shallow | `net.loss_pct` | 0.6 | 0.7 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 18.0 | 15.8 (closer) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 21.5 | 17.4 (closer) |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 1.3 | 1.5 (further) |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 5.7 | 9.4 (further) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 9.2 | 5.3 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 1.8 | 2.8 (further) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 9.5 | 5.4 (closer) |
| issue_21_shallow_drop_repair | `video.render_fps` | 1.8 | 2.8 (further) |
| issue_23_capacity_drop | `video.freeze_total_ms` | 647 | 350 (closer) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 227 | 293 (further) |
| issue_23_handover | `cc.rtt_p50_ms` | 84.5 | 75.4 (closer) |
| issue_23_handover | `repair.overhead_pct` | 5.7 | 6.9 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 1.6 | 2.3 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 55.2 | 25.0 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 34 | 17 (closer) |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 216 | 538 (further) |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 9.5 | 5.4 (closer) |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 216 | 538 (further) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 37 | 44 (further) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 485 | 394 (closer) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 251 | 186 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 3 | 5.7 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 2424 | 1253 (closer) |
| issue_257_zero_max_payload | `video.render_fps` | 9.8 | 8.1 (closer) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2513 | 1550 (closer) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 78.2 | 103 (further) |
| issue_257_zero_max_payload | `video.avg_kbps` | 577 | 424 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 5.5 | 1.4 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2052 | 1306 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 221 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 9.6 | 1.1 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.6 | 2.3 (further) |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 216 | 538 (further) |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 735 | 432 (closer) |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 223 | 291 (further) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 277 | 387 (further) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 281 | 387 (further) |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 1.3 | 1.6 (further) |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 9.5 | 5.4 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 3.1 | 3.4 (further) |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 84.5 | 75.4 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 5.7 | 6.9 (further) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 206 | 238 (further) |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 204 | 225 (further) |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 3.2 | 4.2 (further) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 9.5 | 5.4 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 2240 | 4600 (further) |
| lossy_feedback | `sync.av_offset_p95_ms` | 30.5 | 22.9 (closer) |
| low_bandwidth | `video.render_fps` | 1.6 | 2.3 (further) |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 647 | 350 (closer) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 227 | 293 (further) |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 647 | 350 (closer) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 227 | 293 (further) |
| very_low_capacity | `video.render_fps` | 2.7 | 2.9 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.7 | 2.9 (further) |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 84.5 | 75.4 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 5.7 | 6.9 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| burst_loss | `video.avg_kbps` | 1496 | 1410 | ±74.8 | ahead | 6/10 | #37, #61 |
| capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| capacity_drop | `cc.rtt_p50_ms` | 367 | 73.8 | ±18.4 | behind | 10/10 | #61 |
| capacity_drop | `video.freeze_total_ms` | 2715 | 3065 | ±235 | ahead | 7/10 | #5, #37, #24 |
| cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `repair.overhead_pct` | 34.4 | 39.8 | ±3.8 | ahead | 7/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1247 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 57.4 | 133 | ±10 | ahead | 9/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 25.7 | 32.5 | ±2.8 | ahead | 10/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 367 | 73.8 | ±18.4 | behind | 10/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2715 | 3065 | ±235 | ahead | 7/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.3 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_124_burst_loss_v2 | `video.avg_kbps` | 1496 | 1410 | ±74.8 | ahead | 6/10 | #37, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.3 | 37.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.avg_kbps` | 627 | 593 | ±31.3 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_burst_loss | `video.avg_kbps` | 1496 | 1410 | ±74.8 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `cc.utilization_pct` | 91.5 | 86.6 | ±4.6 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.3 | 3.7 | ±0.8 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 655 | 268 | ±96.9 | behind | 10/10 | #77 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1182 | 1108 | ±59.1 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 764 | 377 | ±95.8 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 34.4 | 39.8 | ±3.8 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.4 | 84.4 | ±10 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.9 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 2 | 0.9 | ±0.7 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 11775 | 5460 | ±4644 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 7.1 | 6.4 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3 | 8.1 | ±3.6 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 627 | 1604 | ±696 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 60.2 | 57.0 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.5 | 7.0 | ±0.7 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 155 | 138 | ±13.9 | behind | 7/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 779 | 732 | ±38.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 228 | 212 | ±12.6 | behind | 3/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 136 | 245 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 33.8 | 41.5 | ±4.2 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 32.2 | 41.6 | ±2.6 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 35.2 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 35.6 | 40.9 | ±3.6 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 571 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 397 | 311 | ±19.9 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 688 | 534 | ±34.4 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3170 | 2300 | ±776 | behind | 4/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 258 | 187 | ±16.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 318 | 231 | ±35.7 | behind | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.freeze_total_ms` | 370 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.longest_freeze_ms` | 346 | 52.5 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.9 | 23.1 | ±1.9 | ahead | 9/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.4 | 39.8 | ±3.8 | ahead | 7/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.9 | 23.1 | ±1.9 | ahead | 9/10 | #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 367 | 73.8 | ±18.4 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2715 | 3065 | ±235 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1247 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63 |
| issue_23_handover | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `cc.rtt_p50_ms` | 57.4 | 133 | ±10 | ahead | 9/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 25.7 | 32.5 | ±2.8 | ahead | 10/10 | #21, #7, #29 |
| issue_23_handover | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61 |
| issue_23_handover | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 27.4 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 120 | 137 | ±12.5 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 189 | 214 | ±11.4 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 762 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 82.9 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 93.9 | 154 | ±56.5 | ahead | 8/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 243 | 309 | ±60.4 | ahead | 8/10 | #24, #37 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1965 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1735 | 1197 | ±255 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 34.4 | 39.8 | ±3.8 | ahead | 7/10 | #21, #7, #29 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 82.9 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 93.9 | 154 | ±56.5 | ahead | 8/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 243 | 309 | ±60.4 | ahead | 8/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1965 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1735 | 1197 | ±255 | behind | 8/10 | #5, #37, #24 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.4 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 310 | 124 | ±18.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 72 | 28 | ±20 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 599 | 205 | ±29.9 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 265 | 44.2 | ±13.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 310 | 60.6 | ±15.5 | behind | 9/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.1 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1364 | 58.1 | ±68.2 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1586 | 2010 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1668 | 118 | ±83.4 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.7 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3789 | 0 | ±189 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 8/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1260 | 7.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.6 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1074 | 984 | ±77.3 | ahead | 6/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.4 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 82.9 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 93.9 | 154 | ±56.5 | ahead | 8/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 243 | 309 | ±60.4 | ahead | 8/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1965 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1735 | 1197 | ±255 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_5_adjuster_default | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.avg_kbps` | 1502 | 1414 | ±75.1 | ahead | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 148 | 204 | ±20 | ahead | 9/10 | #63 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 365 | 74.0 | ±18.2 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2675 | 3107 | ±271 | ahead | 7/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `cc.utilization_pct` | 91.5 | 86.6 | ±4.6 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.3 | 3.7 | ±0.8 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 655 | 268 | ±96.9 | behind | 10/10 | #77 |
| issue_61_googcc_glue_congestive | `video.avg_kbps` | 1182 | 1108 | ±59.1 | ahead | 7/10 | #37, #61 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 764 | 377 | ±95.8 | behind | 10/10 | #24, #37 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 34.4 | 39.8 | ±3.8 | ahead | 7/10 | #21, #7, #29 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 81.9 | 72.4 | ±7.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 28.6 | 32.0 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `sync.av_offset_p95_ms` | 60.0 | 49.9 | ±10 | behind | 5/10 | #77 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1536 | 1324 | ±138 | ahead | 8/10 | #37, #61 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 157 | 143 | ±10 | behind | 6/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1247 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 57.4 | 133 | ±10 | ahead | 9/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 25.7 | 32.5 | ±2.8 | ahead | 10/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.1 | 1.2 | ±0.5 | ahead | 9/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 134 | 184 | ±27.2 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 809 | 1034 | ±117 | ahead | 8/10 | #77 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1293 | 1532 | ±121 | ahead | 8/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 2509 | 2827 | ±262 | ahead | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 35.7 | 39.9 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1385 | 1270 | ±79.8 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 24560 | 29930 | ±1496 | ahead | 3/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 33.6 | 28.3 | ±4.5 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 34.4 | 39.8 | ±3.8 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 549 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.5 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9937 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 4970 | ±1761 | ahead | 9/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.freeze_total_ms` | 159 | 57 | ±100 | behind | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 191 | 224 | ±28.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 48.1 | 71.0 | ±22.7 | ahead | 7/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.4 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| low_fps_clean_link | `video.e2e_delay_p95_ms` | 259 | 234 | ±12.9 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 367 | 73.8 | ±18.4 | behind | 10/10 | #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2715 | 3065 | ±235 | ahead | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.1 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 140 | 201 | ±20 | ahead | 10/10 | #63 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 367 | 73.8 | ±18.4 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2715 | 3065 | ±235 | ahead | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 402 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 692 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 311 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| probe_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.freeze_total_ms` | 371 | 38.5 | ±122 | behind | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 347 | 52.4 | ±117 | behind | 8/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 14.7 | 18.0 | ±2 | ahead | 10/10 | #21, #7, #29 |
| satellite | `video.avg_kbps` | 991 | 930 | ±49.6 | ahead | 6/10 | #37, #61 |
| very_low_capacity | `repair.overhead_pct` | 34.3 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.3 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1247 | 1433 | ±169 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 425 | 531 | ±85.6 | ahead | 8/10 | #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 63.0 | 137 | ±59.3 | ahead | 8/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 57.4 | 133 | ±10 | ahead | 9/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 25.7 | 32.5 | ±2.8 | ahead | 10/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.avg_kbps` | 1279 | 1155 | ±114 | ahead | 9/10 | #37, #61, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 2.8 | 4.6 | ±0.8 | ahead | 8/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3247 | 4951 | ±819 | ahead | 9/10 | #5, #37, #24, #20 |

