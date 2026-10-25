## Parity with libwebrtc M157.8103@{#0}

112 scenarios, mean of 10 seeds. **79 gaps**: 18 behind libwebrtc, 61 ahead of it; 10 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

78 gaps on `main`, 79 here: **30 closed, 31 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 4.4 | 3.4 | ±0.8 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 134 | 146 | ±10 | ahead | 5/10 | #63 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 79 | 91 | ±10 | ahead | 5/10 | #63 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 38.8 | 43.8 | ±4.1 | ahead | 7/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.6 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_204_cross_traffic_join | `video.longest_freeze_ms` | 670 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 253 | 268 | ±13.4 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.6 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_224_congestive_e2e_flip | `net.loss_pct` | 4.4 | 3.4 | ±0.8 | behind | 7/10 | #21, #61, #12 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 55.9 | 45.2 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_256_audio_24khz | `cc.queue_p95_ms` | 46.9 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_298_audio_24khz_plays | `cc.queue_p95_ms` | 46.9 | 35.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1880 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 670 | 941 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 253 | 268 | ±13.4 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 4670 | 2610 | ±976 | behind | 3/10 | #15, #48, #61, #20 |
| issue_348_scaling_outage_500ms | `cc.queue_p95_ms` | 55.9 | 45.2 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_390_cross_join_seed15 | `video.longest_freeze_ms` | 609 | 916 | ±205 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 4.4 | 3.4 | ±0.8 | behind | 7/10 | #21, #61, #12 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1419 | 1543 | ±116 | ahead | 3/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `cc.queue_p95_ms` | 304 | 284 | ±15.2 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_84_encoder_qp_360p_low_rate | `audio.jb_delay_p95_ms` | 130 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 102 | 114 | ±10 | ahead | 6/10 | **none** |
| issue_84_encoder_qp_720p_low_rate | `audio.jb_delay_p95_ms` | 130 | 146 | ±15.8 | ahead | 4/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 102 | 114 | ±10 | ahead | 6/10 | **none** |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_12_lossbwe_policer | `video.freeze_count` | 5 | 6.6 | ±1.6 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1879 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1879 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 22781 | 19227 | ±2908 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 583 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_346_reconfigure_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 6/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1879 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_5_adjuster_default | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.7 | 39.8 | ±2 | ahead | 4/10 | #21, #7, #29 |
| low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| probe_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| capacity_drop | `video.freeze_total_ms` | 190 | 236 (further) |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 190 | 236 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 3.6 | 2.9 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.9 | 5.9 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 757 | 1152 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 11.5 | 16.1 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 27.8 | 35.2 (further) |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 15.2 | 27.3 (further) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.5 | 3 (closer) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.5 | 3 (closer) |
| issue_23_capacity_drop | `video.freeze_total_ms` | 190 | 236 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 2.2 | 2 (closer) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 2.3 | 2.0 (closer) |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 190 | 236 (further) |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 190 | 236 (further) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 2.2 | 2 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.6 | 2.2 (closer) |
| low_bandwidth | `video.render_fps` | 2.2 | 2 (closer) |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 190 | 236 (further) |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 190 | 236 (further) |
| very_low_capacity | `video.render_fps` | 3.6 | 2.9 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 3.6 | 2.9 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.2 | 50.2 | ±10.8 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.7 | 8.7 | ±1.2 | ahead | 9/10 | #37, #24 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `video.freeze_count` | 5 | 6.6 | ±1.6 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2 | 7.9 | ±4.4 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 419 | 1571 | ±812 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 84.5 | 117 | ±10 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 185 | 220 | ±11.0 | ahead | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 230 | 257 | ±21.0 | ahead | 6/10 | #360 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.2 | 6.2 | ±2.7 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.2 | 6.2 | ±2.7 | ahead | 8/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1937 | 2137 | ±189 | ahead | 7/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 140 | 235 | ±81.7 | ahead | 8/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1879 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1879 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 22781 | 19227 | ±2908 | behind | 7/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.4 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 301 | 421 | ±63.0 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 152 | 246 | ±69.5 | ahead | 9/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.9 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 583 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 6/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1937 | 2137 | ±189 | ahead | 7/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 140 | 235 | ±81.7 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1879 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_5_adjuster_default | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.0 | 32.2 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.7 | 39.8 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1937 | 2137 | ±189 | ahead | 7/10 | #63, #224 |
| low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `cc.rtt_p50_ms` | 140 | 235 | ±81.7 | ahead | 8/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 9/10 | #37, #24 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| very_low_capacity | `repair.overhead_pct` | 35.2 | 50.2 | ±10.8 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 11.7 | 8.7 | ±1.2 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.2 | 50.2 | ±10.8 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.7 | 8.7 | ±1.2 | ahead | 9/10 | #37, #24 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_15_aimd_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_346_reconfigure_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_355_cross_traffic_no_alr_probe | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_5_adjuster_default | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| probe_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |

