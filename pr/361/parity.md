## Parity with libwebrtc M157.8103@{#0}

106 scenarios, mean of 10 seeds. **112 gaps**: 58 behind libwebrtc, 54 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

113 gaps on `main`, 112 here: **25 closed, 24 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.4 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.4 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 657 | 926 | ±269 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 615 | 885 | ±269 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 108 | 253 | ±100 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.8 | 0.9 | ±0.7 | behind | 6/10 | #23 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 748 | 706 | ±41.1 | behind | 5/10 | #63 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.8 | 0.9 | ±0.7 | behind | 6/10 | #23 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 81.0 | 102 | ±16.2 | ahead | 9/10 | #77 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 48.9 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 97 | 153 | ±55.6 | ahead | 7/10 | #63 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `cc.max_settle_ms` | 13810 | 14740 | ±737 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 44.0 | 55.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| low_fps_clean_link | `cc.queue_p95_ms` | 109 | 98.6 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `video.render_fps` | 10.4 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 35.4 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.4 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.4 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| bufferbloat | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 140 | 150 | ±10 | ahead | 5/10 | #63 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 210 | 222 | ±11.1 | ahead | 5/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 45.4 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 45.4 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_224_congestive_e2e_flip | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.avg_kbps` | 1454 | 1369 | ±72.7 | ahead | 4/10 | #37, #61 |
| issue_346_reconfigure_cross_join | `cc.utilization_pct` | 61.1 | 57.7 | ±3.1 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_356_capacity_drop_xr_rtt | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_356_capacity_drop_xr_rtt | `audio.longest_interruption_ms` | 147 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_356_capacity_drop_xr_rtt | `video.longest_freeze_ms` | 1941 | 2587 | ±644 | ahead | 5/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.4 | 39.6 | ±2.0 | ahead | 5/10 | #21, #7, #29 |
| very_low_capacity | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_shallow | `video.freeze_count` | 4.4 | 6.1 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 816 | 1182 (further) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 67.0 | 58.7 (closer) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 41 | 35 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 2.9 | 3.7 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2478 | 3164 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1340 | 1856 (further) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2821 | 2500 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.6 | 5 (further) |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 202 | 145 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 11.8 | 9.2 (closer) |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.3 | 10.2 (closer) |
| issue_329_utilization_reordering | `repair.overhead_pct` | 2.7 | 2.0 (closer) |
| issue_346_reconfigure_capacity_drop | `video.longest_freeze_ms` | 1045 | 823 (closer) |
| issue_346_reconfigure_cross_join | `audio.mouth_to_ear_p95_ms` | 52.4 | 97.9 (further) |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 35 | 74.4 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 5.4 | 9.1 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1862 | 3450 (further) |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 241 | 372 (further) |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 67.9 | 129 (further) |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 19.7 | 25.6 (further) |
| issue_346_reconfigure_cross_join | `cc.max_settle_ms` | 3460 | 5790 (further) |
| issue_346_reconfigure_cross_join | `net.loss_pct` | 0.5 | 1.0 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.4 | 3.0 (further) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 31.9 | 27.8 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| bufferbloat | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.9 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
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
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 45.4 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.2 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 290 | 266 | ±19.8 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 45.4 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.2 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 29.9 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 124 | 159 | ±28.1 | ahead | 8/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 202 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.2 | 85.0 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.2 | 85.0 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 664 | 123 | ±33.2 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 50 | 22 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 980 | 205 | ±49.0 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 209 | 64.4 | ±10.5 | behind | 9/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 9.2 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 10.2 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1944 | 71.9 | ±97.2 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1547 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2624 | 123 | ±131 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 3.7 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3164 | 0 | ±158 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 5 | 0 | ±0.5 | behind | 6/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1870 | 13.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 20.6 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 149 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1973 | ±98.7 | ahead | 1/10 | #63 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 35.2 | 37.2 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 26.3 | ±1.4 | ahead | 5/10 | #37, #24 |
| issue_346_reconfigure_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_346_reconfigure_capacity_drop | `audio.longest_interruption_ms` | 153 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_346_reconfigure_capacity_drop | `video.longest_freeze_ms` | 1865 | 2688 | ±625 | ahead | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 454 | 380 | ±22.7 | behind | 9/10 | #63 |
| issue_346_reconfigure_cross_join | `audio.mouth_to_ear_p95_ms` | 796 | 698 | ±39.8 | behind | 9/10 | #63 |
| issue_346_reconfigure_cross_join | `cc.max_settle_ms` | 14360 | 8570 | ±1897 | behind | 10/10 | #15, #48, #61, #20 |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 293 | 268 | ±14.7 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_cross_join | `cc.utilization_pct` | 61.1 | 57.7 | ±3.1 | ahead | 4/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_cross_join | `net.loss_pct` | 1.4 | 0.4 | ±0.5 | behind | 6/10 | #21, #61, #12 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 361 | 231 | ±21.4 | behind | 9/10 | #77 |
| issue_346_reconfigure_cross_join | `video.avg_kbps` | 1454 | 1369 | ±72.7 | ahead | 4/10 | #37, #61 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 823 | 451 | ±72.8 | behind | 9/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 11.4 | 2.3 | ±1.3 | behind | 9/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 4799 | 1349 | ±247 | behind | 9/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 3730 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_355_rate_tracker_ramp | `audio.jb_delay_p95_ms` | 90 | 79 | ±10 | behind | 5/10 | #63 |
| issue_355_rate_tracker_ramp | `audio.mouth_to_ear_p95_ms` | 171 | 160 | ±10 | behind | 5/10 | #63 |
| issue_355_rate_tracker_ramp | `sync.av_offset_p95_ms` | 26.9 | 16.4 | ±10 | behind | 4/10 | #77 |
| issue_356_capacity_drop_xr_rtt | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_356_capacity_drop_xr_rtt | `audio.longest_interruption_ms` | 147 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_356_capacity_drop_xr_rtt | `video.longest_freeze_ms` | 1941 | 2587 | ±644 | ahead | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.2 | 85.0 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 5.4 | 3.9 | ±1.4 | behind | 6/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.5 | 32.4 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.9 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.4 | 39.6 | ±2.0 | ahead | 5/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 127 | 159 | ±23.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 225 | 279 | ±32.9 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 2690 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.9 | 116 | ±16.9 | ahead | 7/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 136 | 164 | ±25.6 | ahead | 9/10 | #24, #37 |
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
| wifi_to_lte_handover | `repair.overhead_pct` | 29.9 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29, #20 |

