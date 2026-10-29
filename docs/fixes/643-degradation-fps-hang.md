---
issue: 643
title: DegradationController hangs with kBalanced and input fps <= 0
severity: sev:3
status: fixed
area: area:video
regression_tests: tests/unit/cc/allocation_adaptation_test.cc
---

# #643: DegradationController hangs with kBalanced and input fps <= 0

## Issue

Constructing `sm::cc::DegradationController` with
`DegradationPreference::kBalanced` and an input frame rate of 0 never
returned. Negative rates hung the same way, and so did
`kMaintainResolution` when `min_fps` was 0 or negative. Found by the Tester
probing #635 on 3a17725, filed by the Builder:
https://github.com/aray4702/streaming_media/issues/643. The test bench never
builds this controller (the send controller wires only MAINTAIN_FRAMERATE,
through `VideoStreamAdapter`), so only an app calling the API directly was
affected.

## Root cause

`FpsSteps` (`src/cc/video_adaptation.cc:27-32` on 5f62b22) builds the
frame-rate ladder with `for (f = from; f >= to; f = f * 2 / 3)`. When
`to <= 0` and `from <= 0`, `f * 2 / 3` never gets below `to` (0 stays 0, a
negative rate climbs toward 0), so the loop pushes values forever. With
`kBalanced`, `to` is `min(fps, balanced_fps)`, i.e. the input rate itself,
so an input rate of 0 or less always hit it.

libwebrtc cannot get there. `VideoStreamAdapter`
(`compare/webrtc/reference/call/adaptation/video_stream_adapter.cc`) makes
one frame-rate step at a time, clamps every target to `kMinFrameRateFps`
(2, line 40 and line 529), only steps to a rate lower than the current one
(`CanDecreaseFrameRateTo`), and refuses MAINTAIN_RESOLUTION adaptation when
the input rate is below `kMinFrameRateFps`
(`HasSufficientInputForAdaptation`, lines 714-719).

## Fix

`FpsSteps` clamps its floor to libwebrtc's `kMinFrameRateFps` (2) and
returns only the input rate when that rate is not above the floor (0,
negative, or below 2), so no frame-rate step is taken, as in libwebrtc.
The loop now always ends, because `f` shrinks geometrically toward a
positive floor. The rule also stops a step from *raising* a low input rate
up to `min_fps`, which the old code did (input 3 fps, `min_fps` 5 gave a
5 fps rung); libwebrtc never decreases to a rate that is not lower. The
defaults (30 fps input, `min_fps` 5, `balanced_fps` 15) give the same
ladders as before. No public API change, and the test bench doesn't
construct this controller, so no scenario moves.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/cc/allocation_adaptation_test.cc` `VideoAdaptation.NonPositiveInputFpsBuildsLadderForEveryPreference` | hangs; `ctest -R unit.VideoAdaptation --timeout 20` reports Timeout | passes, native and ASan+UBSan |

There is no `issue_643_*.scn`: no test-bench path constructs
`DegradationController`, so a scenario can't reach the bug (see the PR).

## Results

The issue's reproduction (`input = {1280, 720, 0}`, `kBalanced`): hang ->
a one-rung ladder at 0 fps. Every preference with input fps 0, -1, -30 and
1, and `min_fps` 5, 0 and -1, now builds a ladder of 1-63 rungs whose rates
never exceed the input, and `OnTargetBitrate` / `OnResourceOveruse` run on
it.

The scenario suite against the baseline (`tools/testbench_compare.py`): no
metric changed, as expected (the controller isn't in the call path).
