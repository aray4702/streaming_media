## Parity with libwebrtc M157.8103@{#0}

159 scenarios, mean of 10 seeds. **11 gaps**: 5 behind libwebrtc, 6 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

12 gaps on `main`, 11 here: **2 closed, 1 opened**, 0 changed direction.

Closed:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 134 | 151 | ±10 | ahead | 8/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 79 | 96 | ±10 | ahead | 7/10 | #411, #479 (closed, no note: #63, #476) |

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 124 | 112 | ±10 | behind | 4/10 | #783 (closed, no note: #63, #476) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_123_v2_allocation_min | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.jb_delay_p95_ms` | 124 | 112 | ±10 | behind | 4/10 | #783 (closed, no note: #63, #476) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_415_cross_80pct_standing_queue | `video.longest_freeze_ms` | 2020 | 1602 | ±296 | behind | 5/10 | #474 (closed, no note: #5, #37, #24) |
| issue_437_default_flip_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |
| issue_437_render_smoothing_very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739, #479 (closed, no note: #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2637 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.1 | 20.7 | ±2.9 | behind | 7/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 11.2 | 9.4 | ±1.7 | ahead | 8/10 | #466 (closed, no note: #37, #24, #411) |
| very_low_capacity | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |
| very_low_capacity_allocator_v2 | `video.render_fps` | 9.9 | 8.2 | ±1.2 | ahead | 8/10 | #739 (closed, no note: #37, #24) |

