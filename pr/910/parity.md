## Parity with libwebrtc M157.8103@{#0}

158 scenarios, mean of 10 seeds. **28 gaps**: 11 behind libwebrtc, 17 ahead of it; 4 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

25 gaps on `main`, 28 here: **1 closed, 4 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_127_all_v2_no_stall | `audio.jb_delay_p95_ms` | 229 | 244 | ±12.6 | ahead | 5/10 | #479 (closed, no note: #63, #476) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783, #20) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_123_v2_allocation_min | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 3055 | 3445 | ±385 | ahead | 4/10 | #741 |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 79 | 96 | ±10 | ahead | 5/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 134 | 151 | ±10 | ahead | 7/10 | #411, #479 (closed, no note: #63, #476) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2104 | 1602 | ±296 | behind | 7/10 | #474 (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739, #479 (closed, no note: #37, #24) |
| issue_451_host_rtt_near_limit | `audio.longest_interruption_ms` | 114 | 74.6 | ±34.8 | behind | 4/10 | #451 (closed, no note: #63, #476) |
| issue_451_host_rtt_near_limit | `cc.max_settle_ms` | 12000 | 9090 | ±943 | behind | 10/10 | #451 (closed, no note: #15, #48, #61, #20) |
| issue_451_host_rtt_near_limit | `cc.utilization_pct` | 60.0 | 64.8 | ±3.2 | behind | 10/10 | #451 (closed, no note: #19, #15, #48, #12, #61) |
| issue_451_host_rtt_near_limit | `sync.av_offset_p95_ms` | 298 | 221 | ±60.1 | behind | 7/10 | #451 |
| issue_451_host_rtt_near_limit | `video.avg_kbps` | 1205 | 1308 | ±65.4 | behind | 10/10 | #451 (closed, no note: #37, #61) |
| issue_451_host_rtt_near_limit | `video.e2e_delay_p95_ms` | 318 | 297 | ±17.2 | behind | 4/10 | #451 (closed, no note: #24, #37) |
| issue_466_overshoot_very_low_capacity | `audio.jb_delay_p95_ms` | 1560 | 1979 | ±250 | ahead | 9/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `audio.mouth_to_ear_p95_ms` | 1891 | 2311 | ±250 | ahead | 9/10 | #466, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3222 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5945 | 4944 | ±964 | behind | 5/10 | #466, #479 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 8.8 | 7.2 | ±1.4 | ahead | 7/10 | #466, #479 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.pause_total_ms` | 4187 | 1245 | ±2661 | behind | 5/10 | #466, #479 (closed, no note: #5, #37, #24) |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_867_nack_reorder_loss | `video.freeze_total_ms` | 22.8 | 331 | ±293 | ahead | 7/10 | #870 (closed, no note: #5, #37, #24) |
| issue_867_nack_reorder_loss | `video.longest_freeze_ms` | 38.2 | 207 | ±148 | ahead | 6/10 | #870 (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.7 | 8.2 | ±1.2 | ahead | 7/10 | #739 (closed, no note: #37, #24) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783, #20) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_120_rtx_payload_padding | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_23_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| issue_76_rtx_v2_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783) |
| wifi_to_lte_handover | `audio.mouth_to_ear_p95_ms` | 416 | 519 | ±88.9 | ahead | 8/10 | **none** (closed, no note: #63, #476, #783, #20) |

