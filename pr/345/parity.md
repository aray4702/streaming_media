## Parity with libwebrtc M157.8103@{#0}

99 scenarios, mean of 10 seeds. **108 gaps**: 45 behind libwebrtc, 63 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

103 gaps on `main`, 108 here: **4 closed, 9 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.2 | 37.1 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.3 | 32.4 | ±2 | ahead | 4/10 | #21, #7, #29 |
| issue_85_video_pause | `cc.queue_p95_ms` | 45.3 | 55.6 | ±10 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| low_fps_clean_link | `cc.queue_p95_ms` | 110 | 98.6 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `net.loss_pct` | 19.8 | 16.6 | ±3.2 | behind | 6/10 | #21, #61, #12 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_257_zero_max_payload | `video.pause_total_ms` | 683 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 35.1 | 37.2 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_335_fps1_keyframe_packets | `video.time_to_first_frame_ms` | 558 | 610 | ±50 | ahead | 6/10 | #24, #61 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| very_low_capacity | `net.loss_pct` | 19.8 | 16.6 | ±3.2 | behind | 6/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 19.8 | 16.6 | ±3.2 | behind | 6/10 | #21, #61, #12 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| high_jitter | `sync.av_offset_p95_ms` | 20.7 | 13.0 (closer) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 3.4 | 3.1 (closer) |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 1.1 (further) |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 75 | 161 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 1.9 | 1.6 (closer) |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 1.3 | 1.1 (closer) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.6 | 2.9 (further) |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 20.7 | 13.0 (closer) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 337 | 384 (further) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 337 | 384 (further) |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 11.2 | 15.6 (further) |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 18.7 | 22.9 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 25.3 | 32.4 (further) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.6 | 1.9 (closer) |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 20.7 | 13.0 (closer) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.6 | 1.9 (closer) |
| issue_23_handover | `repair.overhead_pct` | 3.4 | 3.1 (closer) |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 232 | 265 (further) |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 20.7 | 13.0 (closer) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2391 | 2790 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1487 | 2059 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 5.2 (further) |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 232 | 265 (further) |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 20.7 | 13.0 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 3.4 | 3.1 (closer) |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 20.7 | 13.0 (closer) |
| low_bandwidth | `sync.av_offset_p95_ms` | 232 | 265 (further) |
| very_low_capacity | `audio.interruptions` | 1 | 1.1 (further) |
| very_low_capacity | `audio.longest_interruption_ms` | 75 | 161 (further) |
| very_low_capacity | `video.render_fps` | 1.9 | 1.6 (closer) |
| very_low_capacity | `video.keyframe_requests` | 1.3 | 1.1 (closer) |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 1.1 (further) |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 75 | 161 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.9 | 1.6 (closer) |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 1.3 | 1.1 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 3.4 | 3.1 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| high_jitter | `sync.av_offset_p95_ms` | 64.3 | 51.3 | ±12.7 | behind | 4/10 | #77, #19, #24 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.7 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1.1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 277 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `net.loss_pct` | 19.8 | 16.6 | ±3.2 | behind | 6/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 37.2 | 48.3 | ±9.2 | ahead | 7/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.1 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.6 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 321 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.7 | 37.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 64.3 | 51.3 | ±12.7 | behind | 4/10 | #77 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 501 | 885 | ±269 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 543 | 926 | ±269 | ahead | 7/10 | #63, #217 |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 51.9 | 79.5 | ±25.8 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 107 | 130 | ±10.7 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 207 | 222 | ±11.1 | ahead | 6/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.6 | 9.6 | ±2.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 550 | 1902 | ±522 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 82.3 | 114 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 182 | 214 | ±11.9 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.7 | 4.6 | ±1.8 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 158 | 970 | ±353 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 124 | 253 | ±100 | ahead | 5/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.5 | 45.8 | ±2.9 | ahead | 4/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.8 | 6.7 | ±1.7 | ahead | 6/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 64.3 | 51.3 | ±12.7 | behind | 4/10 | #77 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.8 | 6.7 | ±1.7 | ahead | 6/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 29.7 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 200 | 145 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1840 | 1576 | ±188 | behind | 7/10 | #77 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 159 | ±28.1 | ahead | 10/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 190 | 260 | ±43.1 | ahead | 10/10 | #63, #218 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 47.6 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 72.9 | 102 | ±16.2 | ahead | 10/10 | #77 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 64.3 | 51.3 | ±12.7 | behind | 4/10 | #77 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 694 | 123 | ±34.7 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 46 | 22 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1010 | 205 | ±50.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 274 | 64.4 | ±13.7 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 11.4 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.0 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2092 | 71.9 | ±105 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1553 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2862 | 123 | ±143 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.6 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2790 | 0 | ±139 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 5.2 | 0 | ±0.5 | behind | 5/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 2072 | 13.3 | ±104 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.pause_total_ms` | 683 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 20.8 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1973 | ±98.7 | ahead | 1/10 | #63 |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 86 | 153 | ±55.6 | ahead | 9/10 | #63 |
| issue_29_ulpfec_over_red | `audio.mouth_to_ear_p95_ms` | 168 | 252 | ±73.3 | ahead | 9/10 | #63 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 35.1 | 37.2 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_335_fps1_keyframe_packets | `video.time_to_first_frame_ms` | 558 | 610 | ±50 | ahead | 6/10 | #24, #61 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 200 | 145 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1840 | 1576 | ±188 | behind | 7/10 | #77 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 64.3 | 51.3 | ±12.7 | behind | 4/10 | #77 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.7 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 64.3 | 51.3 | ±12.7 | behind | 4/10 | #77 |
| issue_84_encoder_qp_360p_low_rate | `audio.jb_delay_p95_ms` | 127 | 146 | ±15.8 | ahead | 5/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 6/10 | #63 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 98.6 | 114 | ±10 | ahead | 7/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `audio.jb_delay_p95_ms` | 127 | 146 | ±15.8 | ahead | 5/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `audio.mouth_to_ear_p95_ms` | 229 | 251 | ±21.2 | ahead | 6/10 | #63 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 98.6 | 114 | ±10 | ahead | 7/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 55.9 | 43.4 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 129 | 159 | ±23.3 | ahead | 8/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 227 | 279 | ±32.9 | ahead | 8/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2690 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 92.9 | 116 | ±16.9 | ahead | 6/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 136 | 164 | ±25.6 | ahead | 8/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 200 | 145 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1840 | 1576 | ±188 | behind | 7/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| very_low_capacity | `audio.interruptions` | 1.1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 277 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `net.loss_pct` | 19.8 | 16.6 | ±3.2 | behind | 6/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 37.2 | 48.3 | ±9.2 | ahead | 7/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 3.1 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| very_low_capacity | `video.render_fps` | 10.6 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 321 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1.1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 277 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 19.8 | 16.6 | ±3.2 | behind | 6/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 37.2 | 48.3 | ±9.2 | ahead | 7/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.1 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.6 | 9.1 | ±1.2 | ahead | 7/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 321 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.7 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29, #20 |

