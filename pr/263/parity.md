## Parity with libwebrtc M157.8103@{#0}

82 scenarios, mean of 10 seeds. **163 gaps**: 78 behind libwebrtc, 85 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

163 gaps on `main`, 163 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 145 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `sync.av_offset_p95_ms` | 1335 | 1832 | ±467 | ahead | 7/10 | #77 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 1623 | 2259 | ±508 | ahead | 7/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 36.0 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_count` | 3.3 | 1.1 | ±0.7 | behind | 8/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.freeze_total_ms` | 753 | 240 | ±150 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.3 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 698 | 307 | ±100 | behind | 10/10 | #77 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 808 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 5 | 3.7 | ±1.1 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.4 | 84.4 | ±10 | ahead | 4/10 | #62, #63 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2626 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2691 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 6.9 | 6.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 1.3 | 14.8 | ±5.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 271 | 2820 | ±1084 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 59.4 | 56.3 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.3 | 6.7 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.1 | 12.7 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 152 | 130 | ±13.0 | behind | 8/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 775 | 736 | ±38.8 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 228 | 209 | ±13.5 | behind | 5/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.5 | 43.7 | ±3.2 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 36.3 | 43.6 | ±3.8 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 34.4 | 44.9 | ±3.3 | ahead | 9/10 | #21, #7, #29 |
| issue_151_outage_3s_v2 | `audio.jb_delay_p95_ms` | 141 | 119 | ±19.1 | behind | 4/10 | #62, #63 |
| issue_151_outage_3s_v2 | `audio.mouth_to_ear_p95_ms` | 243 | 202 | ±20.3 | behind | 5/10 | #62, #63 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 63.4 | 40.2 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 159 | 113 | ±10 | behind | 5/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 349 | 377 | ±21.5 | ahead | 7/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 613 | 661 | ±39.5 | ahead | 7/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `cc.rtt_p50_ms` | 87.0 | 65.7 | ±10 | behind | 1/10 | #61 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 208 | 283 | ±59.5 | ahead | 9/10 | #77 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.4 | 7.1 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.7 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.4 | 7.1 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.7 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_handover | `sync.av_offset_p95_ms` | 1335 | 1832 | ±467 | ahead | 7/10 | #77 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 1623 | 2259 | ±508 | ahead | 7/10 | #24, #37 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 141 | 119 | ±19.1 | behind | 4/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 243 | 202 | ±20.3 | behind | 5/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 63.4 | 40.2 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 159 | 113 | ±10 | behind | 5/10 | #24, #37 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 151 | ±19.7 | ahead | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 190 | 247 | ±33.5 | ahead | 10/10 | #62, #63 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 2165 | 1759 | ±108 | behind | 10/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 2354 | 1949 | ±118 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1111 | 875 | ±186 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2165 | 1759 | ±108 | behind | 10/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2354 | 1949 | ±118 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1111 | 875 | ±186 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_256_audio_24khz | `audio.jb_delay_p95_ms` | 0 | 129 | ±41.5 | ahead | 10/10 | #62, #63 |
| issue_256_audio_24khz | `audio.longest_interruption_ms` | 0 | 28 | ±20 | ahead | 4/10 | #62, #63 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 81.9 | 219 | ±55.1 | ahead | 10/10 | #62, #63 |
| issue_256_audio_24khz | `video.e2e_delay_p95_ms` | 205 | 227 | ±19.1 | ahead | 6/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 2165 | 1759 | ±108 | behind | 10/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 2354 | 1949 | ±118 | behind | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1111 | 875 | ±186 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 364 | 72.5 | ±18.2 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 1997 | 2643 | ±377 | ahead | 10/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.3 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 698 | 307 | ±100 | behind | 10/10 | #77 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 808 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 5 | 3.7 | ±1.1 | behind | 7/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 145 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 145 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `audio.jb_delay_p95_ms` | 77 | 64 | ±10 | behind | 4/10 | #62, #63 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.1 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.1 | 32.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `sync.av_offset_p95_ms` | 1335 | 1832 | ±467 | ahead | 7/10 | #77 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 1623 | 2259 | ±508 | ahead | 7/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `cc.rtt_p50_ms` | 85.3 | 70.8 | ±10 | behind | 2/10 | #61 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.3 | 39.8 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 133 | 163 | ±24.5 | ahead | 8/10 | #62, #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 233 | 282 | ±32.0 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 97.4 | 131 | ±22.5 | ahead | 7/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 134 | 156 | ±17.1 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `sync.av_offset_p95_ms` | 79.9 | 110 | ±27.3 | ahead | 9/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| outage_3s | `audio.jb_delay_p95_ms` | 141 | 119 | ±19.1 | behind | 4/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 243 | 202 | ±20.3 | behind | 5/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 63.4 | 40.2 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 159 | 113 | ±10 | behind | 5/10 | #24, #37, #20 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| satellite | `repair.overhead_pct` | 14.9 | 18.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `repair.overhead_pct` | 36.0 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 36.0 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `sync.av_offset_p95_ms` | 1335 | 1832 | ±467 | ahead | 7/10 | #77, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 1623 | 2259 | ±508 | ahead | 7/10 | #24, #37, #20 |

