## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **87 gaps**: 29 behind libwebrtc, 58 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

89 gaps on `main`, 87 here: **2 closed, 0 opened**, 2 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 3720 | 300 | ±500 | behind | 9/10 | #15, #48, #61, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_257_zero_max_payload | `video.avg_kbps` | 1415 | 875 | ±93.9 | ahead | 9/10 | #37, #61 |
| issue_257_zero_max_payload | `cc.utilization_pct` | 73.5 | 60.9 | ±6.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 417 | 281 (closer) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 275 | 192 (closer) |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 95.1 | 61.7 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 162 | 108 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63, #19, #24 |
| high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63, #19, #24 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.6 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 37.2 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 12.2 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_12_lossbwe_high_jitter_no_loss | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 206 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3 | 7 | ±3.1 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 602 | 1401 | ±595 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 80.5 | 117 | ±10 | ahead | 10/10 | #379 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 186 | 214 | ±10.7 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 188 | 749 | ±545 | ahead | 7/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 150 | 139 | ±10 | behind | 5/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 227 | 342 | ±109 | ahead | 6/10 | #24, #61 |
| issue_154_shallow_drop_repair_v2 | `repair.overhead_pct` | 20.0 | 24.1 | ±3.2 | ahead | 7/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3.5 | 6.8 | ±2.6 | ahead | 8/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.5 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_19_cc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_21_shallow_drop_repair | `repair.overhead_pct` | 20.0 | 24.1 | ±3.2 | ahead | 7/10 | #21, #7, #29 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3.5 | 6.8 | ±2.6 | ahead | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.5 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_23_handover | `repair.overhead_pct` | 28.6 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1941 | 2140 | ±179 | ahead | 7/10 | #63, #224 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 140 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_24_timing_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 400 | 208 | ±33.3 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 623 | 341 | ±53.2 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 205 | 97.0 | ±21.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.utilization_pct` | 73.5 | 60.9 | ±6.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 0.5 | 0 | ±0.5 | behind | 4/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1065 | 116 | ±53.2 | behind | 10/10 | #379 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1415 | 875 | ±93.9 | ahead | 9/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1321 | 190 | ±67.3 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 4.7 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2950 | 0 | ±148 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1.6 | 0 | ±0.5 | behind | 8/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1103 | 16.0 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 23.1 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 281 | 219 | ±50 | behind | 10/10 | #24, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1243 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.3 | 37.1 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.queue_p95_ms` | 297 | 416 | ±66.1 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `cc.rtt_p50_ms` | 150 | 256 | ±58.7 | ahead | 9/10 | #61 |
| issue_331_low_bandwidth_av_offset_seed4 | `sync.av_offset_p95_ms` | 1234 | 1566 | ±328 | ahead | 8/10 | #379 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.9 | 25.5 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1941 | 2140 | ±179 | ahead | 7/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 140 | 238 | ±80.3 | ahead | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 155 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.6 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.6 | 39.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `sync.av_offset_p95_ms` | 64.8 | 51.1 | ±10 | behind | 5/10 | #379 |
| issue_76_tcc_v2_high_jitter | `audio.jb_delay_p95_ms` | 146 | 122 | ±18.1 | behind | 6/10 | #63 |
| issue_76_tcc_v2_high_jitter | `audio.mouth_to_ear_p95_ms` | 265 | 241 | ±17.8 | behind | 6/10 | #63 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_85_video_pause | `cc.max_settle_ms` | 4260 | 5140 | ±643 | ahead | 3/10 | #15, #48, #61, #20 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 115 | 146 | ±18.4 | ahead | 8/10 | #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 195 | 236 | ±20.0 | ahead | 8/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 54.2 | 81.5 | ±19.1 | ahead | 8/10 | #379 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 112 | 122 | ±10 | ahead | 5/10 | #24, #37 |
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 1941 | 2140 | ±179 | ahead | 7/10 | #63, #224 |
| low_bandwidth | `cc.rtt_p50_ms` | 140 | 238 | ±80.3 | ahead | 8/10 | #61 |
| low_bandwidth | `video.render_fps` | 27.9 | 25.7 | ±1.4 | ahead | 8/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 37.2 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 12.2 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 37.2 | 50.6 | ±6.3 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 12.2 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.6 | 31.9 | ±3.0 | ahead | 7/10 | #21, #7, #29, #20 |

