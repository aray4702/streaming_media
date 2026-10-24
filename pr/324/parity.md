## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **81 gaps**: 39 behind libwebrtc, 42 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

83 gaps on `main`, 81 here: **48 closed, 46 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| bufferbloat | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #77 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 140 | 150 | ±10 | ahead | 5/10 | #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 111 | 130 | ±10.7 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1.2 | 4.6 | ±1.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 245 | 970 | ±353 | ahead | 9/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.4 | 45.8 | ±2.9 | ahead | 4/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 45.3 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 45.3 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 200 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 122 | 159 | ±28.1 | ahead | 9/10 | #63, #218 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 227 | 64.4 | ±11.3 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.2 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1831 | 1973 | ±98.7 | ahead | 1/10 | #63 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.4 | 39.6 | ±2.0 | ahead | 5/10 | #21, #7, #29 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| very_low_capacity | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 8/10 | #23 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.8 | 32.8 | ±2 | ahead | 5/10 | #21, #7, #29, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.9 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3100 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.6 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.6 | 0.6 | ±0.5 | behind | 8/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_29_ulpfec_over_red | `video.freeze_count` | 9.1 | 7.4 | ±1.3 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24687 | 20966 | ±2845 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 3.7 | 0.5 | ±1.5 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1685 | 979 | ±426 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1083 | 806 | ±225 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 349 | 226 | ±100 | behind | 4/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 3170 | 0 | ±159 | behind | 5/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2319 | 1441 | ±394 | behind | 9/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 732 | 930 | ±156 | behind | 8/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 1 | 0.1 | ±0.5 | behind | 7/10 | #23 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 41.4 | 51.6 | ±8.0 | behind | 8/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 567 | 127 | ±202 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 1870 | 9440 | ±3638 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 195 | 218 | ±23.6 | ahead | 6/10 | #63 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_85_video_pause | `cc.max_settle_ms` | 4370 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 198 | 236 | ±20.0 | ahead | 10/10 | #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 54.2 | 81.5 | ±19.1 | ahead | 9/10 | #77 |
| very_low_capacity | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity | `repair.overhead_pct` | 35.9 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.4 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #77 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.9 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_shallow | `video.freeze_count` | 6.1 | 3.9 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 1182 | 787 (closer) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 12.6 | 11.1 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.5 | 2.9 (further) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.5 | 2.9 (further) |
| issue_23_low_bandwidth | `video.render_fps` | 1.4 | 2.0 (further) |
| issue_257_zero_max_payload | `audio.concealed_pct` | 0.9 | 0.7 (closer) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 28 | 22.8 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 808 | 442 (closer) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 574 | 258 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 3.4 | 5.3 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2722 | 3353 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1600 | 1275 (closer) |
| issue_257_zero_max_payload | `video.render_fps` | 8.7 | 7.5 (closer) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2648 | 1384 (closer) |
| issue_257_zero_max_payload | `video.avg_kbps` | 584 | 458 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 1.1 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1969 | 1085 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 204 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 9.6 | 1.3 (closer) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 1.7 | 2.3 (further) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.4 | 2.0 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 3.0 | 3.4 (further) |
| low_bandwidth | `video.render_fps` | 1.4 | 2.0 (further) |

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

