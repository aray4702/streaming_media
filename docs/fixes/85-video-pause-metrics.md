---
issue: 85
title: Video blackouts of 5 s or more are reported as no freeze
severity: sev:3
status: fixed
area: area:video, area:testbench
regression_tests: testbench/scenarios/issue_85_video_pause.scn
---

# #85: Video blackouts of 5 s or more are reported as no freeze

## Issue

In the test bench a video outage of 5 s or longer read `video.freeze_total_ms` 0 and `video.longest_freeze_ms` ~20 ms, so no `expect` line could see it. https://github.com/aray4702/streaming_media/issues/85. Found by the Tester (coverage area 4) on 5adecb5.

## Root cause

`video::FreezeDetector::OnFrameRendered` (`src/video/freeze_detector.cc`) follows libwebrtc's `VideoQualityObserver` (`compare/webrtc/reference/video/video_quality_observer2.cc`): a gap of 5 s or more is a pause, not a freeze. It counted pauses, but `testbench/call.cc` and `compare/webrtc/webrtc_call.cc` never exported them, so the blackout appeared in no metric.

## Fix

Export `video.pause_count` and `video.pause_total_ms` from the test bench and `sm_compare` (shown in its table, owned by the same issues as the freeze metrics in `tools/parity_owners.json`). The freeze metrics are unchanged, as the Owner decided on the issue, so the libwebrtc comparison stays like for like. A pause still in progress at the end of the call is counted too. No library change.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_85_video_pause.scn` | metrics do not exist, `expect video.pause_count == 1` fails | pause_count 1, pause_total_ms 10168 |

## Results

| seed | metric | before | after |
|---|---|---|---|
| 1 | video.pause_total_ms | n/a | 10169 |
| 2 | video.pause_total_ms | n/a | 10174 |
| 3 | video.pause_total_ms | n/a | 10172 |
| 4 | video.pause_total_ms | n/a | 10171 |
| 5 | video.pause_total_ms | n/a | 10168 |

Suite against the baseline: 0 regressions, 0 improvements. Every scenario gains `video.pause_count` and `video.pause_total_ms` (both 0), and the new scenario is added.
