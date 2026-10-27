## Parity with libwebrtc M157.8103@{#0}

121 scenarios, mean of 10 seeds. **77 gaps**: 35 behind libwebrtc, 42 ahead of it; 3 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

63 gaps on `main`, 77 here: **20 closed, 34 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 3071 | 2135 | ±793 | behind | 7/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `sync.av_offset_p95_ms` | 183 | 168 | ±12.4 | behind | 5/10 | **none** |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 128 | 247 | ±100 | ahead | 6/10 | #5, #37, #24, #411 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.3 | 6.2 | ±2.9 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.3 | 6.2 | ±2.9 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_23_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1329 | 1582 | ±229 | ahead | 6/10 | #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1288 | 1647 | ±212 | ahead | 7/10 | #360, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 298 | 414 | ±65.7 | ahead | 10/10 | #19, #15, #48, #12, #61, #411 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 154 | 244 | ±77.5 | ahead | 8/10 | #61, #411 |
| issue_346_reconfigure_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| issue_410_queue_sample_phase | `audio.jb_delay_p95_ms` | 220 | 130 | ±88.3 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1329 | 1582 | ±229 | ahead | 6/10 | #411 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1297 | 1516 | ±144 | ahead | 5/10 | #24, #37, #411 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 852 | 1036 | ±144 | ahead | 5/10 | #411 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1329 | 1582 | ±229 | ahead | 6/10 | #411 |
| pacer_v2_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |
| probe_v2_capacity_drop | `video.freeze_count` | 2.2 | 1.7 | ±0.5 | behind | 5/10 | #5, #37, #24, #409 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `video.e2e_delay_p95_ms` | 121 | 110 | ±10 | behind | 3/10 | #24, #37 |
| cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 6.9 | 5.7 | ±1.1 | behind | 6/10 | #23 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 121 | 110 | ±10 | behind | 3/10 | #24, #37 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.5 | 6.6 | ±1.6 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 5845 | 10097 | ±2662 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_count` | 2.6 | 1.2 | ±0.8 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 15691 | 7600 | ±5222 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.9 | 2.5 | ±1.2 | behind | 7/10 | #23 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 132 | 145 | ±10 | ahead | 8/10 | #63 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 77 | 90 | ±10 | ahead | 5/10 | #63 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 9.0 | 7.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 9.0 | 7.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_23_handover | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 379 | 235 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 614 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_411_d4_audio_rate | `video.render_fps` | 27.6 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63 |
| issue_84_quality_scaling_rate_rise | `video.avg_kbps` | 877 | 824 | ±44.5 | ahead | 5/10 | #37, #61 |
| issue_84_quality_scaling_rate_rise | `cc.utilization_pct` | 49.6 | 46.7 | ±2.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_84_quality_scaling_rate_rise | `cc.max_settle_ms` | 17420 | 18560 | ±977 | ahead | 5/10 | #15, #48, #61, #20 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| very_low_capacity | `video.keyframe_requests` | 6.9 | 5.7 | ±1.1 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 6.9 | 5.7 | ±1.1 | behind | 6/10 | #23 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.freeze_count` | 4 | 2.8 (closer) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.4 | 1.7 (closer) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 19 | 13 (closer) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 34.6 | 27.7 (closer) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 35.4 | 30.5 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 2.4 | 1.6 (closer) |
| issue_21_shallow_drop_repair | `video.render_fps` | 2.4 | 1.6 (closer) |
| issue_23_low_bandwidth | `video.render_fps` | 2.1 | 1.8 (closer) |
| issue_24_timing_v2_default | `video.freeze_count` | 0.8 | 0.7 (closer) |
| issue_24_timing_v2_render_fps | `video.freeze_count` | 0.8 | 0.7 (closer) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 2.1 | 1.9 (closer) |
| issue_430_av_outage_6s | `audio.jb_delay_p95_ms` | 28 | 32 (further) |
| issue_430_av_outage_6s | `sync.av_offset_p95_ms` | 29.0 | 32.7 (further) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 2.1 | 1.8 (closer) |
| low_bandwidth | `video.render_fps` | 2.1 | 1.8 (closer) |
| very_low_capacity | `video.freeze_count` | 4 | 2.8 (closer) |
| very_low_capacity | `video.render_fps` | 2.4 | 1.7 (closer) |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 4 | 2.8 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.4 | 1.7 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `video.e2e_delay_p95_ms` | 121 | 110 | ±10 | behind | 3/10 | #24, #37 |
| cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24, #411 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19, #411 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 34.8 | 55.6 | ±12.0 | ahead | 9/10 | #21, #7, #29, #411 |
| issue_123_v2_allocation_min | `video.freeze_count` | 24.1 | 21.3 | ±2.2 | behind | 6/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 6.9 | 5.7 | ±1.1 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 11.0 | 9.2 | ±1.5 | ahead | 7/10 | #37, #24, #411 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 121 | 110 | ±10 | behind | 3/10 | #24, #37 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 232 | 245 | ±12.2 | ahead | 4/10 | #63, #411 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4.5 | 6.6 | ±1.6 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 5845 | 10097 | ±2662 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.9 | 2.5 | ±1.2 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `video.pause_count` | 2.6 | 1.2 | ±0.8 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 15691 | 7600 | ±5222 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 77 | 90 | ±10 | ahead | 5/10 | #63 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 132 | 145 | ±10 | ahead | 8/10 | #63 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 219 | ±11.0 | ahead | 6/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.4 | 6.8 | ±3.8 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 481 | 1366 | ±701 | ahead | 9/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 86.8 | 117 | ±10 | ahead | 9/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 190 | 218 | ±10.9 | ahead | 8/10 | #24, #37, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1.2 | 4.2 | ±2.4 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 256 | 885 | ±484 | ahead | 8/10 | #5, #37, #24, #411 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61, #411 |
| issue_154_shallow_drop_repair_v2 | `net.loss_pct` | 9.0 | 7.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 25.3 | 23.7 | ±1.4 | ahead | 7/10 | #37, #24, #411 |
| issue_15_aimd_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_21_shallow_drop_repair | `net.loss_pct` | 9.0 | 7.8 | ±0.7 | behind | 7/10 | #21, #61, #12 |
| issue_21_shallow_drop_repair | `video.render_fps` | 25.3 | 23.7 | ±1.4 | ahead | 7/10 | #37, #24, #411 |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_23_handover | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63 |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_24_timing_v2_default | `video.freeze_count` | 3 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_24_timing_v2_render_fps | `video.freeze_count` | 3 | 2.3 | ±0.7 | behind | 6/10 | #5, #37, #24, #409 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 379 | 235 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 614 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_355_cross_traffic_no_alr_probe | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_372_outage_interruption_count | `net.loss_pct` | 5.6 | 7.4 | ±1.7 | ahead | 6/10 | #21, #61, #12, #411 |
| issue_411_d4_audio_rate | `video.render_fps` | 27.6 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_430_av_outage_6s | `audio.jb_delay_p95_ms` | 148 | 116 | ±25.9 | behind | 4/10 | #63 |
| issue_430_av_outage_6s | `audio.mouth_to_ear_p95_ms` | 239 | 202 | ±27.8 | behind | 4/10 | #63 |
| issue_430_av_outage_6s | `sync.av_offset_p95_ms` | 102 | 68.8 | ±16.7 | behind | 3/10 | #446 |
| issue_430_av_outage_6s | `video.e2e_delay_p95_ms` | 131 | 113 | ±10 | behind | 4/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| issue_5_adjuster_default | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_62_neteq_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #411 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #411 |
| issue_84_quality_scaling_rate_rise | `cc.max_settle_ms` | 17420 | 18560 | ±977 | ahead | 5/10 | #15, #48, #61, #20 |
| issue_84_quality_scaling_rate_rise | `cc.utilization_pct` | 49.6 | 46.7 | ±2.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_84_quality_scaling_rate_rise | `video.avg_kbps` | 877 | 824 | ±44.5 | ahead | 5/10 | #37, #61 |
| low_bandwidth | `video.render_fps` | 27.6 | 25.8 | ±1.4 | ahead | 8/10 | #37, #24, #411 |
| probe_v2_cross_traffic | `video.e2e_delay_p95_ms` | 250 | 227 | ±14.1 | behind | 6/10 | #24, #37 |
| very_low_capacity | `repair.overhead_pct` | 34.8 | 55.6 | ±12.0 | ahead | 9/10 | #21, #7, #29, #411 |
| very_low_capacity | `video.freeze_count` | 24.1 | 21.3 | ±2.2 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 6.9 | 5.7 | ±1.1 | behind | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 11.0 | 9.2 | ±1.5 | ahead | 7/10 | #37, #24, #411 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 34.8 | 55.6 | ±12.0 | ahead | 9/10 | #21, #7, #29, #411 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 24.1 | 21.3 | ±2.2 | behind | 6/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 6.9 | 5.7 | ±1.1 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 11.0 | 9.2 | ±1.5 | ahead | 7/10 | #37, #24, #411 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 317 | 294 | ±22.5 | behind | 6/10 | #63, #20 |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 252 | 188 | ±50.6 | behind | 7/10 | **none** |

