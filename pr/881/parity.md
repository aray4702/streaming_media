## Parity with libwebrtc M157.8103@{#0}

157 scenarios, mean of 10 seeds. **30 gaps**: 9 behind libwebrtc, 21 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

30 gaps on `main`, 30 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 3033 | 3445 | ±385 | ahead | 7/10 | #741 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 3510 | 4019 | ±497 | ahead | 7/10 | #479 (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 82 | 96 | ±10 | ahead | 7/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 137 | 151 | ±10 | ahead | 7/10 | #411, #479 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed10 | `sync.av_offset_p95_ms` | 47.1 | 65.3 | ±14.6 | ahead | 6/10 | #731 |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 152 | 168 | ±11.4 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 263 | 280 | ±14.0 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 152 | 168 | ±11.4 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 263 | 280 | ±14.0 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1942 | 1602 | ±296 | behind | 7/10 | #474 (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 7/10 | #739, #479 (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.0 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 289 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1206 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1693 | 1979 | ±250 | ahead | 7/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 2024 | 2311 | ±250 | ahead | 7/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 25014 | 30195 | ±2918 | ahead | 8/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 0.8 | 0.1 | ±0.5 | behind | 7/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 6117 | 561 | ±1774 | behind | 8/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.5 | 20.7 | ±2.9 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_536_loss20_av_sync | `video.freeze_count` | 21.8 | 27.4 | ±5.0 | ahead | 9/10 | #536, #729 (closed, no note: #5, #37, #24) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 152 | 168 | ±11.4 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 263 | 280 | ±14.0 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1399 | 1527 | ±96.8 | ahead | 5/10 | #479 (closed, no note: #24, #37) |
| very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |

