## Parity with libwebrtc M157.8103@{#0}

97 scenarios, mean of 10 seeds. **224 gaps**: 117 behind libwebrtc, 107 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

152 gaps on `main`, 224 here: **55 closed, 127 opened**, 18 changed direction.

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
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1610 | 1268 | ±223 | behind | 6/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.5 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 131 | 156 | ±17.1 | ahead | 8/10 | #24, #37 |
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
| bufferbloat | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61, #19, #24 |
| high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_123_v2_allocation_min | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| issue_124_bufferbloat_v2 | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5.3 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3539 | 2917 | ±591 | behind | 4/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 573 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 36.5 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.freeze_count` | 1.3 | 0.1 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.freeze_total_ms` | 460 | 38.5 | ±122 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.longest_freeze_ms` | 389 | 52.5 | ±117 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 328 | 231 | ±35.7 | behind | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3490 | 2300 | ±776 | behind | 4/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_15_aimd_v2_random_loss | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 765 | 989 | ±206 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 538 | 435 | ±62.8 | behind | 8/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.avg_kbps` | 1303 | 1373 | ±68.7 | behind | 4/10 | #37, #61 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 200 | 232 | ±24.8 | ahead | 6/10 | #77 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 11330 | 8400 | ±2016 | behind | 7/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 762 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 238 | 281 | ±41.4 | ahead | 7/10 | #24, #37 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 82.8 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 82.8 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1090 | 984 | ±77.3 | ahead | 7/10 | #37, #61 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 37.5 | 49.5 | ±10.4 | ahead | 8/10 | #21, #7, #29 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 82.8 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_5_adjuster_default | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 163 | 204 | ±20 | ahead | 7/10 | #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 316 | 290 | ±15.8 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 174 | 155 | ±13.4 | behind | 3/10 | #63 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 88 | 73.3 | ±12.5 | behind | 3/10 | #63 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 154 | 143 | ±10 | behind | 5/10 | #24, #37 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1519 | 1324 | ±138 | ahead | 7/10 | #37, #61 |
| issue_76_packetization_v2 | `sync.av_offset_p95_ms` | 60.9 | 49.9 | ±10 | behind | 3/10 | #77 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 81.3 | 72.4 | ±7.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 28.5 | 18.3 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.4 | 1.2 | ±0.5 | ahead | 7/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3112 | 2827 | ±262 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 305 | 282 | ±15.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1373 | 1270 | ±79.8 | ahead | 7/10 | #37, #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.0 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_85_video_pause | `video.freeze_total_ms` | 159 | 57 | ±100 | behind | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 194 | 224 | ±28.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 47.0 | 71.0 | ±22.7 | ahead | 8/10 | #77 |
| low_fps_clean_link | `video.e2e_delay_p95_ms` | 257 | 234 | ±12.9 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| probe_v2_cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| random_loss_5pct | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| random_loss_5pct | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| very_low_capacity | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| wifi_to_lte_handover | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 692 | 534 | ±34.6 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 399 | 311 | ±20.0 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 259 | 187 | ±16.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `cc.rtt_p50_ms` | 279 | 311 (further) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 4.2 | 5.0 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 279 | 311 (further) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.7 | 2.2 (closer) |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 13.8 | 15.6 (further) |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 104 | 162 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 19.9 | 30.9 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 16.8 | 29.4 (further) |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 78.4 | 14.7 (closer) |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 78.4 | 14.7 (closer) |
| issue_204_cross_traffic_join | `cc.rtt_p50_ms` | 249 | 218 (closer) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 279 | 311 (further) |
| issue_23_handover | `repair.overhead_pct` | 4.2 | 5.0 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 1.6 | 2.4 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 56.6 | 23.7 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 35 | 17 (closer) |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 12.6 | 51.3 (further) |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 12.6 | 51.3 (further) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 37 | 41 (further) |
| issue_257_zero_max_payload | `video.freeze_count` | 2.4 | 6.2 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2014 | 4102 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1100 | 1256 (further) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2917 | 1546 (closer) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 78.2 | 103 (further) |
| issue_257_zero_max_payload | `video.avg_kbps` | 587 | 383 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.1 | 2 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2432 | 1310 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 219 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 12.6 | 1.4 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.6 | 2.4 (further) |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 12.6 | 51.3 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.3 | 2.8 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 4.2 | 5.0 (further) |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 57.8 | 41.7 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 2230 | 4600 (further) |
| low_bandwidth | `video.render_fps` | 1.6 | 2.4 (further) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 279 | 311 (further) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 279 | 311 (further) |
| wifi_to_lte_handover | `repair.overhead_pct` | 4.2 | 5.0 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| bufferbloat | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `cc.rtt_p50_ms` | 384 | 73.8 | ±19.2 | behind | 10/10 | #61 |
| cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1236 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 52.1 | 133 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.6 | 32.5 | ±2.8 | ahead | 8/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 384 | 73.8 | ±19.2 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.9 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 8/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.9 | 37.0 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.4 | 84.4 | ±10 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5.3 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 3539 | 2917 | ±591 | behind | 4/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_12_lossbwe_random_loss | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 139 | 155 | ±14.7 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 214 | 228 | ±11.8 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.5 | 8.1 | ±3.6 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 519 | 1604 | ±696 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 108 | 138 | ±13.9 | ahead | 8/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 181 | 212 | ±12.6 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.4 | 3.5 | ±2.6 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 85 | 742 | ±521 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 83.3 | 245 | ±100 | ahead | 7/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 112 | 97.3 | ±10 | behind | 1/10 | #61 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 112 | 97.3 | ±10 | behind | 1/10 | #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 36.5 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 573 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 399 | 311 | ±20.0 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 692 | 534 | ±34.6 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3490 | 2300 | ±776 | behind | 4/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 259 | 187 | ±16.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 328 | 231 | ±35.7 | behind | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `video.freeze_count` | 1.3 | 0.1 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.freeze_total_ms` | 460 | 38.5 | ±122 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.longest_freeze_ms` | 389 | 52.5 | ±117 | behind | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_15_aimd_v2_random_loss | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_18_random_loss_keyframes | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 11330 | 8400 | ±2016 | behind | 7/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `cc.rtt_p50_ms` | 312 | 94.0 | ±15.6 | behind | 10/10 | #61 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 200 | 232 | ±24.8 | ahead | 6/10 | #77 |
| issue_204_cross_traffic_join | `video.avg_kbps` | 1303 | 1373 | ±68.7 | behind | 4/10 | #37, #61 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 538 | 435 | ±62.8 | behind | 8/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 765 | 989 | ±206 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_224_congestive_e2e_flip | `cc.rtt_p50_ms` | 102 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 384 | 73.8 | ±19.2 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1236 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 52.1 | 133 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 27.6 | 32.5 | ±2.8 | ahead | 8/10 | #21, #7, #29 |
| issue_23_handover | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `video.render_fps` | 27.5 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 120 | 137 | ±12.5 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 191 | 214 | ±11.4 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 238 | 281 | ±41.4 | ahead | 7/10 | #24, #37 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 762 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 82.8 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 132 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 82.8 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 132 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.4 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 312 | 124 | ±18.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 69 | 28 | ±20 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 598 | 205 | ±29.9 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 263 | 44.2 | ±13.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 309 | 60.6 | ±15.4 | behind | 9/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.4 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1368 | 58.1 | ±68.4 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1627 | 2010 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1664 | 118 | ±83.2 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 6.2 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 4102 | 0 | ±205 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 2 | 0 | ±0.5 | behind | 9/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1263 | 7.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.0 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 94.1 | 74.0 | ±10 | behind | 8/10 | #77 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 37.5 | 49.5 | ±10.4 | ahead | 8/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1090 | 984 | ±77.3 | ahead | 7/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.5 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 82.8 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 132 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_5_adjuster_default | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 163 | 204 | ±20 | ahead | 7/10 | #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 316 | 290 | ±15.8 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 388 | 74.0 | ±19.4 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 88 | 73.3 | ±12.5 | behind | 3/10 | #63 |
| issue_76_packetization_v2 | `audio.mouth_to_ear_p95_ms` | 174 | 155 | ±13.4 | behind | 3/10 | #63 |
| issue_76_packetization_v2 | `cc.queue_p95_ms` | 28.5 | 18.3 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 81.3 | 72.4 | ±7.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.2 | 32.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `sync.av_offset_p95_ms` | 60.9 | 49.9 | ±10 | behind | 3/10 | #77 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1519 | 1324 | ±138 | ahead | 7/10 | #37, #61 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 154 | 143 | ±10 | behind | 5/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1236 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 52.1 | 133 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 27.6 | 32.5 | ±2.8 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.4 | 1.2 | ±0.5 | ahead | 7/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 305 | 282 | ±15.3 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3112 | 2827 | ±262 | behind | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.0 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1373 | 1270 | ±79.8 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 21870 | 29930 | ±1496 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 33.8 | 28.3 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 533 | 445 | ±67.9 | ahead | 6/10 | #37, #61 |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| issue_7_no_fec_beside_nack | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.4 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9937 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 211 | 252 | ±41.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 4970 | ±1761 | ahead | 9/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.freeze_total_ms` | 159 | 57 | ±100 | behind | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 194 | 224 | ±28.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 47.0 | 71.0 | ±22.7 | ahead | 8/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.5 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| low_fps_clean_link | `video.e2e_delay_p95_ms` | 257 | 234 | ±12.9 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 384 | 73.8 | ±19.2 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 313 | 291 | ±15.6 | behind | 7/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 384 | 73.8 | ±19.2 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 397 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 687 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.max_settle_ms` | 3330 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 256 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 322 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| probe_v2_cross_traffic | `video.freeze_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.freeze_total_ms` | 365 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 316 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| random_loss_5pct | `sync.av_offset_p95_ms` | 108 | 95.4 | ±10 | behind | 5/10 | #77 |
| random_loss_5pct | `video.avg_kbps` | 1394 | 1319 | ±69.7 | ahead | 5/10 | #37, #61 |
| very_low_capacity | `repair.overhead_pct` | 34.9 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.9 | 55.5 | ±10.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 23.4 | 18.2 | ±4.4 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.6 | 7.7 | ±1.7 | ahead | 8/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1236 | 1433 | ±169 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 52.1 | 133 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.6 | 32.5 | ±2.8 | ahead | 8/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 3.7 | 4.6 | ±0.8 | ahead | 7/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4128 | 4951 | ±819 | ahead | 8/10 | #5, #37, #24, #20 |

