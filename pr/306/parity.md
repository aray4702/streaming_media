## Parity with libwebrtc M157.8103@{#0}

95 scenarios, mean of 10 seeds. **168 gaps**: 83 behind libwebrtc, 85 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

160 gaps on `main`, 168 here: **10 closed, 18 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.keyframe_requests` | 3.9 | 3.2 | ±0.6 | behind | 5/10 | #23 |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 70.3 | 84.4 | ±10 | ahead | 4/10 | #63, #217 |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 29 | 43 | ±10 | ahead | 3/10 | #63, #217 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 40.9 | 44.9 | ±3.3 | ahead | 4/10 | #21, #7, #29 |
| issue_152_alr_request_probe_v2 | `cc.queue_p95_ms` | 221 | 248 | ±22.5 | ahead | 7/10 | #19, #15, #48, #12, #61 |
| issue_29_ulpfec_over_red | `video.pause_total_ms` | 1252 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_76_rtx_v2_keyframe_flush | `audio.longest_interruption_ms` | 175 | 142 | ±20.4 | behind | 5/10 | #63 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 37.2 | 39.8 | ±2 | ahead | 7/10 | #21, #7, #29 |
| very_low_capacity | `video.keyframe_requests` | 3.9 | 3.2 | ±0.6 | behind | 5/10 | #23 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 3.9 | 3.2 | ±0.6 | behind | 5/10 | #23 |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 406 | 278 | ±34.3 | behind | 9/10 | #63, #224 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 321 | 192 | ±33.9 | behind | 9/10 | #63, #224 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1790 | 1401 | ±199 | behind | 9/10 | #77 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 816 | 638 | ±40.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 695 | 517 | ±34.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 816 | 638 | ±40.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 695 | 517 | ±34.8 | behind | 10/10 | #63, #224 |
| issue_256_audio_24khz | `video.e2e_delay_p95_ms` | 201 | 227 | ±19.1 | ahead | 6/10 | #24, #37 |
| issue_298_audio_24khz_plays | `video.e2e_delay_p95_ms` | 201 | 227 | ±19.1 | ahead | 6/10 | #24, #37 |
| issue_29_ulpfec_over_red | `sync.av_offset_p95_ms` | 94.7 | 120 | ±21.0 | ahead | 7/10 | #77 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1790 | 1401 | ±199 | behind | 9/10 | #77 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 816 | 638 | ±40.8 | behind | 10/10 | #63, #224 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 695 | 517 | ±34.8 | behind | 10/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 406 | 278 | ±34.3 | behind | 9/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 321 | 192 | ±33.9 | behind | 9/10 | #63, #224 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1519 | 1268 | ±223 | behind | 7/10 | #5, #37, #24 |
| issue_85_video_pause | `cc.queue_p95_ms` | 46.3 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1790 | 1401 | ±199 | behind | 9/10 | #77 |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| cross_traffic | `cc.queue_p95_ms` | 30.3 | 25.8 (closer) |
| high_jitter | `repair.overhead_pct` | 3.8 | 4.3 (further) |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 2.8 | 3.1 (further) |
| issue_123_v2_allocation_min | `video.render_fps` | 2.9 | 2.3 (closer) |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 20.7 | 16.3 (closer) |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 3.2 | 2.6 (closer) |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 265 | 175 (closer) |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 3.8 | 4.3 (further) |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 18.7 | 32.9 (further) |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 73.0 | 63.9 (closer) |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 30.3 | 25.8 (closer) |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 3.8 | 4.3 (further) |
| issue_23_handover | `repair.overhead_pct` | 2.8 | 3.1 (further) |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 36.5 | 98.3 (further) |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 532 | 321 (closer) |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 3.8 | 4.3 (further) |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 532 | 321 (closer) |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 37 | 25 (closer) |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 443 | 846 (further) |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 209 | 612 (further) |
| issue_257_zero_max_payload | `video.freeze_count` | 2 | 2.8 (further) |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2347 | 2710 (further) |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1651 | 1855 (further) |
| issue_257_zero_max_payload | `video.keyframe_requests` | 3.6 | 4.4 (further) |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2447 | 2052 (closer) |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 4.6 | 5.8 (further) |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 36.5 | 98.3 (further) |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 532 | 321 (closer) |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 30.3 | 25.8 (closer) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 265 | 175 (closer) |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 30.3 | 25.8 (closer) |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 3.8 | 4.3 (further) |
| issue_76_packetization_v2 | `repair.overhead_pct` | 2.3 | 2.1 (closer) |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 2.8 | 3.1 (further) |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 3.8 | 4.3 (further) |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 23.6 | 31.6 (further) |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 35.6 | 24.9 (closer) |
| low_bandwidth | `audio.jb_delay_p95_ms` | 36.5 | 98.3 (further) |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 30.3 | 25.8 (closer) |
| very_low_capacity | `video.render_fps` | 2.9 | 2.3 (closer) |
| very_low_capacity | `repair.overhead_pct` | 20.7 | 16.3 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.9 | 2.3 (closer) |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 20.7 | 16.3 (closer) |
| wifi_to_lte_handover | `repair.overhead_pct` | 2.8 | 3.1 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| capacity_drop | `cc.rtt_p50_ms` | 354 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| cross_traffic | `audio.jb_delay_p95_ms` | 341 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| cross_traffic | `audio.mouth_to_ear_p95_ms` | 600 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| high_jitter | `repair.overhead_pct` | 39.4 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29, #19, #24 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 354 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 39.3 | 55.6 | ±11.1 | ahead | 8/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.render_fps` | 9.5 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 35.1 | 37.7 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `audio.jb_delay_p95_ms` | 321 | 192 | ±33.9 | behind | 9/10 | #63, #224 |
| issue_12_lossbwe_congestive | `audio.mouth_to_ear_p95_ms` | 406 | 278 | ±34.3 | behind | 9/10 | #63, #224 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.1 | 3.8 | ±0.7 | behind | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 445 | 270 | ±88.2 | behind | 9/10 | #77 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 674 | 420 | ±98.3 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 39.4 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_policer | `video.freeze_count` | 3.9 | 6.6 | ±2.5 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 5617 | 10722 | ±3617 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.keyframe_requests` | 4.3 | 2.9 | ±1.4 | behind | 7/10 | #23 |
| issue_12_lossbwe_policer | `video.pause_count` | 2.5 | 1.3 | ±0.8 | behind | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.pause_total_ms` | 15732 | 8280 | ±5343 | behind | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 7.1 | 6.3 | ±0.5 | behind | 7/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 3.8 | 8.9 | ±3.3 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 905 | 1740 | ±635 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 7.7 | 6.7 | ±0.5 | behind | 8/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 80.2 | 113 | ±10 | ahead | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 186 | 209 | ±13.5 | ahead | 7/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 209 | 92.3 | ±10.5 | behind | 6/10 | #61 |
| issue_133_all_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 176 | 94.2 | ±10 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 209 | 92.3 | ±10.5 | behind | 6/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 176 | 94.2 | ±10 | behind | 6/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 39.9 | 43.7 | ±3.2 | ahead | 26/40 | #21, #7, #29 |
| issue_152_alr_request_probe_v2 | `audio.jb_delay_p95_ms` | 337 | 377 | ±21.5 | ahead | 8/10 | #63, #204 |
| issue_152_alr_request_probe_v2 | `audio.mouth_to_ear_p95_ms` | 597 | 661 | ±39.5 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.jb_delay_p95_ms` | 341 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 600 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_15_aimd_v2_cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 39.4 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 354 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| issue_23_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 1853 | 1952 | ±97.6 | ahead | 4/10 | #63, #224 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 316 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 1790 | 1401 | ±199 | behind | 9/10 | #77 |
| issue_23_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 117 | 151 | ±19.7 | ahead | 9/10 | #63, #218 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 193 | 247 | ±33.5 | ahead | 9/10 | #63, #218 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 695 | 517 | ±34.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 816 | 638 | ±40.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_default | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_default | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 7/10 | #61 |
| issue_24_timing_v2_default | `sync.av_offset_p95_ms` | 1816 | 1495 | ±90.8 | behind | 10/10 | #77 |
| issue_24_timing_v2_default | `video.e2e_delay_p95_ms` | 2466 | 1949 | ±123 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_default | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 39.4 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 695 | 517 | ±34.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 816 | 638 | ±40.8 | behind | 10/10 | #63, #224 |
| issue_24_timing_v2_render_fps | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 7/10 | #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 1816 | 1495 | ±90.8 | behind | 10/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2466 | 1949 | ±123 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_256_audio_24khz | `video.e2e_delay_p95_ms` | 201 | 227 | ±19.1 | ahead | 6/10 | #24, #37 |
| issue_257_zero_max_payload | `audio.concealed_pct` | 1.3 | 0.3 | ±0.5 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.jb_delay_p95_ms` | 731 | 119 | ±36.6 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `audio.longest_interruption_ms` | 46 | 21 | ±20 | behind | 4/10 | #63 |
| issue_257_zero_max_payload | `audio.mouth_to_ear_p95_ms` | 1047 | 201 | ±52.3 | behind | 10/10 | #63 |
| issue_257_zero_max_payload | `cc.max_settle_ms` | 1870 | 200 | ±500 | behind | 8/10 | #15, #48, #61, #20 |
| issue_257_zero_max_payload | `cc.queue_p95_ms` | 300 | 45.3 | ±15.0 | behind | 10/10 | #19, #15, #48, #12, #61 |
| issue_257_zero_max_payload | `cc.rtt_p50_ms` | 340 | 65.3 | ±17.0 | behind | 10/10 | #61 |
| issue_257_zero_max_payload | `net.loss_pct` | 12.2 | 0 | ±0.6 | behind | 10/10 | #21, #61, #12 |
| issue_257_zero_max_payload | `repair.overhead_pct` | 14.4 | 0.1 | ±2 | behind | 10/10 | #21, #7, #29 |
| issue_257_zero_max_payload | `sync.av_offset_p95_ms` | 2122 | 69.5 | ±106 | behind | 10/10 | #77 |
| issue_257_zero_max_payload | `video.avg_kbps` | 1522 | 2110 | ±105 | behind | 10/10 | #37, #61 |
| issue_257_zero_max_payload | `video.e2e_delay_p95_ms` | 2872 | 121 | ±144 | behind | 10/10 | #24, #37 |
| issue_257_zero_max_payload | `video.freeze_count` | 2.8 | 0 | ±0.5 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.freeze_total_ms` | 2710 | 0 | ±136 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.keyframe_requests` | 4.4 | 0 | ±0.5 | behind | 6/10 | #23 |
| issue_257_zero_max_payload | `video.longest_freeze_ms` | 1874 | 18.9 | ±100 | behind | 10/10 | #5, #37, #24 |
| issue_257_zero_max_payload | `video.render_fps` | 21.2 | 29.8 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_257_zero_max_payload | `video.time_to_first_frame_ms` | 132 | 53.6 | ±50 | behind | 10/10 | #24, #61 |
| issue_268_freeze_rule_outage | `cc.rtt_p50_ms` | 209 | 92.3 | ±10.5 | behind | 6/10 | #61 |
| issue_268_freeze_rule_outage | `video.freeze_total_ms` | 2881 | 4037 | ±867 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.longest_freeze_ms` | 2822 | 3964 | ±759 | ahead | 3/10 | #5, #37, #24 |
| issue_268_freeze_rule_outage | `video.pause_total_ms` | 1514 | 0 | ±100 | behind | 3/10 | #5, #37, #24 |
| issue_298_audio_24khz_plays | `video.e2e_delay_p95_ms` | 201 | 227 | ±19.1 | ahead | 6/10 | #24, #37 |
| issue_29_ulpfec_over_red | `repair.overhead_pct` | 34.0 | 39.8 | ±2.6 | ahead | 9/10 | #21, #7, #29 |
| issue_29_ulpfec_over_red | `sync.av_offset_p95_ms` | 94.7 | 120 | ±21.0 | ahead | 7/10 | #77 |
| issue_29_ulpfec_over_red | `video.avg_kbps` | 1131 | 1073 | ±56.6 | ahead | 5/10 | #37, #61 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 1853 | 1952 | ±97.6 | ahead | 4/10 | #63, #224 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 316 | 232 | ±72.2 | behind | 8/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 1790 | 1401 | ±199 | behind | 9/10 | #77 |
| issue_48_prober_low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 695 | 517 | ±34.8 | behind | 10/10 | #63, #224 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 816 | 638 | ±40.8 | behind | 10/10 | #63, #224 |
| issue_53_cwnd_pushback | `cc.queue_p95_ms` | 95.6 | 85.4 | ±10 | behind | 7/10 | #19, #15, #48, #12, #61 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 140 | 126 | ±11.9 | behind | 7/10 | #61 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 1816 | 1495 | ±90.8 | behind | 10/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 2466 | 1949 | ±123 | behind | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.render_fps` | 28.9 | 27.4 | ±1.4 | ahead | 7/10 | #37, #24 |
| issue_5_adjuster_default | `audio.jb_delay_p95_ms` | 341 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_5_adjuster_default | `audio.mouth_to_ear_p95_ms` | 600 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_5_adjuster_default | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_5_bitrate_adjuster | `cc.queue_p95_ms` | 47.2 | 36.7 | ±10 | behind | 4/10 | #19, #15, #48, #12, #61 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 357 | 72.5 | ±17.9 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `audio.jb_delay_p95_ms` | 321 | 192 | ±33.9 | behind | 9/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `audio.mouth_to_ear_p95_ms` | 406 | 278 | ±34.3 | behind | 9/10 | #63, #224 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.1 | 3.8 | ±0.7 | behind | 8/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 445 | 270 | ±88.2 | behind | 9/10 | #77 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 674 | 420 | ±98.3 | behind | 9/10 | #24, #37 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 150 | 167 | ±10 | ahead | 8/10 | #63 |
| issue_62_neteq_v2_cross_traffic | `audio.jb_delay_p95_ms` | 341 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 600 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| issue_62_neteq_v2_cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 39.4 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.3 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 30.0 | 32.1 | ±2 | ahead | 5/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 6/10 | #21, #7, #29 |
| issue_76_rtx_v2_keyframe_flush | `video.longest_freeze_ms` | 1519 | 1268 | ±223 | behind | 7/10 | #5, #37, #24 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 39.4 | 43.7 | ±2.5 | ahead | 6/10 | #21, #7, #29 |
| issue_84_encoder_qp_360p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_encoder_qp_720p_low_rate | `sync.av_offset_p95_ms` | 97.7 | 112 | ±11.9 | ahead | 6/10 | #77 |
| issue_84_quality_scaling_rate_rise | `cc.queue_p95_ms` | 54.6 | 42.7 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 129 | 163 | ±26.3 | ahead | 9/10 | #63, #218 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 227 | 283 | ±33.5 | ahead | 10/10 | #63, #218 |
| issue_85_video_pause | `cc.max_settle_ms` | 380 | 2610 | ±500 | ahead | 10/10 | #15, #48, #61, #20 |
| issue_85_video_pause | `cc.queue_p95_ms` | 46.3 | 56.6 | ±10 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 93.1 | 118 | ±17.0 | ahead | 9/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 135 | 167 | ±20.6 | ahead | 9/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 1853 | 1952 | ±97.6 | ahead | 4/10 | #63, #224 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 316 | 232 | ±72.2 | behind | 8/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 1790 | 1401 | ±199 | behind | 9/10 | #77 |
| low_bandwidth | `video.render_fps` | 27.8 | 26.2 | ±1.4 | ahead | 7/10 | #37, #24 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 354 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 354 | 72.2 | ±17.7 | behind | 10/10 | #61 |
| probe_v2_cross_traffic | `audio.jb_delay_p95_ms` | 341 | 382 | ±28.5 | ahead | 8/10 | #63, #204 |
| probe_v2_cross_traffic | `audio.mouth_to_ear_p95_ms` | 600 | 669 | ±51.0 | ahead | 7/10 | #63, #204 |
| probe_v2_cross_traffic | `cc.queue_p95_ms` | 226 | 251 | ±23.2 | ahead | 6/10 | #19, #15, #48, #12, #61 |
| very_low_capacity | `repair.overhead_pct` | 39.3 | 55.6 | ±11.1 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity | `video.render_fps` | 9.5 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 39.3 | 55.6 | ±11.1 | ahead | 8/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.5 | 7.2 | ±1.6 | ahead | 8/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1238 | 1422 | ±165 | ahead | 2/10 | #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.3 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 30.0 | 33.1 | ±2 | ahead | 6/10 | #21, #7, #29, #20 |

