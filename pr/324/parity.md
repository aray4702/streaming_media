## Parity with libwebrtc M157.8103@{#0}

97 scenarios, mean of 10 seeds. **196 gaps**: 63 behind libwebrtc, 133 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

152 gaps on `main`, 196 here: **55 closed, 99 opened**, 1 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29, #19, #24 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2745 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2680 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 48.0 | 73.4 | ±23.1 | ahead | 9/10 | #21, #7, #29 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 40.3 | 43.7 | ±3.2 | ahead | 23/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 41.2 | 44.9 | ±3.3 | ahead | 4/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 95.2 | ±18.4 | ahead | 7/10 | #77 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.6 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2788 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2788 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1513 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 228 | 92.3 | ±11.4 | behind | 6/10 | #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1877 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1107 | 876 | ±186 | behind | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.3 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.1 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.4 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 92.6 | 131 | ±22.5 | ahead | 10/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 318 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9195 | 7658 | ±1520 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8758 | 7211 | ±1537 | behind | 7/10 | #77 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61, #19, #24 |
| high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 382 | 570 | ±183 | ahead | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1002 | 1381 | ±373 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5.3 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3539 | 2917 | ±591 | behind | 4/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.pause_total_ms` | 16757 | 15310 | ±838 | behind | 5/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.render_fps` | 17.4 | 18.4 | ±0.9 | behind | 5/10 | #37, #24 |
| issue_133_all_v2_outage_3s | `cc.max_settle_ms` | 21660 | 27000 | ±1350 | ahead | 2/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.queue_p95_ms` | 164 | 212 | ±35.9 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 10910 | 2640 | ±1002 | behind | 9/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `net.loss_pct` | 7.7 | 8.3 | ±0.5 | ahead | 4/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_15s | `video.pause_total_ms` | 16757 | 15310 | ±838 | behind | 5/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_15s | `video.render_fps` | 17.4 | 18.4 | ±0.9 | behind | 5/10 | #37, #24 |
| issue_133_pacer_v2_outage_3s | `cc.max_settle_ms` | 21660 | 27000 | ±1350 | ahead | 2/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.queue_p95_ms` | 164 | 212 | ±35.9 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 10910 | 2640 | ±1002 | behind | 9/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 7.7 | 8.3 | ±0.5 | ahead | 4/10 | #21, #61, #12 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 558 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 35.8 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 185 | 231 | ±35.7 | ahead | 10/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.1 | ±1.9 | ahead | 9/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 748 | 704 | ±37.4 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 412 | 386 | ±20.6 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.8 | 2.5 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 3078 | 1484 | ±322 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 686 | 989 | ±206 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 682 | 435 | ±62.8 | behind | 10/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 289 | 232 | ±24.8 | behind | 7/10 | #77 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 288 | 267 | ±14.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 12030 | 8400 | ±2016 | behind | 8/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.1 | ±1.9 | ahead | 9/10 | #37, #24 |
| issue_224_congestive_e2e_flip | `video.freeze_total_ms` | 1002 | 1381 | ±373 | ahead | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 788 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_268_freeze_rule_outage | `cc.max_settle_ms` | 21660 | 27000 | ±1350 | ahead | 2/10 | #15, #48, #61, #20 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_29_ulpfec_over_red | `video.render_fps` | 4.0 | 2.3 | ±1.0 | ahead | 7/10 | #37, #24 |
| issue_29_ulpfec_over_red | `video.keyframe_requests` | 6.7 | 7.4 | ±0.5 | ahead | 6/10 | #23 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 37.2 | 49.5 | ±10.4 | ahead | 8/10 | #21, #7, #29 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.5 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 162 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1779 | 2549 | ±687 | ahead | 8/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 604 | 449 | ±143 | behind | 8/10 | #24, #37 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 306 | 290 | ±15.3 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1002 | 1381 | ±373 | ahead | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.6 | 1.2 | ±0.5 | ahead | 7/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_count` | 3.4 | 2.1 | ±1.1 | behind | 9/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| low_fps_clean_link | `cc.queue_p95_ms` | 99.3 | 89.0 | ±10 | behind | 2/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1649 | 2171 | ±518 | ahead | 8/10 | #5, #37, #24 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `cc.rtt_p50_ms` | 279 | 214 (closer) |
| cross_traffic | `cc.queue_p95_ms` | 30.3 | 36.0 (further) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 4.2 | 4.7 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 279 | 214 (closer) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.7 | 2.3 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 5.7 | 5 (closer) |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 13.8 | 16.0 (further) |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 104 | 144 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 19.9 | 22.9 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 16.8 | 23.3 (further) |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 78.4 | 105 (further) |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 78.4 | 105 (further) |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 27.3 | 35.1 (further) |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 30.3 | 36.0 (further) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 279 | 214 (closer) |
| issue_23_handover | `repair.overhead_pct` | 4.2 | 4.7 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 1.6 | 2.4 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 56.6 | 28.8 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 35 | 19 (closer) |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 12.6 | 45.5 (further) |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 12.6 | 45.5 (further) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 430 | 369 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 2.4 | 5.5 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2014 | 3392 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1100 | 1259 (further) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2917 | 1393 (closer) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 78.2 | 103 (further) |
| issue_257_zero_max_payload | `video.avg_kbps` | 587 | 435 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.1 | 1.2 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2432 | 1204 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 206 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 12.6 | 1.2 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.6 | 2.4 (further) |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 12.6 | 45.5 (further) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 30.3 | 36.0 (further) |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 287 | 219 (closer) |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 30.3 | 36.0 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 4.2 | 4.7 (further) |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 57.8 | 44.8 (closer) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 24.3 | 14.5 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 2230 | 4630 (further) |
| low_bandwidth | `video.render_fps` | 1.6 | 2.4 (further) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 279 | 214 (closer) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 279 | 214 (closer) |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 30.3 | 36.0 (further) |
| wifi_to_lte_handover | `repair.overhead_pct` | 4.2 | 4.7 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| capacity_drop | `cc.rtt_p50_ms` | 287 | 73.8 | ±14.4 | behind | 9/10 | #61 |
| capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| cross_traffic | `audio.jb_delay_p95_ms` | 268 | 310 | ±22.8 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 454 | 535 | ±37.3 | ahead | 10/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 152 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 56.2 | 133 | ±10 | ahead | 9/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.8 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 287 | 73.8 | ±14.4 | behind | 9/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.9 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 8/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 37.0 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 382 | 570 | ±183 | ahead | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 101 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `video.freeze_total_ms` | 1002 | 1381 | ±373 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.4 | 84.4 | ±10 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5.3 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3539 | 2917 | ±591 | behind | 4/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 139 | 155 | ±14.7 | ahead | 6/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 214 | 228 | ±11.8 | ahead | 4/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.1 | 8.1 | ±3.6 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 634 | 1604 | ±696 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 114 | 138 | ±13.9 | ahead | 9/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 212 | ±12.6 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.8 | 3.5 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 170 | 742 | ±521 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 101 | 245 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.pause_total_ms` | 16757 | 15310 | ±838 | behind | 5/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.render_fps` | 17.4 | 18.4 | ±0.9 | behind | 5/10 | #37, #24 |
| issue_133_all_v2_outage_3s | `cc.max_settle_ms` | 21660 | 27000 | ±1350 | ahead | 2/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 10910 | 2640 | ±1002 | behind | 9/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.queue_p95_ms` | 164 | 212 | ±35.9 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 202 | 97.3 | ±10.1 | behind | 6/10 | #61 |
| issue_133_all_v2_outage_8s | `net.loss_pct` | 7.7 | 8.3 | ±0.5 | ahead | 4/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_15s | `video.pause_total_ms` | 16757 | 15310 | ±838 | behind | 5/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_15s | `video.render_fps` | 17.4 | 18.4 | ±0.9 | behind | 5/10 | #37, #24 |
| issue_133_pacer_v2_outage_3s | `cc.max_settle_ms` | 21660 | 27000 | ±1350 | ahead | 2/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 10910 | 2640 | ±1002 | behind | 9/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.queue_p95_ms` | 164 | 212 | ±35.9 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 202 | 97.3 | ±10.1 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 7.7 | 8.3 | ±0.5 | ahead | 4/10 | #21, #61, #12 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 35.8 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 558 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 269 | 311 | ±16.2 | ahead | 9/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 454 | 534 | ±30.6 | ahead | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 152 | 187 | ±16.1 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 185 | 231 | ±35.7 | ahead | 10/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.3 | 23.1 | ±1.9 | ahead | 9/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 268 | 310 | ±22.8 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 454 | 535 | ±37.3 | ahead | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 152 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 412 | 386 | ±20.6 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 748 | 704 | ±37.4 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 12030 | 8400 | ±2016 | behind | 8/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 288 | 267 | ±14.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `cc.rtt_p50_ms` | 340 | 94.0 | ±17.0 | behind | 10/10 | #61 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 289 | 232 | ±24.8 | behind | 7/10 | #77 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 682 | 435 | ±62.8 | behind | 10/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.8 | 2.5 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 3078 | 1484 | ±322 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 686 | 989 | ±206 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.3 | 23.1 | ±1.9 | ahead | 9/10 | #37, #24 |
| issue_224_congestive_e2e_flip | `cc.rtt_p50_ms` | 101 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_224_congestive_e2e_flip | `video.freeze_total_ms` | 1002 | 1381 | ±373 | ahead | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 287 | 73.8 | ±14.4 | behind | 9/10 | #61 |
| issue_23_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 56.2 | 133 | ±10 | ahead | 9/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 27.8 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_23_handover | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 27.5 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 118 | 137 | ±12.5 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 186 | 214 | ±11.4 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 788 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 127 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 127 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 310 | 124 | ±18.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 63 | 28 | ±20 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 574 | 205 | ±28.7 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 250 | 44.2 | ±12.5 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 308 | 60.6 | ±15.4 | behind | 9/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.2 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1262 | 58.1 | ±63.1 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1575 | 2010 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1511 | 118 | ±75.5 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.5 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3392 | 0 | ±170 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.2 | 0 | ±0.5 | behind | 8/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1266 | 7.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 22.2 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.max_settle_ms` | 21660 | 27000 | ±1350 | ahead | 2/10 | #15, #48, #61, #20 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 37.2 | 49.5 | ±10.4 | ahead | 8/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `video.keyframe_requests` | 6.7 | 7.4 | ±0.5 | ahead | 6/10 | #23 |
| issue_29_ulpfec_over_red | `video.render_fps` | 4.0 | 2.3 | ±1.0 | ahead | 7/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.5 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 127 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 268 | 310 | ±22.8 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 454 | 535 | ±37.3 | ahead | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 152 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.5 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 162 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 306 | 290 | ±15.3 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 293 | 74.0 | ±14.7 | behind | 9/10 | #61 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 604 | 449 | ±143 | behind | 8/10 | #24, #37 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1779 | 2549 | ±687 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 101 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `video.freeze_total_ms` | 1002 | 1381 | ±373 | ahead | 7/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 268 | 310 | ±22.8 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 454 | 535 | ±37.3 | ahead | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 152 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 56.2 | 133 | ±10 | ahead | 9/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 27.8 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.6 | 1.2 | ±0.5 | ahead | 7/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_count` | 3.4 | 2.1 | ±1.1 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1649 | 2171 | ±518 | ahead | 8/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 22680 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 33.1 | 28.3 | ±4.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 518 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9937 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 207 | 252 | ±41.3 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 340 | 4970 | ±1761 | ahead | 9/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 128 | 142 | ±11.6 | ahead | 6/10 | #24, #37 |
| low_bandwidth | `video.render_fps` | 27.5 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| low_fps_clean_link | `cc.queue_p95_ms` | 99.3 | 89.0 | ±10 | behind | 2/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 287 | 73.8 | ±14.4 | behind | 9/10 | #61 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.6 | 1.1 | ±0.5 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 170 | 201 | ±20 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 287 | 73.8 | ±14.4 | behind | 9/10 | #61 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.7 | 1.8 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.longest_freeze_ms` | 1706 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 268 | 310 | ±22.8 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 454 | 535 | ±37.3 | ahead | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 152 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 185 | 232 | ±35.9 | ahead | 10/10 | #24, #37 |
| very_low_capacity | `repair.overhead_pct` | 34.9 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.9 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 8/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 56.2 | 133 | ±10 | ahead | 9/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.8 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4081 | 4951 | ±819 | ahead | 6/10 | #5, #37, #24, #20 |

