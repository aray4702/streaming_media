## Parity with libwebrtc M157.8103@{#0}

113 scenarios, mean of 10 seeds. **78 gaps**: 31 behind libwebrtc, 47 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

78 gaps on `main`, 78 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| burst_loss | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63, #410 |
| capacity_drop | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24, #411 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19, #411 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 38.3 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29, #411 |
| issue_123_v2_allocation_min | `video.render_fps` | 12.2 | 9.2 | ±1.5 | ahead | 9/10 | #37, #24, #411 |
| issue_124_burst_loss_v2 | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63, #410 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 227 | 245 | ±12.2 | ahead | 6/10 | #63, #411 |
| issue_12_lossbwe_burst_loss | `audio.jb_delay_p95_ms` | 194 | 174 | ±16.5 | behind | 6/10 | #63, #410 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 219 | ±11.0 | ahead | 6/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2 | 6.8 | ±3.8 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 413 | 1366 | ±701 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 82.0 | 117 | ±10 | ahead | 10/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 183 | 218 | ±10.9 | ahead | 9/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1 | 4.2 | ±2.4 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 213 | 885 | ±484 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 128 | 247 | ±100 | ahead | 6/10 | #5, #37, #24, #411 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61, #411 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.1 | 6.2 | ±2.9 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.1 | 23.7 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.1 | 6.2 | ±2.9 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.1 | 23.7 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| issue_23_capacity_drop | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| issue_23_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1311 | 1582 | ±229 | ahead | 6/10 | #411 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.8 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| issue_24_timing_v2_default | `video.freeze_count` | 3.1 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_24_timing_v2_default | `video.freeze_total_ms` | 1820 | 1616 | ±190 | behind | 6/10 | #5, #37, #24, #409 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_24_timing_v2_render_fps | `video.freeze_count` | 3.1 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_24_timing_v2_render_fps | `video.freeze_total_ms` | 1820 | 1616 | ±190 | behind | 6/10 | #5, #37, #24, #409 |
| issue_29_ulpfec_over_red | `video.e2e_delay_p95_ms` | 241 | 213 | ±18.9 | behind | 6/10 | #24, #37, #410 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.1 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 299 | 414 | ±65.7 | ahead | 10/10 | #19, #15, #48, #12, #61, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 154 | 244 | ±77.5 | ahead | 8/10 | #61, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1276 | 1647 | ±212 | ahead | 8/10 | #360, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_346_reconfigure_capacity_drop | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| issue_346_reconfigure_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_356_capacity_drop_xr_rtt | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.4 | ±1.7 | ahead | 6/10 | #21, #61, #12, #411 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1311 | 1582 | ±229 | ahead | 6/10 | #411 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.8 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| issue_53_cwnd_pushback | `video.freeze_count` | 3.1 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 1820 | 1616 | ±190 | behind | 6/10 | #5, #37, #24, #409 |
| issue_5_burst15_freeze | `cc.queue_p95_ms` | 50.5 | 35.9 | ±10 | behind | 8/10 | #19, #15, #48, #12, #61, #410 |
| issue_5_keyframe_flush | `cc.queue_p95_ms` | 299 | 281 | ±14.9 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.6 | 31.9 | ±2 | ahead | 5/10 | #21, #7, #29, #411 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 851 | 1036 | ±144 | ahead | 5/10 | #411 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1255 | 1516 | ±144 | ahead | 5/10 | #24, #37, #411 |
| issue_76_session_layer_v2 | `audio.jb_delay_p95_ms` | 99 | 80 | ±16.3 | behind | 6/10 | #63, #420 |
| issue_76_session_layer_v2 | `audio.mouth_to_ear_p95_ms` | 191 | 170 | ±18.7 | behind | 7/10 | #63, #420 |
| issue_76_session_layer_v2 | `sync.av_offset_p95_ms` | 60.6 | 47.8 | ±10 | behind | 6/10 | #63, #420 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1311 | 1582 | ±229 | ahead | 6/10 | #411 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.8 | ±1.4 | ahead | 9/10 | #37, #24, #411 |
| pacer_v2_capacity_drop | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| probe_v2_capacity_drop | `cc.queue_p95_ms` | 300 | 281 | ±15.0 | behind | 5/10 | #19, #15, #48, #12, #61, #410 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| satellite | `video.freeze_count` | 30.9 | 28.3 | ±1.8 | behind | 7/10 | #5, #37, #24, #420 |
| satellite | `video.freeze_total_ms` | 13271 | 12350 | ±697 | behind | 8/10 | #5, #37, #24, #420 |
| very_low_capacity | `repair.overhead_pct` | 38.3 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29, #411 |
| very_low_capacity | `video.render_fps` | 12.2 | 9.2 | ±1.5 | ahead | 9/10 | #37, #24, #411 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 38.3 | 55.6 | ±12.0 | ahead | 10/10 | #21, #7, #29, #411 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 12.2 | 9.2 | ±1.5 | ahead | 9/10 | #37, #24, #411 |

