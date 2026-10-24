## Parity with libwebrtc M157.8103@{#0}

103 scenarios, mean of 10 seeds. **181 gaps**: 76 behind libwebrtc, 105 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

112 gaps on `main`, 181 here: **47 closed, 116 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| issue_123_v2_allocation_min | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 657 | 926 | ±269 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 615 | 885 | ±269 | ahead | 6/10 | #63, #217 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 112 | 130 | ±10.7 | ahead | 8/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0.9 | 4.6 | ±1.8 | ahead | 10/10 | #5, #37, #24 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 42.7 | 45.8 | ±2.9 | ahead | 4/10 | #21, #7, #29 |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_23_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1576 | ±188 | behind | 6/10 | #77 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 193 | 260 | ±43.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 118 | 159 | ±28.1 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `sync.av_offset_p95_ms` | 81.0 | 102 | ±16.2 | ahead | 9/10 | #77 |
| issue_23_outage_500ms | `cc.queue_p95_ms` | 48.9 | 68.5 | ±18.2 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 267 | 64.4 | ±13.3 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 13.4 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `audio.jb_delay_p95_ms` | 97 | 153 | ±55.6 | ahead | 7/10 | #63 |
| issue_329_utilization_reordering | `repair.overhead_pct` | 34.4 | 37.2 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1576 | ±188 | behind | 6/10 | #77 |
| issue_53_cwnd_pushback | `video.render_fps` | 29.3 | 27.4 | ±1.5 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.3 | 85.0 | ±10 | behind | 5/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `cc.max_settle_ms` | 13810 | 14740 | ±737 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 222 | 279 | ±32.9 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 126 | 159 | ±23.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 90.9 | 116 | ±16.9 | ahead | 7/10 | #77 |
| issue_85_video_pause | `cc.queue_p95_ms` | 44.0 | 55.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1765 | 1576 | ±188 | behind | 6/10 | #77 |
| very_low_capacity | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `audio.longest_interruption_ms` | 203 | 116 | ±20 | behind | 10/10 | #63 |
| very_low_capacity_allocator_v2 | `video.time_to_first_frame_ms` | 345 | 780 | ±101 | ahead | 10/10 | #24, #61 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.2 | 4.2 | ±0.8 | ahead | 6/10 | #23 |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 501 | 559 | ±55.5 | ahead | 5/10 | #63, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 30.0 | 32.8 | ±2 | ahead | 6/10 | #21, #7, #29, #20 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 687 | 468 | ±168 | behind | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1280 | 2500 | ±958 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_policer | `video.render_fps` | 17.1 | 19.9 | ±2.0 | behind | 7/10 | #37, #24 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 130 | 153 | ±22.0 | behind | 6/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.8 | 2.7 | ±0.9 | behind | 8/10 | #23 |
| issue_133_all_v2_outage_15s | `video.pause_total_ms` | 16167 | 15301 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `video.pause_total_ms` | 16167 | 15301 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 458 | 531 | ±30.1 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 269 | 310 | ±16.3 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 215 | 232 | ±11.6 | ahead | 7/10 | #24, #37 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 223 | 259 | ±13.0 | ahead | 9/10 | #77 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 156 | 185 | ±15.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 415 | 380 | ±20.8 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.7 | 2.3 | ±1.3 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 3210 | 1349 | ±247 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 692 | 451 | ±72.8 | behind | 9/10 | #24, #37 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 299 | 231 | ±21.4 | behind | 7/10 | #77 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 12030 | 8570 | ±1897 | behind | 7/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `net.loss_pct` | 0.9 | 0.4 | ±0.5 | behind | 3/10 | #21, #61, #12 |
| issue_224_congestive_e2e_flip | `video.longest_freeze_ms` | 687 | 468 | ±168 | behind | 6/10 | #5, #37, #24 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1280 | 2500 | ±958 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_23_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 788 | 560 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1327 | 1041 | ±181 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 270 | 333 | ±33.1 | ahead | 6/10 | #24, #37 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 116 | 173 | ±36.3 | ahead | 8/10 | #77 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1327 | 1041 | ±181 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 270 | 333 | ±33.1 | ahead | 6/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 116 | 173 | ±36.3 | ahead | 8/10 | #77 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2298 | 2152 | ±118 | behind | 4/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24186 | 20966 | ±2845 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 627 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 5 | 0.5 | ±1.6 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 1 | 0.1 | ±0.5 | behind | 8/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.longest_interruption_ms` | 514 | 77 | ±240 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 2135 | 979 | ±426 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1286 | 806 | ±225 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `video.freeze_count` | 0.6 | 0 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.freeze_total_ms` | 2814 | 0 | ±141 | behind | 6/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 2945 | 226 | ±147 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 1.4 | 0 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 8476 | 0 | ±424 | behind | 8/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.render_fps` | 0.3 | 0.8 | ±0.5 | behind | 6/10 | #37, #24 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2776 | 1441 | ±394 | behind | 9/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 8/10 | #23 |
| issue_335_fps1_keyframe_packets | `sync.av_offset_p95_ms` | 1856 | 1244 | ±316 | behind | 8/10 | #77 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 814 | 127 | ±202 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 1870 | 9440 | ±3638 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1327 | 1041 | ±181 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 270 | 333 | ±33.1 | ahead | 6/10 | #24, #37 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 116 | 173 | ±36.3 | ahead | 8/10 | #77 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 148 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1595 | 2587 | ±644 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `video.longest_freeze_ms` | 687 | 468 | ±168 | behind | 6/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1280 | 2500 | ±958 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 153 | 218 | ±24.4 | ahead | 8/10 | #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.6 | 39.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 197 | 236 | ±20.0 | ahead | 9/10 | #63 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 113 | 146 | ±18.4 | ahead | 9/10 | #63 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 111 | 122 | ±10 | ahead | 6/10 | #24, #37 |
| lossy_feedback | `sync.av_offset_p95_ms` | 53.8 | 81.5 | ±19.1 | ahead | 9/10 | #77 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| satellite | `video.freeze_count` | 31.3 | 28 | ±3.2 | behind | 7/10 | #5, #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63, #20 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 1.3 | 2.3 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 12.9 | 17.0 (further) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 2.6 | 2.2 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_count` | 4.4 | 3.7 (closer) |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 816 | 722 (closer) |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 796 | 579 (closer) |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 28.7 | 25.3 (closer) |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 2.3 | 2.7 (further) |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 0.9 | 0.8 (closer) |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 42.0 | 52.4 (further) |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 23.7 | 19.7 (closer) |
| issue_21_shallow_drop_repair | `video.freeze_count` | 2.3 | 2.7 (further) |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 0.9 | 0.8 (closer) |
| issue_23_low_bandwidth | `video.render_fps` | 1.5 | 1.8 (further) |
| issue_257_zero_max_payload | `audio.concealed_pct` | 0.9 | 0.7 (closer) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 28 | 24 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 830 | 446 (closer) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 597 | 263 (closer) |
| issue_257_zero_max_payload | `video.freeze_count` | 2.9 | 5.1 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2478 | 3225 (further) |
| issue_257_zero_max_payload | `video.render_fps` | 8.4 | 7.4 (closer) |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2821 | 1387 (closer) |
| issue_257_zero_max_payload | `video.avg_kbps` | 586 | 471 (closer) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.6 | 1 (closer) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2043 | 1090 (closer) |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 255 | 203 (closer) |
| issue_257_zero_max_payload | `net.loss_pct` | 11.8 | 1.3 (closer) |
| issue_48_prober_low_bandwidth | `video.render_fps` | 1.5 | 1.8 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.4 | 2.8 (further) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 31.9 | 14.4 (closer) |
| issue_85_video_pause | `cc.max_settle_ms` | 2320 | 4800 (further) |
| low_bandwidth | `video.render_fps` | 1.5 | 1.8 (further) |
| low_fps_clean_link | `cc.queue_p95_ms` | 10.7 | 12.1 (further) |
| very_low_capacity | `video.render_fps` | 1.3 | 2.3 (further) |
| very_low_capacity | `repair.overhead_pct` | 12.9 | 17.0 (further) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 1.3 | 2.3 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 12.9 | 17.0 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23, #19, #24 |
| high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_120_rtx_payload_padding_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 33.6 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 10.5 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 34.7 | 36.9 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `cc.max_settle_ms` | 1280 | 2500 | ±958 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_congestive | `video.longest_freeze_ms` | 687 | 468 | ±168 | behind | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 130 | 153 | ±22.0 | behind | 6/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 3.8 | 2.7 | ±0.9 | behind | 8/10 | #23 |
| issue_12_lossbwe_policer | `video.render_fps` | 17.1 | 19.9 | ±2.0 | behind | 7/10 | #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.3 | 7 | ±3.1 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 679 | 1401 | ±595 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 80.5 | 117 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 189 | 214 | ±10.7 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 170 | 749 | ±545 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 101 | 236 | ±100 | ahead | 6/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.pause_total_ms` | 16167 | 15301 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_15s | `video.pause_total_ms` | 16167 | 15301 | ±808 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 3680 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 269 | 310 | ±16.3 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 458 | 531 | ±30.1 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 156 | 185 | ±15.6 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 223 | 259 | ±13.0 | ahead | 9/10 | #77 |
| issue_152_alr_request_probe_v2 | `video.e2e_delay_p95_ms` | 215 | 232 | ±11.6 | ahead | 7/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 4.1 | 6.8 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_154_shallow_drop_repair_v2 | `video.keyframe_requests` | 1.4 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_204_cross_traffic_join | `audio.jb_delay_p95_ms` | 415 | 380 | ±20.8 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `audio.mouth_to_ear_p95_ms` | 750 | 698 | ±37.5 | behind | 4/10 | #63 |
| issue_204_cross_traffic_join | `cc.max_settle_ms` | 12030 | 8570 | ±1897 | behind | 7/10 | #15, #48, #61, #20 |
| issue_204_cross_traffic_join | `cc.queue_p95_ms` | 287 | 268 | ±14.4 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_204_cross_traffic_join | `net.loss_pct` | 0.9 | 0.4 | ±0.5 | behind | 3/10 | #21, #61, #12 |
| issue_204_cross_traffic_join | `sync.av_offset_p95_ms` | 299 | 231 | ±21.4 | behind | 7/10 | #77 |
| issue_204_cross_traffic_join | `video.e2e_delay_p95_ms` | 692 | 451 | ±72.8 | behind | 9/10 | #24, #37 |
| issue_204_cross_traffic_join | `video.freeze_count` | 7.7 | 2.3 | ±1.3 | behind | 8/10 | #5, #37, #24 |
| issue_204_cross_traffic_join | `video.freeze_total_ms` | 3210 | 1349 | ±247 | behind | 8/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 4.1 | 6.8 | ±2.6 | ahead | 7/10 | #5, #37, #24 |
| issue_21_shallow_drop_repair | `video.keyframe_requests` | 1.4 | 0.6 | ±0.5 | behind | 6/10 | #23 |
| issue_224_congestive_e2e_flip | `cc.max_settle_ms` | 1280 | 2500 | ±958 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_224_congestive_e2e_flip | `video.longest_freeze_ms` | 687 | 468 | ±168 | behind | 6/10 | #5, #37, #24 |
| issue_23_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_23_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_23_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63 |
| issue_23_handover | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63 |
| issue_23_low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_23_outage_500ms | `video.freeze_count` | 1 | 2 | ±0.5 | ahead | 10/10 | #5, #37, #24 |
| issue_23_outage_500ms | `video.longest_freeze_ms` | 788 | 560 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 116 | 173 | ±36.3 | ahead | 8/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 270 | 333 | ±33.1 | ahead | 6/10 | #24, #37 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1327 | 1041 | ±181 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_24_timing_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 116 | 173 | ±36.3 | ahead | 8/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 270 | 333 | ±33.1 | ahead | 6/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1327 | 1041 | ±181 | behind | 8/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.0 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 388 | 125 | ±19.4 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 53 | 29 | ±20 | behind | 5/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 651 | 205 | ±32.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 300 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 247 | 43.5 | ±12.3 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 1.3 | 0 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 1147 | 57.0 | ±57.3 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1531 | 2002 | ±100 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 1508 | 122 | ±75.4 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 5.1 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 3225 | 0 | ±161 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 1 | 0 | ±0.5 | behind | 7/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1273 | 11.6 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 22.3 | 29.7 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 289 | 186 | ±50 | behind | 10/10 | #24, #61 |
| issue_283_longest_freeze_no_metrics | `audio.longest_interruption_ms` | 1832 | 1961 | ±98.1 | ahead | 1/10 | #63 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2298 | 2152 | ±118 | behind | 4/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.freeze_total_ms` | 24186 | 20966 | ±2845 | behind | 7/10 | #5, #37, #24 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 627 | 0 | ±100 | behind | 1/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `audio.concealed_pct` | 5 | 0.5 | ±1.6 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.interruptions` | 1 | 0.1 | ±0.5 | behind | 8/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.jb_delay_p95_ms` | 1286 | 806 | ±225 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.longest_interruption_ms` | 514 | 77 | ±240 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `audio.mouth_to_ear_p95_ms` | 2135 | 979 | ±426 | behind | 10/10 | #63 |
| issue_335_fps1_keyframe_packets | `cc.max_settle_ms` | 1870 | 9440 | ±3638 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_335_fps1_keyframe_packets | `cc.queue_p95_ms` | 814 | 127 | ±202 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_335_fps1_keyframe_packets | `sync.av_offset_p95_ms` | 1856 | 1244 | ±316 | behind | 8/10 | #77 |
| issue_335_fps1_keyframe_packets | `video.e2e_delay_p95_ms` | 2776 | 1441 | ±394 | behind | 9/10 | #24, #37 |
| issue_335_fps1_keyframe_packets | `video.freeze_count` | 0.6 | 0 | ±0.5 | behind | 6/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.freeze_total_ms` | 2814 | 0 | ±141 | behind | 6/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.keyframe_requests` | 2.3 | 0.1 | ±0.5 | behind | 8/10 | #23 |
| issue_335_fps1_keyframe_packets | `video.longest_freeze_ms` | 2945 | 226 | ±147 | behind | 7/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_count` | 1.4 | 0 | ±0.5 | behind | 8/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 8476 | 0 | ±424 | behind | 8/10 | #5, #37, #24 |
| issue_335_fps1_keyframe_packets | `video.render_fps` | 0.3 | 0.8 | ±0.5 | behind | 6/10 | #37, #24 |
| issue_346_reconfigure_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_346_reconfigure_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| issue_346_reconfigure_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `audio.jb_delay_p95_ms` | 415 | 380 | ±20.8 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `audio.mouth_to_ear_p95_ms` | 750 | 698 | ±37.5 | behind | 4/10 | #63 |
| issue_346_reconfigure_cross_join | `cc.max_settle_ms` | 12030 | 8570 | ±1897 | behind | 7/10 | #15, #48, #61, #20 |
| issue_346_reconfigure_cross_join | `cc.queue_p95_ms` | 287 | 268 | ±14.4 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_cross_join | `net.loss_pct` | 0.9 | 0.4 | ±0.5 | behind | 3/10 | #21, #61, #12 |
| issue_346_reconfigure_cross_join | `sync.av_offset_p95_ms` | 299 | 231 | ±21.4 | behind | 7/10 | #77 |
| issue_346_reconfigure_cross_join | `video.e2e_delay_p95_ms` | 692 | 451 | ±72.8 | behind | 9/10 | #24, #37 |
| issue_346_reconfigure_cross_join | `video.freeze_count` | 7.7 | 2.3 | ±1.3 | behind | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_join | `video.freeze_total_ms` | 3210 | 1349 | ±247 | behind | 8/10 | #5, #37, #24 |
| issue_346_reconfigure_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_346_reconfigure_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_346_reconfigure_outage_8s | `cc.max_settle_ms` | 3680 | 2610 | ±976 | behind | 6/10 | #15, #48, #61, #20 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 116 | 173 | ±36.3 | ahead | 8/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 270 | 333 | ±33.1 | ahead | 6/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1327 | 1041 | ±181 | behind | 8/10 | #5, #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `audio.longest_interruption_ms` | 148 | 204 | ±20 | ahead | 8/10 | #63 |
| issue_5_keyframe_flush | `video.longest_freeze_ms` | 1595 | 2587 | ±644 | ahead | 8/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.max_settle_ms` | 1280 | 2500 | ±958 | ahead | 7/10 | #15, #48, #61, #20 |
| issue_61_googcc_glue_congestive | `video.longest_freeze_ms` | 687 | 468 | ±168 | behind | 6/10 | #5, #37, #24 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.8 | 32.6 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 153 | 218 | ±24.4 | ahead | 8/10 | #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.6 | 39.9 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.keyframe_requests` | 0.4 | 1.2 | ±0.8 | ahead | 6/10 | #23 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 228 | 433 | ±139 | ahead | 8/10 | #24, #61 |
| issue_85_video_pause | `audio.interruptions` | 2.9 | 1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9979 | ±499 | ahead | 1/10 | #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 340 | 5140 | ±643 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 129 | 143 | ±14.3 | ahead | 5/10 | #24, #37 |
| lossy_feedback | `audio.jb_delay_p95_ms` | 113 | 146 | ±18.4 | ahead | 9/10 | #63 |
| lossy_feedback | `audio.mouth_to_ear_p95_ms` | 197 | 236 | ±20.0 | ahead | 9/10 | #63 |
| lossy_feedback | `sync.av_offset_p95_ms` | 53.8 | 81.5 | ±19.1 | ahead | 9/10 | #77 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 111 | 122 | ±10 | ahead | 6/10 | #24, #37 |
| low_bandwidth | `video.render_fps` | 27.6 | 25.7 | ±1.4 | ahead | 9/10 | #37, #24 |
| low_fps_clean_link | `cc.queue_p95_ms` | 101 | 88.9 | ±10 | behind | 3/10 | #19, #15, #48, #12, #61 |
| pacer_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| pacer_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| pacer_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `audio.interruptions` | 0.3 | 1.1 | ±0.5 | ahead | 8/10 | #63 |
| probe_v2_capacity_drop | `audio.longest_interruption_ms` | 154 | 202 | ±20 | ahead | 7/10 | #63 |
| probe_v2_capacity_drop | `video.longest_freeze_ms` | 1643 | 2688 | ±625 | ahead | 8/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 284 | 309 | ±22.8 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 479 | 529 | ±42.2 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 161 | 184 | ±20.9 | ahead | 9/10 | #19, #15, #48, #12, #61 |
| satellite | `video.freeze_count` | 31.3 | 28 | ±3.2 | behind | 7/10 | #5, #37, #24 |
| very_low_capacity | `repair.overhead_pct` | 33.6 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 10.5 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 33.6 | 50.6 | ±6.3 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 10.5 | 8.2 | ±1.8 | ahead | 9/10 | #37, #24 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 324 | 293 | ±24.5 | behind | 5/10 | #63, #20 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1242 | 1429 | ±167 | ahead | 2/10 | #63, #20 |

