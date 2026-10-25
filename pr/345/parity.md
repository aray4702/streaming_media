## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **96 gaps**: 44 behind libwebrtc, 52 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

82 gaps on `main`, 96 here: **7 closed, 21 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.4 | 39.6 | ±2.0 | ahead | 5/10 | #21, #7, #29 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| very_low_capacity | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29, #19, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.9 | 9.1 | ±1.2 | ahead | 9/10 | #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.3 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.7 | 79.5 | ±25.8 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 95 | ±10 | ahead | 3/10 | #63 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 95.4 | 253 | ±100 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.7 | 0.9 | ±0.7 | behind | 5/10 | #23 |
| issue_154_shallow_drop_repair_v2 | `cc.max_settle_ms` | 7530 | 4570 | ±2125 | behind | 8/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.7 | 0.9 | ±0.7 | behind | 5/10 | #23 |
| issue_21_shallow_drop_repair | `cc.max_settle_ms` | 7530 | 4570 | ±2125 | behind | 8/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.8 | 2.3 | ±1.3 | behind | 4/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 255 | 231 | ±21.4 | behind | 3/10 | #77 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 10.9 | 9.1 | ±1.2 | ahead | 9/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 34.3 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.9 | 9.1 | ±1.2 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.3 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.keyframe_requests` | 1 | 1.2 (further) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 10.3 | 12.2 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 12.6 | 11.2 (closer) |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 3.4 | 3.9 (further) |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 726 | 829 (further) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 3.4 | 4.5 (further) |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 37.6 | 50.8 (further) |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 3.8 | 5.1 (further) |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 37.6 | 50.8 (further) |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 3.8 | 5.1 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 3.8 (closer) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 1.7 | 1.6 (closer) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 382 | 808 (further) |
| very_low_capacity | `video.keyframe_requests` | 1 | 1.2 (further) |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 1 | 1.2 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| bufferbloat | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 206 | 118 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.3 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.9 | 9.1 | ±1.2 | ahead | 9/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 49.7 | 79.5 | ±25.8 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 95 | ±10 | ahead | 3/10 | #63 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 150 | ±10 | ahead | 6/10 | #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 113 | 130 | ±10.7 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 222 | ±11.1 | ahead | 4/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.2 | 9.6 | ±2.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 662 | 1902 | ±522 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 83.5 | 114 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 187 | 214 | ±11.9 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.7 | 4.6 | ±1.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 142 | 970 | ±353 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 95.4 | 253 | ±100 | ahead | 8/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 41.3 | 45.8 | ±2.9 | ahead | 5/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `cc.max_settle_ms` | 7530 | 4570 | ±2125 | behind | 8/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 44.0 | 49.1 | ±2.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 419 | 469 | ±28.3 | behind | 7/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.7 | 0.9 | ±0.7 | behind | 5/10 | #23 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `cc.max_settle_ms` | 7530 | 4570 | ±2125 | behind | 8/10 | #15, #48, #61, #20 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 44.0 | 49.1 | ±2.9 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 419 | 469 | ±28.3 | behind | 7/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4 | 6.7 | ±1.7 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.7 | 0.9 | ±0.7 | behind | 5/10 | #23 |
| issue_23_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 121 | 159 | ±28.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 199 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.4 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.4 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.2 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 712 | 123 | ±35.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 51.6 | 23.8 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1027 | 205 | ±51.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 219 | 64.4 | ±10.9 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 10.0 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.5 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2043 | 71.9 | ±102 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1516 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2772 | 123 | ±139 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 3.3 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2672 | 0 | ±134 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.8 | 0 | ±0.5 | behind | 5/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1613 | 13.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.4 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 149 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.2 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 26.3 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 255 | 231 | ±21.4 | behind | 3/10 | #77 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 4.8 | 2.3 | ±1.3 | behind | 4/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 2156 | 1349 | ±247 | behind | 4/10 | #5, #37, #24 |
| issue_372_outage_interruption_count | `audio.mouth_to_ear_p95_ms` | 222 | 256 | ±30.2 | ahead | 7/10 | #63 |
| issue_372_outage_interruption_count | `video.e2e_delay_p95_ms` | 129 | 145 | ±13.0 | ahead | 7/10 | #24, #37 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.4 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.7 | 32.4 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 40.2 | 44.3 | ±4.1 | ahead | 4/10 | #21, #7, #29 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 206 | 118 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `repair.overhead_pct` | 34.3 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 3 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| very_low_capacity | `video.render_fps` | 10.9 | 9.1 | ±1.2 | ahead | 9/10 | #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 206 | 118 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.3 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.9 | 9.1 | ±1.2 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| wifi_to_lte_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29, #20 |

