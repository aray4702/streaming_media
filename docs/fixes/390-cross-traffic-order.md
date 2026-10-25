---
issue: 390
title: "GoogCC: cross-traffic join stuck above capacity on about 12 % of seeds (libwebrtc 0/80)"
severity: sev:2
status: partial
area: area:testbench, area:cc
regression_tests: testbench/scenarios/issue_390_cross_join_seed15.scn, tests/unit/testbench/testbench_metrics_test.cc
---

# #390: GoogCC: cross-traffic join stuck above capacity on about 12 % of seeds (libwebrtc 0/80)

## Issue

On `issue_204_cross_traffic_join`, 2 Mbps of non-responsive cross traffic joins a 3 Mbps bottleneck with a
300 ms drop-tail queue at 10 s. On some seeds our call stayed above the free 1 Mbps for several seconds: the
queue stayed full and video froze about once a second. On `main` 82a6378 this happened on 9 of seeds 1-80
(10 or more freezes; mean 4.75 freezes). libwebrtc on the same links: 0 of 80, mean 2.76 (#389,
`compare_seeds 80`). The Fixer found it while attributing draft #389, and the Owner placed it on #64:
https://github.com/aray4702/streaming_media/issues/390.

## Root cause

The bug is in the test bench, not in the library. The two harnesses put the packets of one millisecond on the
bottleneck in opposite orders:

- `sm_compare` (`compare/webrtc/webrtc_call.cc`, `tick`) sends the cross traffic of millisecond t first. libwebrtc's
  pacer runs after that, in tasks at the same time. On #392's traces, every millisecond where both send has the
  cross traffic first: 215-224 such milliseconds per seed, on all four seeds traced.
- `sm_testbench` (`testbench/call.cc`, the 1 ms loop on the commit before the fix) ran `sender.Process(now)` and put
  the call's packets on the link first, then the cross traffic: 215-231 milliseconds per seed with the call first.

Packets of a millisecond arrive as a burst: a whole video frame, and one 1200 B cross packet every 4.8 ms. When the
queue is full, the second arrival is the one dropped. So on our stack the cross traffic took the drops, and on
libwebrtc's the call did. In 10.4-11.4 s after the join (#392's traces), 8-15 % of our packets were dropped against
10-19 % of libwebrtc's, and 4-6 % of the cross traffic on ours against 2-4 % on libwebrtc's.

With fewer drops, our acknowledged rate ran 2-3 % higher (seed 30, the same cut times on both stacks: 1592 vs
1556 kbps delivered in the second before the 11.09 s cut). So each delay-based cut (0.85 x acked) landed higher:
1365 vs 1330, then 1186 vs 1162 kbps. Ours then kept the queue full after the second cut: on seed 15, 1127 kbps on
the link in 11.3-12.5 s against libwebrtc's 891. The delay gradient stayed flat, so the overuse that takes
libwebrtc down to about 880 kbps at 14 s never came. `LossBasedBweV2` held 1126-1210 kbps on 3-8 % loss, as its
rule does with those inputs (`loss_based_bwe_v2.cc`, read against the port: no difference).

The congestion-window pushback, as libwebrtc's, decides between the two outcomes. No `main` seed that stuck dropped
a frame for the window in 10-13 s, while 34 of the 71 that didn't stick did. On seeds 15 and 25, libwebrtc's bytes
in flight passed its window after the second cut (fill 1.01-1.03, reduce ratio up to 0.145), and ours peaked at
0.87.

Ruled out on the way, on 80 seeds each:
- The RTT given to `AimdRateControl`: any fixed reduction interval from 80 to 200 ms gave 8-12 stuck seeds.
- The RR schedule: our receiver sending at libwebrtc's RR times gave 14. A per-seed random schedule gave 6-10.
- A second, audio-only RR stream as libwebrtc's audio receive stream sends: 10-11.

## Fix

`testbench/call.cc`: in each millisecond, the cross traffic goes on the forward link before the sender's packets, as
in `sm_compare`. `compare/README.md` states the order under "The same links". No library code changes.

This changes every scenario with `cross=`. Three seed lines measured with the old order fail, and are re-tabled
for the Owner (`needs-human`). On 10 seeds, all three metrics stay in band with libwebrtc:
- `issue_152_alr_request_probe_v2` `cc.utilization_pct >= 68` -> `>= 64`: 67.4 on seed 4; mean of seeds 4-13 71.2,
  libwebrtc 71.9.
- `issue_346_reconfigure_cross_traffic` and `issue_355_cross_traffic_no_alr_probe` `video.e2e_delay_p95_ms` `< 320` /
  `< 300` -> `< 395`: 373 on seed 1, where the call now takes two queue drops; mean of seeds 1-10 237, libwebrtc 232.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_390_cross_join_seed15.scn` | `video.freeze_count` 15 (expect <= 9), `video.freeze_total_ms` 5434 (< 3000), `cc.queue_p95_ms` 295.9 (< 285) | 4, 1886, 270.2 |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.CrossTrafficEntersTheBottleneckBeforeTheCallsPacketsOfTheSameMillisecond` | a millisecond with call packets ahead of the cross traffic | passes |

## Results

`issue_204_cross_traffic_join`, seeds 1-80 (stuck = 10 or more freezes):

| | stuck | mean freezes | mean loss % | mean queue p95 ms |
|---|---|---|---|---|
| before (`main` 82a6378) | 9 (15, 25, 30, 60, 62, 65, 68, 72, 76) | 4.75 | 0.588 | 273.6 |
| after | 3 (5, 62, 76) | 3.27 | 0.451 | 259.3 |
| libwebrtc (#389, 80 seeds) | 0 | 2.76 | 0.398 | 263.4 |

The seeds `main` stuck on, before -> after: 15: 15 -> 4 freezes, 25: 15 -> 3, 30: 10 -> 5, 60: 19 -> 5,
65: 18 -> 3, 68: 18 -> 3, 72: 19 -> 1. 62 and 76 still stick (16 -> 24, 16 -> 23), and seed 5 now does (4 -> 10).

Cross-traffic scenarios, means over `sm_compare`'s seeds, before -> after (libwebrtc from `main`'s published
`compare-webrtc.json`):

| scenario (seeds) | metric | before | after | libwebrtc |
|---|---|---|---|---|
| `cross_traffic` family (1-10) | `video.freeze_count` | 0 | 0.2 | 0 |
| | `video.e2e_delay_p95_ms` | 235 | 237 | 232 |
| | `cc.utilization_pct` | 73.8 | 71.1 | 72.9 |
| | `cc.queue_p95_ms` | 181 | 181 | 184 |
| `issue_152_alr_request_probe_v2` (4-13) | `cc.utilization_pct` | 74.6 | 71.2 | 71.9 |
| | `video.e2e_delay_p95_ms` | 230 | 224 | 232 |
| `issue_204_cross_traffic_join` (1-10) | `video.freeze_count` | 3.0 | 3.4 | 2.3 |
| | `cc.queue_p95_ms` | 275 | 258 | 268 |
| | `net.loss_pct` | 0.428 | 0.474 | 0.386 |

The `cross_traffic` family is `cross_traffic`, `issue_15_aimd_v2_cross_traffic`, `issue_346_reconfigure_cross_traffic`,
`issue_355_cross_traffic_no_alr_probe`, `issue_5_adjuster_default`, `issue_62_neteq_v2_cross_traffic` and
`probe_v2_cross_traffic`, which give the same numbers.

The suite against the baseline: 55 regressions and 10 improvements, all in the scenarios with `cross=`. The seed-1
values of the `cross_traffic` family move most (utilization 73.6 -> 68.8 %, e2e p95 298 -> 373 ms), because on that
seed the call now takes two drops. The 10-seed means above stay in band. `issue_204_cross_traffic_join` and
`issue_346_reconfigure_cross_join` seed 1 improve (freeze total 1404 -> 943 ms).

## Remaining

Seeds 62 and 76 still stick, 3 of 80 counting seed 5, against libwebrtc's 0. On 10 seeds the cross-traffic
family has one seed with two freezes (0.2 against 0). Next step: compare the pushback's window and bytes in flight on
those seeds against libwebrtc's, with #392's trace rows (`cwnd`, `rtt`) on them.
