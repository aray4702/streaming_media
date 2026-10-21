---
issue: 283
title: Test bench video.longest_freeze_ms misreports short freezes when metrics are compiled out
severity: sev:3
status: fixed
area: area:testbench, area:video
regression_tests: testbench/scenarios/issue_283_longest_freeze_no_metrics.scn, tests/unit/video/video_test.cc
---

# #283: Test bench video.longest_freeze_ms misreports short freezes when metrics are compiled out

## Issue

On the `embedded` preset (`SM_ENABLE_METRICS=OFF`), `sm_testbench` reported
`video.longest_freeze_ms` as the render gap at the end of the call (tens of
ms) instead of the longest freeze: 19 ms instead of 1575 ms on
`capacity_drop`. Every `longest_freeze_ms` expect therefore passed falsely on
that build, and `tools/testbench_compare.py` showed 40 scenarios as
"improvements". Found by the Tester (run 22, coverage area 7) on bb1bf29:
https://github.com/aray4702/streaming_media/issues/283. The library's freeze
counting was right; `freeze_count` and `freeze_total_ms` matched native.

## Root cause

`testbench/call.cc:882` (and the same line in `compare/webrtc/webrtc_call.cc`)
took the longest freeze from `FreezeStats::freeze_ms.max()`, a library
`Histogram`. `Histogram::Add` (`src/core/metrics.cc:30`) does nothing when
metrics are compiled out, so the histogram stayed empty and only the
`std::max(longest, tail)` term for the gap at the end of the call was left.
No other bench metric reads a library `Histogram`.

## Fix

`FreezeStats` gets a plain `longest_freeze_ms`, updated by
`FreezeDetector::OnFrameRendered` next to `total_freezes_ms`, so it does not
depend on `SM_ENABLE_METRICS`. Both benches read it instead of the histogram.
The histogram is unchanged and still exported as a metric.

`tools/check_fix_record.py` learns a scenario line
`# fix-record-base-cmake: -DSM_<NAME>=ON|OFF ...`: the `fix-record` job then
builds the base commit's `sm_testbench` with those options, so a problem that
only shows in one build configuration can still be shown to fail before the
fix. Only `-DSM_*=ON|OFF` options are accepted.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_283_longest_freeze_no_metrics.scn` (base built with `-DSM_ENABLE_METRICS=OFF -DSM_ENABLE_EVENTS=OFF`) | `video.longest_freeze_ms` 13 (expect >= 1900) | 2084, native and embedded |
| `tests/unit/video/video_test.cc` `FreezeDetector.TracksLongestFreeze` | does not compile (no `longest_freeze_ms`) | passes |

## Results

The reproduction scenario (a 2 s outage) across seeds, metrics compiled out
(before -> after); the native build gives the "after" value both before and
after the fix:

| seed | metric | before | after |
|---|---|---|---|
| 1 | `video.longest_freeze_ms` | 13 | 2084 |
| 2 | `video.longest_freeze_ms` | 10 | 2080 |
| 3 | `video.longest_freeze_ms` | 25 | 2082 |
| 4 | `video.longest_freeze_ms` | 5 | 2075 |
| 5 | `video.longest_freeze_ms` | 7 | 2116 |

The scenario suite against the baseline: native, 0 regressions and 0
improvements (only the new scenario is added). The `embedded` build's suite
now also gives 0 regressions and 0 improvements against the native baseline,
where before the fix it showed `video.longest_freeze_ms` lower in 40
scenarios.
