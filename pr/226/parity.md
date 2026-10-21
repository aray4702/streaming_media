## Parity with libwebrtc M157.8103@{#0}

80 scenarios, mean of 10 seeds. **262 gaps**: 144 behind libwebrtc, 118 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

322 gaps on `main`, 262 here: **138 closed, 78 opened**, 5 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.concealed_pct` | 1.6 | 0.5 | ±0.5 | behind | 9/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 117 | 147 | ±10 | ahead | 10/10 | #62, #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 155 | 176 | ±20.4 | ahead | 6/10 | #62, #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 75.2 | 115 | ±17.8 | ahead | 10/10 | #62, #63 |
| burst_loss | `audio.concealed_pct` | 2.0 | 1.3 | ±0.5 | behind | 6/10 | #62, #63 |
| burst_loss | `audio.mouth_to_ear_p95_ms` | 185 | 248 | ±56.2 | ahead | 9/10 | #62, #63 |
| burst_loss | `audio.jb_delay_p95_ms` | 83.6 | 157 | ±45.7 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.mouth_to_ear_p95_ms` | 663 | 794 | ±39.7 | ahead | 10/10 | #62, #63 |
| capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| clean_link | `audio.jb_delay_p95_ms` | 84.8 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 446 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| cross_traffic | `audio.jb_delay_p95_ms` | 182 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| high_jitter | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63, #19, #24 |
| high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 111 | ±11.0 | ahead | 10/10 | #62, #63, #19, #24 |
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 363 | 525 | ±63.8 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 170 | 314 | ±23.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.mouth_to_ear_p95_ms` | 663 | 794 | ±39.7 | ahead | 10/10 | #62, #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 155 | 176 | ±20.4 | ahead | 6/10 | #62, #63 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 75.2 | 115 | ±17.8 | ahead | 10/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.concealed_pct` | 2.0 | 1.3 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.mouth_to_ear_p95_ms` | 185 | 248 | ±56.2 | ahead | 9/10 | #62, #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 83.6 | 157 | ±45.7 | ahead | 10/10 | #62, #63 |
| issue_124_clean_link_v2 | `audio.jb_delay_p95_ms` | 84.8 | 133 | ±21.9 | ahead | 38/40 | #62, #63 |
| issue_126_audio_v2 | `audio.jb_delay_p95_ms` | 18 | 36 | ±10 | ahead | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.concealed_pct` | 0.8 | 0.2 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 299 | 332 | ±33.5 | ahead | 5/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 89.6 | 144 | ±29.5 | ahead | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.concealed_pct` | 1.0 | 0.1 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.jb_delay_p95_ms` | 72 | 99 | ±10 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.concealed_pct` | 2.0 | 1.3 | ±0.5 | behind | 6/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.mouth_to_ear_p95_ms` | 185 | 248 | ±56.2 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 83.6 | 157 | ±45.7 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 60 | 85 | ±10.8 | ahead | 9/10 | #62, #63 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 81.6 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 9 | 43 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4 | 7 | ±2.7 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 5628 | 10663 | ±3593 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 2329 | 3230 | ±749 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `audio.jb_delay_p95_ms` | 66 | 101 | ±20.2 | ahead | 8/10 | #62, #63 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 18 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 18.4 | 42 | ±10 | ahead | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 775 | 736 | ±38.8 | ahead | 5/10 | #37, #61 |
| issue_150_gcc_v2_high_jitter | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 40/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 83.6 | 106 | ±10.3 | ahead | 39/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.concealed_pct` | 1.3 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed10 | `audio.jb_delay_p95_ms` | 83.2 | 112 | ±14.8 | ahead | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 78.4 | 108 | ±11.4 | ahead | 10/10 | #62, #63 |
| issue_151_outage_3s_v2 | `audio.jb_delay_p95_ms` | 90.8 | 119 | ±19.1 | ahead | 8/10 | #62, #63 |
| issue_151_outage_3s_v2 | `video.freeze_total_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.longest_freeze_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 154 | 113 | ±10 | behind | 4/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 436 | 641 | ±39.5 | ahead | 10/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 173 | 377 | ±21.5 | ahead | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 22.4 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.e2e_delay_p95_ms` | 1530 | 2624 | ±988 | ahead | 9/10 | #24, #37 |
| issue_15_aimd_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 446 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 182 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `audio.jb_delay_p95_ms` | 66 | 101 | ±20.2 | ahead | 8/10 | #62, #63 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_18_random_loss_keyframes | `audio.jb_delay_p95_ms` | 66 | 101 | ±20.2 | ahead | 8/10 | #62, #63 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 22.4 | 40 | ±10 | ahead | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 1530 | 2624 | ±988 | ahead | 9/10 | #24, #37 |
| issue_23_capacity_drop | `audio.mouth_to_ear_p95_ms` | 663 | 794 | ±39.7 | ahead | 10/10 | #62, #63 |
| issue_23_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 363 | 525 | ±63.8 | ahead | 10/10 | #62, #63 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 170 | 314 | ±23.7 | ahead | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 655 | 778 | ±68.6 | ahead | 9/10 | #62, #63 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 90.8 | 119 | ±19.1 | ahead | 8/10 | #62, #63 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.longest_freeze_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 154 | 113 | ±10 | behind | 4/10 | #24, #37 |
| issue_24_timing_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| issue_24_timing_v2_high_jitter | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 201 | 229 | ±22.9 | ahead | 7/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 655 | 778 | ±68.6 | ahead | 9/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 201 | 229 | ±22.9 | ahead | 7/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 446 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 182 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.jb_delay_p95_ms` | 78.8 | 116 | ±11.7 | ahead | 10/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.concealed_pct` | 2.3 | 1.6 | ±0.6 | behind | 6/10 | #62, #63 |
| issue_5_burst15_freeze | `audio.jb_delay_p95_ms` | 85.2 | 135 | ±48.8 | ahead | 8/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 661 | 795 | ±39.8 | ahead | 10/10 | #62, #63 |
| issue_5_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 495 | ±33.8 | ahead | 10/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 60 | 85 | ±10.8 | ahead | 9/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `audio.jb_delay_p95_ms` | 62.4 | 88 | ±24.9 | ahead | 7/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 363 | 525 | ±63.8 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 170 | 314 | ±23.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.concealed_pct` | 1.6 | 0.9 | ±0.5 | behind | 7/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 119 | 142 | ±20.4 | ahead | 3/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.mouth_to_ear_p95_ms` | 652 | 765 | ±48.5 | ahead | 9/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.jb_delay_p95_ms` | 352 | 484 | ±38.7 | ahead | 10/10 | #62, #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.rtt_p50_ms` | 85.3 | 70.8 | ±10 | behind | 2/10 | #61 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 153 | 188 | ±27.9 | ahead | 9/10 | #62, #63 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 58.8 | 112 | ±26.2 | ahead | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.concealed_pct` | 1.2 | 0.2 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 81.6 | 111 | ±11.0 | ahead | 10/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `audio.jb_delay_p95_ms` | 66 | 101 | ±20.2 | ahead | 8/10 | #62, #63 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 162 | 224 | ±46.2 | ahead | 10/10 | #62, #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 78.4 | 152 | ±33.3 | ahead | 10/10 | #62, #63 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 655 | 778 | ±68.6 | ahead | 9/10 | #62, #63 |
| outage_3s | `audio.jb_delay_p95_ms` | 90.8 | 119 | ±19.1 | ahead | 8/10 | #62, #63, #20 |
| outage_3s | `video.freeze_total_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24, #20 |
| outage_3s | `video.longest_freeze_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 154 | 113 | ±10 | behind | 4/10 | #24, #37, #20 |
| pacer_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 663 | 794 | ±39.7 | ahead | 10/10 | #62, #63 |
| pacer_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.mouth_to_ear_p95_ms` | 663 | 794 | ±39.7 | ahead | 10/10 | #62, #63 |
| probe_v2_capacity_drop | `audio.jb_delay_p95_ms` | 352 | 496 | ±34.1 | ahead | 10/10 | #62, #63 |
| probe_v2_clean_link | `audio.jb_delay_p95_ms` | 84.8 | 130 | ±24.0 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 446 | 649 | ±51.0 | ahead | 10/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 182 | 382 | ±28.5 | ahead | 10/10 | #62, #63 |
| random_loss_5pct | `audio.concealed_pct` | 1.1 | 0.3 | ±0.5 | behind | 9/10 | #62, #63 |
| random_loss_5pct | `audio.jb_delay_p95_ms` | 66 | 101 | ±20.2 | ahead | 8/10 | #62, #63 |
| random_loss_5pct | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| satellite | `audio.jb_delay_p95_ms` | 96.8 | 164 | ±57.6 | ahead | 9/10 | #62, #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 159 | 203 | ±20 | ahead | 10/10 | #62, #63 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 363 | 525 | ±63.8 | ahead | 10/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 170 | 314 | ±23.7 | ahead | 10/10 | #62, #63, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63, #19, #24 |
| high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_120_rtx_payload_padding_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| issue_123_v2_allocation_min | `audio.mouth_to_ear_p95_ms` | 552 | 499 | ±27.6 | behind | 7/10 | #62, #63 |
| issue_124_burst_loss_v2 | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| issue_126_audio_v2 | `audio.mouth_to_ear_p95_ms` | 96.6 | 71.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_126_audio_v2 | `video.e2e_delay_p95_ms` | 77.3 | 59.0 | ±10 | behind | 10/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.mouth_to_ear_p95_ms` | 204 | 190 | ±10.2 | behind | 7/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 76.4 | 195 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 167 | 151 | ±11.6 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2194 | 3204 | ±494 | ahead | 9/10 | #77 |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 98.3 | 75.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 245 | 149 | ±12.2 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 99.1 | 77.1 | ±10 | behind | 9/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 359 | 130 | ±18.0 | behind | 10/10 | #77 |
| issue_150_gcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 223 | 206 | ±11.2 | behind | 29/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 225 | 208 | ±11.2 | behind | 7/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `sync.av_offset_p95_ms` | 79.5 | 51.4 | ±15.9 | behind | 5/10 | #77 |
| issue_151_outage_3s_v2 | `audio.mouth_to_ear_p95_ms` | 218 | 182 | ±20.3 | behind | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 96.5 | 73.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 3120 | 1610 | ±985 | behind | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 9.1 | 8.0 | ±1.0 | behind | 7/10 | #21, #61, #12 |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 96.5 | 73.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 3120 | 1610 | ±985 | behind | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 9.1 | 8.0 | ±1.0 | behind | 7/10 | #21, #61, #12 |
| issue_23_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| issue_23_handover | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_23_handover | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_23_handover | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 218 | 182 | ±20.3 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2166 | 1759 | ±108 | behind | 10/10 | #77 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 1435 | 1759 | ±107 | ahead | 9/10 | #77 |
| issue_5_bitrate_adjuster | `sync.av_offset_p95_ms` | 21.4 | 43.5 | ±10 | ahead | 8/10 | #77 |
| issue_5_burst15_freeze | `sync.av_offset_p95_ms` | 67.0 | 102 | ±30.1 | ahead | 10/10 | #77 |
| issue_5_keyframe_flush | `sync.av_offset_p95_ms` | 347 | 289 | ±37.0 | behind | 8/10 | #77 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 167 | 151 | ±11.6 | behind | 7/10 | #62, #63 |
| issue_62_neteq_default | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| issue_62_neteq_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 209 | 737 | ±198 | ahead | 10/10 | #77 |
| issue_76_session_layer_v2 | `sync.av_offset_p95_ms` | 49.3 | 80.9 | ±23.2 | ahead | 7/10 | #77 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #62, #63 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 86.7 | 131 | ±22.5 | ahead | 10/10 | #77 |
| lossy_feedback | `sync.av_offset_p95_ms` | 80.4 | 110 | ±27.3 | ahead | 8/10 | #77 |
| low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 218 | 182 | ±20.3 | behind | 10/10 | #62, #63, #20 |
| pacer_v2_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| probe_v2_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| random_loss_5pct | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| satellite | `sync.av_offset_p95_ms` | 454 | 497 | ±29.2 | ahead | 8/10 | #77 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 552 | 499 | ±27.6 | behind | 7/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 552 | 499 | ±27.6 | behind | 7/10 | #62, #63 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24, #20 |
| wifi_to_lte_handover | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 75.5 | 64.4 | ±10 | behind | 7/10 | #62, #63 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 196 | 171 | ±12.4 | behind | 10/10 | #62, #63 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.jb_delay_p95_ms` | 37 | 10.2 (closer) |
| capacity_drop | `video.e2e_delay_p95_ms` | 184 | 164 (closer) |
| capacity_drop | `cc.rtt_p50_ms` | 294 | 240 (closer) |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 184 | 164 (closer) |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 294 | 240 (closer) |
| issue_12_lossbwe_congestive | `video.render_fps` | 4.0 | 3.3 (closer) |
| issue_12_lossbwe_congestive | `net.loss_pct` | 1.6 | 1.2 (closer) |
| issue_12_lossbwe_shallow | `net.loss_pct` | 0.6 | 0.9 (further) |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 195 | 173 (closer) |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 2.3 | 2.8 (further) |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 7.3 | 5.7 (closer) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 10.5 | 9.2 (closer) |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 24.1 | 10.7 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.1 | 3.5 (closer) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.1 | 3.5 (closer) |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 184 | 164 (closer) |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 294 | 240 (closer) |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 129 | 33 (closer) |
| issue_23_outage_3s | `cc.queue_p95_ms` | 24.1 | 10.7 (closer) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 74.4 | 37.1 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 72.2 | 34.6 (closer) |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 28.7 | 41.9 (further) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 48 | 27.2 (closer) |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 235 | 187 (closer) |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 129 | 33 (closer) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 48 | 27.2 (closer) |
| issue_53_cwnd_pushback | `video.freeze_count` | 4.0 | 5.7 (further) |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1900 | 2515 (further) |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 249 | 190 (closer) |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 178 | 157 (closer) |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 292 | 236 (closer) |
| issue_61_googcc_glue_congestive | `video.render_fps` | 4.0 | 3.3 (closer) |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 1.6 | 1.2 (closer) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 730 | 812 (further) |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 68.9 | 32.6 (closer) |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 69.8 | 32.6 (closer) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 20.7 | 24.0 (further) |
| lossy_feedback | `video.e2e_delay_p95_ms` | 30.8 | 18.0 (closer) |
| low_bandwidth | `audio.jb_delay_p95_ms` | 129 | 33 (closer) |
| outage_3s | `cc.queue_p95_ms` | 24.1 | 10.7 (closer) |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 184 | 164 (closer) |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 294 | 240 (closer) |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 184 | 164 (closer) |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 294 | 240 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| burst_loss | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| burst_loss | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| capacity_drop | `cc.rtt_p50_ms` | 312 | 72.2 | ±15.6 | behind | 10/10 | #61 |
| capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| capacity_drop | `video.e2e_delay_p95_ms` | 223 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| capacity_drop | `video.freeze_total_ms` | 1986 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| cross_traffic | `cc.rtt_p50_ms` | 87.3 | 64.5 | ±10 | behind | 1/10 | #61 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63, #19, #24 |
| high_jitter | `repair.overhead_pct` | 34.2 | 43.7 | ±2.5 | ahead | 9/10 | #21, #7, #29, #19, #24 |
| high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 205 | 150 | ±25.7 | behind | 9/10 | #24, #37, #19 |
| high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 743 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 312 | 72.2 | ±15.6 | behind | 10/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 223 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1986 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.mouth_to_ear_p95_ms` | 552 | 499 | ±27.6 | behind | 7/10 | #62, #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.2 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 12.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| issue_124_burst_loss_v2 | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| issue_126_audio_v2 | `audio.mouth_to_ear_p95_ms` | 96.6 | 71.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_126_audio_v2 | `video.e2e_delay_p95_ms` | 77.3 | 59.0 | ±10 | behind | 10/10 | #24, #37 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 3 | 11.8 | ±6.3 | ahead | 9/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 852 | 3072 | ±1628 | ahead | 9/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `audio.mouth_to_ear_p95_ms` | 204 | 190 | ±10.2 | behind | 7/10 | #62, #63 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 212 | 185 | ±13.7 | behind | 8/10 | #24, #37 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.longest_freeze_ms` | 76.4 | 195 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_burst_loss | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 167 | 151 | ±11.6 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 101 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.0 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `video.render_fps` | 26.6 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 34.2 | 43.7 | ±2.5 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 205 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 75.5 | 64.4 | ±10 | behind | 7/10 | #62, #63 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2194 | 3204 | ±494 | ahead | 9/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2259 | 3282 | ±495 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 294 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.4 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 93.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 98.3 | 75.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 7.2 | 6.3 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 245 | 149 | ±12.2 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 320 | 226 | ±16.0 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 0.1 | 14.8 | ±5.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 36.7 | 2820 | ±1084 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 57.9 | 217 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 99.1 | 77.1 | ±10 | behind | 9/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 59.6 | 56.3 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.2 | 6.7 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.6 | 12.7 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 359 | 130 | ±18.0 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 435 | 209 | ±21.7 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.1 | 4.3 | ±2.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 18.7 | 842 | ±556 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 37.9 | 211 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 169 | 92.4 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 169 | 92.4 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_150_gcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 223 | 206 | ±11.2 | behind | 29/40 | #62, #63 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.5 | 43.7 | ±3.2 | ahead | 36/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 188 | 148 | ±15.6 | behind | 34/40 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 180 | 96.5 | ±50 | behind | 23/40 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 37.9 | 43.6 | ±3.8 | ahead | 8/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 189 | 144 | ±10 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.time_to_first_frame_ms` | 179 | 88.6 | ±50 | behind | 6/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 225 | 208 | ±11.2 | behind | 7/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 35.7 | 44.9 | ±3.3 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `sync.av_offset_p95_ms` | 79.5 | 51.4 | ±15.9 | behind | 5/10 | #77 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.e2e_delay_p95_ms` | 206 | 147 | ±26.1 | behind | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 196 | 89.4 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `audio.mouth_to_ear_p95_ms` | 218 | 182 | ±20.3 | behind | 10/10 | #62, #63 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 50.9 | 40.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `cc.rtt_p50_ms` | 87.8 | 65.7 | ±10 | behind | 1/10 | #61 |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 96.5 | 73.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 9.1 | 8.0 | ±1.0 | behind | 7/10 | #21, #61, #12 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.6 | 7.1 | ±3.3 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.longest_freeze_ms` | 3120 | 1610 | ±985 | behind | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 87.3 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 294 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.4 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 93.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 294 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.4 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 93.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.2 | 43.7 | ±2.5 | ahead | 9/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 205 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 96.5 | 73.1 | ±10 | behind | 10/10 | #62, #63 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 9.1 | 8.0 | ±1.0 | behind | 7/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.6 | 7.1 | ±3.3 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.longest_freeze_ms` | 3120 | 1610 | ±985 | behind | 8/10 | #5, #37, #24 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 312 | 72.2 | ±15.6 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 223 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 1986 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 28.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_handover | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 743 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_23_handover | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 320 | 353 | ±29.5 | ahead | 7/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 2404 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 218 | 182 | ±20.3 | behind | 10/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 50.9 | 40.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 151 | ±19.7 | ahead | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 190 | 227 | ±33.5 | ahead | 7/10 | #62, #63 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 138 | 180 | ±27.1 | ahead | 8/10 | #24, #37 |
| issue_24_timing_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 34.2 | 43.7 | ±2.5 | ahead | 9/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 101 | 128 | ±23.0 | ahead | 7/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2166 | 1759 | ±108 | behind | 10/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2352 | 1949 | ±118 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1062 | 875 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 320 | 353 | ±29.5 | ahead | 7/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2404 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 101 | 128 | ±23.0 | ahead | 7/10 | #62, #63 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 1435 | 1759 | ±107 | ahead | 9/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1608 | 1949 | ±107 | ahead | 9/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 13 | 7.3 | ±2.1 | behind | 9/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 5294 | 2779 | ±615 | behind | 10/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1065 | 875 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 18.0 | 27.4 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 87.3 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_5_bitrate_adjuster | `audio.mouth_to_ear_p95_ms` | 196 | 171 | ±12.4 | behind | 10/10 | #62, #63 |
| issue_5_bitrate_adjuster | `sync.av_offset_p95_ms` | 21.4 | 43.5 | ±10 | ahead | 8/10 | #77 |
| issue_5_burst15_freeze | `sync.av_offset_p95_ms` | 67.0 | 102 | ±30.1 | ahead | 10/10 | #77 |
| issue_5_burst15_freeze | `video.e2e_delay_p95_ms` | 191 | 263 | ±36.6 | ahead | 10/10 | #24, #37 |
| issue_5_burst15_freeze | `video.freeze_count` | 3.6 | 1.5 | ±1.4 | behind | 8/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.freeze_total_ms` | 903 | 361 | ±330 | behind | 8/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 308 | 72.5 | ±15.4 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `sync.av_offset_p95_ms` | 347 | 289 | ±37.0 | behind | 8/10 | #77 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 235 | 392 | ±123 | ahead | 10/10 | #24, #37 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2026 | 2643 | ±377 | ahead | 10/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 167 | 151 | ±11.6 | behind | 7/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 101 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.0 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 26.6 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_62_neteq_default | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 87.3 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_62_neteq_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 34.2 | 43.7 | ±2.5 | ahead | 9/10 | #21, #7, #29 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_62_neteq_v2_high_jitter | `video.e2e_delay_p95_ms` | 205 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_62_opus_fec_low_rate | `audio.jb_delay_p95_ms` | 74.6 | 64 | ±10 | behind | 7/10 | #62, #63 |
| issue_62_opus_fec_low_rate | `audio.mouth_to_ear_p95_ms` | 146 | 125 | ±10 | behind | 10/10 | #62, #63 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.1 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.3 | 32.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 180 | 149 | ±10 | behind | 9/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 743 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_handover | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 209 | 737 | ±198 | ahead | 10/10 | #77 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 409 | 1221 | ±198 | ahead | 10/10 | #24, #37 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.4 | 39.8 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `sync.av_offset_p95_ms` | 49.3 | 80.9 | ±23.2 | ahead | 7/10 | #77 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 179 | 156 | ±10 | behind | 10/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 224 | 212 | ±11.2 | behind | 6/10 | #62, #63 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 34.2 | 43.7 | ±2.5 | ahead | 9/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 79.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 205 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 294 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.4 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 93.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 130 | 163 | ±24.5 | ahead | 8/10 | #62, #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 230 | 262 | ±32.0 | ahead | 7/10 | #62, #63 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 86.7 | 131 | ±22.5 | ahead | 10/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 180 | 156 | ±17.1 | behind | 8/10 | #24, #37 |
| lossy_feedback | `sync.av_offset_p95_ms` | 80.4 | 110 | ±27.3 | ahead | 8/10 | #77 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 101 | 119 | ±15.5 | ahead | 6/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 320 | 353 | ±29.5 | ahead | 7/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 2404 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 218 | 182 | ±20.3 | behind | 10/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 50.9 | 40.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 312 | 72.2 | ±15.6 | behind | 10/10 | #61 |
| pacer_v2_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 223 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 1986 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 312 | 72.2 | ±15.6 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `sync.av_offset_p95_ms` | 355 | 286 | ±33.0 | behind | 8/10 | #77 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 223 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 1986 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 87.3 | 64.5 | ±10 | behind | 1/10 | #61 |
| random_loss_5pct | `sync.av_offset_p95_ms` | 165 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 294 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.4 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 93.9 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 14.9 | 18.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| satellite | `sync.av_offset_p95_ms` | 454 | 497 | ±29.2 | ahead | 8/10 | #77 |
| satellite | `video.e2e_delay_p95_ms` | 875 | 927 | ±46.4 | ahead | 6/10 | #24, #37 |
| satellite | `video.freeze_count` | 20 | 30.6 | ±4.7 | ahead | 7/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6369 | 13227 | ±2008 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.8 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 552 | 499 | ±27.6 | behind | 7/10 | #62, #63 |
| very_low_capacity | `repair.overhead_pct` | 35.2 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 12.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 2.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 552 | 499 | ±27.6 | behind | 7/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.2 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 12.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1239 | 1422 | ±165 | ahead | 2/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 49.9 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.2 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `sync.av_offset_p95_ms` | 436 | 1832 | ±467 | ahead | 10/10 | #77, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 743 | 2259 | ±508 | ahead | 10/10 | #24, #37, #20 |
| wifi_to_lte_handover | `video.freeze_count` | 5.4 | 4.1 | ±1.3 | behind | 6/10 | #5, #37, #24, #20 |

