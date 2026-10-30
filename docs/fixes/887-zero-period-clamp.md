---
issue: 887
title: FrameCadenceAdapter Process() never returns when max fps rounds the frame period to 0 µs
severity: sev:3
status: fixed
area: area:video
regression_tests: tests/unit/video/frame_cadence_adapter_test.cc
---

# #887: FrameCadenceAdapter Process() never returns when max fps rounds the frame period to 0 µs

## Issue

`FrameCadenceAdapter::Process(now)` never returned when a zero-hertz
(screen-share) source reported a max frame rate of about 2 000 000 fps or
more. The `out` vector kept growing while the call ran. At 1e6 fps one
`Process` call 2 s after a frame returned 2 000 000 outputs. At 1e7 or 1e300
fps it did not return within 20 s. The Tester's probe of #881 found it on
5b4eb98, and the Fixer filed it:
https://github.com/aray4702/streaming_media/issues/887. No real source
reports such a rate, and the test bench has no screen-share source, so no
call metric was affected.

## Root cause

`PeriodOf(fps)` (`src/video/frame_cadence_adapter.cc:11` on 9804746) rounds
`1 s / fps` to the microsecond with `std::llround`, as libwebrtc's
`TimeDelta::Seconds(1) / max_fps_` does
(`rtc_base/units/unit_base.h:247-248`, `video/frame_cadence_adapter.cc:230`).
From about 2e6 fps the period is 0 µs. `ScheduleRepeat` then posts each
repeat due at the time it runs (`now + FrameDuration()`), and the refresh
requester does the same. `Process(now)` runs every task due at or before
`now`, including tasks posted during the call, so it never reaches a task due
after `now`. libwebrtc has the same rounding. There, a real task queue keeps
running the zero-delay reposts one after another, so its encoder queue spins
instead.

## Fix

`PeriodOf` returns at least 1 µs. This covers both `frame_delay` and
`restricted_frame_delay`, so it also covers `FrameDuration()`,
`RepeatDuration()` and the refresh period. Time now advances by at least
1 µs per repeat, and `Process(now)` ends. Below about 2e6 fps the rounded
period is already 1 µs or more, so every rate a source reports keeps
libwebrtc's period exactly. This is a deviation from libwebrtc, limited to
rates no source produces. It is recorded in `tools/catalog.py` (7.4) and in
`docs/design/libwebrtc-parity.md`. The issue's option 1 was chosen over
option 2 (bounding the work one `Process` call does), because option 2
would change when tasks run at ordinary rates.

## Regression tests

The test bench never builds a `FrameCadenceAdapter` in zero-hertz mode (no
screen-share source), so no `issue_887_*.scn` can reach this path. As for
#643, the regression test is a unit test.

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/video/frame_cadence_adapter_test.cc` `FrameCadenceAdapter.FrameRateAboveOneMegahertzStillAdvancesTime` (max fps 1e6, 2e6, 1e7, 1e300; one frame; `Process(2 ms)`) | does not return: killed by `timeout 20` (exit 143) at 1e7 fps | passes in 0.4 ms: at most 2002 outputs, and `NextProcessTime()` > 2 ms for every rate |

## Results

The adapter is not on any scenario's path, so the scenario suite is
unchanged. See the PR for the `testbench_compare.py` output: 0 regressions,
and no metric changed.

| max fps | `Process(t + 2 ms)` before | after |
|---|---|---|
| 1e6 (period 1 µs) | 2000 outputs | 2000 outputs (same) |
| 2e6 (0.5 µs, rounds to 1 µs) | 2000 outputs | 2000 outputs (same) |
| 1e7 (rounds to 0) | never returns | 2000 outputs |
| 1e300 (rounds to 0) | never returns | 2000 outputs |
