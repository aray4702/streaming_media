## Parity with libwebrtc M157.8103@{#0}

160 scenarios, mean of 10 seeds. **1 gaps**: 1 behind libwebrtc, 0 ahead of it; 0 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

0 gaps on `main`, 1 here: **0 closed, 1 opened**, 0 changed direction.

Opened:

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2746 | 561 | ±1774 | behind | 2/10 | #466 (closed, no note: #5, #37, #24) |

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_466_overshoot_very_low_capacity | `video.pause_total_ms` | 2746 | 561 | ±1774 | behind | 2/10 | #466 (closed, no note: #5, #37, #24) |

