## Parity with libwebrtc M157.8103@{#0}

98 scenarios, mean of 10 seeds. **110 gaps**: 43 behind libwebrtc, 67 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

116 gaps on `main`, 110 here: **16 closed, 10 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 238 | 275 | ±32.3 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.8 | 73.4 | ±23.1 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 40.1 | 43.7 | ±3.2 | ahead | 24/40 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 94.0 | ±17.0 | ahead | 7/10 | #77 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 47.6 | 64.0 | ±16.1 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `sync.av_offset_p95_ms` | 96.5 | 120 | ±21.0 | ahead | 8/10 | #77 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.8 | 3.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.8 | 3.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 1848 | 1952 | ±97.6 | ahead | 5/10 | #63, #224 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 34.4 | 37.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 1848 | 1952 | ±97.6 | ahead | 5/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.8 | 3.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.0 | 32.1 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.8 | 39.8 | ±2 | ahead | 4/10 | #21, #7, #29 |
| lossy_feedback | `sync.av_offset_p95_ms` | 76.3 | 107 | ±25.3 | ahead | 8/10 | #77 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 1848 | 1952 | ±97.6 | ahead | 5/10 | #63, #224 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| high_jitter | `repair.overhead_pct` | 3.2 | 2.7 (closer) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 3.7 | 3.1 (closer) |
| issue_123_v2_allocation_min | `video.render_fps` | 3.8 | 3.2 (closer) |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 35.7 | 47.3 (further) |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 37 | 47 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 3.2 | 2.7 (closer) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 299 | 232 (closer) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 299 | 232 (closer) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 524 | 597 (further) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 5.7 | 3.7 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 1069 | 653 (closer) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 14.5 | 11.4 (closer) |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 106 | 130 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 20.4 | 23.8 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 28.3 | 33.9 (further) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 3.2 | 2.7 (closer) |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 35.7 | 47.3 (further) |
| issue_224_congestive_e2e_flip | `audio.jb_delay_p95_ms` | 37 | 47 (further) |
| issue_23_handover | `repair.overhead_pct` | 3.7 | 3.1 (closer) |
| issue_23_low_bandwidth | `video.render_fps` | 1.5 | 1.7 (further) |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 406 | 364 (closer) |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 61.6 | 40.8 (closer) |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 62 | 40.9 (closer) |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 3.2 | 2.7 (closer) |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 61.6 | 40.8 (closer) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 62 | 40.9 (closer) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 25 | 29 (further) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 78.2 | 95.5 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 3.6 (closer) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 6.0 | 4.9 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.5 | 1.7 (further) |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 406 | 364 (closer) |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 61.6 | 40.8 (closer) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 62 | 40.9 (closer) |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 35.7 | 47.3 (further) |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 37 | 47 (further) |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 3.2 | 2.7 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 3.7 | 3.1 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 523 | 371 (closer) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 3.2 | 2.7 (closer) |
| issue_85_video_pause | `cc.queue_p95_ms` | 11.3 | 12.7 (further) |
| low_bandwidth | `video.render_fps` | 1.5 | 1.7 (further) |
| low_bandwidth | `sync.av_offset_p95_ms` | 406 | 364 (closer) |
| very_low_capacity | `video.render_fps` | 3.8 | 3.2 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 3.8 | 3.2 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 3.7 | 3.1 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| high_jitter | `repair.overhead_pct` | 41.1 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.4 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.4 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.7 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 239 | 192 | ±33.9 | behind | 5/10 | #63, #224 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 325 | 278 | ±34.3 | behind | 6/10 | #63, #224 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.8 | 3.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 41.1 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 615 | 847 | ±208 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 657 | 888 | ±208 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2685 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 112 | 127 | ±14.4 | ahead | 6/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 214 | 226 | ±11.3 | ahead | 3/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 5.2 | 8.9 | ±3.3 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 1087 | 1740 | ±635 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 79.3 | 113 | ±10 | ahead | 8/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 185 | 209 | ±13.5 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.9 | 3.6 | ±2.4 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 174 | 742 | ±482 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 108 | 238 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2915 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2917 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2915 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2917 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 41.1 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_224_congestive_e2e_flip | `audio.jb_delay_p95_ms` | 239 | 192 | ±33.9 | behind | 5/10 | #63 |
| issue_224_congestive_e2e_flip | `audio.mouth_to_ear_p95_ms` | 325 | 278 | ±34.3 | behind | 6/10 | #63 |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.8 | 3.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_23_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_23_handover | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 1848 | 1952 | ±97.6 | ahead | 5/10 | #63, #224 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1401 | ±199 | behind | 7/10 | #77 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.2 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 118 | 151 | ±19.7 | ahead | 10/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 193 | 247 | ±33.5 | ahead | 10/10 | #63, #218 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 558 | 517 | ±33.0 | behind | 6/10 | #63, #224 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 679 | 638 | ±34.0 | behind | 6/10 | #63, #224 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 41.1 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 558 | 517 | ±33.0 | behind | 6/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 679 | 638 | ±34.0 | behind | 6/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 720 | 119 | ±36 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 50 | 21 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1036 | 201 | ±51.8 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 267 | 65.3 | ±13.3 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 11.8 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.4 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2115 | 69.5 | ±106 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1524 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2944 | 121 | ±147 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.9 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2478 | 0 | ±124 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.6 | 0 | ±0.5 | behind | 4/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1353 | 18.9 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.4 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 149 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2915 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2917 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 34.9 | 39.8 | ±2.6 | ahead | 8/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1131 | 1073 | ±56.6 | ahead | 5/10 | #37, #61 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 34.4 | 37.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 1848 | 1952 | ±97.6 | ahead | 5/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1401 | ±199 | behind | 7/10 | #77 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.2 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 558 | 517 | ±33.0 | behind | 6/10 | #63, #224 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 679 | 638 | ±34.0 | behind | 6/10 | #63, #224 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.1 | 36.7 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 239 | 192 | ±33.9 | behind | 5/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 325 | 278 | ±34.3 | behind | 6/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.8 | 3.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 41.1 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.0 | 32.1 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1638 | 1268 | ±223 | behind | 7/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.8 | 39.8 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 41.1 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 55.0 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 126 | 163 | ±26.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 222 | 283 | ±33.5 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 44.0 | 56.6 | ±10 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.9 | 118 | ±17.0 | ahead | 8/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 132 | 167 | ±20.6 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `sync.av_offset_p95_ms` | 76.3 | 107 | ±25.3 | ahead | 8/10 | #77 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 1848 | 1952 | ±97.6 | ahead | 5/10 | #63, #224 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1401 | ±199 | behind | 7/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.2 | ±1.4 | ahead | 6/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 35.4 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 10.4 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.4 | 55.6 | ±11.1 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.4 | 7.2 | ±1.6 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |

