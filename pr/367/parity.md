## Parity with libwebrtc M157.8103@{#0}

110 scenarios, mean of 10 seeds. **83 gaps**: 42 behind libwebrtc, 41 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

95 gaps on `main`, 83 here: **15 closed, 3 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2921 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 6.4 | 2.3 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.longest_freeze_ms` | 537 | 941 | ±205 | ahead | 9/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 590 | 451 | ±72.8 | behind | 7/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 277 | 231 | ±21.4 | behind | 7/10 | #77 |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 284 | 268 | ±14.2 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_cross_traffic | `audio.mouth_to_ear_p95_ms` | 462 | 529 | ±42.2 | ahead | 10/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `audio.jb_delay_p95_ms` | 267 | 309 | ±22.8 | ahead | 10/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 204 | 232 | ±14.8 | ahead | 10/10 | #24, #37 |
| issue_346_reconfigure_cross_traffic | `sync.av_offset_p95_ms` | 231 | 258 | ±24.7 | ahead | 8/10 | #77 |
| issue_346_reconfigure_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2921 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 3.4 | 2.9 (closer) |
| issue_23_handover | `repair.overhead_pct` | 3.4 | 2.9 (closer) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3164 | 2722 (closer) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1856 | 1600 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 5 | 4.3 (closer) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 1.6 | 1.7 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1004 | 382 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 3.4 | 2.9 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 3.4 | 2.9 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| bufferbloat | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 140 | 150 | ±10 | ahead | 5/10 | #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 111 | 130 | ±10.7 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 210 | 222 | ±11.1 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.5 | 9.6 | ±2.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 720 | 1902 | ±522 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 80.3 | 114 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 187 | 214 | ±11.9 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1.2 | 4.6 | ±1.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 245 | 970 | ±353 | ahead | 9/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.4 | 45.8 | ±2.9 | ahead | 4/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 45.3 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.2 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 45.3 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.2 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 122 | 159 | ±28.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 200 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 698 | 123 | ±34.9 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 50 | 22 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1013 | 205 | ±50.7 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 227 | 64.4 | ±11.3 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 9.6 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.2 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2041 | 71.9 | ±102 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1526 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2772 | 123 | ±139 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 3.4 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2722 | 0 | ±136 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 0 | ±0.5 | behind | 5/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1613 | 13.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.2 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 149 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1973 | ±98.7 | ahead | 1/10 | #63 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.2 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 28.0 | 26.3 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.5 | 32.4 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.4 | 39.6 | ±2.0 | ahead | 5/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| very_low_capacity | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| very_low_capacity | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29, #20 |

