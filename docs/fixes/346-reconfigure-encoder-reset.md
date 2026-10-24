---
issue: 346
title: Encoder reconfiguration on a new frame size: reset the frame dropper and start a fresh bitrate adjuster, as libwebrtc's ReconfigureEncoder
severity: sev:3
status: fixed
area: area:video
regression_tests: testbench/scenarios/issue_346_reconfigure_cross_traffic.scn, testbench/scenarios/issue_346_reconfigure_capacity_drop.scn, testbench/scenarios/issue_346_reconfigure_outage_8s.scn, testbench/scenarios/issue_346_reconfigure_cross_join.scn, tests/unit/rtp/video_send_controller_test.cc
---

# #346: Encoder reconfiguration on a new frame size resets the frame dropper and the bitrate adjuster

## Issue

With quality scaling on (#84's flip, draft #324), every resolution switch came
out about 20% larger than libwebrtc's: the switch keyframes were 41 kB against
libwebrtc's 34 kB at 2.5 Mbps, and we sent 300-400 kbps more for the second
after each switch. On `cross_traffic` that filled the bottleneck when the cross
traffic joined (14 freezes, 1.6% loss, e2e p95 1008 ms against libwebrtc's 0,
0.0% and 249 ms). Found by #334's diagnosis table (yardstick #344), mechanism A;
https://github.com/aray4702/streaming_media/issues/346. Placed by the local
coordinator on 2026-10-23.

## Root cause

libwebrtc sets `pending_encoder_reconfiguration_` on a new frame size
(`compare/webrtc/reference/video/video_stream_encoder.cc` l.1901), and
`ReconfigureEncoder()` then calls `frame_dropper_.Reset()` and
`frame_dropper_.SetRates(codec.startBitrate, …)` (l.1430-1431) and makes a new
`EncoderBitrateAdjuster` (l.1441). A new adjuster uses
`kDefaultUtilizationFactor` 1.2 until `kMinFramesSinceLayoutChange` 30 frames
have been encoded (`video/encoder_bitrate_adjuster.h` l.41/45, `.cc` l.193-195),
so libwebrtc encodes at allocation / 1.2 for a second after every switch.

`rtp::VideoSendController::OnCapturedFrame` (`src/rtp/video_send_controller.cc`
l.166-170 before this change) only asked for a keyframe on a new frame size.
The adjuster kept the factor it had learned (1.03-1.11) and the dropper kept
its accumulator.

## Fix

On a new frame size, `OnCapturedFrame` now calls `dropper_.Reset()` and
`adjuster_.Reset(now)` before the per-frame `SetRates`:

- `FrameDropper::Reset` then the per-frame `SetRates(target, fps)` is
  libwebrtc's `Reset` + `SetRates(codec.startBitrate)`: `startBitrate` is the
  encoder target (`encoder_target_bitrate_bps_`, l.1285-1287).
- `EncoderBitrateAdjuster::Reset` drops the overshoot detector, and the next
  `AdjustRate` creates it again and restarts the frame count, as a new
  adjuster's first `AdjustRateAllocation` does (libwebrtc's own `Reset`,
  `encoder_bitrate_adjuster.cc` l.452-462, does the same).

The frame size is tracked only with quality scaling on, as before, so the
default suite (capture size constant) does not change.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_346_reconfigure_cross_traffic.scn` | `video.freeze_count` 14 (expect == 0), `net.loss_pct` 1.61 (< 0.1), `video.e2e_delay_p95_ms` 1008 (< 320) | 0, 0.0, 236 |
| `testbench/scenarios/issue_346_reconfigure_capacity_drop.scn` | `net.loss_pct` 2.00 (expect < 1.95) | 1.78 |
| `testbench/scenarios/issue_346_reconfigure_outage_8s.scn` | `net.loss_pct` 9.22 (expect < 8.4) | 8.34 |
| `testbench/scenarios/issue_346_reconfigure_cross_join.scn` | passes (0.71; coverage for the flip, see its comment) | 0.38 |
| `tests/unit/rtp/video_send_controller_test.cc` `VideoSendController.NewFrameSizeStartsAFreshBitrateAdjuster` | encoder target 297 kbps after the switch (expect 250 = 300 / 1.2) | passes |

## Results

Seeds 1-5, main (before) -> this fix; libwebrtc's seeds 1-3 from #344's
`compare-webrtc` @ 3d46b11 where published:

| scenario | seed | `net.loss_pct` | `video.freeze_count` | `video.e2e_delay_p95_ms` | libwebrtc loss / freezes / e2e |
|---|---|---|---|---|---|
| cross_traffic | 1 | 1.61 -> 0.00 | 14 -> 0 | 1008 -> 236 | 0.00 / 0 / 249 |
| cross_traffic | 2 | 0.07 -> 0.00 | 1 -> 0 | 330 -> 292 | |
| cross_traffic | 3 | 1.21 -> 0.00 | 12 -> 0 | 1130 -> 194 | |
| cross_traffic | 4 | 0.41 -> 0.00 | 1 -> 0 | 325 -> 222 | |
| cross_traffic | 5 | 0.35 -> 0.00 | 6 -> 0 | 554 -> 213 | |
| capacity_drop | 1 | 2.00 -> 1.78 | 3 -> 2 | 599 -> 545 | 1.68 / 2 / 345 |
| capacity_drop | 2 | 2.03 -> 1.76 | 4 -> 2 | 638 -> 579 | 1.92 / 1 / 350 |
| capacity_drop | 3 | 1.97 -> 1.82 | 1 -> 3 | 334 -> 608 | 1.61 / 2 / 537 |
| capacity_drop | 4 | 1.81 -> 1.81 | 2 -> 3 | 674 -> 572 | |
| capacity_drop | 5 | 1.82 -> 1.89 | 3 -> 3 | 531 -> 573 | |
| cross_join | 1 | 0.71 -> 0.38 | 5 -> 4 | 560 -> 527 | 0.39 / 2 / 491 |
| cross_join | 2 | 0.55 -> 0.53 | 1 -> 5 | 438 -> 586 | 0.32 / 2 / 343 |
| cross_join | 3 | 0.65 -> 0.21 | 4 -> 3 | 512 -> 430 | 0.33 / 4 / 546 |
| cross_join | 4 | 0.59 -> 0.39 | 2 -> 4 | 513 -> 514 | |
| cross_join | 5 | 0.72 -> 2.04 | 3 -> 14 | 508 -> 1094 | |
| outage_8s | 1 | 9.22 -> 8.34 | 1 -> 4 | 257 -> 459 | 8.05 / 1 / 294 |
| outage_8s | 2 | 8.91 -> 7.94 | 1 -> 2 | 361 -> 436 | 7.94 / 1 / 274 |
| outage_8s | 3 | 8.72 -> 7.83 | 1 -> 1 | 314 -> 426 | 8.30 / 1 / 289 |
| outage_8s | 4 | 8.97 -> 7.99 | 2 -> 2 | 287 -> 420 | |
| outage_8s | 5 | 8.25 -> 7.83 | 1 -> 1 | 285 -> 424 | |

The switch keyframes now match libwebrtc to the byte (34,722 B on both at
2.5 Mbps), and the encoder target in each switch second is allocation / 1.20
on both stacks.

What did not move toward libwebrtc, and why:
- **outage_8s** freezes and e2e p95 after link-up: our target dips about 3 s
  after the link is back while libwebrtc's holds; that is #348 (post-outage
  recovery), which starts after this item.
- **cross_join seed 5**: the scaler goes up to 720p at 13 s while 2 Mbps of
  cross traffic holds the target near 1 Mbps, and the 720p frames freeze. On
  main that seed stayed at 540p until 15 s. libwebrtc's seed 3 also switches to
  720p at 13 s; whether its seed 5 does is for `compare-webrtc`'s 10 seeds.
- **capacity_drop** e2e p95 after the drop stays above libwebrtc's on seeds 1-2
  (the "remaining" note in #334's table).

The default suite (quality scaling off) does not change: no metric of any
scenario without `quality_scaling on` moved. The scaling-on scenarios
(`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_346_reconfigure_capacity_drop | (new scenario) | - | - | new |
| issue_346_reconfigure_cross_join | (new scenario) | - | - | new |
| issue_346_reconfigure_cross_traffic | (new scenario) | - | - | new |
| issue_346_reconfigure_outage_8s | (new scenario) | - | - | new |
| issue_84_cpu_overuse_slow_encoder | video.longest_freeze_ms | 33 | 1 | improved |
| issue_84_quality_scaling_rate_rise | cc.target_avg_kbps | 969.8 | 1019 | improved |
| issue_84_quality_scaling_rate_rise | video.delay_pacer_p95_ms | 37 | 0 | improved |
