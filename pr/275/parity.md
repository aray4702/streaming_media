## Parity with libwebrtc M157.8103@{#0}

86 scenarios, mean of 10 seeds. **175 gaps**: 80 behind libwebrtc, 95 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

175 gaps on `main`, 175 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| capacity_drop | `cc.rtt_p50_ms` | 365 | 72.2 | ±18.2 | behind | 10/10 | #61 |
| capacity_drop | `video.freeze_total_ms` | 1950 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| cross_traffic | `audio.jb_delay_p95_ms` | 337 | 382 | ±28.5 | ahead | 9/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 596 | 669 | ±51.0 | ahead | 9/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 225 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.3 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 365 | 72.2 | ±18.2 | behind | 10/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1950 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 36.0 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.8 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_count` | 3.3 | 1.1 | ±0.7 | behind | 8/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 753 | 240 | ±150 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.4 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 9.8 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 704 | 307 | ±100 | behind | 10/10 | #77 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 812 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 5 | 3.7 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.5 | 84.4 | ±10 | ahead | 4/10 | #62, #63 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2538 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2603 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 7.1 | 6.3 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 1.7 | 14.8 | ±5.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 345 | 2820 | ±1084 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.1 | 6.7 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 153 | 130 | ±13.0 | behind | 8/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 229 | 209 | ±13.5 | behind | 5/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.4 | 43.7 | ±3.2 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 36.3 | 43.6 | ±3.8 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 34.4 | 44.9 | ±3.3 | ahead | 9/10 | #21, #7, #29 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 52.9 | 40.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 326 | 377 | ±21.5 | ahead | 9/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 582 | 661 | ±39.5 | ahead | 9/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 222 | 248 | ±22.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 216 | 283 | ±59.5 | ahead | 7/10 | #77 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.2 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 337 | 382 | ±28.5 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 596 | 669 | ±51.0 | ahead | 9/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 225 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.2 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 365 | 72.2 | ±18.2 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 1950 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 28.3 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 52.9 | 40.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 120 | 151 | ±19.7 | ahead | 8/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 196 | 247 | ±33.5 | ahead | 8/10 | #62, #63 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 105 | 128 | ±23.0 | ahead | 6/10 | #62, #63 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 226 | 249 | ±22.9 | ahead | 7/10 | #62, #63 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 141 | 126 | ±11.9 | behind | 7/10 | #61 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 2141 | 1759 | ±107 | behind | 10/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 2327 | 1949 | ±116 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1067 | 875 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 105 | 128 | ±23.0 | ahead | 6/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 226 | 249 | ±22.9 | ahead | 7/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 141 | 126 | ±11.9 | behind | 7/10 | #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2141 | 1759 | ±107 | behind | 10/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2327 | 1949 | ±116 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1067 | 875 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_256_audio_24khz | `audio.jb_delay_p95_ms` | 0 | 129 | ±41.5 | ahead | 10/10 | #62, #63 |
| issue_256_audio_24khz | `audio.longest_interruption_ms` | 0 | 28 | ±20 | ahead | 4/10 | #62, #63 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 81.1 | 219 | ±55.1 | ahead | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 382 | 119 | ±19.7 | behind | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 53 | 21 | ±20 | behind | 4/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 697 | 201 | ±34.9 | behind | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 340 | 65.3 | ±17.0 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 9.7 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.3 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2218 | 72.9 | ±111 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1521 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2731 | 113 | ±137 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 3.2 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2742 | 0 | ±137 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 0 | ±0.5 | behind | 6/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1787 | 26.0 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.5 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 105 | 128 | ±23.0 | ahead | 6/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 226 | 249 | ±22.9 | ahead | 7/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 141 | 126 | ±11.9 | behind | 7/10 | #61 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 2141 | 1759 | ±107 | behind | 10/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 2327 | 1949 | ±116 | behind | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1067 | 875 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 337 | 382 | ±28.5 | ahead | 9/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 596 | 669 | ±51.0 | ahead | 9/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 225 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 363 | 72.5 | ±18.1 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 1983 | 2643 | ±377 | ahead | 10/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.4 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.8 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 704 | 307 | ±100 | behind | 10/10 | #77 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 812 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 5 | 3.7 | ±1.1 | behind | 8/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 337 | 382 | ±28.5 | ahead | 9/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 596 | 669 | ±51.0 | ahead | 9/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 225 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.3 | 32.1 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.3 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `cc.rtt_p50_ms` | 87.7 | 70.8 | ±10 | behind | 4/10 | #61 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1492 | 1268 | ±223 | behind | 8/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.3 | 39.8 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 131 | 163 | ±24.5 | ahead | 9/10 | #62, #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 230 | 282 | ±32.0 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 97.5 | 131 | ±22.5 | ahead | 10/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 134 | 156 | ±17.1 | ahead | 9/10 | #24, #37 |
| lossy_feedback | `sync.av_offset_p95_ms` | 78.8 | 110 | ±27.3 | ahead | 9/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| outage_3s | `cc.queue_p95_ms` | 52.9 | 40.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 365 | 72.2 | ±18.2 | behind | 10/10 | #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 1950 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 365 | 72.2 | ±18.2 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 1950 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 337 | 382 | ±28.5 | ahead | 9/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 596 | 669 | ±51.0 | ahead | 9/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 225 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| satellite | `repair.overhead_pct` | 14.9 | 18.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `repair.overhead_pct` | 36.0 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 36.0 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.3 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |

