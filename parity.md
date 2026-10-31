## Parity with libwebrtc M157.8103@{#0}

157 scenarios, mean of 10 seeds. **6 gaps**: 3 behind libwebrtc, 3 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_shallow | `audio.jb_delay_p95_ms` | 84 | 96 | ±10 | ahead | 4/10 | #411, #479 (closed, no note: #63, #476) |
| issue_12_lossbwe_shallow | `audio.mouth_to_ear_p95_ms` | 139 | 151 | ±10 | ahead | 5/10 | #411, #479 (closed, no note: #63, #476) |
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 3470 | 561 | ±1774 | behind | 4/10 | #466 (closed, no note: #5, #37, #24) |
| issue_466_scene_change_very_low_capacity | `sync.av_offset_p95_ms` | 6022 | 4944 | ±964 | behind | 5/10 | #466, #479 |
| issue_466_scene_change_very_low_capacity | `video.render_fps` | 8.7 | 7.2 | ±1.4 | ahead | 8/10 | #466, #479 (closed, no note: #37, #24) |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.9 | 20.7 | ±2.9 | behind | 6/10 | #466 (closed, no note: #5, #37, #24) |

