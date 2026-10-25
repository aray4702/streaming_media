## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **81 gaps**: 39 behind libwebrtc, 42 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

110 gaps on `main`, 81 here: **48 closed, 19 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.7 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 125 | 109 | ±16.0 | behind | 4/10 | #63 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.5 | 3.5 | ±2.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 98.3 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 91.2 | 236 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 150 | 139 | ±10 | behind | 5/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 39.8 | 43.8 | ±4.1 | ahead | 5/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_204_cross_traffic_join | `video.freeze_count` | 5.3 | 2.3 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 533 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 258 | 231 | ±21.4 | behind | 4/10 | #77 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_23_handover | `repair.overhead_pct` | 28.7 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 148 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_257_zero_max_payload | `cc.utilization_pct` | 75.5 | 85.7 | ±4.3 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 628 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 301 | 416 | ±66.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 156 | 256 | ±58.7 | ahead | 9/10 | #61 |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 0.7 | 0.1 | ±0.5 | behind | 5/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 0.6 | 0 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.time_to_first_frame_ms` | 6058 | 5485 | ±481 | behind | 6/10 | #24, #61 |
| issue_346_reconfigure_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 5.3 | 2.3 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 533 | 451 | ±72.8 | behind | 4/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 258 | 231 | ±21.4 | behind | 4/10 | #77 |
| issue_356_capacity_drop_xr_rtt | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 148 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 2877 | 3104 | ±187 | ahead | 5/10 | #5, #37, #24 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.7 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 2527 | 1932 | ±486 | behind | 8/10 | #5, #37, #24 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1954 | 2139 | ±180 | ahead | 7/10 | #63, #224 |
| low_bandwidth | `cc.rtt_p50_ms` | 148 | 238 | ±80.3 | ahead | 8/10 | #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 2886 | 3123 | ±188 | ahead | 5/10 | #5, #37, #24 |
| satellite | `cc.queue_p95_ms` | 43.2 | 31.3 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 25 | 20.3 | ±3.9 | behind | 8/10 | #5, #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 326 | 293 | ±24.5 | behind | 6/10 | #63, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.7 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 222 | ±11.1 | ahead | 4/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3100 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 51.8 | 29 | ±20 | behind | 5/10 | #63 |
| issue_29_ulpfec_over_red | `video.freeze_count` | 9.1 | 7.4 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24687 | 20966 | ±2845 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1685 | 979 | ±426 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1083 | 806 | ±225 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 732 | 930 | ±156 | behind | 8/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 7/10 | #23 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 41.4 | 51.6 | ±8.0 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.2 | 32.6 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 195 | 218 | ±23.6 | ahead | 6/10 | #63 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 11.7 | 16.3 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 4.0 | 2.2 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 4.9 | 3.9 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 968 | 787 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 0.7 | 1 (further) |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 942 | 382 (closer) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0.7 | 1 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 2.2 | 2.0 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 399 | 442 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3040 | 3353 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1100 | 1275 (further) |
| issue_257_zero_max_payload | `video.render_fps` | 6.6 | 7.5 (further) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1207 | 1384 (further) |
| issue_257_zero_max_payload | `video.avg_kbps` | 553 | 458 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.6 | 1.1 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 167 | 204 (further) |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 3420 | 1570 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 0.5 | 1.3 (further) |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 2.1 | 3.2 (further) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 3787 | 3170 (closer) |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 464 | 879 (further) |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 282 | 439 (further) |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 5930 | 7570 (further) |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 942 | 382 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 2.2 | 2.0 (closer) |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 11.7 | 16.3 (further) |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 11.7 | 16.3 (further) |
| issue_85_video_pause | `cc.max_settle_ms` | 870 | 770 (closer) |
| low_bandwidth | `video.render_fps` | 2.2 | 2.0 (closer) |
| very_low_capacity | `video.render_fps` | 4.0 | 2.2 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 4.0 | 2.2 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.9 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.4 | 36.9 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 211 | 222 | ±11.1 | ahead | 4/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.1 | 7 | ±3.1 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 614 | 1401 | ±595 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 83.7 | 117 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 190 | 214 | ±10.7 | ahead | 9/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3100 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.9 | 6.8 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.6 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.9 | 6.8 | ±2.6 | ahead | 9/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.6 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_23_low_bandwidth | `video.render_fps` | 27.7 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 383 | 125 | ±19.2 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 51.8 | 29 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 648 | 205 | ±32.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 248 | 43.5 | ±12.4 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.3 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1142 | 57.0 | ±57.1 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1544 | 2002 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1506 | 122 | ±75.3 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.3 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3353 | 0 | ±168 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.1 | 0 | ±0.5 | behind | 8/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1287 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 22.2 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_29_ulpfec_over_red | `video.freeze_count` | 9.1 | 7.4 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24687 | 20966 | ±2845 | behind | 7/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.5 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 25.5 | ±1.4 | ahead | 10/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 3.7 | 0.5 | ±1.5 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1083 | 806 | ±225 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1685 | 979 | ±426 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 1870 | 9440 | ±3638 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 567 | 127 | ±202 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 41.4 | 51.6 | ±8.0 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 732 | 930 | ±156 | behind | 8/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2319 | 1441 | ±394 | behind | 9/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 7/10 | #23 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 349 | 226 | ±100 | behind | 4/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 3170 | 0 | ±159 | behind | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.7 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.2 | 32.6 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 195 | 218 | ±23.6 | ahead | 6/10 | #63 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_85_video_pause | `cc.max_settle_ms` | 4370 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 8/10 | #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 198 | 236 | ±20.0 | ahead | 10/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 54.2 | 81.5 | ±19.1 | ahead | 9/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.7 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 35.9 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.9 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |

