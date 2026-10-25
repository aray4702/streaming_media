## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **98 gaps**: 52 behind libwebrtc, 46 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

81 gaps on `main`, 98 here: **9 closed, 26 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #379 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 195 | 218 | ±23.6 | ahead | 6/10 | #63 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #379 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9604 | 7368 | ±1761 | behind | 7/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8532 | 6028 | ±1493 | behind | 8/10 | #379 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 205 | 185 | ±18.2 | behind | 7/10 | #63 |
| bufferbloat | `video.e2e_delay_p95_ms` | 125 | 113 | ±10.1 | behind | 4/10 | #24, #37 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 7.2 | 5.6 | ±1.6 | behind | 6/10 | #23 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 205 | 185 | ±18.2 | behind | 7/10 | #63 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 125 | 113 | ±10.1 | behind | 4/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.7 | 2.7 | ±0.9 | behind | 7/10 | #23 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 182 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 132 | 236 | ±100 | ahead | 4/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_133_all_v2_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 164 | 139 | ±10 | behind | 7/10 | #24, #37 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 617 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `cc.rtt_p50_ms` | 103 | 86.4 | ±10 | behind | 6/10 | #61 |
| issue_346_reconfigure_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_346_reconfigure_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.5 | 27.1 | ±1.4 | ahead | 4/10 | #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.7 | 39.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 112 | 122 | ±10 | ahead | 5/10 | #24, #37 |
| very_low_capacity | `video.keyframe_requests` | 7.2 | 5.6 | ±1.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 7.2 | 5.6 | ±1.6 | behind | 6/10 | #23 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 16.3 | 10.4 (closer) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 14.8 | 18.5 (further) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.6 | 2.1 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.9 | 4.8 (further) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 787 | 957 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 11.1 | 14.2 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 24.7 | 20.6 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.9 | 3.9 (further) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.9 | 3.9 (further) |
| issue_257_zero_max_payload | `audio.concealed_pct` | 0.7 | 0.6 (closer) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 22.8 | 25.3 (further) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 442 | 513 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 258 | 320 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.1 | 1.4 (further) |
| issue_257_zero_max_payload | `net.loss_pct` | 1.3 | 1.8 (further) |
| issue_29_ulpfec_over_red | `video.freeze_count` | 1.7 | 1.4 (closer) |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 3721 | 3057 (closer) |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 123 | 200 (further) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 3170 | 2547 (closer) |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 198 | 170 (closer) |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 0.9 | 1 (further) |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 10.2 | 8.7 (closer) |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 16.3 | 10.4 (closer) |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 16.3 | 10.4 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 3.4 | 2.8 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 770 | 1380 (further) |
| very_low_capacity | `repair.overhead_pct` | 14.8 | 18.5 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 14.8 | 18.5 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 156 | 167 | ±10 | ahead | 6/10 | #63 |
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 205 | 185 | ±18.2 | behind | 7/10 | #63 |
| bufferbloat | `video.e2e_delay_p95_ms` | 125 | 113 | ±10.1 | behind | 4/10 | #24, #37 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 32.1 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 7.2 | 5.6 | ±1.6 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.2 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 205 | 185 | ±18.2 | behind | 7/10 | #63 |
| issue_124_bufferbloat_v2 | `video.e2e_delay_p95_ms` | 125 | 113 | ±10.1 | behind | 4/10 | #24, #37 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.8 | 36.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.7 | 2.7 | ±0.9 | behind | 7/10 | #23 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 208 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.2 | 7 | ±3.1 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 444 | 1401 | ±595 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 83.2 | 117 | ±10 | ahead | 10/10 | #379 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 194 | 214 | ±10.7 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 182 | 749 | ±545 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 132 | 236 | ±100 | ahead | 4/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_133_all_v2_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_133_pacer_v2_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_133_pacer_v2_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 164 | 139 | ±10 | behind | 7/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `cc.max_settle_ms` | 3130 | 2310 | ±781 | behind | 4/10 | #15, #48, #61, #20 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.9 | 6.8 | ±2.6 | ahead | 10/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.5 | 0.6 | ±0.5 | behind | 7/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.9 | 6.8 | ±2.6 | ahead | 10/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.5 | 0.6 | ±0.5 | behind | 7/10 | #23 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63 |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 0.9 | 0.3 | ±0.5 | behind | 8/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 445 | 125 | ±22.2 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 54.3 | 29 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 718 | 205 | ±35.9 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 43.5 | ±12.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.8 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1089 | 57.0 | ±54.5 | behind | 10/10 | #379 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1541 | 2002 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1445 | 122 | ±72.2 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.8 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3541 | 0 | ±177 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.4 | 0 | ±0.5 | behind | 8/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1278 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 22.0 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_29_ulpfec_over_red | `video.freeze_count` | 8.8 | 7.4 | ±1.3 | behind | 6/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24020 | 20963 | ±2840 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 617 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.5 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.6 | 25.5 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 3.7 | 0.5 | ±1.5 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1086 | 806 | ±225 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 1695 | 979 | ±426 | behind | 9/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 1870 | 9440 | ±3638 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 574 | 127 | ±202 | behind | 9/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.rtt_p50_ms` | 103 | 86.4 | ±10 | behind | 6/10 | #61 |
| issue_335_fps1_keyframe_packets | `cc.utilization_pct` | 42.8 | 51.6 | ±8.0 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `video.avg_kbps` | 760 | 930 | ±156 | behind | 7/10 | #37, #61 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2368 | 1441 | ±394 | behind | 9/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 1.1 | 0.1 | ±0.5 | behind | 8/10 | #23 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 426 | 226 | ±100 | behind | 5/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 2547 | 0 | ±127 | behind | 4/10 | #5, #37, #24 |
| issue_346_reconfigure_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_346_reconfigure_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 156 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 156 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.5 | 27.1 | ±1.4 | ahead | 4/10 | #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.7 | 39.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_85_video_pause | `cc.max_settle_ms` | 3760 | 5140 | ±643 | ahead | 4/10 | #15, #48, #61, #20 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 9/10 | #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 196 | 236 | ±20.0 | ahead | 10/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 53.5 | 81.5 | ±19.1 | ahead | 9/10 | #379 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 112 | 122 | ±10 | ahead | 5/10 | #24, #37 |
| low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 7/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 32.1 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 7.2 | 5.6 | ±1.6 | behind | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 10.2 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 32.1 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 7.2 | 5.6 | ±1.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.2 | 8.2 | ±1.8 | ahead | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 330 | 293 | ±24.5 | behind | 4/10 | #63, #20 |

