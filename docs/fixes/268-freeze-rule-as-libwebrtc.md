---
issue: 268
title: Video freeze detection differs from libwebrtc's VideoQualityObserver near the threshold
severity: sev:3
status: fixed
area: area:video
regression_tests: testbench/scenarios/issue_268_freeze_rule_outage.scn, tests/unit/video/video_test.cc
---

# #268: Video freeze detection differs from libwebrtc's VideoQualityObserver near the threshold

## Issue

`video::FreezeDetector` counted freezes differently from libwebrtc's
`VideoQualityObserver` near the threshold and when the frame rate changes, so
`video.freeze_*` was not the number getStats' `freezeCount` and
`totalFreezesDuration` give for the same render times. Found by #63
section 7 (`sm_video_compare`, PR #267); filed by the
Fixer and placed on #64 by the Owner:
https://github.com/aray4702/streaming_media/issues/268.

## Root cause

`src/video/freeze_detector.cc` (`FreezeDetector::OnFrameRendered`, on fcb9607)
differed from `VideoQualityObserver::OnRenderedFrame`
(`compare/webrtc/reference/video/video_quality_observer2.cc:155-185`) in four ways:

1. It tested an interval against the *previous* intervals only; libwebrtc
   adds the current interval to `render_interframe_delays_` first.
2. It left freezes out of the average; libwebrtc keeps them, so after a long
   gap the threshold rises and the stalls that follow are not counted again.
3. It averaged in fractional ms; libwebrtc uses whole-ms intervals
   (`Timestamp::ms()`) and `GetAverageRoundedDown()`.
4. It needed 5 earlier intervals; libwebrtc tests once the window holds
   `kMinFrameSamplesToDetectFreeze` = 5 samples, the current one included.

(`rtc_base/numerics/moving_average.cc` is not in `reference/`; its
behaviour, a 30-sample window whose `GetAverageRoundedDown()` is the integer
sum over the size, is confirmed by `sm_video_compare` giving the same counts.)

## Fix

`FreezeDetector::OnFrameRendered` now does what `OnRenderedFrame` does:
the interval, rounded to whole ms, enters a 30-interval window first
(`kAvgInterframeDelaysWindowSizeFrames`), freezes stay in it, the average
is the integer mean, and the test `delay >= max(3 x avg, avg + 150)`
(`kMinIncreaseForFreezeMs`) runs once 5 intervals are in the window. A freeze's
duration is the whole-ms interval, as libwebrtc's `freezes_durations_`.
Pauses (gaps of 5 s or more, libwebrtc's `OnStreamInactive`) and the harmonic
frame rate are unchanged; they already matched. Both sides of `sm_compare`
use `FreezeDetector`, so `video.freeze_*` changes on both sides alike. The
test bench's count of a freeze still running at the end of the call
(`testbench/call.cc`, `compare/webrtc/webrtc_call.cc`) is its own and is
unchanged.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_268_freeze_rule_outage.scn` (seed 1) | `video.freeze_count` 3 (expect <= 2), `video.freeze_total_ms` 5282 (expect < 5200) | 1, 4710 |
| `tests/unit/video/video_test.cc` `FreezeDetector.CurrentIntervalEntersAverageBeforeTest` | 10 freezes, 2075 ms | 9, 1890 (libwebrtc 9, 1890) |
| `FreezeDetector.FreezesStayInAverage` | 9 freezes, 2997 ms | 7, 2331 (libwebrtc 7, 2331) |
| `FreezeDetector.RandomGapsAsLibwebrtc` | 59 freezes, 26974 ms | 57, 26580 (libwebrtc 57, 26580) |
| `FreezeDetector.TestStartsAtFiveSamplesIncludingCurrent` | 0 freezes (expect 1) | 1 |
| `FreezeDetector.AverageRoundedDownToWholeMs` | 205 ms gap: freeze; 204 ms: freeze (expect none) | 204 ms: none; 205 ms: freeze |

The unit tests replay `sm_video_compare`'s render times and expect
libwebrtc's values from `docs/results/video-webrtc.md`.

## Results

`sm_video_compare`'s freeze inputs, ours after the fix against libwebrtc (all
rows of the freeze block; the other four were already the same):

| input | metric | before | after | libwebrtc |
|---|---|---|---|---|
| 30 fps, one gap of 180, 185, ..., 230 ms every 5 s | freezes / total ms | 10 / 2075 | 9 / 1890 | 9 / 1890 |
| 30 fps, 3 s at 3 fps, 30 fps | freezes / total ms | 9 / 2997 | 7 / 2331 | 7 / 2331 |
| 30 fps, 2% of frames followed by a 100-800 ms gap, 120 s | freezes / total ms | 59 / 26974 | 57 / 26580 | 57 / 26580 |
| 30 fps, render jitter 8 ms; 400 ms gap every 1 s; 15 fps gaps; outages 2/6/1 s | freezes, pauses, harmonic fps | same | same | same |

The reproduction scenario across seeds (before -> after):

| seed | video.freeze_count | video.freeze_total_ms | result |
|---|---|---|---|
| 1 | 3 -> 1 | 5282 -> 4710 | FAIL -> PASS |
| 2 | 1 -> 1 | 3257 -> 3257 | PASS -> PASS |
| 3 | 1 -> 1 | 396 -> 396 | PASS -> PASS |
| 4 | 4 -> 2 | 5643 -> 5070 | FAIL -> PASS |
| 5 | 1 -> 1 | 3259 -> 3259 | PASS -> PASS |

The whole suite on seeds 2 and 3 fails the same scenarios before and after
(seed 2: `issue_133_*_outage_8s`, `issue_152_alr_request_probe_v2`,
`issue_5_adjuster_default`; seed 3: `issue_150_gcc_v2_high_jitter_seed5/10`),
so the change moves no expectation.

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_123_v2_allocation_min | video.freeze_count | 24 | 19 | improved |
| issue_127_all_v2_no_stall | video.freeze_count | 9 | 7 | improved |
| issue_127_all_v2_no_stall | video.freeze_total_ms | 2139 | 1778 | improved |
| issue_12_lossbwe_congestive | video.freeze_total_ms | 1593 | 1410 | improved |
| issue_133_all_v2_outage_3s | video.freeze_count | 3 | 1 | improved |
| issue_133_all_v2_outage_3s | video.freeze_total_ms | 5282 | 4710 | improved |
| issue_133_pacer_v2_outage_3s | video.freeze_count | 3 | 1 | improved |
| issue_133_pacer_v2_outage_3s | video.freeze_total_ms | 5282 | 4710 | improved |
| issue_24_timing_v2_default | video.freeze_total_ms | 3388 | 3155 | improved |
| issue_24_timing_v2_render_fps | video.freeze_total_ms | 3388 | 3155 | improved |
| issue_268_freeze_rule_outage | (new scenario) | - | - | new |
| issue_53_cwnd_pushback | video.freeze_total_ms | 3388 | 3155 | improved |
| issue_61_googcc_glue_congestive | video.freeze_total_ms | 1593 | 1410 | improved |
| very_low_capacity | video.freeze_count | 24 | 19 | improved |
| very_low_capacity_allocator_v2 | video.freeze_count | 24 | 19 | improved |

Only freeze counts and totals change, all downward: stalls right after a
long freeze (an outage, a burst) no longer count as new freezes, because the
long interval stays in the average as in libwebrtc. Nothing got worse. These
are the same render times measured with libwebrtc's rule; the call itself
does not change.
