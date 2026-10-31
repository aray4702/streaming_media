## Parity with libwebrtc M157.8103@{#0}

158 scenarios, mean of 10 seeds. **12 gaps**: 4 behind libwebrtc, 8 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

25 gaps on `main`, 12 here: **14 closed, 1 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.pause_count` | 0.2 | 0.8 | ±0.5 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_123_v2_allocation_min | `video.pause_total_ms` | 1201 | 6505 | ±4365 | ahead | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.e2e_delay_p95_ms` | 1739 | 1577 | ±150 | behind | 5/10 | #769 (closed, no note: #24, #37) |
| issue_415_cross_80pct_standing_queue | `sync.av_offset_p95_ms` | 1209 | 1051 | ±151 | behind | 6/10 | #769 |
| issue_437_default_flip_very_low_capacity | `video.pause_count` | 0.2 | 0.8 | ±0.5 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.pause_total_ms` | 1201 | 6505 | ±4365 | ahead | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.pause_count` | 0.2 | 0.8 | ±0.5 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.pause_total_ms` | 1201 | 6505 | ±4365 | ahead | 8/10 | **none** (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 8.8 | 7.2 | ±1.4 | ahead | 7/10 | #466, #479 (closed, no note: #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 5917 | 4944 | ±964 | behind | 4/10 | #466, #479 |
| very_low_capacity | `video.pause_count` | 0.2 | 0.8 | ±0.5 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity | `video.pause_total_ms` | 1201 | 6505 | ±4365 | ahead | 8/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.pause_count` | 0.2 | 0.8 | ±0.5 | ahead | 7/10 | **none** (closed, no note: #5, #37, #24) |
| very_low_capacity_allocator_v2 | `video.pause_total_ms` | 1201 | 6505 | ±4365 | ahead | 8/10 | **none** (closed, no note: #5, #37, #24) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.2 | 9.4 | ±1.7 | ahead | 8/10 | #466 (closed, no note: #37, #24, #411) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 2.3 | 1.7 (closer) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 13.9 | 16.9 (further) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 14 | 17 (further) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 552 | 418 (closer) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 2.3 | 1.7 (closer) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 2.3 | 1.7 (closer) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2908 | 2076 (closer) |
| very_low_capacity | `video.render_fps` | 2.3 | 1.7 (closer) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 2.3 | 1.7 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 79 | 96 | ±10 | ahead | 7/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 134 | 151 | ±10 | ahead | 8/10 | #411, #479 (closed, no note: #63, #476) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2020 | 1602 | ±296 | behind | 5/10 | #474 (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739, #479 (closed, no note: #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2637 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.1 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.2 | 9.4 | ±1.7 | ahead | 8/10 | #466 (closed, no note: #37, #24, #411) |
| very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |

