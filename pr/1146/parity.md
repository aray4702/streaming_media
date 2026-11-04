## Parity with libwebrtc M157.8103@{#0}

165 scenarios, mean of 10 seeds. **28 gaps**: 0 behind libwebrtc, 28 ahead of it; 14 without an owner.

A gap is a metric whose means differ by more than the band, in either direction: the larger of the `sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. `seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. An owner is an open issue, or a closed one whose rule carries a note (an explained difference); closed issues without a note are listed but own nothing.

### Against `main`

28 gaps on `main`, 28 here: **0 closed, 0 opened**, 0 changed direction.

### Gaps

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `audio.jb_delay_p95_ms` | 300 | 1006 | ±211 | ahead | 10/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `audio.mouth_to_ear_p95_ms` | 343 | 1047 | ±211 | ahead | 10/10 | #719, #217, #479 (closed, no note: #63, #476, #860, #875) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 17.2 | 13.4 | ±2 | ahead | 8/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #875) |
| issue_12_lossbwe_policer | `net.loss_pct` | 8.7 | 12.5 | ±1.6 | ahead | 10/10 | #479 (closed, no note: #21, #61, #12, #860, #875) |
| issue_12_lossbwe_policer | `repair.overhead_pct` | 32.6 | 62.5 | ±11.6 | ahead | 10/10 | #479 (closed, no note: #21, #7, #29, #860, #875) |
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 1887 | 3445 | ±385 | ahead | 9/10 | #741 |
| issue_12_lossbwe_policer | `video.avg_kbps` | 189 | 138 | ±20 | ahead | 9/10 | **none** (closed, no note: #37, #61, #875) |
| issue_12_lossbwe_policer | `video.e2e_delay_p95_ms` | 2051 | 4019 | ±497 | ahead | 9/10 | #479 (closed, no note: #24, #37) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_536_loss20_av_sync | `audio.jb_delay_p95_ms` | 176 | 665 | ±449 | ahead | 9/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_loss20_av_sync | `audio.mouth_to_ear_p95_ms` | 240 | 728 | ±447 | ahead | 9/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_loss20_av_sync | `sync.av_offset_p95_ms` | 355 | 1173 | ±652 | ahead | 10/10 | #536, #729 |
| issue_536_loss20_av_sync | `video.e2e_delay_p95_ms` | 512 | 1776 | ±1002 | ahead | 10/10 | #536, #729 (closed, no note: #24, #37) |
| issue_536_reverse_outage_45s | `audio.jb_delay_p95_ms` | 221 | 1978 | ±98.9 | ahead | 10/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_reverse_outage_45s | `audio.mouth_to_ear_p95_ms` | 312 | 2070 | ±104 | ahead | 10/10 | #536, #729 (closed, no note: #63, #476) |
| issue_536_reverse_outage_45s | `sync.av_offset_p95_ms` | 147 | 1595 | ±81.9 | ahead | 10/10 | #536, #729 |
| issue_536_reverse_outage_45s | `video.e2e_delay_p95_ms` | 324 | 2307 | ±115 | ahead | 10/10 | #536, #729 (closed, no note: #24, #37) |
| issue_795_loss20_mouth_to_ear | `audio.jb_delay_p95_ms` | 176 | 665 | ±449 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 240 | 728 | ±447 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `sync.av_offset_p95_ms` | 355 | 1173 | ±652 | ahead | 10/10 | **none** |
| issue_795_loss20_mouth_to_ear | `video.e2e_delay_p95_ms` | 512 | 1776 | ±1002 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_795_reverse_outage_mouth_to_ear | `audio.jb_delay_p95_ms` | 221 | 1978 | ±98.9 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 312 | 2070 | ±104 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `sync.av_offset_p95_ms` | 147 | 1595 | ±81.9 | ahead | 10/10 | **none** |
| issue_795_reverse_outage_mouth_to_ear | `video.e2e_delay_p95_ms` | 324 | 2307 | ±115 | ahead | 10/10 | **none** (closed, no note: #24, #37) |

### Gaps without an owner

| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |
|---|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `video.avg_kbps` | 189 | 138 | ±20 | ahead | 9/10 | **none** (closed, no note: #37, #61, #875) |
| issue_12_lossbwe_policer | `cc.utilization_pct` | 17.2 | 13.4 | ±2 | ahead | 8/10 | **none** (closed, no note: #19, #15, #48, #12, #61, #875) |
| issue_154_shallow_drop_repair_v2 | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_154_shallow_drop_repair_v2 | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_21_shallow_drop_repair | `audio.mouth_to_ear_p95_ms` | 483 | 763 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_21_shallow_drop_repair | `audio.jb_delay_p95_ms` | 430 | 710 | ±191 | ahead | 9/10 | **none** (closed, no note: #913, #63, #476) |
| issue_795_loss20_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 240 | 728 | ±447 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `audio.jb_delay_p95_ms` | 176 | 665 | ±449 | ahead | 9/10 | **none** (closed, no note: #63, #476) |
| issue_795_loss20_mouth_to_ear | `video.e2e_delay_p95_ms` | 512 | 1776 | ±1002 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_795_loss20_mouth_to_ear | `sync.av_offset_p95_ms` | 355 | 1173 | ±652 | ahead | 10/10 | **none** |
| issue_795_reverse_outage_mouth_to_ear | `audio.mouth_to_ear_p95_ms` | 312 | 2070 | ±104 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `audio.jb_delay_p95_ms` | 221 | 1978 | ±98.9 | ahead | 10/10 | **none** (closed, no note: #63, #476) |
| issue_795_reverse_outage_mouth_to_ear | `video.e2e_delay_p95_ms` | 324 | 2307 | ±115 | ahead | 10/10 | **none** (closed, no note: #24, #37) |
| issue_795_reverse_outage_mouth_to_ear | `sync.av_offset_p95_ms` | 147 | 1595 | ±81.9 | ahead | 10/10 | **none** |

