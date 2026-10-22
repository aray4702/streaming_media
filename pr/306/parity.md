## Parity with libwebrtc M157.8103@{#0}

90 scenarios, mean of 10 seeds. **224 gaps**: 115 behind libwebrtc, 109 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

180 gaps on `main`, 224 here: **13 closed, 57 opened**, 7 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| issue_12_lossbwe_congestive | `video.freeze_count` | 4.5 | 3.5 | ±1.0 | behind | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.5 | 84.4 | ±10 | ahead | 4/10 | #62, #63 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #62, #63 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2538 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_152_alr_request_probe_v2 | `sync.av_offset_p95_ms` | 216 | 283 | ±59.5 | ahead | 7/10 | #77 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `video.freeze_count` | 4.5 | 3.5 | ±1.0 | behind | 6/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `cc.rtt_p50_ms` | 87.7 | 70.8 | ±10 | behind | 4/10 | #61 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 316 | 353 | ±29.5 | ahead | 8/10 | #62, #63 |
| very_low_capacity | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61, #19, #24 |
| high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20, #19, #24 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9570 | 7658 | ±1520 | behind | 8/10 | #24, #37 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8393 | 6450 | ±1708 | behind | 7/10 | #77 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.9 | 18.0 | ±3.8 | behind | 6/10 | #21, #61, #12 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 559 | 443 | ±27.9 | behind | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 350 | 235 | ±17.8 | behind | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `sync.av_offset_p95_ms` | 183 | 167 | ±10.6 | behind | 4/10 | #77 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 473 | 278 | ±34.3 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 386 | 192 | ±33.9 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 187 | 144 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 132 | 89 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 102 | 127 | ±14.4 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 210 | 149 | ±10.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 155 | 94 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 14.8 | 12.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 250 | 229 | ±12.5 | behind | 5/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 130 | 109 | ±11.0 | behind | 5/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `sync.av_offset_p95_ms` | 86.7 | 52.3 | ±15.5 | behind | 8/10 | #77 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63 |
| issue_23_handover | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1945 | 1401 | ±198 | behind | 9/10 | #77 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1945 | 1401 | ±198 | behind | 9/10 | #77 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 473 | 278 | ±34.3 | behind | 10/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 386 | 192 | ±33.9 | behind | 10/10 | #62, #63 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 46.2 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1945 | 1401 | ±198 | behind | 9/10 | #77 |
| satellite | `audio.mouth_to_ear_p95_ms` | 637 | 588 | ±31.8 | behind | 6/10 | #62, #63 |
| satellite | `audio.jb_delay_p95_ms` | 281 | 226 | ±20.1 | behind | 8/10 | #62, #63 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9570 | 7658 | ±1520 | behind | 8/10 | #24, #37 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8393 | 6450 | ±1708 | behind | 7/10 | #77 |
| very_low_capacity | `net.loss_pct` | 21.9 | 18.0 | ±3.8 | behind | 6/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9570 | 7658 | ±1520 | behind | 8/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8393 | 6450 | ±1708 | behind | 7/10 | #77 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.9 | 18.0 | ±3.8 | behind | 6/10 | #21, #61, #12 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61, #20 |

Changed direction:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 97.7 | 113 | ±10 | ahead | 7/10 | #77 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 808 | 638 | ±40.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 687 | 517 | ±34.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 808 | 638 | ±40.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 687 | 517 | ±34.4 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 808 | 638 | ±40.4 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 687 | 517 | ±34.4 | behind | 10/10 | #62, #63 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| high_jitter | `repair.overhead_pct` | 9.7 | 5.6 (closer) |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 0.8 | 0.9 (further) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 19.6 | 13.9 (closer) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 396 | 272 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 9.7 | 5.6 (closer) |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 8.3 | 6.7 (closer) |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 7.3 | 5.7 (closer) |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 10.5 | 5.9 (closer) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 9.7 | 5.6 (closer) |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 87.4 | 74.3 (closer) |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 382 | 196 (closer) |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 15.0 | 13.4 (closer) |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 9.7 | 5.6 (closer) |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 382 | 196 (closer) |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 15.0 | 13.4 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 497 | 824 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 263 | 590 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2648 | 2967 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1761 | 2011 (further) |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 87.4 | 74.3 (closer) |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 382 | 196 (closer) |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 15.0 | 13.4 (closer) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 396 | 272 (closer) |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 9.7 | 5.6 (closer) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.8 | 3.1 (further) |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 224 | 357 (further) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 9.7 | 5.6 (closer) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 21.7 | 29.2 (further) |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 33.3 | 23.2 (closer) |
| low_bandwidth | `cc.rtt_p50_ms` | 87.4 | 74.3 (closer) |
| satellite | `repair.overhead_pct` | 3.4 | 3.8 (further) |
| very_low_capacity | `video.keyframe_requests` | 0.8 | 0.9 (further) |
| very_low_capacity | `repair.overhead_pct` | 19.6 | 13.9 (closer) |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 0.8 | 0.9 (further) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 19.6 | 13.9 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| capacity_drop | `video.freeze_total_ms` | 1825 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #62, #63 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 598 | 669 | ±51.0 | ahead | 8/10 | #62, #63 |
| cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20, #19, #24 |
| high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61, #24 |
| high_jitter | `repair.overhead_pct` | 38.1 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29, #19, #24 |
| high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61, #19, #24 |
| issue_120_rtx_payload_padding | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1247 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 28.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1825 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `net.loss_pct` | 21.9 | 18.0 | ±3.8 | behind | 6/10 | #21, #61, #12 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 41.7 | 55.6 | ±11.1 | ahead | 5/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `sync.av_offset_p95_ms` | 8393 | 6450 | ±1708 | behind | 7/10 | #77 |
| issue_123_v2_allocation_min | `video.e2e_delay_p95_ms` | 9570 | 7658 | ±1520 | behind | 8/10 | #24, #37 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 7/10 | #23 |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 350 | 235 | ±17.8 | behind | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `audio.mouth_to_ear_p95_ms` | 559 | 443 | ±27.9 | behind | 10/10 | #62, #63 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.3 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `sync.av_offset_p95_ms` | 183 | 167 | ±10.6 | behind | 4/10 | #77 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 386 | 192 | ±33.9 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 473 | 278 | ±34.3 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 103 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.4 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 9.9 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 542 | 270 | ±88.2 | behind | 9/10 | #77 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 808 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_12_lossbwe_high_jitter_no_loss | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 38.1 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2603 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 132 | 89 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 187 | 144 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 7.1 | 6.3 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 102 | 127 | ±14.4 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 2.1 | 8.9 | ±3.3 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 490 | 1740 | ±635 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `audio.jb_delay_p95_ms` | 155 | 94 | ±10 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `audio.mouth_to_ear_p95_ms` | 210 | 149 | ±10.5 | behind | 10/10 | #62, #63 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.2 | 6.7 | ±0.5 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 14.8 | 12.7 | ±2 | behind | 5/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 97.7 | 113 | ±10 | ahead | 7/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 229 | 209 | ±13.5 | behind | 5/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 37.0 | 43.7 | ±3.2 | ahead | 33/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 37.9 | 43.6 | ±3.8 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 130 | 109 | ±11.0 | behind | 5/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 250 | 229 | ±12.5 | behind | 5/10 | #62, #63 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.0 | 44.9 | ±3.3 | ahead | 7/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `sync.av_offset_p95_ms` | 86.7 | 52.3 | ±15.5 | behind | 8/10 | #77 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 52.9 | 39.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 329 | 377 | ±21.5 | ahead | 8/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 586 | 661 | ±39.5 | ahead | 8/10 | #62, #63 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 223 | 248 | ±22.5 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_154_shallow_drop_repair_v2 | `video.render_fps` | 26.2 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 598 | 669 | ±51.0 | ahead | 8/10 | #62, #63 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_19_cc_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 38.1 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_21_shallow_drop_repair | `video.render_fps` | 26.2 | 23.8 | ±1.6 | ahead | 6/10 | #37, #24 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 1825 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| issue_23_handover | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63 |
| issue_23_handover | `audio.longest_interruption_ms` | 1247 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_23_handover | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 28.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 307 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1945 | 1401 | ±198 | behind | 9/10 | #77 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 52.9 | 39.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 122 | 151 | ±19.7 | ahead | 7/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 197 | 247 | ±33.5 | ahead | 9/10 | #62, #63 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 687 | 517 | ±34.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 808 | 638 | ±40.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 5/10 | #61 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 1691 | 1495 | ±87.1 | behind | 8/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 2322 | 1949 | ±116 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_default | `video.longest_freeze_ms` | 1067 | 876 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_default | `video.render_fps` | 29 | 27.4 | ±1.5 | ahead | 6/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_24_timing_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 38.1 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_24_timing_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 687 | 517 | ±34.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 808 | 638 | ±40.4 | behind | 10/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 5/10 | #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 1691 | 1495 | ±87.1 | behind | 8/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2322 | 1949 | ±116 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1067 | 876 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29 | 27.4 | ±1.5 | ahead | 6/10 | #37, #24 |
| issue_256_audio_24khz | `audio.jb_delay_p95_ms` | 0 | 132 | ±37.7 | ahead | 10/10 | #62, #63 |
| issue_256_audio_24khz | `audio.longest_interruption_ms` | 0 | 28 | ±20 | ahead | 4/10 | #62, #63 |
| issue_256_audio_24khz | `audio.mouth_to_ear_p95_ms` | 84.3 | 222 | ±51.5 | ahead | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 709 | 119 | ±35.5 | behind | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 53 | 21 | ±20 | behind | 4/10 | #62, #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1025 | 201 | ±51.2 | behind | 10/10 | #62, #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 340 | 65.3 | ±17.0 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 10.2 | 0 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 12.3 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2064 | 69.5 | ±103 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1527 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2736 | 121 | ±137 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 3 | 0 | ±0.5 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2967 | 0 | ±148 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.6 | 0 | ±0.5 | behind | 7/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 2030 | 18.9 | ±102 | behind | 9/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 20.8 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1011 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_count` | 1.2 | 1.8 | ±0.5 | ahead | 6/10 | #5, #37, #24 |
| issue_283_longest_freeze_no_metrics | `video.freeze_total_ms` | 2134 | 2272 | ±114 | ahead | 6/10 | #5, #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 307 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1945 | 1401 | ±198 | behind | 9/10 | #77 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 687 | 517 | ±34.4 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 808 | 638 | ±40.4 | behind | 10/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 5/10 | #61 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 1691 | 1495 | ±87.1 | behind | 8/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 2322 | 1949 | ±116 | behind | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1067 | 876 | ±186 | behind | 7/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 29 | 27.4 | ±1.5 | ahead | 6/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #62, #63 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 598 | 669 | ±51.0 | ahead | 8/10 | #62, #63 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 364 | 72.5 | ±18.2 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `video.freeze_count` | 1.4 | 2.3 | ±0.7 | ahead | 8/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 1811 | 2524 | ±299 | ahead | 10/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 386 | 192 | ±33.9 | behind | 10/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 473 | 278 | ±34.3 | behind | 10/10 | #62, #63 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 103 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.4 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.9 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 542 | 270 | ±88.2 | behind | 9/10 | #77 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 808 | 420 | ±98.3 | behind | 10/10 | #24, #37 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 598 | 669 | ±51.0 | ahead | 8/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_62_neteq_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 38.1 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_62_neteq_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.0 | 32.1 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1247 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 28.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1625 | 1268 | ±223 | behind | 6/10 | #5, #37, #24 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.6 | 39.8 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `cc.max_settle_ms` | 22040 | 27250 | ±4941 | ahead | 6/10 | #15, #48, #61, #20 |
| issue_76_tcc_v2_high_jitter | `cc.utilization_pct` | 39.7 | 35.3 | ±4.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 38.1 | 43.7 | ±2.5 | ahead | 7/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `video.avg_kbps` | 644 | 554 | ±64.5 | ahead | 7/10 | #37, #61 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.5 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.5 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 131 | 163 | ±26.3 | ahead | 9/10 | #62, #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 230 | 283 | ±33.5 | ahead | 10/10 | #62, #63 |
| issue_85_video_pause | `cc.max_settle_ms` | 370 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 46.2 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 94.8 | 118 | ±17.0 | ahead | 9/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 138 | 167 | ±20.6 | ahead | 10/10 | #24, #37 |
| lossy_feedback | `sync.av_offset_p95_ms` | 78.7 | 107 | ±25.3 | ahead | 9/10 | #77 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 307 | 232 | ±72.2 | behind | 8/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1945 | 1401 | ±198 | behind | 9/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| outage_3s | `cc.queue_p95_ms` | 52.9 | 39.9 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61, #20 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 1825 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 1825 | 2449 | ±334 | ahead | 9/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 338 | 382 | ±28.5 | ahead | 8/10 | #62, #63 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 598 | 669 | ±51.0 | ahead | 8/10 | #62, #63 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| satellite | `audio.jb_delay_p95_ms` | 281 | 226 | ±20.1 | behind | 8/10 | #62, #63 |
| satellite | `audio.mouth_to_ear_p95_ms` | 637 | 588 | ±31.8 | behind | 6/10 | #62, #63 |
| satellite | `repair.overhead_pct` | 14.5 | 18.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| very_low_capacity | `net.loss_pct` | 21.9 | 18.0 | ±3.8 | behind | 6/10 | #21, #61, #12 |
| very_low_capacity | `repair.overhead_pct` | 41.7 | 55.6 | ±11.1 | ahead | 5/10 | #21, #7, #29 |
| very_low_capacity | `sync.av_offset_p95_ms` | 8393 | 6450 | ±1708 | behind | 7/10 | #77 |
| very_low_capacity | `video.e2e_delay_p95_ms` | 9570 | 7658 | ±1520 | behind | 8/10 | #24, #37 |
| very_low_capacity | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 7/10 | #23 |
| very_low_capacity_allocator_v2 | `net.loss_pct` | 21.9 | 18.0 | ±3.8 | behind | 6/10 | #21, #61, #12 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 41.7 | 55.6 | ±11.1 | ahead | 5/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `sync.av_offset_p95_ms` | 8393 | 6450 | ±1708 | behind | 7/10 | #77 |
| very_low_capacity_allocator_v2 | `video.e2e_delay_p95_ms` | 9570 | 7658 | ±1520 | behind | 8/10 | #24, #37 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 7/10 | #23 |
| wifi_to_lte_handover | `audio.jb_delay_p95_ms` | 381 | 315 | ±23.7 | behind | 7/10 | #62, #63, #20 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1247 | 1422 | ±165 | ahead | 2/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.queue_p95_ms` | 94.2 | 152 | ±48.7 | ahead | 6/10 | #19, #15, #48, #12, #61, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 28.0 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |

