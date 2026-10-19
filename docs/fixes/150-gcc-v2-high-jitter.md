---
issue: 150
title: GoogCC v2: target far below libwebrtc under 40 ms jitter with reordering (high_jitter 594 vs 955 kbps)
severity: sev:3
status: partial
area: area:cc
regression_tests: testbench/scenarios/issue_150_gcc_v2_high_jitter.scn, tests/unit/cc/transport_feedback_test.cc
---

# #150: GoogCC v2: target far below libwebrtc under 40 ms jitter with reordering

## Issue

With every v2 switch on, `high_jitter` (3 Mbps, 30 ms, jitter 40 ms with reordering) held the target
at 594 kbps where libwebrtc holds 955 kbps, and utilization at 23 % against 37 %. Found by the #61
acceptance sweep (Owner, 2026-10-19). https://github.com/aray4702/streaming_media/issues/150

## Root cause

`SendHistory::OnFeedback` (`src/cc/transport_feedback.cc`) returned a packet again each time `tcc v2`'s
generator re-reported it. libwebrtc's generator (`transport_sequence_number_feedback_generator.cc`,
`SendPeriodicFeedbacks`: "Don't erase items from packet_arrival_times_ after sending, in case they need to be
re-sent after a reordering") does re-report, and `TransportFeedbackAdapter` makes that harmless: it erases a
received packet from `history_`, so the second report fails the lookup (`transport_feedback_adapter.cc`, "packet
reported received more than once"). Ours counted every re-report, so feedback after feedback carried receive times
earlier than the last one. `BitrateEstimator::UpdateWindow` resets when time moves backwards, so the 500 ms initial
window never completed: no acknowledged rate for the first 3.3 s (debug trace, seed 1). The first overuse then
took the `!acked_bitrate` branch of `DelayBasedBwe::MaybeUpdateEstimate`, which halves the estimate on every
feedback (`InitialTimeToReduceFurther`), 1116 -> 558 -> 279 kbps in 50 ms. `AimdRateControl` and the
`MaybeUpdateEstimate` branch already match the reference line for line.

## Fix

A packet reported received is marked in `SendHistory`, and a later report of it is ignored, as libwebrtc's
history does. A packet reported lost can still be reported received later. `tcc v1` reports each arrival once,
so it is unchanged.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_150_gcc_v2_high_jitter.scn` | seed 1: `cc.utilization_pct` 19.5 (expect >= 30), `video.avg_kbps` 323 (expect >= 450); fails on seeds 1, 3, 4, 5 | passes on seeds 1-5 |
| `tests/unit/cc/transport_feedback_test.cc` `TransportFeedback.PacketReportedReceivedTwiceIsIgnored` | a packet reported twice is returned twice | returned once |

## Results

`high_jitter`, every v2 switch on, seeds 1-10, ours before -> after, libwebrtc (10 seeds, parity-data of main):

| metric | main (seed 1) | after (mean, 10 seeds) | libwebrtc (mean) | band |
|---|---|---|---|---|
| `cc.target_avg_kbps` | 503 | 1086 | 954.6 | ±47.7 |
| `cc.utilization_pct` | 19.5 | 41.7 | 37.1 | ±7.2 |
| `video.avg_kbps` | 323 | 669 | 584.5 | ±111 |
| `cc.queue_p95_ms` | 4.4 | 11.9 | 4.6 | ±10 |
| `video.e2e_delay_p95_ms` | 174 | 176 | 136 | |

Per seed, `cc.utilization_pct` after: 45.7, 37.4, 32.0, 35.9, 64.0, 38.3, 29.0, 35.9, 33.7, 65.0; libwebrtc: 47.5, 44.1,
25.3, 31.2, 30.8, 40.6, 41.0, 39.2, 28.2, 42.9.

The suite against the baseline is in the PR. The only regressions are in `issue_76_tcc_v2_high_jitter`, which runs
`tcc v2` with `cc v1`, a mix libwebrtc does not have: v1's delay estimator now sees each arrival once, and its target
there is 175 kbps (9.6 -> 8.2 % utilization), both far from libwebrtc's 37 %. `repair.overhead_pct` and
`video.delay_network_p95_ms` in `issue_12_lossbwe_high_jitter_no_loss` rise because the link now carries 4x the video.

## Remaining

Seeds 5 and 10 reach 64-65 % utilization where libwebrtc's seeds are 31-43 %, so the target mean is 131 kbps above
libwebrtc's (band 47.7), and the queue p95 is 11.9 against 4.6 ms (band ±10, seeds 5 and 10: 24 and 22 ms). The
e2e delay part (176 vs 136 ms) is the receive side: #24. Next step: the per-update bwe trace of #149 (PR #156) to
see which bound holds libwebrtc's estimate on those seeds.
