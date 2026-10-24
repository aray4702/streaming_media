## Parity with libwebrtc M157.8103@{#0}

98 scenarios, mean of 10 seeds. **154 gaps**: 49 behind libwebrtc, 105 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

116 gaps on `main`, 154 here: **57 closed, 95 opened**, 1 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29, #19, #24 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 313 | 278 | ±34.3 | behind | 5/10 | #63, #224 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 229 | 192 | ±33.9 | behind | 5/10 | #63, #224 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 589 | 888 | ±208 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 548 | 847 | ±208 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2758 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.8 | 73.4 | ±23.1 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 111 | 127 | ±14.4 | ahead | 4/10 | #77 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2947 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2948 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2947 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2948 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 40.1 | 43.7 | ±3.2 | ahead | 24/40 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 313 | 278 | ±34.3 | behind | 5/10 | #63 |
| issue_224_congestive_e2e_flip | `audio.jb_delay_p95_ms` | 229 | 192 | ±33.9 | behind | 5/10 | #63 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1807 | 1401 | ±199 | behind | 8/10 | #77 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 94.0 | ±17.0 | ahead | 7/10 | #77 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 47.6 | 64.0 | ±16.1 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 700 | 638 | ±35.0 | behind | 7/10 | #63, #224 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 579 | 517 | ±33.0 | behind | 7/10 | #63, #224 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 700 | 638 | ±35.0 | behind | 7/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 579 | 517 | ±33.0 | behind | 7/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 272 | 65.3 | ±13.6 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.9 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2947 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2948 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1131 | 1073 | ±56.5 | ahead | 4/10 | #37, #61 |
| issue_29_ulpfec_over_red | `sync.av_offset_p95_ms` | 96.5 | 120 | ±21.0 | ahead | 8/10 | #77 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1807 | 1401 | ±199 | behind | 8/10 | #77 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 700 | 638 | ±35.0 | behind | 7/10 | #63, #224 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 579 | 517 | ±33.0 | behind | 7/10 | #63, #224 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.2 | 36.7 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 313 | 278 | ±34.3 | behind | 5/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 229 | 192 | ±33.9 | behind | 5/10 | #63, #224 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 5/10 | #21, #7, #29 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 55.0 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 163 | ±26.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 135 | 167 | ±20.6 | ahead | 9/10 | #24, #37 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.7 | 118 | ±17.0 | ahead | 9/10 | #77 |
| issue_85_video_pause | `cc.queue_p95_ms` | 45.3 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1807 | 1401 | ±199 | behind | 8/10 | #77 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.freeze_count` | 22.9 | 18.2 | ±4.4 | behind | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 1.7 | 0.9 | ±0.7 | behind | 4/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 10133 | 5460 | ±4644 | behind | 5/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.pause_total_ms` | 16167 | 15310 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 463 | 356 | ±102 | behind | 8/10 | #24, #37 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2640 | ±1002 | behind | 6/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `video.pause_total_ms` | 16167 | 15310 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 463 | 356 | ±102 | behind | 8/10 | #24, #37 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2640 | ±1002 | behind | 6/10 | #15, #48, #61, #20 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 551 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 24.9 | 22.0 | ±2.7 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 458 | 534 | ±30.6 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 269 | 311 | ±16.2 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 156 | 187 | ±16.1 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.1 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 750 | 704 | ±37.5 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 415 | 386 | ±20.8 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.7 | 2.5 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 3210 | 1484 | ±322 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 746 | 989 | ±206 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 692 | 435 | ±62.8 | behind | 9/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 299 | 232 | ±24.8 | behind | 7/10 | #77 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 287 | 267 | ±14.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 12030 | 8400 | ±2016 | behind | 8/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.1 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 788 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 83.8 | 70.0 | ±12.0 | behind | 5/10 | #77 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 83.8 | 70.0 | ±12.0 | behind | 5/10 | #77 |
| issue_29_ulpfec_over_red | `video.render_fps` | 2.9 | 2.3 | ±0.5 | ahead | 5/10 | #37, #24 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 148 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `video.freeze_count` | 2.8 | 1.9 | ±0.9 | behind | 6/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1595 | 2549 | ±687 | ahead | 8/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 168 | 155 | ±13.4 | behind | 5/10 | #63 |
| issue_76_packetization_v2 | `video.freeze_total_ms` | 138 | 28.6 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.3 | 1.2 | ±0.5 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 153 | 184 | ±27.2 | ahead | 7/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_count` | 3.4 | 2.1 | ±1.1 | behind | 9/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.6 | 39.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| low_fps_clean_link | `cc.queue_p95_ms` | 101 | 89.0 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `video.freeze_count` | 22.9 | 18.2 | ±4.4 | behind | 5/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 22.9 | 18.2 | ±4.4 | behind | 5/10 | #5, #37, #24 |
| wifi_to_lte_handover | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1548 | 2171 | ±518 | ahead | 9/10 | #5, #37, #24 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 3.8 | 2.9 (closer) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 19.5 | 21.9 (further) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.7 | 2.3 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 5.7 | 4.8 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 1069 | 925 (closer) |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 106 | 144 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 20.4 | 22.9 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 28.3 | 33.5 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 1.5 | 2.4 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 57.7 | 28.8 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 35 | 19 (closer) |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.7 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 842 | 446 (closer) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 608 | 264 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 2.8 | 5.1 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2391 | 3225 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1482 | 1260 (closer) |
| issue_257_zero_max_payload | `video.render_fps` | 8.6 | 7.4 (closer) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2833 | 1386 (closer) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 78.2 | 103 (further) |
| issue_257_zero_max_payload | `video.avg_kbps` | 575 | 479 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 1 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2080 | 1091 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 203 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 12.0 | 1.3 (closer) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 6.0 | 10.1 (further) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.5 | 2.4 (further) |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 58.6 | 44.4 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 2230 | 4630 (further) |
| low_bandwidth | `video.render_fps` | 1.5 | 2.4 (further) |
| very_low_capacity | `video.render_fps` | 3.8 | 2.9 (closer) |
| very_low_capacity | `repair.overhead_pct` | 19.5 | 21.9 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 3.8 | 2.9 (closer) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 19.5 | 21.9 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| burst_loss | `video.e2e_delay_p95_ms` | 253 | 292 | ±38.6 | ahead | 8/10 | #24, #37 |
| capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 33.6 | 55.5 | ±10.1 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 22.9 | 18.2 | ±4.4 | behind | 5/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.5 | 7.7 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 253 | 292 | ±38.6 | ahead | 8/10 | #24, #37 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.7 | 37.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 253 | 292 | ±38.6 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 1.7 | 0.9 | ±0.7 | behind | 4/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 10133 | 5460 | ±4644 | behind | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 214 | 228 | ±11.8 | ahead | 4/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.3 | 8.1 | ±3.6 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 679 | 1604 | ±696 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 80.5 | 114 | ±12.7 | ahead | 9/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 212 | ±12.6 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.8 | 3.5 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 170 | 742 | ±521 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 101 | 245 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.pause_total_ms` | 16167 | 15310 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2640 | ±1002 | behind | 6/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `video.e2e_delay_p95_ms` | 463 | 356 | ±102 | behind | 8/10 | #24, #37 |
| issue_133_pacer_v2_outage_15s | `video.pause_total_ms` | 16167 | 15310 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2640 | ±1002 | behind | 6/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `video.e2e_delay_p95_ms` | 463 | 356 | ±102 | behind | 8/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 24.9 | 22.0 | ±2.7 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 551 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 269 | 311 | ±16.2 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 458 | 534 | ±30.6 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 156 | 187 | ±16.1 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.1 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 415 | 386 | ±20.8 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 750 | 704 | ±37.5 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 12030 | 8400 | ±2016 | behind | 8/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 287 | 267 | ±14.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 299 | 232 | ±24.8 | behind | 7/10 | #77 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 692 | 435 | ±62.8 | behind | 9/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.7 | 2.5 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 3210 | 1484 | ±322 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 746 | 989 | ±206 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.1 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_23_handover | `repair.overhead_pct` | 29.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_23_handover | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 118 | 137 | ±12.5 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 186 | 214 | ±11.4 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 788 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 83.8 | 70.0 | ±12.0 | behind | 5/10 | #77 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 388 | 124 | ±19.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 53 | 28 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 651 | 205 | ±32.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 247 | 44.2 | ±12.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.3 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1147 | 55.6 | ±57.3 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1531 | 2010 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1508 | 123 | ±75.4 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.1 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3225 | 0 | ±161 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 7/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1273 | 12.8 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 22.3 | 29.6 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 83.8 | 70.0 | ±12.0 | behind | 5/10 | #77 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 39.0 | 49.1 | ±8.2 | ahead | 7/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `video.render_fps` | 2.9 | 2.3 | ±0.5 | ahead | 5/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 148 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `video.freeze_count` | 2.8 | 1.9 | ±0.9 | behind | 6/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1595 | 2549 | ±687 | ahead | 8/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 168 | 155 | ±13.4 | behind | 5/10 | #63 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.freeze_total_ms` | 138 | 28.6 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.3 | 1.2 | ±0.5 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 153 | 184 | ±27.2 | ahead | 7/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_count` | 3.4 | 2.1 | ±1.1 | behind | 9/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1548 | 2171 | ±518 | ahead | 9/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.6 | 39.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 24880 | 29930 | ±1496 | ahead | 4/10 | #15, #48, #61, #20 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9937 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 208 | 252 | ±41.3 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 340 | 4970 | ±1761 | ahead | 9/10 | #15, #48, #61, #20 |
| low_bandwidth | `video.render_fps` | 27.6 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| low_fps_clean_link | `cc.queue_p95_ms` | 101 | 89.0 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 201 | ±20 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.6 | 1.8 | ±0.8 | behind | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2522 | ±667 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 310 | ±22.8 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 535 | ±37.3 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 188 | ±17.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `repair.overhead_pct` | 33.6 | 55.5 | ±10.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 22.9 | 18.2 | ±4.4 | behind | 5/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 10.5 | 7.7 | ±1.7 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 33.6 | 55.5 | ±10.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 22.9 | 18.2 | ±4.4 | behind | 5/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.5 | 7.7 | ±1.7 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1242 | 1433 | ±169 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 3.3 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 3854 | 4956 | ±827 | ahead | 7/10 | #5, #37, #24, #20 |

