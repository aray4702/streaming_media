## Parity with libwebrtc M157.8103@{#0}

112 scenarios, mean of 10 seeds. **59 gaps**: 12 behind libwebrtc, 47 ahead of it; 1 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

79 gaps on `main`, 59 here: **37 closed, 17 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_12_lossbwe_policer | `video.freeze_count` | 5 | 6.6 | ±1.6 | ahead | 8/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 230 | 257 | ±21.0 | ahead | 6/10 | #360 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_15_aimd_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1937 | 2137 | ±189 | ahead | 7/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 583 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_346_reconfigure_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1937 | 2137 | ±189 | ahead | 7/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_5_adjuster_default | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 518 | 353 | ±151 | behind | 8/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 316 | 188 | ±120 | behind | 9/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| issue_62_neteq_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.7 | 39.8 | ±2 | ahead | 4/10 | #21, #7, #29 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1937 | 2137 | ±189 | ahead | 7/10 | #63, #224 |
| low_bandwidth | `cc.queue_p95_ms` | 284 | 389 | ±97.8 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2908 | 3144 | ±179 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 246 | 228 | ±15.8 | behind | 3/10 | #24, #37 |
| probe_v2_cross_traffic | `sync.av_offset_p95_ms` | 230 | 260 | ±28.4 | ahead | 7/10 | **none** |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 227 | 241 | ±13.7 | ahead | 4/10 | #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 106 | 122 | ±13.7 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1 | 3.8 | ±2.1 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 213 | 802 | ±422 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 128 | 246 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 13.6 | 15.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_204_cross_traffic_join | `video.freeze_count` | 3.4 | 2.2 | ±0.6 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1830 | 1380 | ±279 | behind | 5/10 | #5, #37, #24 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1276 | 1711 | ±417 | ahead | 8/10 | #360 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 3.4 | 2.2 | ±0.6 | behind | 7/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1830 | 1380 | ±279 | behind | 5/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 50.5 | 36.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1255 | 1518 | ±98.1 | ahead | 7/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 851 | 1033 | ±96.1 | ahead | 7/10 | **none** |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 2.9 | 3.5 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 15.0 | 11.9 (closer) |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 223 | 165 (closer) |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 223 | 165 (closer) |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 223 | 165 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.2 | 2.6 (further) |
| very_low_capacity | `video.render_fps` | 2.9 | 3.5 (further) |
| very_low_capacity | `repair.overhead_pct` | 15.0 | 11.9 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.9 | 3.5 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 15.0 | 11.9 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 38.3 | 50.2 | ±10.8 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 12.2 | 8.7 | ±1.2 | ahead | 10/10 | #37, #24 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 227 | 241 | ±13.7 | ahead | 4/10 | #63 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 106 | 122 | ±13.7 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2 | 7.9 | ±4.4 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 413 | 1571 | ±812 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 13.6 | 15.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 82.0 | 117 | ±10 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 183 | 220 | ±11.0 | ahead | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1 | 3.8 | ±2.1 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 213 | 802 | ±422 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 128 | 246 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.1 | 6.2 | ±2.7 | ahead | 9/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `video.freeze_count` | 3.4 | 2.2 | ±0.6 | behind | 7/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1830 | 1380 | ±279 | behind | 5/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.1 | 6.2 | ±2.7 | ahead | 9/10 | #5, #37, #24 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 143 | 235 | ±81.7 | ahead | 9/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1820 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1820 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 22602 | 19227 | ±2908 | behind | 6/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.1 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 299 | 421 | ±63.0 | ahead | 10/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 154 | 246 | ±69.5 | ahead | 9/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1276 | 1711 | ±417 | ahead | 8/10 | #360 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.8 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 3.4 | 2.2 | ±0.6 | behind | 7/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1830 | 1380 | ±279 | behind | 5/10 | #5, #37, #24 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.3 | ±1.6 | ahead | 6/10 | #21, #61, #12 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 143 | 235 | ±81.7 | ahead | 9/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1820 | 1656 | ±164 | behind | 7/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 50.5 | 36.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.6 | 32.2 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 851 | 1033 | ±96.1 | ahead | 7/10 | **none** |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1255 | 1518 | ±98.1 | ahead | 7/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| low_bandwidth | `cc.rtt_p50_ms` | 143 | 235 | ±81.7 | ahead | 9/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.9 | ±1.4 | ahead | 8/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 38.3 | 50.2 | ±10.8 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 12.2 | 8.7 | ±1.2 | ahead | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 38.3 | 50.2 | ±10.8 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 12.2 | 8.7 | ±1.2 | ahead | 10/10 | #37, #24 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 851 | 1033 | ±96.1 | ahead | 7/10 | **none** |

