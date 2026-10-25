## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **108 gaps**: 42 behind libwebrtc, 66 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

104 gaps on `main`, 108 here: **37 closed, 41 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63, #19, #24 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.5 | 31.9 | ±3.0 | ahead | 5/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 207 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `repair.overhead_pct` | 20.4 | 24.1 | ±3.2 | ahead | 6/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_204_cross_traffic_join | `video.freeze_count` | 4.7 | 2.3 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 530 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 256 | 231 | ±21.4 | behind | 4/10 | #77 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 20.4 | 24.1 | ±3.2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 28.5 | 31.9 | ±3.0 | ahead | 5/10 | #21, #7, #29 |
| issue_24_timing_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_29_ulpfec_over_red | `video.longest_freeze_ms` | 3529 | 3298 | ±176 | behind | 1/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `cc.rtt_p50_ms` | 96.5 | 86.4 | ±10 | behind | 6/10 | #61 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.7 | 2.3 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 530 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 256 | 231 | ±21.4 | behind | 4/10 | #77 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2877 | 3104 | ±187 | ahead | 5/10 | #5, #37, #24 |
| issue_62_neteq_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.5 | 31.9 | ±3.0 | ahead | 5/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.5 | 39.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 260 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 4270 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 195 | 236 | ±20.0 | ahead | 8/10 | #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 53.8 | 81.5 | ±19.1 | ahead | 8/10 | #77 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 16.0 | 18.0 | ±2 | ahead | 5/10 | #21, #7, #29 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.5 | 31.9 | ±3.0 | ahead | 5/10 | #21, #7, #29, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 568 | 386 | ±176 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1490 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 425 | 737 | ±264 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 384 | 695 | ±265 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2214 | 3118 | ±823 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_policer | `net.loss_pct` | 7.8 | 10.1 | ±1.6 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 32.0 | 56.5 | ±16.7 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.4 | 3.5 | ±2.8 | ahead | 9/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 152 | 139 | ±10 | behind | 4/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.3 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 2328 | 5004 | ±2441 | ahead | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.pause_total_ms` | 5568 | 2229 | ±2885 | behind | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.7 | 0.6 | ±0.5 | behind | 9/10 | #23 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 2328 | 5004 | ±2441 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 5568 | 2229 | ±2885 | behind | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.7 | 0.6 | ±0.5 | behind | 9/10 | #23 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1490 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 58.2 | 45.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 244 | 206 | ±35.5 | behind | 6/10 | #63 |
| issue_256_audio_24khz | `cc.queue_p95_ms` | 49.4 | 35.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_298_audio_24khz_plays | `audio.mouth_to_ear_p95_ms` | 244 | 206 | ±35.5 | behind | 6/10 | #63 |
| issue_298_audio_24khz_plays | `cc.queue_p95_ms` | 49.4 | 35.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1231 | 1566 | ±328 | ahead | 8/10 | #77 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_348_scaling_outage_500ms | `cc.queue_p95_ms` | 58.2 | 45.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1490 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_count` | 2 | 2.9 | ±0.9 | ahead | 7/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1391 | 1543 | ±116 | ahead | 4/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 300 | 284 | ±15.0 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_84_encoder_qp_360p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 7/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `audio.jb_delay_p95_ms` | 128 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 100 | 114 | ±10 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 7/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `audio.jb_delay_p95_ms` | 128 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 100 | 114 | ±10 | ahead | 6/10 | #77 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.freeze_count` | 4.7 | 4.2 (closer) |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 568 | 638 (further) |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 124 | 111 (closer) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 22.6 | 32.3 (further) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 723 | 254 (closer) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 247 | 273 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.6 | 1.1 (closer) |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1815 | 1261 (closer) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 723 | 254 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.2 | 2.5 (further) |
| very_low_capacity | `video.freeze_count` | 4.7 | 4.2 (closer) |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 4.7 | 4.2 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.7 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 24.5 | 20.3 | ±3.9 | behind | 7/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 12.0 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 36.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.longest_freeze_ms` | 568 | 386 | ±176 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1490 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 384 | 695 | ±265 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 425 | 737 | ±264 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `net.loss_pct` | 7.8 | 10.1 | ±1.6 | ahead | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 32.0 | 56.5 | ±16.7 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2214 | 3118 | ±823 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.6 | 7 | ±3.1 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 556 | 1401 | ±595 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 85.1 | 117 | ±10 | ahead | 9/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 182 | 214 | ±10.7 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.4 | 3.5 | ±2.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 111 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 125 | 236 | ±100 | ahead | 7/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.3 | 43.8 | ±4.1 | ahead | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 152 | 139 | ±10 | behind | 4/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.4 | 6.8 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_total_ms` | 2328 | 5004 | ±2441 | ahead | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.7 | 0.6 | ±0.5 | behind | 9/10 | #23 |
| issue_154_shallow_drop_repair_v2 | `video.pause_total_ms` | 5568 | 2229 | ±2885 | behind | 8/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1602 | 1349 | ±247 | behind | 2/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.4 | 6.8 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 2328 | 5004 | ±2441 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.7 | 0.6 | ±0.5 | behind | 9/10 | #23 |
| issue_21_shallow_drop_repair | `video.pause_total_ms` | 5568 | 2229 | ±2885 | behind | 8/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1490 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1958 | 2139 | ±180 | ahead | 6/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 139 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 58.2 | 45.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 244 | 206 | ±35.5 | behind | 6/10 | #63 |
| issue_256_audio_24khz | `cc.queue_p95_ms` | 49.4 | 35.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 398 | 125 | ±19.9 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 628 | 205 | ±31.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 3720 | 300 | ±500 | behind | 9/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 211 | 43.5 | ±10.6 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.utilization_pct` | 75.1 | 85.7 | ±4.3 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 0.5 | 0 | ±0.5 | behind | 3/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1043 | 57.0 | ±52.2 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1440 | 2002 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1310 | 122 | ±65.5 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 4.5 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2913 | 0 | ±146 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.1 | 0 | ±0.5 | behind | 7/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1105 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 23.2 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 281 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_298_audio_24khz_plays | `audio.mouth_to_ear_p95_ms` | 244 | 206 | ±35.5 | behind | 6/10 | #63 |
| issue_298_audio_24khz_plays | `cc.queue_p95_ms` | 49.4 | 35.9 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1261 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.4 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 296 | 416 | ±66.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 148 | 256 | ±58.7 | ahead | 10/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1231 | 1566 | ±328 | ahead | 8/10 | #77 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.9 | 25.5 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1602 | 1349 | ±247 | behind | 2/10 | #5, #37, #24 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_348_scaling_outage_500ms | `cc.queue_p95_ms` | 58.2 | 45.2 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1958 | 2139 | ±180 | ahead | 6/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 139 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1490 | 2500 | ±958 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.1 | 32.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 300 | 284 | ±15.0 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1391 | 1543 | ±116 | ahead | 4/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `video.freeze_count` | 2 | 2.9 | ±0.9 | ahead | 7/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_84_encoder_qp_360p_low_rate | `audio.jb_delay_p95_ms` | 128 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 7/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 100 | 114 | ±10 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `audio.jb_delay_p95_ms` | 128 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 7/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 100 | 114 | ±10 | ahead | 6/10 | #77 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1958 | 2139 | ±180 | ahead | 6/10 | #63, #224 |
| low_bandwidth | `cc.rtt_p50_ms` | 139 | 238 | ±80.3 | ahead | 8/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 34.7 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 24.5 | 20.3 | ±3.9 | behind | 7/10 | #5, #37, #24 |
| very_low_capacity | `video.render_fps` | 12.0 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.7 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 24.5 | 20.3 | ±3.9 | behind | 7/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 12.0 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 318 | 293 | ±24.5 | behind | 6/10 | #63, #20 |

