## Parity with libwebrtc M157.8103@{#0}

155 scenarios, mean of 10 seeds. **32 gaps**: 21 behind libwebrtc, 11 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

42 gaps on `main`, 32 here: **12 closed, 2 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.pause_total_ms` | 1744 | 6505 | ±4365 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 245 | 232 | ±12.2 | behind | 4/10 | #783 (closed, no note: #63, #476) |
| issue_154_shallow_drop_repair_v2 | `cc.max_settle_ms` | 4000 | 6020 | ±1849 | ahead | 8/10 | #842 (closed, no note: #15, #48, #61, #20) |
| issue_21_shallow_drop_repair | `cc.max_settle_ms` | 4000 | 6020 | ±1849 | ahead | 8/10 | #842 (closed, no note: #15, #48, #61, #20) |
| issue_437_default_flip_very_low_capacity | `video.pause_total_ms` | 1744 | 6505 | ±4365 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.pause_total_ms` | 1744 | 6505 | ±4365 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `net.loss_pct` | 17.8 | 14.7 | ±2.5 | behind | 8/10 | #466 (closed, no note: #854, #21, #61, #12) |
| issue_466_overshoot_very_low_capacity | `repair.overhead_pct` | 37.4 | 31.4 | ±5.6 | behind | 7/10 | #466 (closed, no note: #854, #21, #7, #29) |
| issue_466_undershoot_very_low_capacity | `video.keyframe_requests` | 5.3 | 4.4 | ±0.8 | behind | 8/10 | #466, #479 (closed, no note: #854, #23) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6703 | 7576 | ±787 | ahead | 6/10 | #466 |
| very_low_capacity | `video.pause_total_ms` | 1744 | 6505 | ±4365 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.pause_total_ms` | 1744 | 6505 | ±4365 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3659 | 561 | ±1774 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 23.8 | 20.7 | ±2.9 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_466_scene_change_very_low_capacity | `video.pause_total_ms` | 5080 | 5682 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 8/10 | #474 |
| issue_12_lossbwe_congestive | `video.e2e_delay_p95_ms` | 416 | 358 | ±53.9 | behind | 5/10 | #825, #474 (closed, no note: #24, #37) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 724 | 1006 | ±211 | ahead | 8/10 | #719, #217, #479, #860 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 766 | 1047 | ±211 | ahead | 8/10 | #719, #217, #479, #860 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `net.loss_pct` | 10.6 | 12.5 | ±1.6 | ahead | 8/10 | #479, #860 (closed, no note: #21, #61, #12) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 48.9 | 62.5 | ±11.6 | ahead | 7/10 | #479, #860 (closed, no note: #21, #7, #29) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 81 | 96 | ±10 | ahead | 4/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 136 | 151 | ±10 | ahead | 6/10 | #411, #479 (closed, no note: #63, #476) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 124 | 112 | ±10 | behind | 3/10 | #783 (closed, no note: #63, #476) |
| issue_224_congestive_e2e_flip | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 8/10 | #474 |
| issue_224_congestive_e2e_flip | `video.e2e_delay_p95_ms` | 416 | 358 | ±53.9 | behind | 5/10 | #825, #474 (closed, no note: #24, #37) |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 156 | 168 | ±11.4 | ahead | 4/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 156 | 168 | ±11.4 | ahead | 4/10 | #224 (closed, no note: #63, #476) |
| issue_256_audio_24khz | `sync.av_offset_p95_ms` | 89.7 | 78.6 | ±11.1 | behind | 6/10 | #861 |
| issue_298_audio_24khz_plays | `sync.av_offset_p95_ms` | 89.7 | 78.6 | ±11.1 | behind | 6/10 | #861 |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1303 | 1051 | ±151 | behind | 6/10 | #769 |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1827 | 1577 | ±150 | behind | 5/10 | #769 (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2006 | 1602 | ±296 | behind | 4/10 | #474 (closed, no note: #5, #37, #24) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.0 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 289 | 221 | ±60.1 | behind | 6/10 | #451 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1206 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_464_start_rate_above_int_bps | `video.longest_freeze_ms` | 561 | 441 | ±100 | behind | 2/10 | #777 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3659 | 561 | ±1774 | behind | 5/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.freeze_total_ms` | 26786 | 21417 | ±3905 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.pause_total_ms` | 4272 | 9953 | ±4815 | ahead | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 23.8 | 20.7 | ±2.9 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 156 | 168 | ±11.4 | ahead | 4/10 | #224 (closed, no note: #63, #476) |
| issue_61_googcc_glue_congestive | `sync.av_offset_p95_ms` | 230 | 181 | ±42.6 | behind | 8/10 | #474 |
| issue_61_googcc_glue_congestive | `video.e2e_delay_p95_ms` | 416 | 358 | ±53.9 | behind | 5/10 | #825, #474 (closed, no note: #24, #37) |
| issue_76_rtx_v2_keyframe_flush | `video.e2e_delay_p95_ms` | 1402 | 1527 | ±96.8 | ahead | 3/10 | #479 (closed, no note: #24, #37) |

