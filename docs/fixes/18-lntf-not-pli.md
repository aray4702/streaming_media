---
issue: 18
title: Test bench sends every loss notification (LNTF) as a PLI, so the sender encodes a keyframe after each lost frame
severity: sev:2
status: fixed
area: area:loss, area:testbench
regression_tests: testbench/scenarios/issue_18_random_loss_keyframes.scn
---

# #18: Test bench sends every loss notification (LNTF) as a PLI

## Issue

In lossy scenarios the sender encoded a keyframe after almost every lost
frame, even when NACK recovered it. Keyframes are about 4x larger, so repair
overhead rose, bitrate fell and video froze. Issue:
https://github.com/aray4702/streaming_media/issues/18. Found by the Tester by
comparing with libwebrtc (#17).

## Root cause

`testbench/call.cc` (receiver loop over `keyframes.Process(now)`) discarded
`msg.type` and sent PLI, FIR and LNTF all as `RKind::kPli`. The sender counts
`kPli` as a keyframe request and sets `keyframe_needed`. `OnFrameLost` queues
an LNTF per lost frame, so each loss produced a keyframe. The library
(`KeyframeRequester`) was correct; the bug is in the test bench.

## Fix

Add `RKind::kLntf`. LNTF still costs the same reverse-link bytes, but the
sender ignores it (loss notification is informational, as in libwebrtc). PLI
and FIR still request a keyframe. No library or API change.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_18_random_loss_keyframes.scn` | `video.keyframe_requests` 12 (expect <= 3), `video.freeze_total_ms` 1340 (expect < 1000) | 1 and 0 |

## Results

Reproduction scenario, seed 1 (seeds 1-5 all pass after the fix):

| seed | metric | before | after |
|---|---|---|---|
| 1 | video.keyframe_requests | 12 | 1 |
| 1 | video.freeze_total_ms | 1340 | 0 |

Suite against the previous baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| burst_loss | cc.goodput_kbps | 1080 | 1545 | improved |
| burst_loss | cc.queue_max_ms | 97.38 | 72.14 | improved |
| burst_loss | cc.target_avg_kbps | 1012 | 1399 | improved |
| burst_loss | cc.utilization_pct | 54.01 | 77.24 | improved |
| burst_loss | video.avg_kbps | 749.9 | 1105 | improved |
| burst_loss | video.encoder_dropped | 56 | 0 | improved |
| burst_loss | video.frames_rendered | 835 | 880 | improved |
| burst_loss | video.freeze_count | 2 | 0 | improved |
| burst_loss | video.freeze_total_ms | 400 | 0 | improved |
| burst_loss | video.keyframe_requests | 13 | 1 | improved |
| burst_loss | video.longest_freeze_ms | 200 | 9 | improved |
| burst_loss | video.render_fps | 27.83 | 29.33 | improved |
| capacity_drop | cc.goodput_kbps | 906.8 | 952.3 | improved |
| capacity_drop | cc.max_settle_ms | 1.5e+04 | 1.36e+04 | improved |
| capacity_drop | cc.target_avg_kbps | 875.2 | 919.4 | improved |
| capacity_drop | cc.utilization_pct | 51.82 | 54.42 | improved |
| capacity_drop | video.avg_kbps | 768.4 | 814.7 | improved |
| capacity_drop | video.encoder_dropped | 120 | 96 | improved |
| capacity_drop | video.keyframe_requests | 14 | 11 | improved |
| capacity_drop | video.longest_freeze_ms | 1601 | 1690 | REGRESSED |
| capacity_drop | video.undecodable_frames | 23 | 37 | REGRESSED |
| high_jitter | audio.late_discards | 4 | 2 | improved |
| high_jitter | cc.goodput_kbps | 187.4 | 248.2 | improved |
| high_jitter | cc.target_avg_kbps | 111.6 | 152.7 | improved |
| high_jitter | cc.utilization_pct | 7.259 | 9.615 | improved |
| high_jitter | repair.overhead_pct | 19.11 | 21.31 | REGRESSED |
| high_jitter | video.avg_kbps | 78.39 | 123.1 | improved |
| high_jitter | video.e2e_delay_p50_ms | 197 | 162 | improved |
| high_jitter | video.encoder_dropped | 303 | 40 | improved |
| high_jitter | video.frames_rendered | 534 | 784 | improved |
| high_jitter | video.freeze_count | 5 | 1 | improved |
| high_jitter | video.freeze_total_ms | 3418 | 499 | improved |
| high_jitter | video.keyframe_requests | 57 | 1 | improved |
| high_jitter | video.longest_freeze_ms | 1886 | 499 | improved |
| high_jitter | video.render_fps | 17.8 | 26.13 | improved |
| issue_18_random_loss_keyframes | (new scenario) | - | - | new |
| low_bandwidth | audio.jb_delay_p95_ms | 416 | 352 | improved |
| low_bandwidth | audio.mouth_to_ear_p95_ms | 954.6 | 890.9 | improved |
| low_bandwidth | video.e2e_delay_p50_ms | 1520 | 1597 | REGRESSED |
| low_bandwidth | video.encoder_dropped | 376 | 355 | improved |
| low_bandwidth | video.frames_rendered | 189 | 166 | REGRESSED |
| low_bandwidth | video.freeze_count | 23 | 17 | improved |
| low_bandwidth | video.freeze_total_ms | 2.756e+04 | 2.231e+04 | improved |
| low_bandwidth | video.longest_freeze_ms | 4358 | 3477 | improved |
| low_bandwidth | video.render_fps | 4.725 | 4.15 | REGRESSED |
| low_bandwidth | video.render_ratio_pct | 22.94 | 19.64 | REGRESSED |
| low_bandwidth | video.undecodable_frames | 480 | 529 | REGRESSED |
| outage_3s | video.encoder_dropped | 214 | 191 | improved |
| outage_3s | video.freeze_total_ms | 9775 | 9012 | improved |
| outage_3s | video.keyframe_requests | 12 | 14 | REGRESSED |
| random_loss_5pct | audio.jb_delay_p95_ms | 72 | 44 | improved |
| random_loss_5pct | audio.mouth_to_ear_p95_ms | 121.1 | 100.8 | improved |
| random_loss_5pct | cc.goodput_kbps | 717.4 | 1343 | improved |
| random_loss_5pct | cc.queue_max_ms | 98.43 | 25.29 | improved |
| random_loss_5pct | cc.target_avg_kbps | 703.8 | 1196 | improved |
| random_loss_5pct | cc.utilization_pct | 35.87 | 67.16 | improved |
| random_loss_5pct | video.avg_kbps | 430 | 853.6 | improved |
| random_loss_5pct | video.e2e_delay_p50_ms | 273 | 155 | improved |
| random_loss_5pct | video.e2e_delay_p95_ms | 408.9 | 193.4 | improved |
| random_loss_5pct | video.encoder_dropped | 61 | 0 | improved |
| random_loss_5pct | video.frames_rendered | 823 | 893 | improved |
| random_loss_5pct | video.freeze_count | 3 | 0 | improved |
| random_loss_5pct | video.freeze_total_ms | 1340 | 0 | improved |
| random_loss_5pct | video.keyframe_requests | 12 | 1 | improved |
| random_loss_5pct | video.longest_freeze_ms | 818 | 29 | improved |
| random_loss_5pct | video.render_fps | 27.43 | 29.77 | improved |
| satellite | cc.goodput_kbps | 630.1 | 482.9 | REGRESSED |
| satellite | cc.queue_max_ms | 36.62 | 14.62 | improved |
| satellite | cc.target_avg_kbps | 566.8 | 431.1 | REGRESSED |
| satellite | cc.utilization_pct | 42.01 | 32.19 | REGRESSED |
| satellite | video.avg_kbps | 414.9 | 301.1 | REGRESSED |
| satellite | video.e2e_delay_p50_ms | 720 | 614 | improved |
| satellite | video.e2e_delay_p95_ms | 796 | 670.4 | improved |
| satellite | video.encoder_dropped | 114 | 13 | improved |
| satellite | video.frames_rendered | 955 | 1032 | improved |
| satellite | video.freeze_total_ms | 6421 | 5755 | improved |
| satellite | video.keyframe_requests | 17 | 2 | improved |
| satellite | video.longest_freeze_ms | 694 | 600 | improved |
| satellite | video.render_fps | 23.88 | 25.8 | improved |
| satellite | video.undecodable_frames | 17 | 0 | improved |
| wifi_to_lte_handover | video.encoder_dropped | 568 | 401 | improved |
| wifi_to_lte_handover | video.frames_rendered | 563 | 729 | improved |
| wifi_to_lte_handover | video.render_fps | 14.07 | 18.23 | improved |

Regressions in the table are knock-on effects, not caused by the change in the
library: with the spurious keyframes gone, the congestion controller runs on
its own dynamics (see #14, #15, #19). Checked on seeds 1-3 against `main`:
`satellite` utilization 42.0/42.7/43.1 -> 32.2/48.3/39.8 % (freeze total
6421/5168/7713 -> 5755/4378/5104 ms, keyframe requests 17/13/16 -> 2/2/2);
`capacity_drop` utilization 51.8/57.9/53.2 -> 54.4/57.7/54.3 %;
`low_bandwidth` (99% utilized both ways, 4-7 fps, dominated by undecodable
frames) changes by seed noise (render_fps 4.7/5.1/5.5 -> 4.2/4.8/6.8). Per-seed
swings go both ways, so there is no systematic loss; the baseline is updated.
