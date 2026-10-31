## Parity with libwebrtc M157.8103@{#0}

158 scenarios, mean of 10 seeds. **7 gaps**: 2 behind libwebrtc, 5 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

4 gaps on `main`, 7 here: **0 closed, 3 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 155 | 168 | ±11.4 | ahead | 3/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 155 | 168 | ±11.4 | ahead | 3/10 | #224 (closed, no note: #63, #476) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 155 | 168 | ±11.4 | ahead | 3/10 | #224 (closed, no note: #63, #476) |

Still gaps, distance to libwebrtc changed by more than 10%:

| scenario | metric | distance on main | distance here |
|---|---|---|---|
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 16.0 | 13.0 (closer) |
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 16 | 13 (closer) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 83 | 96 | ±10 | ahead | 5/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 138 | 151 | ±10 | ahead | 6/10 | #411, #479 (closed, no note: #63, #476) |
| issue_24_timing_v2_default | `audio.jb_delay_p95_ms` | 155 | 168 | ±11.4 | ahead | 3/10 | #224 (closed, no note: #63, #476) |
| issue_24_timing_v2_render_fps | `audio.jb_delay_p95_ms` | 155 | 168 | ±11.4 | ahead | 3/10 | #224 (closed, no note: #63, #476) |
| issue_335_fps1_keyframe_packets | `video.pause_total_ms` | 581 | 0 | ±100 | behind | 1/10 | #770 (closed, no note: #5, #37, #24) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3670 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_53_cwnd_pushback | `audio.jb_delay_p95_ms` | 155 | 168 | ±11.4 | ahead | 3/10 | #224 (closed, no note: #63, #476) |

