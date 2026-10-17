---
issue: 19
title: Delay-based BWE reads jitter and reordering as overuse (front end ported as libwebrtc's, behind cc v2)
severity: sev:2
status: partial
area: area:timing, area:cc, area:video
regression_tests: testbench/scenarios/issue_19_cc_v2_high_jitter.scn, tests/unit/cc/bwe_test.cc
---

# #19: Delay-based BWE reads jitter and reordering as overuse

## Issue

On congested Wi-Fi (40 ms jitter with reordering, no loss, bottleneck queue about 1.4 ms) the congestion-control target collapses to
about 160 kbps on a 3 Mbps link, where libwebrtc holds about 1 Mbps. Issue: https://github.com/aray4702/streaming_media/issues/19
(Owner-curated as Phase 3 item 1 of #64: port `InterArrivalDelta` and the trendline detector behind a `cc v1|v2` switch).

## Root cause

Two things feed the delay detector wrongly:

1. `InterArrival` (`src/cc/delay_based_bwe.cc`, v1) forms groups only by the 5 ms send-time span, in the sequence
   order of the feedback. It has no `BelongsToBurst`, so packets a link releases back to back start new groups; reordered packets
   (arrival order differs from send order) give negative or inflated arrival deltas; there is no clock-offset or
   reordered-group reset and no 2 s stream time-out.
2. The feedback is read in sequence-number order; libwebrtc reads it sorted by receive time
   (`TransportPacketsFeedback::SortedByReceiveTime`).

The accumulated delay therefore random-walks upward with jitter alone, and the trendline detector (which is libwebrtc's)
fires. Seeds 1-5 of `high_jitter` with `tcc v2`: 17-28 overuse signals in 30 s.

## Fix

`cc v2` (`GccConfig::version`, scenario directive `cc v1|v2`; v1 stays the default) uses
`DelayBasedBwe::OnPacketResultsV2`, a port of `DelayBasedBwe::IncomingPacketFeedbackVector` / `IncomingPacketFeedback`
(`compare/webrtc/reference/modules/congestion_controller/goog_cc/delay_based_bwe.cc`) with `InterArrivalDelta`
(`.../goog_cc/inter_arrival_delta.{h,cc}`): feedback sorted by receive time (then send time, then sequence), `BelongsToBurst`
(5 ms, 100 ms), reordered packets dropped, reset after 3 reordered groups or a 3 s arrival-clock jump, a 2 s stream time-out that
resets the grouping and the trendline. The trendline estimator was compared line by line with `trendline_estimator.cc` and is equal;
v2 passes it integer-millisecond arrival times as libwebrtc does.  A feedback with no received packet returns without a rate update, as libwebrtc's empty `Result`. The rate control is unchanged (that is #15).
`PacketResult::ReceiveTimeOrder` is not in `compare/webrtc/reference/` (see #57); the ordering is from memory of `api/transport/network_types.cc`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_19_cc_v2_high_jitter.scn` | the base commit does not know `cc v2` | passes on seeds 1-5 |
| `tests/unit/cc/bwe_test.cc` `DelayBweV2.InterArrivalDelta*` | class does not exist | grouping, burst, 100 ms cap, reordered packet, clock-offset reset |
| `tests/unit/cc/bwe_test.cc` `DelayBweV2.JitterAndReorderingReadAsOveruseLessOften` | v1 grouping: more overuse signals than v2 | v2 at most 5 signals |
| `tests/unit/cc/bwe_test.cc` `DelayBweV2.StreamTimeoutResetsDetector` | n/a | one reset after a 2 s gap |

## Results

`high_jitter`, overuse signals in 30 s (events `gcc/overuse`), `tcc v2` in both columns:

| seed | `cc v1` | `cc v2` |
|---|---|---|
| 1 | 28 | 2 |
| 2 | 18 | 3 |
| 3 | 21 | 3 |
| 4 | 17 | 2 |
| 5 | 25 | 5 |

The user-facing target does not move yet: `cc.target_avg_kbps` is 160-310 kbps in both. With the loss-based estimate taken out
(experiment only, not committed) the same scenario gives `cc v2` target 1226-1797 kbps, utilization 48-70 % (libwebrtc 1003 kbps,
39 %), against 344-411 kbps with `cc v1`. The loss-based estimator (`loss_bwe`, 25-29 decreases, "loss" 13 % on a link with 0 % loss: packets
that the feedback has not yet reported count as lost) is what pins the target now; that is #12.
Suite: `main` baseline unchanged (v1 is the default); one new scenario.

## Remaining

- Scenario guard: the `expect cc.target_avg_kbps > 120` line holds on the suite's seed 1 only (v1 80, v2 170 kbps); on seeds 1-5 v1 gives 64-124 and v2 96-222 kbps.

- The target and utilization acceptance of #19 (within the parity band on `high_jitter`) needs #12 (`LossBasedBweV2`), and #15/#48
  for the rest of GoogCC; they are later on the work list.
- 25 ms jitter with `reorder=off` (Tester's suggestion): same front end; check once #12 has landed.
- Video delay (#24) is unchanged by this PR (157-176 ms p95 with `tcc v2` either way).
