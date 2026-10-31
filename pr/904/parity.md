## Parity with libwebrtc M157.8103@{#0}

157 scenarios, mean of 10 seeds. **39 gaps**: 10 behind libwebrtc, 29 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

37 gaps on `main`, 39 here: **0 closed, 2 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 733 | 801 | ±49.9 | ahead | 7/10 | #892 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 270 | 326 | ±25.2 | ahead | 8/10 | #892 (closed, no note: #19, #15, #48, #12, #61) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 99.4 | 285 (further) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 66.9 | 86.2 (further) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 4.6 | 13.4 (further) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 2910 | 8090 (further) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 9/10 | #739 (closed, no note: #37, #24) |
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 230 | 244 | ±12.6 | ahead | 4/10 | #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 637 | 1006 | ±211 | ahead | 7/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 679 | 1047 | ±211 | ahead | 7/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `net.loss_pct` | 9.8 | 12.5 | ±1.6 | ahead | 8/10 | #479 (closed, no note: #21, #61, #12, #860, #875) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 46.4 | 62.5 | ±11.6 | ahead | 8/10 | #479 (closed, no note: #21, #7, #29, #860, #875) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 2413 | 3445 | ±385 | ahead | 9/10 | #741 |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2744 | 4019 | ±497 | ahead | 9/10 | #479 (closed, no note: #24, #37) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 85 | 96 | ±10 | ahead | 6/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 140 | 151 | ±10 | ahead | 7/10 | #411, #479 (closed, no note: #63, #476) |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 154 | 168 | ±11.4 | ahead | 5/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_default | `audio.mouth_to_ear_p95_ms` | 265 | 280 | ±14.0 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 154 | 168 | ±11.4 | ahead | 5/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.mouth_to_ear_p95_ms` | 265 | 280 | ±14.0 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 1928 | 1602 | ±296 | behind | 6/10 | #474 (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 9/10 | #739 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 9/10 | #739, #479 (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.mouth_to_ear_p95_ms` | 733 | 801 | ±49.9 | ahead | 7/10 | #892 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 3910 | ±950 | behind | 10/10 | #892 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.queue_p95_ms` | 270 | 326 | ±25.2 | ahead | 8/10 | #892 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.2 | 73.5 | ±3.7 | behind | 10/10 | #892 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 288 | 202 | ±46.2 | behind | 9/10 | #892 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1208 | 1494 | ±74.7 | behind | 10/10 | #892 (closed, no note: #37, #61) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1728 | 1979 | ±250 | ahead | 6/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 2060 | 2311 | ±250 | ahead | 6/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.freeze_total_ms` | 25830 | 30195 | ±2918 | ahead | 7/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_count` | 1 | 0.1 | ±0.5 | behind | 7/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 6403 | 561 | ±1774 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 8.8 | 7.2 | ±1.4 | ahead | 6/10 | #466, #479 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `sync.av_offset_p95_ms` | 6102 | 7576 | ±787 | ahead | 7/10 | #466 |
| issue_466_undershoot_very_low_capacity | `video.e2e_delay_p95_ms` | 7668 | 9166 | ±1159 | ahead | 7/10 | #466 (closed, no note: #24, #37) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 25.2 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 154 | 168 | ±11.4 | ahead | 5/10 | #224 (closed, no note: #63, #476) |
| issue_53_cwnd_pushback | `audio.mouth_to_ear_p95_ms` | 265 | 280 | ±14.0 | ahead | 6/10 | #224 (closed, no note: #63, #476) |
| issue_76_rtx_v2_keyframe_flush | `cc.max_settle_ms` | 13980 | 14720 | ±736 | ahead | 4/10 | #875 (closed, no note: #15, #48, #61, #20) |
| issue_867_nack_reorder_loss | `sync.av_offset_p95_ms` | 125 | 97.1 | ±27.7 | behind | 7/10 | #870 |
| very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 9/10 | #739 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 9/10 | #739 (closed, no note: #37, #24) |

