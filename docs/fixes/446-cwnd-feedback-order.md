---
issue: 446
title: Video-only 4 s outage, bottleneck queue p95 behind libwebrtc on 7/10 seeds
severity: sev:3
status: fixed
area: area:cc
regression_tests: testbench/scenarios/issue_446_cwnd_after_outage.scn, tests/e2e/cwnd_pushback_e2e_test.cc
---

# #446: Video-only 4 s outage, bottleneck queue p95 behind libwebrtc on 7/10 seeds

## Issue

After a 4 s outage on both paths of a video-only call at 1500 kbps
(`issue_430_video_only_outage_4s`), our bottleneck queue p95 was 67.1 ms
against libwebrtc's 52.7 ms (band ±13.8), behind on 7 of 10 seeds. The
Coordinator split it from #430 when #430 closed as parity:
https://github.com/aray4702/streaming_media/issues/446. Measured on `main`
a78522c.

## Root cause

Both stacks send the same retransmission burst at link-up: about 150 RTX
packets in 0.8 s on seed 1, packet for packet the same on the published
forward-link traces (`parity-data/traces/issue_430_video_only_outage_4s.1.*`).
What differs is what the encoder does next. libwebrtc keeps dropping every
other frame through the second after link-up (15 congestion-window drops in
19-20 s, encoder target 373 kbps at 20 s, half its allocation). We dropped 2
frames and the encoder target was back at 734 kbps. The full target then
went into a queue that the retransmissions still held (mean 114 ms in
20-21 s, against libwebrtc's 20 ms).

The reason is the order of congestion-window updates after a transport
feedback. During the outage the data in flight passes 1.5 windows on every
update, so `CongestionWindowPushback`'s encoding-rate ratio falls to about
1e-12. Only a fill under 0.1 resets it to 1. Otherwise it climbs 5 % per
update, and from 1e-12 that takes hundreds of updates. The first feedback after link-up
reports the outage's packets lost and clears them from flight (263 KB ->
6 KB on seed 1).

- libwebrtc, `GoogCcNetworkController::OnTransportPacketsFeedback`
  (`goog_cc_network_control.cc:421-541`): `UpdateOutstandingData`, then
  `MaybeTriggerOnNetworkChanged` (and with it
  `CongestionWindowPushbackController::UpdateTargetBitrate`) only
  `if (result.updated)` for the delay-based estimate, and only after that
  `UpdateCongestionWindowSize` from `last_loss_based_target_rate_`. The
  pushback weighs the 6 KB against the window from before the feedback
  (56.6 KB, fill 0.106), so the ratio stays near zero until the
  retransmissions have drained.
- This library, `SendSideController::OnTransportFeedback`
  (`src/rtp/send_side_controller.cc:387-388` on a78522c): `UpdatePushback(now, true)`
  resized the window first, from the new target (71.0 KB), and then ran the
  pushback on every feedback. The fill was 0.084, under 0.1, so the ratio
  reset to 1 and the pushback ended at the first feedback.

`OnProcessInterval` had the same difference in a smaller way: libwebrtc
sizes the window from `last_loss_based_target_rate_`, the target of the
last trigger, before this interval's trigger runs. We used the current target.

## Fix

`SendSideController` now follows GoogCC's order:

- After a feedback, the pushback (`UpdatePushback(now, false)`) runs only
  when the delay-based estimate updated
  (`GccController::last_feedback_delay_updated()`, which is
  `DelayBasedBwe::Result::updated`). Then the feedback RTT joins the window's
  RTT history, and the window is resized.
- The window is sized from `window_target_`, the target at the last pushback
  update (GoogCC's `last_loss_based_target_rate_`), on feedback and on the
  process interval.

No constant changes. The congestion-window pushback controller itself
(`cc/congestion_window_pushback.cc`) was already line for line libwebrtc's.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_446_cwnd_after_outage.scn` (seed 3) | `cc.queue_p95_ms` 92.14 (expect < 70) | 51.57 |
| `tests/e2e/cwnd_pushback_e2e_test.cc` `E2E_CongestionWindowPushback.PushbackHoldsAcrossTheFirstFeedbackAfterAnOutage` | `max_ratio_after` 1 (expect < 0.5): the ratio resets at the first feedback after link-up | 1.9e-10 |

## Results

The reproduction (`issue_446_cwnd_after_outage` with seeds 3-7), `cc.queue_p95_ms`:

| seed | before | after | libwebrtc |
|---|---|---|---|
| 3 | 92.14 | 51.57 | 65 |
| 4 | 57.47 | 51.60 | 46 |
| 5 | 54.23 | 47.58 | 40 |
| 6 | 85.80 | 49.38 | 62 |
| 7 | 29.52 | 29.52 | 30 |

`issue_430_video_only_outage_4s` on 10 seeds (main's `compare-webrtc` for
libwebrtc and for "before"; this branch's `sm_testbench` on the same seeds
for "after"):

| metric | before | after | libwebrtc |
|---|---|---|---|
| `cc.queue_p95_ms` | 67.1 | 53.1 | 52.7 |
| `video.pause_total_ms` | 6218 | 6189 | 6189 |
| `cc.utilization_pct` | 86.8 | 85.7 | 85.2 |
| `cc.max_settle_ms` | 630 | 1150 | 830 |

The parity report over all 118 scenarios on 10 seeds
(`tools/parity_report.py --base` with main's `compare-webrtc.json`) shows 1
gap closed (this one), 0 opened and 0 that changed direction. Three gaps
grew: `very_low_capacity`, `very_low_capacity_allocator_v2` and
`issue_123_v2_allocation_min` `video.freeze_count`, distance 3.6 -> 4.0
freezes (band ±2.2; libwebrtc 21.3). #437 owns them. `issue_76_rtx_v2_keyframe_flush` `video.e2e_delay_p95_ms` moved closer
(distance 263 -> 219 ms).

The scenario suite against the baseline (`tools/testbench_compare.py`, seed
1) is in the PR. The changes come from the same order change wherever the
pushback is active: after outages, capacity drops and loss bursts. They are
judged by the parity report above, not by the seed-1 baseline.
