---
issue: 365
title: Transport feedback receive times rounded down to 1 ms before GoogCC, as libwebrtc
severity: sev:3
status: fixed
area: area:cc
regression_tests: tests/unit/cc/transport_feedback_test.cc, tests/e2e/cc_e2e_test.cc
---

# #365: Transport feedback receive times rounded down to 1 ms before GoogCC, as libwebrtc

## Issue

GoogCC's delay-based and loss-based estimators got each packet's receive
time at the transport-cc wire resolution (250 us). libwebrtc gives them that
time rounded down to 1 ms. #363's detector rows showed it: receive deltas of
5.5 / 36.25 / 24.5 ms for us, against 6 / 36 / 24 for libwebrtc. The local
coordinator split this out of #354 on 2026-10-24
(https://github.com/aray4702/streaming_media/issues/365). It is the rule
#276 ported before the Owner closed that PR to port it later.

## Root cause

`TransportFeedbackAdapter::ProcessTransportFeedback`
(`compare/webrtc/reference/modules/congestion_controller/rtp/transport_feedback_adapter.cc:238,270`)
sets `result.receive_time = current_offset_ + delta_since_base.RoundDownTo(TimeDelta::Millis(1))`.
The feedback's base delta is rounded the same way, but it is always a
multiple of 64 ms, so that rounding changes nothing.

Ours, `SendHistory::OnFeedback` (`src/cc/transport_feedback.cc`, before this
change), used the time from `TransportFeedback::ArrivalTimes()` as it was,
in 250 us ticks.

`RoundDownTo` is `(value / resolution) * resolution`, which is integer
division and rounds toward zero. A packet that arrived before the base time
(a negative delta) is rounded up toward the base time. The file that defines
`RoundDownTo` (`rtc_base/units/unit_base.h`) is not in `reference/`.

## Fix

`SendHistory::OnFeedback` now builds each receive time as
`base + trunc((arrival - base) / 1 ms) * 1 ms`, where `base` is
`reference_time * 64 ms`. That is the adapter's rule. Our time base is the
feedback's reference time rather than the first feedback's local receive
time, which doesn't matter because every estimator uses only differences.
There is no switch: this is libwebrtc's only behaviour, and the v1 paths
were deleted in #167.

The change came from `fix/354-feedback-ms-receive-times` (389806f),
re-applied on `main` after #324.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/cc/transport_feedback_test.cc` `TransportFeedback.HistoryRoundsReceiveTimesToTheMillisecond` | times 641.75 / 643.25 / 639.25 ms (expected 641 / 643 / 640) | passes |
| `tests/e2e/cc_e2e_test.cc` `E2E_TransportFeedback.EveryPacketReportedReceivedOnceWithinQuantization` | the new check that each received result is the wire time rounded down to 1 ms since base fails on every packet with a sub-ms offset | passes; the wire's own error against the true arrival is still held to <= 250 us |

The e2e test used to hold `SendHistory`'s output to 250 us of the true
arrival time. That bound was the wire's quantization, and libwebrtc's
adapter adds up to 1 ms on top of it. The test now checks the wire against
the true arrival at the same 250 us, and checks the adapter's output for
the exact libwebrtc rounding. Neither check is looser than before.

## Results

Ten seeds of the scenarios whose seed-1 lines move, on `main` (bbee233) and
on this branch, with libwebrtc from `main`'s published `compare-webrtc.json`:

| scenario | metric | `main` mean (seeds 1-10) | this branch | libwebrtc mean |
|---|---|---|---|---|
| `issue_12_lossbwe_random_loss` | `cc.utilization_pct` | 72.3 | 73.0 | 75.8 |
| `issue_24_timing_v2_high_jitter` | `video.e2e_delay_p50_ms` | 143.4 | 141.7 | 125.2 |
| `issue_5_keyframe_flush` | `video.freeze_total_ms` | 2975 | 3014 | 2552 |
| `issue_150_gcc_v2_high_jitter` | `cc.utilization_pct` | 22.9 | 22.3 | 25.4 |
| `capacity_drop` | `cc.queue_p95_ms` | 297.7 | 302.4 | 277.0 |

On seed 1 these single-seed guard lines fail:

| scenario | line | `main` | this branch | libwebrtc seeds 1-10 |
|---|---|---|---|---|
| `issue_150_gcc_v2_high_jitter_seed10` | `cc.utilization_pct >= 25` | 28.36 | 23.84 | 18.5-34.3 (fails on 6 of 10) |
| `issue_5_keyframe_flush` | `video.freeze_total_ms < 3000` | 2743 | 3021 | 1957-3109 (fails on 1 of 10) |
| `issue_12_lossbwe_random_loss` | `cc.utilization_pct >= 70` | 70.9 | 69.18 | 72.3-78.3 (meets) |
| `issue_24_timing_v2_high_jitter` | `video.e2e_delay_p50_ms < 170` | 143 | 176 | mean 125.2 (per-seed values not published) |

`main` itself fails the last two lines on other seeds: `issue_12` reads 69.0
and 66.4 on seeds 4 and 8, and `issue_24` reads 175 on seed 8.

Three e2e and bench lines, which have no libwebrtc counterpart, also move:

| test | line | `main` | this branch |
|---|---|---|---|
| `E2E_DelayBwe.TracksCapacityStepsWithLowQueueing`, `cc/Q_Gcc_capacity_steps` | 800 kbps phase `queue_p95_ms <= 600` | 557 | 636 |
| `E2E_CongestionWindowPushback.EncoderBacksOffWhenTheBottleneckFills` | `with.queue_dropped < without.queue_dropped` | 232 < 336 | 293 vs 274 |

These lines are left for the Owner (PR, `needs-human`).

The scenario suite against the baseline on seed 1 shows 342 regressed and
347 improved cells, as `testbench_compare.py` counts them. That is the
seed-level spread that a change on every GoogCC input produces.
