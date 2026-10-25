## Parity with libwebrtc M157.8103@{#0}

111 scenarios, mean of 10 seeds. **88 gaps**: 42 behind libwebrtc, 46 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

82 gaps on `main`, 88 here: **19 closed, 25 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| bufferbloat | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| bufferbloat | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| bufferbloat | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #379 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| issue_124_bufferbloat_v2 | `audio.mouth_to_ear_p95_ms` | 210 | 193 | ±14.3 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `audio.jb_delay_p95_ms` | 130 | 114 | ±12.6 | behind | 5/10 | #63 |
| issue_124_bufferbloat_v2 | `sync.av_offset_p95_ms` | 83.7 | 66.4 | ±16.4 | behind | 4/10 | #379 |
| issue_154_shallow_drop_repair_v2 | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_154_shallow_drop_repair_v2 | `cc.utilization_pct` | 45.3 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_21_shallow_drop_repair | `video.avg_kbps` | 432 | 469 | ±28.3 | behind | 8/10 | #37, #61 |
| issue_21_shallow_drop_repair | `cc.utilization_pct` | 45.3 | 49.1 | ±2.9 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 1731 | 1349 | ±247 | behind | 5/10 | #5, #37, #24 |
| issue_372_outage_interruption_count | `audio.mouth_to_ear_p95_ms` | 220 | 256 | ±30.2 | ahead | 7/10 | #63 |
| issue_372_outage_interruption_count | `video.e2e_delay_p95_ms` | 129 | 145 | ±13.0 | ahead | 7/10 | #24, #37 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| low_bandwidth | `video.render_fps` | 27.9 | 26.4 | ±1.4 | ahead | 6/10 | #37, #24 |
| very_low_capacity | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.3 | 16.6 | ±3.2 | behind | 7/10 | #21, #61, #12 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379, #19, #24 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.8 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 33.6 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 95 | ±10 | ahead | 4/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 242 | 229 | ±12.1 | behind | 4/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 122 | 108 | ±10.3 | behind | 3/10 | #63 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 606 | 192 | ±365 | behind | 5/10 | #5, #37, #24 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 84.2 | 102 | ±16.2 | ahead | 8/10 | #379 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 48.8 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 918 | 863 | ±45.9 | behind | 4/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 153 | 98.7 | ±42.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_346_reconfigure_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 606 | 192 | ±365 | behind | 5/10 | #5, #37, #24 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| low_bandwidth | `video.longest_freeze_ms` | 606 | 192 | ±365 | behind | 5/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 16.0 | 18.2 | ±2 | ahead | 5/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 10.8 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 33.6 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.8 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 33.6 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 16.3 | 10.4 (closer) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 10.3 | 12.2 (further) |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 18.8 | 23.8 (further) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 27.2 | 24.2 (closer) |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 60.3 | 69.6 (further) |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 37 | 43 (further) |
| issue_257_zero_max_payload | `audio.concealed_pct` | 0.9 | 0.7 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 808 | 962 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 574 | 729 (further) |
| issue_257_zero_max_payload | `video.freeze_count` | 3.4 | 2.8 (closer) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2722 | 2345 (closer) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1600 | 1407 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.3 | 2.9 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 9.6 | 10.8 (further) |
| issue_257_zero_max_payload | `repair.overhead_pct` | 11.1 | 12.4 (further) |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 1.7 | 1.5 (closer) |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 16.3 | 10.4 (closer) |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 16.3 | 10.4 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 3.0 | 2.4 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 156 | 167 | ±10 | ahead | 6/10 | #63 |
| high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379, #19, #24 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 29.6 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 205 | 118 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 33.6 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.3 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.8 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.0 | 37.6 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 95 | ±10 | ahead | 4/10 | #63 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 150 | ±10 | ahead | 7/10 | #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 106 | 130 | ±10.7 | ahead | 7/10 | #379 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 209 | 222 | ±11.1 | ahead | 5/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.4 | 9.6 | ±2.7 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 700 | 1902 | ±522 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 81.5 | 114 | ±10 | ahead | 10/10 | #379 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 190 | 214 | ±11.9 | ahead | 8/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 1.3 | 4.6 | ±1.8 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 274 | 970 | ±353 | ahead | 8/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 122 | 108 | ±10.3 | behind | 3/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 242 | 229 | ±12.1 | behind | 4/10 | #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.1 | 45.8 | ±2.9 | ahead | 5/10 | #21, #7, #29 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4 | 6.7 | ±1.7 | ahead | 7/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4 | 6.7 | ±1.7 | ahead | 7/10 | #5, #37, #24 |
| issue_23_handover | `repair.overhead_pct` | 29.6 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `video.longest_freeze_ms` | 606 | 192 | ±365 | behind | 5/10 | #5, #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 116 | 159 | ±28.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 191 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 48.8 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 84.2 | 102 | ±16.2 | ahead | 8/10 | #379 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.1 | 85.0 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.4 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.1 | 85.0 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.4 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 9/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 852 | 123 | ±42.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 51.1 | 23.8 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1167 | 205 | ±58.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 229 | 64.4 | ±11.4 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 10.8 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 12.5 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1947 | 71.9 | ±97.4 | behind | 10/10 | #379 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1512 | 2110 | ±106 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2867 | 123 | ±143 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.8 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2345 | 0 | ±117 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 2.9 | 0 | ±0.5 | behind | 4/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1421 | 13.3 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.7 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 149 | 54 | ±50 | behind | 10/10 | #24, #61 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 32.6 | 37.2 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_331_low_bandwidth_av_offset_seed4 | `video.render_fps` | 27.8 | 26.3 | ±1.4 | ahead | 6/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 918 | 863 | ±45.9 | behind | 4/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 153 | 98.7 | ±42.7 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `net.loss_pct` | 8.9 | 8.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_346_reconfigure_outage_8s | `repair.overhead_pct` | 7.7 | 5.6 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `video.longest_freeze_ms` | 606 | 192 | ±365 | behind | 5/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.1 | 85.0 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.4 | 27.4 | ±1.5 | ahead | 10/10 | #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 156 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 156 | 167 | ±10 | ahead | 6/10 | #63 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.0 | 32.4 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 29.6 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.3 | 39.6 | ±2.0 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 67.7 | 51.3 | ±12.7 | behind | 5/10 | #379 |
| low_bandwidth | `video.longest_freeze_ms` | 606 | 192 | ±365 | behind | 5/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 16.0 | 18.2 | ±2 | ahead | 5/10 | #21, #7, #29 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 205 | 118 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `repair.overhead_pct` | 33.6 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 3.3 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| very_low_capacity | `video.render_fps` | 10.8 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| very_low_capacity | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 205 | 118 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 33.6 | 48.3 | ±9.2 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.3 | 4.2 | ±0.8 | ahead | 7/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.8 | 9.1 | ±1.2 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| wifi_to_lte_handover | `repair.overhead_pct` | 29.6 | 32.8 | ±2 | ahead | 7/10 | #21, #7, #29, #20 |

