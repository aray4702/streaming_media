## Parity with libwebrtc M157.8103@{#0}

95 scenarios, mean of 10 seeds. **231 gaps**: 148 behind libwebrtc, 83 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

160 gaps on `main`, 231 here: **61 closed, 132 opened**, 18 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29, #19, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.1 | 7.2 | ±1.6 | ahead | 10/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.9 | 3.2 | ±0.6 | behind | 5/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 39.9 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 5617 | 10722 | ±3617 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 2.5 | 1.3 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 15732 | 8280 | ±5343 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4.3 | 2.9 | ±1.4 | behind | 7/10 | #23 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 209 | 92.3 | ±10.5 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 209 | 92.3 | ±10.5 | behind | 6/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±3.2 | ahead | 21/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 40.9 | 44.9 | ±3.3 | ahead | 4/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 319 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_24_timing_v2_default | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 5/10 | #37, #24 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 2483 | 1949 | ±124 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 2291 | 1759 | ±115 | behind | 10/10 | #77 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 5/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2483 | 1949 | ±124 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2291 | 1759 | ±115 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.7 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 209 | 92.3 | ±10.5 | behind | 6/10 | #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1252 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1132 | 1075 | ±56.6 | ahead | 7/10 | #37, #61 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 34.3 | 38.9 | ±3.3 | ahead | 7/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 319 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 5/10 | #37, #24 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 2483 | 1949 | ±124 | behind | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 2291 | 1759 | ±115 | behind | 10/10 | #77 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.3 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 175 | 142 | ±20.4 | behind | 5/10 | #63 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±2.5 | ahead | 4/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 227 | 282 | ±32.0 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 129 | 163 | ±24.5 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 132 | 156 | ±17.1 | ahead | 8/10 | #24, #37 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 95.2 | 131 | ±22.5 | ahead | 10/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #63, #224 |
| low_bandwidth | `cc.rtt_p50_ms` | 319 | 232 | ±72.2 | behind | 7/10 | #61 |
| very_low_capacity | `video.render_fps` | 10.1 | 7.2 | ±1.6 | ahead | 10/10 | #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 3.9 | 3.2 | ±0.6 | behind | 5/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.1 | 7.2 | ±1.6 | ahead | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.9 | 3.2 | ±0.6 | behind | 5/10 | #23 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77, #19, #24 |
| high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_124_bufferbloat_v2 | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1172 | 1108 | ±58.6 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.9 | 3.5 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 196 | 742 | ±521 | ahead | 7/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `repair.overhead_pct` | 7.8 | 5.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 7.8 | 5.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 555 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 35.3 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `video.freeze_count` | 1.8 | 0.1 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.freeze_total_ms` | 790 | 38.5 | ±122 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.longest_freeze_ms` | 426 | 52.5 | ±117 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 369 | 231 | ±35.7 | behind | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 4420 | 2300 | ±776 | behind | 4/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.rtt_p50_ms` | 92.2 | 60.9 | ±10 | behind | 3/10 | #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.9 | 5.7 | ±2.3 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1030 | 2318 | ±1155 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.9 | 5.7 | ±2.3 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1030 | 2318 | ±1155 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 762 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1892 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1468 | 1197 | ±255 | behind | 6/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `net.loss_pct` | 4.1 | 3.4 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1892 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1468 | 1197 | ±255 | behind | 6/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `net.loss_pct` | 4.1 | 3.4 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_29_ulpfec_over_red | `video.render_fps` | 3.9 | 2.3 | ±1.0 | ahead | 9/10 | #37, #24 |
| issue_29_ulpfec_over_red | `video.keyframe_requests` | 6.5 | 7.4 | ±0.5 | ahead | 7/10 | #23 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1892 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1468 | 1197 | ±255 | behind | 6/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `net.loss_pct` | 4.1 | 3.4 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_5_adjuster_default | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_5_adjuster_default | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| issue_5_burst15_freeze | `video.avg_kbps` | 1500 | 1414 | ±75.0 | ahead | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 163 | 204 | ±20 | ahead | 7/10 | #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 912 | 837 | ±45.6 | behind | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 539 | 509 | ±26.9 | behind | 5/10 | #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 345 | 290 | ±17.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `video.avg_kbps` | 1172 | 1108 | ±58.6 | ahead | 6/10 | #37, #61 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_76_packetization_v2 | `video.freeze_count` | 0.7 | 0.1 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.freeze_total_ms` | 155 | 28.6 | ±100 | behind | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.longest_freeze_ms` | 162 | 50.2 | ±100 | behind | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 158 | 143 | ±10 | behind | 7/10 | #24, #37 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1526 | 1324 | ±138 | ahead | 8/10 | #37, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 81.9 | 72.4 | ±7.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.5 | 1.2 | ±0.5 | ahead | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 867 | 817 | ±43.3 | behind | 4/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3146 | 2827 | ±262 | behind | 8/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1379 | 1532 | ±121 | ahead | 3/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 315 | 282 | ±15.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 176 | 205 | ±29.3 | ahead | 9/10 | #63 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 83 | 110 | ±24.0 | ahead | 8/10 | #63 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1370 | 1270 | ±79.8 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.freeze_total_ms` | 159 | 57 | ±100 | behind | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 194 | 224 | ±28.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 47.0 | 71.0 | ±22.7 | ahead | 8/10 | #77 |
| low_fps_clean_link | `video.e2e_delay_p95_ms` | 257 | 234 | ±12.9 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| probe_v2_cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| satellite | `video.avg_kbps` | 982 | 930 | ±49.1 | ahead | 6/10 | #37, #61 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 708 | 534 | ±35.4 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 412 | 311 | ±20.6 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 262 | 187 | ±16.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `cc.rtt_p50_ms` | 278 | 336 (further) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 2.8 | 4.6 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 278 | 336 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 20.7 | 16.2 (closer) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 3.2 | 2.3 (closer) |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 259 | 349 (further) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 265 | 352 (further) |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 20.6 | 22.8 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 835 | 932 (further) |
| issue_12_lossbwe_shallow | `net.loss_pct` | 0.8 | 0.9 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 22.4 | 17.5 (closer) |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 81.3 | 16.4 (closer) |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 81.3 | 16.4 (closer) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 278 | 336 (further) |
| issue_23_handover | `repair.overhead_pct` | 2.8 | 4.6 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 1.6 | 2.3 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 54.4 | 23.1 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 34 | 18 (closer) |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 10.1 | 11.9 (further) |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 13.6 | 52.2 (further) |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 10.1 | 11.9 (further) |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 13.6 | 52.2 (further) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 37 | 41 (further) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 443 | 397 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 2 | 6 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2347 | 4000 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1651 | 1242 (closer) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2927 | 1561 (closer) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 78.2 | 103 (further) |
| issue_257_zero_max_payload | `video.avg_kbps` | 589 | 429 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.6 | 1.9 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2447 | 1315 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 222 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 12.5 | 1.4 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.6 | 2.3 (further) |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 10.1 | 11.9 (further) |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 13.6 | 52.2 (further) |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 284 | 337 (further) |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 259 | 349 (further) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 265 | 352 (further) |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 20.6 | 22.8 (further) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 2.8 | 4.6 (further) |
| issue_85_video_pause | `cc.max_settle_ms` | 2230 | 4600 (further) |
| low_bandwidth | `video.render_fps` | 1.6 | 2.3 (further) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 278 | 336 (further) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 278 | 336 (further) |
| very_low_capacity | `repair.overhead_pct` | 20.7 | 16.2 (closer) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 20.7 | 16.2 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 2.8 | 4.6 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| bufferbloat | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| capacity_drop | `cc.rtt_p50_ms` | 410 | 73.8 | ±20.5 | behind | 10/10 | #61 |
| cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1234 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 49.3 | 133 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 410 | 73.8 | ±20.5 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 39.2 | 55.5 | ±10.1 | ahead | 8/10 | #21, #7, #29 |
| issue_124_bufferbloat_v2 | `cc.queue_p95_ms` | 57.5 | 47.1 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.7 | 37.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 104 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.9 | 3.7 | ±0.8 | behind | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 620 | 268 | ±96.9 | behind | 10/10 | #77 |
| issue_12_lossbwe_congestive | `video.avg_kbps` | 1172 | 1108 | ±58.6 | ahead | 6/10 | #37, #61 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 726 | 377 | ±95.8 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.6 | 84.4 | ±10 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5.5 | 8 | ±2.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 7.3 | 6.4 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.2 | 8.1 | ±3.6 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 673 | 1604 | ±696 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.1 | 7.0 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 121 | 138 | ±13.9 | ahead | 8/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 195 | 212 | ±12.6 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.9 | 3.5 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 196 | 742 | ±521 | ahead | 7/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 97.3 | ±10 | behind | 1/10 | #61 |
| issue_133_all_v2_outage_8s | `repair.overhead_pct` | 7.8 | 5.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 97.3 | ±10 | behind | 1/10 | #61 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 7.8 | 5.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `cc.utilization_pct` | 35.3 | 30.5 | ±4.5 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.avg_kbps` | 555 | 478 | ±68.2 | ahead | 6/10 | #37, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 412 | 311 | ±20.6 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 708 | 534 | ±35.4 | behind | 10/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 4420 | 2300 | ±776 | behind | 4/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 262 | 187 | ±16.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `cc.rtt_p50_ms` | 92.2 | 60.9 | ±10 | behind | 3/10 | #61 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 369 | 231 | ±35.7 | behind | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `video.freeze_count` | 1.8 | 0.1 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.freeze_total_ms` | 790 | 38.5 | ±122 | behind | 9/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `video.longest_freeze_ms` | 426 | 52.5 | ±117 | behind | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.9 | 5.7 | ±2.3 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 1030 | 2318 | ±1155 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.9 | 5.7 | ±2.3 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 1030 | 2318 | ±1155 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.7 | 23.1 | ±1.9 | ahead | 8/10 | #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 410 | 73.8 | ±20.5 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1234 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 49.3 | 133 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 28.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.render_fps` | 27.4 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 119 | 137 | ±12.5 | ahead | 6/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 191 | 214 | ±11.4 | ahead | 5/10 | #63, #218 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 762 | 561 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 83.3 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_default | `net.loss_pct` | 4.1 | 3.4 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1892 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1468 | 1197 | ±255 | behind | 6/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 83.3 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_24_timing_v2_render_fps | `net.loss_pct` | 4.1 | 3.4 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1892 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1468 | 1197 | ±255 | behind | 6/10 | #5, #37, #24 |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.4 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 313 | 124 | ±18.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 69 | 28 | ±20 | behind | 6/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 602 | 205 | ±30.1 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 266 | 44.2 | ±13.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 309 | 60.6 | ±15.4 | behind | 9/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.4 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1373 | 58.1 | ±68.7 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1581 | 2010 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1678 | 118 | ±83.9 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 6 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 4000 | 0 | ±200 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.9 | 0 | ±0.5 | behind | 8/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1249 | 7.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.1 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1980 | ±99 | ahead | 1/10 | #63 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 92.5 | 74.0 | ±10 | behind | 7/10 | #77 |
| issue_29_ulpfec_over_red | `video.keyframe_requests` | 6.5 | 7.4 | ±0.5 | ahead | 7/10 | #23 |
| issue_29_ulpfec_over_red | `video.render_fps` | 3.9 | 2.3 | ±1.0 | ahead | 9/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.4 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 83.3 | 71.4 | ±10 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 133 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_53_cwnd_pushback | `net.loss_pct` | 4.1 | 3.4 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1892 | 1662 | ±227 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1468 | 1197 | ±255 | behind | 6/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_5_adjuster_default | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_5_adjuster_default | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.avg_kbps` | 1500 | 1414 | ±75.0 | ahead | 7/10 | #37, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 539 | 509 | ±26.9 | behind | 5/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 163 | 204 | ±20 | ahead | 7/10 | #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 912 | 837 | ±45.6 | behind | 8/10 | #63 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 345 | 290 | ±17.2 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 410 | 74.0 | ±20.5 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `cc.queue_p95_ms` | 55.3 | 44.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 104 | 80.8 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.9 | 3.7 | ±0.8 | behind | 8/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 620 | 268 | ±96.9 | behind | 10/10 | #77 |
| issue_61_googcc_glue_congestive | `video.avg_kbps` | 1172 | 1108 | ±58.6 | ahead | 6/10 | #37, #61 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 726 | 377 | ±95.8 | behind | 10/10 | #24, #37 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `cc.utilization_pct` | 81.9 | 72.4 | ±7.1 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.5 | 32.0 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.avg_kbps` | 1526 | 1324 | ±138 | ahead | 8/10 | #37, #61 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 158 | 143 | ±10 | behind | 7/10 | #24, #37 |
| issue_76_packetization_v2 | `video.freeze_count` | 0.7 | 0.1 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.freeze_total_ms` | 155 | 28.6 | ±100 | behind | 6/10 | #5, #37, #24 |
| issue_76_packetization_v2 | `video.longest_freeze_ms` | 162 | 50.2 | ±100 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1234 | 1433 | ±169 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 49.3 | 133 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.5 | 1.2 | ±0.5 | ahead | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 867 | 817 | ±43.3 | behind | 4/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 315 | 282 | ±15.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1379 | 1532 | ±121 | ahead | 3/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_total_ms` | 3146 | 2827 | ±262 | behind | 8/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 83 | 110 | ±24.0 | ahead | 8/10 | #63 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 176 | 205 | ±29.3 | ahead | 9/10 | #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.0 | 39.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `video.avg_kbps` | 1370 | 1270 | ±79.8 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 23080 | 29930 | ±1496 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 61.6 | 44.9 | ±15.4 | behind | 5/10 | #77 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 113 | ±12.7 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.4 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9937 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 4970 | ±1761 | ahead | 9/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.freeze_total_ms` | 159 | 57 | ±100 | behind | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 194 | 224 | ±28.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 47.0 | 71.0 | ±22.7 | ahead | 8/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.4 | 25.1 | ±1.7 | ahead | 9/10 | #37, #24 |
| low_fps_clean_link | `video.e2e_delay_p95_ms` | 257 | 234 | ±12.9 | behind | 9/10 | #24, #37 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 410 | 73.8 | ±20.5 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.2 | 1.1 | ±0.5 | ahead | 9/10 | #63 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 538 | 510 | ±26.9 | behind | 5/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 158 | 201 | ±20 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 909 | 838 | ±45.4 | behind | 8/10 | #63 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 342 | 291 | ±17.1 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 410 | 73.8 | ±20.5 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 409 | 310 | ±22.8 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 702 | 535 | ±37.3 | behind | 10/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.max_settle_ms` | 4260 | 2360 | ±832 | behind | 4/10 | #15, #48, #61, #20 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 260 | 188 | ±17.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 89.9 | 61.0 | ±10 | behind | 2/10 | #61 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 364 | 232 | ±35.9 | behind | 9/10 | #24, #37 |
| probe_v2_cross_traffic | `video.freeze_count` | 1.5 | 0.1 | ±0.5 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.freeze_total_ms` | 696 | 38.5 | ±122 | behind | 7/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.longest_freeze_ms` | 353 | 52.4 | ±117 | behind | 7/10 | #5, #37, #24 |
| satellite | `video.avg_kbps` | 982 | 930 | ±49.1 | ahead | 6/10 | #37, #61 |
| very_low_capacity | `repair.overhead_pct` | 39.2 | 55.5 | ±10.1 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 39.2 | 55.5 | ±10.1 | ahead | 8/10 | #21, #7, #29 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1234 | 1433 | ±169 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 49.3 | 133 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.0 | 32.5 | ±2.8 | ahead | 7/10 | #21, #7, #29, #20 |

