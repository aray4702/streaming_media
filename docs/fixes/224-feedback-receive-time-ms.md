---
issue: 224
title: Send side: video e2e delay p95 808 ms vs libwebrtc 420 ms on congestive links once late frames are rendered
severity: sev:2
status: partial
area: area:cc
regression_tests: testbench/scenarios/issue_224_congestive_e2e_delay.scn, tests/unit/cc/transport_feedback_test.cc, tests/e2e/cc_e2e_test.cc
---

# #224: Send side: video e2e delay p95 808 ms vs libwebrtc 420 ms on congestive links

## Issue

On `issue_61_googcc_glue_congestive` and `issue_12_lossbwe_congestive` (2.5 Mbps, then 0.8 Mbps at 15 s
behind a 60 ms drop-tail queue), the video end-to-end delay p95 over seeds 1-10 is 808 ms against
libwebrtc's 420 ms; the p50 matches (204 vs 196 ms). Split out of #216 by the Owner (local coordinator);
https://github.com/aray4702/streaming_media/issues/224.

## Root cause

Not fully found; this record covers one cause, a port difference in what the delay-based estimator sees.

- **What differs in the call (seed 1, `parity-data/traces/`).** After the drop at 15 s, libwebrtc's
  trendline stays in overuse from 15.53 to 16.03 s and `AimdRateControl` cuts five times
  (1291, 1124, 989, 778, 669 kbps), so its target sits below the 800 kbps link: the bottleneck queue drains
  to about 1.5 KB, nothing is lost, its pacer queue is 533 ms at 16 s and 66 ms at 17 s. Ours is in overuse
  for 15.68-15.83 s only and cuts once (1177 kbps); `LossBasedBweV2` then holds the target at 780-800 kbps,
  the pacer sends 1.1 x that into the 800 kbps link, about 100 kbps is lost all the time, the
  retransmissions keep the pacer backlogged (1366 ms at 17 s), and it drains only by 25 s. Before the drop
  libwebrtc's detector also catches the start-up ramp (overuse 3.63-3.98 s, cut to 2116 kbps); ours does
  not on seed 1, and on 4 of 10 seeds it never reaches overuse before 15 s.
- **One input differs.** libwebrtc's inter-arrival groups (the `detector` rows of the bwe trace) split at
  3010 and 3033 ms where ours merged them into one 100 ms burst. libwebrtc's `TransportFeedbackAdapter`
  rounds every receive time down to a whole millisecond (`transport_feedback_adapter.cc:238` for the
  base-time delta, `:270` for each packet's offset from the base) on a local time base that starts at the
  first feedback's receive time (`:233-245`); its `recv_delta` values in the trace are whole ms. Ours
  (`SendHistory::OnFeedback`, `src/cc/transport_feedback.cc` on `main`) passed the receiver's clock with the
  250 us tick, so `InterArrivalDelta::BelongsToBurst` (`arrival_time_delta <= 5 ms`, negative propagation
  delta) saw different deltas and grouped differently, and the trendline slope differed with it.

## Fix

`SendHistory::OnFeedback` now builds receive times as `TransportFeedbackAdapter::ProcessTransportFeedback`
does: an empty feedback changes nothing; the first feedback sets `current_offset_` to its local receive
time; later ones move it by `GetBaseDelta` (24-bit wrap of the 64 ms base time handled as libwebrtc does)
rounded down to 1 ms, or reset it to the receive time if it would go negative; every received packet's
time is `current_offset_ + RoundDownTo(delta_since_base, 1 ms)`. `RoundDownTo` is integer division
(truncation toward zero) as in libwebrtc's `UnitBase`. `transport_feedback.h` keeps the two fields
(`current_offset_`, `last_transport_feedback_base_time_`).

Every user of `PacketResult::receive_time` works on differences, as in libwebrtc (`DelayBasedBwe`,
`ProbeBitrateEstimator`, the acknowledged-rate estimators, the RTT's propagation term), so only the
resolution and base change.

Guard rails changed under program.md 0.2 (the PR is labelled `needs-human`):
- `issue_12_lossbwe_policer` `video.freeze_total_ms < 6000` -> `< 9500`: ours seed 1 9387; libwebrtc fails
  6000 on the same seed (14858; 10-seed mean 10663, ours 8517 -> 10158).
- `issue_152_alr_request_probe_v2` `video.freeze_count == 0` -> `<= 2`: libwebrtc has 1 freeze on the same
  seed (seed 4); seeds 4-13 mean 1.3 for libwebrtc, ours 1.3 -> 1.0.
- `E2E_TransportFeedback.EveryPacketReportedReceivedOnceWithinQuantization`: receive times are no longer on
  the receiver's clock, so the check is now the spread of (receive time - true arrival) below 2.5 ms (tick
  plus the 1 ms round-down, each truncating toward zero), in place of an absolute error within 250 us.
- `TransportFeedback.BuilderAndHistoryJoin`: expects libwebrtc's local base and whole-ms times.

Not changed, and failing on this branch (libwebrtc's value unknown or passing):
- `issue_48_prober_low_bandwidth` seed 1: `video.freeze_count == 0` (2) and `cc.queue_p95_ms < 400`
  (451). libwebrtc seed 1: 0 freezes, 379 ms. Over 10 seeds: freezes ours 2.5 -> 4.5, libwebrtc 2.5;
  queue p95 388 -> 405, libwebrtc 395 (both within the parity band).
- `E2E_DelayBwe.TracksCapacityStepsWithLowQueueing` phase 1 `queue_p95_ms <= 600` (557 -> 636) and the
  `cc/Q_Gcc_capacity_steps` benchmark on the same simulation; no libwebrtc counterpart.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_224_congestive_e2e_delay.scn` | `video.e2e_delay_p95_ms` 1066 (expect < 950) | 873 |
| `tests/unit/cc/transport_feedback_test.cc` `TransportFeedback.ReceiveTimesAreWholeMillisecondsOnTheLocalBase` | receive times on the receiver's clock with 250 us ticks | whole ms on the local base |
| `tests/e2e/cc_e2e_test.cc` `E2E_TransportFeedback.EveryPacketReportedReceivedOnceWithinQuantization` | (rewritten for the local base) | spread 1540 us |

## Results

`issue_61_googcc_glue_congestive` (same link as the regression scenario), seeds 1-10, before -> after,
with libwebrtc from `parity-data` (`compare-webrtc.json`, `main` 6fcbb5f):

| seed | e2e p95 before | after | libwebrtc | loss % before | after | libwebrtc | freeze ms before | after | libwebrtc |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1066 | 873 | 231 | 5.8 | 4.0 | 2.3 | 1593 | 1981 | 1042 |
| 2 | 556 | 878 | 490 | 5.5 | 4.3 | 3.9 | 2295 | 1537 | 1711 |
| 3 | 710 | 627 | 390 | 4.1 | 4.7 | 4.9 | 1744 | 1494 | 1716 |
| 4 | 831 | 1136 | 486 | 6.4 | 6.4 | 4.2 | 1385 | 1964 | 1711 |
| 5 | 627 | 734 | 285 | 5.0 | 4.8 | 3.2 | 1809 | 1594 | 756 |
| 6 | 1059 | 527 | 426 | 6.4 | 4.6 | 3.8 | 2091 | 1032 | 872 |
| 7 | 793 | 716 | 478 | 4.3 | 4.3 | 3.7 | 1856 | 1364 | 978 |
| 8 | 815 | 595 | 557 | 5.3 | 4.5 | 3.9 | 1165 | 1339 | 1835 |
| 9 | 898 | 567 | 414 | 6.3 | 4.7 | 4.0 | 2041 | 1130 | 1410 |
| 10 | 720 | 744 | 442 | 4.5 | 5.4 | 4.0 | 965 | 1156 | 1368 |
| mean | 808 | 740 | 420 | 5.4 | 4.8 | 3.8 | 1695 | 1459 | 1340 |

The e2e p95 is lower on 6 seeds and higher on 4 (2, 4, 5, 10); the means of all three move toward
libwebrtc's.

**Parity over the whole suite** (83 scenarios, seeds as `sm_compare` runs them, ours rebuilt locally
for `main` and this branch, libwebrtc from `parity-data`, `tools/parity_report.py`): `main` 181 gaps (the
same count as the published report), this branch 156: **51 closed, 26 opened**. Gaps where we are behind
libwebrtc go from 95 to 69. Most of the opened ones are the cross-traffic family (`cross_traffic`,
`probe_v2_cross_traffic` and its copies) moving ahead of libwebrtc on queue and audio delay
(`cc.queue_p95_ms` 228 vs 251).

The seed-1 scenario suite against `testbench/baseline.json`: 279 metrics worse and 286 better
(`testbench_compare.py`); the baseline is updated. Single-seed changes of this size follow from any change
to the delay detector's input and are judged on the 10-seed parity above, not one by one.

## Remaining

- #224's target (e2e p95 within libwebrtc's band, 420 ms) is not reached: 740 ms. On seed 1 our detector
  still leaves overuse at 15.83 s and still misses the start-up overuse (ours first at 4.43 s, libwebrtc
  3.63 s); its groups are closer to libwebrtc's but still merge some frames that libwebrtc splits.
  The next step needs libwebrtc's per-packet `(send_time, receive_time)` as `DelayBasedBwe` sees them, to
  find the remaining input difference.
- Four checks fail where libwebrtc's value doesn't justify a change (listed under Fix). They need the
  Owner's decision before this can merge.
