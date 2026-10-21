## Parity with libwebrtc M157.8103@{#0}

80 scenarios, mean of 10 seeds. **270 gaps**: 136 behind libwebrtc, 134 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction.

### Against `main`

270 gaps on `main`, 270 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| audio_only_lossy | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 143 | 167 | ±10 | ahead | 9/10 | #62, #63 |
| burst_loss | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| burst_loss | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| capacity_drop | `sync.av_offset_p95_ms` | 372 | 286 | ±33.0 | behind | 9/10 | #77 |
| capacity_drop | `video.e2e_delay_p95_ms` | 203 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29, #19, #24 |
| high_jitter | `sync.av_offset_p95_ms` | 76.2 | 58.0 | ±18.0 | behind | 5/10 | #77, #19, #24 |
| high_jitter | `video.e2e_delay_p95_ms` | 202 | 150 | ±25.7 | behind | 9/10 | #24, #37, #19 |
| high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61, #19 |
| issue_120_rtx_payload_padding | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_120_rtx_payload_padding | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_120_rtx_payload_padding | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_120_rtx_payload_padding | `sync.av_offset_p95_ms` | 510 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_120_rtx_payload_padding | `video.e2e_delay_p95_ms` | 832 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| issue_120_rtx_payload_padding_capacity_drop | `sync.av_offset_p95_ms` | 372 | 286 | ±33.0 | behind | 9/10 | #77 |
| issue_120_rtx_payload_padding_capacity_drop | `video.e2e_delay_p95_ms` | 203 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| issue_120_rtx_payload_padding_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| issue_123_v2_allocation_min | `audio.mouth_to_ear_p95_ms` | 552 | 519 | ±27.6 | behind | 5/10 | #62, #63 |
| issue_123_v2_allocation_min | `repair.overhead_pct` | 35.2 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| issue_123_v2_allocation_min | `video.freeze_count` | 12.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| issue_123_v2_allocation_min | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| issue_123_v2_allocation_min | `video.render_fps` | 2.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| issue_124_burst_loss_v2 | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| issue_124_burst_loss_v2 | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| issue_127_all_v2_no_stall | `repair.overhead_pct` | 31.9 | 37.7 | ±2 | ahead | 10/10 | #21, #7, #29 |
| issue_127_all_v2_no_stall | `video.freeze_count` | 3 | 11.8 | ±6.3 | ahead | 9/10 | #5, #37, #24 |
| issue_127_all_v2_no_stall | `video.freeze_total_ms` | 852 | 3072 | ±1628 | ahead | 9/10 | #5, #37, #24 |
| issue_127_pacer_v2_jitter_reorder_loss | `video.e2e_delay_p95_ms` | 215 | 185 | ±13.7 | behind | 8/10 | #24, #37 |
| issue_12_lossbwe_burst_loss | `sync.av_offset_p95_ms` | 62.0 | 102 | ±18.8 | ahead | 10/10 | #77 |
| issue_12_lossbwe_burst_loss | `video.e2e_delay_p95_ms` | 211 | 275 | ±32.3 | ahead | 9/10 | #24, #37 |
| issue_12_lossbwe_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_12_lossbwe_congestive | `net.loss_pct` | 5.3 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_12_lossbwe_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_congestive | `video.render_fps` | 25.9 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_12_lossbwe_high_jitter_no_loss | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_12_lossbwe_high_jitter_no_loss | `sync.av_offset_p95_ms` | 76.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_12_lossbwe_high_jitter_no_loss | `video.e2e_delay_p95_ms` | 202 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_12_lossbwe_high_jitter_no_loss | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2264 | 3204 | ±494 | ahead | 7/10 | #77 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2329 | 3282 | ±495 | ahead | 7/10 | #24, #37 |
| issue_12_lossbwe_policer | `video.freeze_count` | 4 | 7 | ±2.7 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.freeze_total_ms` | 5628 | 10663 | ±3593 | ahead | 7/10 | #5, #37, #24 |
| issue_12_lossbwe_policer | `video.longest_freeze_ms` | 2329 | 3230 | ±749 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `sync.av_offset_p95_ms` | 159 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_12_lossbwe_random_loss | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_random_loss | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.freeze_total_ms` | 69.4 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_12_lossbwe_random_loss | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `net.loss_pct` | 6.9 | 6.3 | ±0.5 | behind | 5/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow | `sync.av_offset_p95_ms` | 246 | 149 | ±12.3 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow | `video.e2e_delay_p95_ms` | 321 | 226 | ±16.0 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow | `video.freeze_count` | 0.1 | 14.8 | ±5.7 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.freeze_total_ms` | 36.8 | 2820 | ±1084 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow | `video.longest_freeze_ms` | 56.2 | 217 | ±100 | ahead | 9/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `cc.utilization_pct` | 59.4 | 56.3 | ±3.0 | ahead | 5/10 | #19, #15, #48, #12, #61 |
| issue_12_lossbwe_shallow_step | `net.loss_pct` | 8.3 | 6.7 | ±0.5 | behind | 10/10 | #21, #61, #12 |
| issue_12_lossbwe_shallow_step | `repair.overhead_pct` | 15.1 | 12.7 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_12_lossbwe_shallow_step | `sync.av_offset_p95_ms` | 360 | 130 | ±18.0 | behind | 10/10 | #77 |
| issue_12_lossbwe_shallow_step | `video.avg_kbps` | 775 | 736 | ±38.8 | ahead | 5/10 | #37, #61 |
| issue_12_lossbwe_shallow_step | `video.e2e_delay_p95_ms` | 436 | 209 | ±21.8 | behind | 10/10 | #24, #37 |
| issue_12_lossbwe_shallow_step | `video.freeze_count` | 0 | 4.3 | ±2.9 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.freeze_total_ms` | 0 | 842 | ±556 | ahead | 10/10 | #5, #37, #24 |
| issue_12_lossbwe_shallow_step | `video.longest_freeze_ms` | 16.2 | 211 | ±100 | ahead | 10/10 | #5, #37, #24 |
| issue_133_all_v2_outage_15s | `video.e2e_delay_p95_ms` | 169 | 92.4 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_all_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_all_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_all_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_all_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_15s | `video.e2e_delay_p95_ms` | 169 | 92.4 | ±10 | behind | 10/10 | #24, #37 |
| issue_133_pacer_v2_outage_3s | `cc.rtt_p50_ms` | 125 | 92.3 | ±10 | behind | 4/10 | #61 |
| issue_133_pacer_v2_outage_3s | `video.pause_total_ms` | 1010 | 0 | ±100 | behind | 2/10 | #5, #37, #24 |
| issue_133_pacer_v2_outage_8s | `cc.max_settle_ms` | 4760 | 3170 | ±1172 | behind | 3/10 | #15, #48, #61, #20 |
| issue_133_pacer_v2_outage_8s | `cc.rtt_p50_ms` | 114 | 94.2 | ±10 | behind | 4/10 | #61 |
| issue_150_gcc_v2_high_jitter | `repair.overhead_pct` | 35.5 | 43.7 | ±3.2 | ahead | 34/40 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 189 | 148 | ±15.6 | behind | 34/40 | #24, #37 |
| issue_150_gcc_v2_high_jitter | `video.time_to_first_frame_ms` | 180 | 96.5 | ±50 | behind | 23/40 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed10 | `repair.overhead_pct` | 36.3 | 43.6 | ±3.8 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.e2e_delay_p95_ms` | 193 | 144 | ±10 | behind | 10/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed10 | `video.time_to_first_frame_ms` | 179 | 88.6 | ±50 | behind | 6/10 | #24, #61 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 34.4 | 44.9 | ±3.3 | ahead | 9/10 | #21, #7, #29 |
| issue_150_gcc_v2_high_jitter_seed5 | `sync.av_offset_p95_ms` | 77.1 | 51.4 | ±15.9 | behind | 5/10 | #77 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.e2e_delay_p95_ms` | 204 | 147 | ±26.1 | behind | 9/10 | #24, #37 |
| issue_150_gcc_v2_high_jitter_seed5 | `video.time_to_first_frame_ms` | 196 | 89.4 | ±50 | behind | 7/10 | #24, #61 |
| issue_151_outage_3s_v2 | `audio.jb_delay_p95_ms` | 144 | 119 | ±19.1 | behind | 4/10 | #62, #63 |
| issue_151_outage_3s_v2 | `audio.mouth_to_ear_p95_ms` | 247 | 202 | ±20.3 | behind | 6/10 | #62, #63 |
| issue_151_outage_3s_v2 | `cc.queue_p95_ms` | 64.2 | 40.2 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_151_outage_3s_v2 | `sync.av_offset_p95_ms` | 101 | 71.8 | ±23.6 | behind | 4/10 | #77 |
| issue_151_outage_3s_v2 | `video.e2e_delay_p95_ms` | 154 | 113 | ±10 | behind | 4/10 | #24, #37 |
| issue_151_outage_3s_v2 | `video.freeze_total_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_151_outage_3s_v2 | `video.longest_freeze_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_152_alr_request_probe_v2 | `cc.rtt_p50_ms` | 87.0 | 65.7 | ±10 | behind | 1/10 | #61 |
| issue_154_shallow_drop_repair_v2 | `sync.av_offset_p95_ms` | 1463 | 2549 | ±989 | ahead | 9/10 | #77 |
| issue_154_shallow_drop_repair_v2 | `video.e2e_delay_p95_ms` | 1530 | 2624 | ±988 | ahead | 9/10 | #24, #37 |
| issue_154_shallow_drop_repair_v2 | `video.freeze_count` | 3 | 7.1 | ±3.3 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_15_aimd_v2_random_loss | `sync.av_offset_p95_ms` | 159 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_15_aimd_v2_random_loss | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_15_aimd_v2_random_loss | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.freeze_total_ms` | 69.4 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_15_aimd_v2_random_loss | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `sync.av_offset_p95_ms` | 159 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_18_random_loss_keyframes | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_18_random_loss_keyframes | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.freeze_total_ms` | 69.4 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_18_random_loss_keyframes | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_19_cc_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_19_cc_v2_high_jitter | `sync.av_offset_p95_ms` | 76.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_19_cc_v2_high_jitter | `video.e2e_delay_p95_ms` | 202 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_19_cc_v2_high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_21_shallow_drop_repair | `sync.av_offset_p95_ms` | 1463 | 2549 | ±989 | ahead | 9/10 | #77 |
| issue_21_shallow_drop_repair | `video.e2e_delay_p95_ms` | 1530 | 2624 | ±988 | ahead | 9/10 | #24, #37 |
| issue_21_shallow_drop_repair | `video.freeze_count` | 3 | 7.1 | ±3.3 | ahead | 8/10 | #5, #37, #24 |
| issue_23_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| issue_23_capacity_drop | `sync.av_offset_p95_ms` | 372 | 286 | ±33.0 | behind | 9/10 | #77 |
| issue_23_capacity_drop | `video.e2e_delay_p95_ms` | 203 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| issue_23_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| issue_23_handover | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_23_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_23_handover | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_23_handover | `sync.av_offset_p95_ms` | 510 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_23_handover | `video.e2e_delay_p95_ms` | 832 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_23_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.jb_delay_p95_ms` | 320 | 353 | ±29.5 | ahead | 7/10 | #62, #63 |
| issue_23_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_23_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_23_low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| issue_23_low_bandwidth | `video.e2e_delay_p95_ms` | 2404 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| issue_23_outage_3s | `audio.jb_delay_p95_ms` | 144 | 119 | ±19.1 | behind | 4/10 | #62, #63 |
| issue_23_outage_3s | `audio.mouth_to_ear_p95_ms` | 247 | 202 | ±20.3 | behind | 6/10 | #62, #63 |
| issue_23_outage_3s | `cc.queue_p95_ms` | 64.2 | 40.2 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61 |
| issue_23_outage_3s | `sync.av_offset_p95_ms` | 101 | 71.8 | ±23.6 | behind | 4/10 | #77 |
| issue_23_outage_3s | `video.e2e_delay_p95_ms` | 154 | 113 | ±10 | behind | 4/10 | #24, #37 |
| issue_23_outage_3s | `video.freeze_total_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_3s | `video.longest_freeze_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24 |
| issue_23_outage_500ms | `audio.jb_delay_p95_ms` | 118 | 151 | ±19.7 | ahead | 10/10 | #62, #63 |
| issue_23_outage_500ms | `audio.mouth_to_ear_p95_ms` | 192 | 247 | ±33.5 | ahead | 10/10 | #62, #63 |
| issue_23_outage_500ms | `video.e2e_delay_p95_ms` | 152 | 180 | ±27.1 | ahead | 7/10 | #24, #37 |
| issue_24_timing_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 102 | 128 | ±23.0 | ahead | 6/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 223 | 249 | ±22.9 | ahead | 6/10 | #62, #63 |
| issue_24_timing_v2_render_fps | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_24_timing_v2_render_fps | `sync.av_offset_p95_ms` | 2165 | 1759 | ±108 | behind | 10/10 | #77 |
| issue_24_timing_v2_render_fps | `video.e2e_delay_p95_ms` | 2354 | 1949 | ±118 | behind | 10/10 | #24, #37 |
| issue_24_timing_v2_render_fps | `video.longest_freeze_ms` | 1111 | 875 | ±186 | behind | 8/10 | #5, #37, #24 |
| issue_24_timing_v2_render_fps | `video.render_fps` | 29.0 | 27.4 | ±1.4 | ahead | 8/10 | #37, #24 |
| issue_48_prober_low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.jb_delay_p95_ms` | 320 | 353 | ±29.5 | ahead | 7/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| issue_48_prober_low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| issue_48_prober_low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| issue_48_prober_low_bandwidth | `video.e2e_delay_p95_ms` | 2404 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 102 | 128 | ±23.0 | ahead | 6/10 | #62, #63 |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 223 | 249 | ±22.9 | ahead | 6/10 | #62, #63 |
| issue_53_cwnd_pushback | `cc.rtt_p50_ms` | 139 | 126 | ±11.9 | behind | 6/10 | #61 |
| issue_53_cwnd_pushback | `sync.av_offset_p95_ms` | 1430 | 1759 | ±107 | ahead | 10/10 | #77 |
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 1607 | 1949 | ±107 | ahead | 10/10 | #24, #37 |
| issue_53_cwnd_pushback | `video.freeze_count` | 11.3 | 7.3 | ±2.1 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 4679 | 2779 | ±615 | behind | 10/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 1125 | 875 | ±186 | behind | 8/10 | #5, #37, #24 |
| issue_53_cwnd_pushback | `video.render_fps` | 18.2 | 27.4 | ±1.4 | behind | 10/10 | #37, #24 |
| issue_5_adjuster_default | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_5_bitrate_adjuster | `sync.av_offset_p95_ms` | 29.9 | 43.5 | ±10 | ahead | 6/10 | #77 |
| issue_5_burst15_freeze | `sync.av_offset_p95_ms` | 67.0 | 102 | ±30.1 | ahead | 10/10 | #77 |
| issue_5_burst15_freeze | `video.e2e_delay_p95_ms` | 191 | 263 | ±36.6 | ahead | 10/10 | #24, #37 |
| issue_5_burst15_freeze | `video.freeze_count` | 3.6 | 1.5 | ±1.4 | behind | 8/10 | #5, #37, #24 |
| issue_5_burst15_freeze | `video.freeze_total_ms` | 903 | 361 | ±330 | behind | 8/10 | #5, #37, #24 |
| issue_5_keyframe_flush | `cc.rtt_p50_ms` | 364 | 72.5 | ±18.2 | behind | 10/10 | #61 |
| issue_5_keyframe_flush | `sync.av_offset_p95_ms` | 365 | 289 | ±37.0 | behind | 8/10 | #77 |
| issue_5_keyframe_flush | `video.e2e_delay_p95_ms` | 215 | 392 | ±123 | ahead | 10/10 | #24, #37 |
| issue_5_keyframe_flush | `video.freeze_total_ms` | 1997 | 2643 | ±377 | ahead | 10/10 | #5, #37, #24 |
| issue_61_googcc_glue_congestive | `cc.rtt_p50_ms` | 102 | 81.2 | ±10 | behind | 10/10 | #61 |
| issue_61_googcc_glue_congestive | `net.loss_pct` | 5.3 | 3.8 | ±0.7 | behind | 9/10 | #21, #61, #12 |
| issue_61_googcc_glue_congestive | `repair.overhead_pct` | 9.5 | 7.5 | ±2 | behind | 6/10 | #21, #7, #29 |
| issue_61_googcc_glue_congestive | `video.render_fps` | 25.9 | 29.9 | ±1.5 | behind | 10/10 | #37, #24 |
| issue_62_neteq_default | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| issue_62_neteq_default | `audio.mouth_to_ear_p95_ms` | 143 | 167 | ±10 | ahead | 9/10 | #62, #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.jb_delay_p95_ms` | 70.8 | 81 | ±10 | ahead | 2/10 | #62, #63 |
| issue_62_neteq_v2_audio_only_lossy | `audio.mouth_to_ear_p95_ms` | 143 | 167 | ±10 | ahead | 9/10 | #62, #63 |
| issue_62_neteq_v2_cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| issue_62_neteq_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_62_neteq_v2_high_jitter | `sync.av_offset_p95_ms` | 76.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_62_neteq_v2_high_jitter | `video.e2e_delay_p95_ms` | 202 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_62_neteq_v2_high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_62_opus_fec_low_rate | `audio.jb_delay_p95_ms` | 74.6 | 64 | ±10 | behind | 7/10 | #62, #63 |
| issue_62_opus_fec_low_rate | `cc.utilization_pct` | 90.1 | 84.3 | ±4.5 | ahead | 8/10 | #19, #15, #48, #12, #61 |
| issue_76_packetization_v2 | `repair.overhead_pct` | 29.1 | 32.1 | ±2 | ahead | 7/10 | #21, #7, #29 |
| issue_76_packetization_v2 | `video.e2e_delay_p95_ms` | 179 | 149 | ±10 | behind | 9/10 | #24, #37 |
| issue_76_rtx_v2_handover | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63 |
| issue_76_rtx_v2_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61 |
| issue_76_rtx_v2_handover | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29 |
| issue_76_rtx_v2_handover | `sync.av_offset_p95_ms` | 510 | 1832 | ±467 | ahead | 10/10 | #77 |
| issue_76_rtx_v2_handover | `video.e2e_delay_p95_ms` | 832 | 2259 | ±508 | ahead | 10/10 | #24, #37 |
| issue_76_rtx_v2_keyframe_flush | `cc.rtt_p50_ms` | 85.3 | 70.8 | ±10 | behind | 2/10 | #61 |
| issue_76_rtx_v2_keyframe_flush | `sync.av_offset_p95_ms` | 211 | 737 | ±198 | ahead | 10/10 | #77 |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 491 | 1221 | ±198 | ahead | 10/10 | #24, #37 |
| issue_76_session_layer_v2 | `repair.overhead_pct` | 36.3 | 39.8 | ±2 | ahead | 8/10 | #21, #7, #29 |
| issue_76_session_layer_v2 | `sync.av_offset_p95_ms` | 47.2 | 80.9 | ±23.2 | ahead | 8/10 | #77 |
| issue_76_session_layer_v2 | `video.e2e_delay_p95_ms` | 178 | 156 | ±10 | behind | 9/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `repair.overhead_pct` | 34.0 | 43.7 | ±2.5 | ahead | 8/10 | #21, #7, #29 |
| issue_76_tcc_v2_high_jitter | `sync.av_offset_p95_ms` | 76.2 | 58.0 | ±18.0 | behind | 5/10 | #77 |
| issue_76_tcc_v2_high_jitter | `video.e2e_delay_p95_ms` | 202 | 150 | ±25.7 | behind | 9/10 | #24, #37 |
| issue_76_tcc_v2_high_jitter | `video.time_to_first_frame_ms` | 209 | 92.6 | ±50 | behind | 7/10 | #24, #61 |
| issue_7_no_fec_beside_nack | `sync.av_offset_p95_ms` | 159 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| issue_7_no_fec_beside_nack | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| issue_7_no_fec_beside_nack | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.freeze_total_ms` | 69.4 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| issue_7_no_fec_beside_nack | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| issue_85_video_pause | `audio.interruptions` | 3 | 1.1 | ±0.5 | behind | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.jb_delay_p95_ms` | 133 | 163 | ±24.5 | ahead | 8/10 | #62, #63 |
| issue_85_video_pause | `audio.longest_interruption_ms` | 9032 | 9942 | ±497 | ahead | 1/10 | #62, #63 |
| issue_85_video_pause | `audio.mouth_to_ear_p95_ms` | 233 | 282 | ±32.0 | ahead | 9/10 | #62, #63 |
| issue_85_video_pause | `sync.av_offset_p95_ms` | 86.9 | 131 | ±22.5 | ahead | 10/10 | #77 |
| issue_85_video_pause | `video.e2e_delay_p95_ms` | 176 | 156 | ±17.1 | behind | 8/10 | #24, #37 |
| lossy_feedback | `video.e2e_delay_p95_ms` | 88.7 | 119 | ±15.5 | ahead | 9/10 | #24, #37 |
| low_bandwidth | `audio.interruptions` | 1 | 0 | ±0.5 | behind | 10/10 | #62, #63 |
| low_bandwidth | `audio.jb_delay_p95_ms` | 320 | 353 | ±29.5 | ahead | 7/10 | #62, #63 |
| low_bandwidth | `audio.longest_interruption_ms` | 199 | 150 | ±20 | behind | 10/10 | #62, #63 |
| low_bandwidth | `cc.rtt_p50_ms` | 320 | 232 | ±72.2 | behind | 7/10 | #61 |
| low_bandwidth | `sync.av_offset_p95_ms` | 2011 | 2487 | ±237 | ahead | 9/10 | #77 |
| low_bandwidth | `video.e2e_delay_p95_ms` | 2404 | 2950 | ±348 | ahead | 10/10 | #24, #37 |
| outage_3s | `audio.jb_delay_p95_ms` | 144 | 119 | ±19.1 | behind | 4/10 | #62, #63, #20 |
| outage_3s | `audio.mouth_to_ear_p95_ms` | 247 | 202 | ±20.3 | behind | 6/10 | #62, #63, #20 |
| outage_3s | `cc.queue_p95_ms` | 64.2 | 40.2 | ±10 | behind | 6/10 | #19, #15, #48, #12, #61, #20 |
| outage_3s | `sync.av_offset_p95_ms` | 101 | 71.8 | ±23.6 | behind | 4/10 | #77, #20 |
| outage_3s | `video.e2e_delay_p95_ms` | 154 | 113 | ±10 | behind | 4/10 | #24, #37, #20 |
| outage_3s | `video.freeze_total_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24, #20 |
| outage_3s | `video.longest_freeze_ms` | 3413 | 3240 | ±171 | behind | 2/10 | #5, #37, #24, #20 |
| pacer_v2_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| pacer_v2_capacity_drop | `sync.av_offset_p95_ms` | 372 | 286 | ±33.0 | behind | 9/10 | #77 |
| pacer_v2_capacity_drop | `video.e2e_delay_p95_ms` | 203 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| pacer_v2_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_capacity_drop | `cc.rtt_p50_ms` | 366 | 72.2 | ±18.3 | behind | 10/10 | #61 |
| probe_v2_capacity_drop | `sync.av_offset_p95_ms` | 372 | 286 | ±33.0 | behind | 9/10 | #77 |
| probe_v2_capacity_drop | `video.e2e_delay_p95_ms` | 203 | 387 | ±125 | ahead | 10/10 | #24, #37 |
| probe_v2_capacity_drop | `video.freeze_total_ms` | 1965 | 2613 | ±382 | ahead | 10/10 | #5, #37, #24 |
| probe_v2_cross_traffic | `cc.rtt_p50_ms` | 87.2 | 64.5 | ±10 | behind | 1/10 | #61 |
| random_loss_5pct | `sync.av_offset_p95_ms` | 159 | 85.1 | ±15.0 | behind | 10/10 | #77 |
| random_loss_5pct | `video.e2e_delay_p95_ms` | 292 | 224 | ±21.9 | behind | 10/10 | #24, #37 |
| random_loss_5pct | `video.freeze_count` | 0.3 | 1.8 | ±1.3 | ahead | 8/10 | #5, #37, #24 |
| random_loss_5pct | `video.freeze_total_ms` | 69.4 | 415 | ±318 | ahead | 8/10 | #5, #37, #24 |
| random_loss_5pct | `video.longest_freeze_ms` | 78.6 | 209 | ±110 | ahead | 6/10 | #5, #37, #24 |
| satellite | `repair.overhead_pct` | 14.9 | 18.3 | ±2 | ahead | 10/10 | #21, #7, #29 |
| satellite | `sync.av_offset_p95_ms` | 454 | 497 | ±29.2 | ahead | 8/10 | #77 |
| satellite | `video.e2e_delay_p95_ms` | 875 | 927 | ±46.4 | ahead | 6/10 | #24, #37 |
| satellite | `video.freeze_count` | 20 | 30.6 | ±4.7 | ahead | 7/10 | #5, #37, #24 |
| satellite | `video.freeze_total_ms` | 6369 | 13227 | ±2008 | ahead | 10/10 | #5, #37, #24 |
| satellite | `video.render_fps` | 24.8 | 29.5 | ±1.5 | behind | 10/10 | #37, #24 |
| very_low_capacity | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| very_low_capacity | `audio.mouth_to_ear_p95_ms` | 552 | 519 | ±27.6 | behind | 5/10 | #62, #63 |
| very_low_capacity | `repair.overhead_pct` | 35.2 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity | `video.freeze_count` | 12.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity | `video.render_fps` | 2.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| very_low_capacity_allocator_v2 | `audio.jb_delay_p95_ms` | 219 | 186 | ±24.5 | behind | 7/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `audio.mouth_to_ear_p95_ms` | 552 | 519 | ±27.6 | behind | 5/10 | #62, #63 |
| very_low_capacity_allocator_v2 | `repair.overhead_pct` | 35.2 | 55.6 | ±11.1 | ahead | 9/10 | #21, #7, #29 |
| very_low_capacity_allocator_v2 | `video.freeze_count` | 12.7 | 21.9 | ±5.4 | ahead | 9/10 | #5, #37, #24 |
| very_low_capacity_allocator_v2 | `video.keyframe_requests` | 4.1 | 3.2 | ±0.6 | behind | 6/10 | #23 |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.4 | 7.2 | ±1.6 | behind | 10/10 | #37, #24 |
| wifi_to_lte_handover | `audio.longest_interruption_ms` | 1231 | 1422 | ±165 | ahead | 2/10 | #62, #63, #20 |
| wifi_to_lte_handover | `cc.rtt_p50_ms` | 50.4 | 134 | ±10 | ahead | 10/10 | #61, #20 |
| wifi_to_lte_handover | `repair.overhead_pct` | 27.7 | 33.1 | ±2 | ahead | 9/10 | #21, #7, #29, #20 |
| wifi_to_lte_handover | `sync.av_offset_p95_ms` | 510 | 1832 | ±467 | ahead | 10/10 | #77, #20 |
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 832 | 2259 | ±508 | ahead | 10/10 | #24, #37, #20 |

