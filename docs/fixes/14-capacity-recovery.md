---
issue: 14
title: Bitrate takes 15+ s to recover after the link improves (AIMD link-capacity estimate stale)
severity: sev:2
status: fixed
area: area:cc
regression_tests: testbench/scenarios/issue_14_capacity_recovery.scn, tests/unit/cc/probe_v2_test.cc
---

# #14: Bitrate takes 15+ s to recover after the link improves

## Issue

When the link capacity drops and later returns, the send target climbs back slowly: on `capacity_drop`
(2.5 Mbps, 500 kbps at 10 s, 2.5 Mbps again at 25 s) it has not reached half the capacity 15 s after the
capacity returned on 8 of 10 seeds (`cc.max_settle_ms` 14870 mean, the 10 s rule of program.md 6.3).
Reported by the Owner and the Fixer's analysis on #12; the Tester added 10x/5x/4x drop variants.
https://github.com/aray4702/streaming_media/issues/14.

libwebrtc M157 does the same (`cc.max_settle_ms` 14870 on the same seeds, CI compare-webrtc on `main`
dd070af), so the Owner moved the issue to `stage:later` and approved going beyond libwebrtc with a
recovery probe, with limits (Owner decisions on the issue; brief of the Builder's window, as #795 / PR #1143).

## Root cause

After the drop, `AimdRateControl` (`src/cc/aimd_rate_control.cc:200-245`, libwebrtc's
`aimd_rate_control.cc`) keeps a link-capacity estimate from the acked rate at overuse. When the
capacity returns, the sender sends only its target, so the acked rate never exceeds the estimate's
upper bound, the estimate is never reset, and the controller stays in additive increase (about one
packet per response time, ~50 kbps/s at these rates). Nothing probes upward: `ProbeController`
(`src/cc/probing.cc`, `RequestProbe`) probes after a drop only within 5 s of it and in or just after an
ALR, and periodic ALR probing is off for a call with video. Both stacks therefore climb from ~450 kbps
to 1250 kbps in about 15 s. The upper-bound and lower-bound resets of the link-capacity estimate are
already ported; the previous analysis on this issue showed they cannot reach the target alone.

## Fix

Recovery probing (beyond libwebrtc), in `ProbeControllerV2::MaybeRecoveryProbe` (`src/cc/probing.cc`),
on by default through `GccConfig::recovery_probing` (`false`, or the test bench's `recovery_probing off`,
restores libwebrtc). A probe at 2x the estimate goes out when all of these hold:

- the estimate has been below half the highest **acknowledged** rate of the last 30 s (what the link
  delivered, not a start rate or a probe result it never carried) for a whole 5 s, so the queue the drop
  built has drained;
- the limit is the delay-based estimate with the detector normal (no delay increase, loss-based or RTT
  limit: `BandwidthLimitedCause::kDelayBasedLimited`), and the sender is not application-limited (ALR);
- the path is clear: the last transport feedback's RTT is within 50 ms of its minimum and the reported
  loss is at most 2% (`GccController::OnTransportFeedback`, `SetRecoveryPathClear`);
- 5 s have passed since the last recovery probe.

The probe is never above the delivered level. A result above 0.7 of the probe rate probes further at 2x
the result, as libwebrtc's exponential probing does. A recovery cluster (`ProbeCluster::recovery`) counts
only if the link kept up with it (`ProbeBitrateEstimator`: receive rate >= 0.9 x send rate); a probe into
a full link (the capacity still low, or competing traffic that FIFO sharing would let a saturated probe
overrate) leaves the estimate alone. So the estimate moves only to a rate the link carried, and the chain
stops at the first probe that finds no room.

The Owner's limits: at most about 2x the current target per probe (each step is 2x the latest estimate,
never above the level), a minimum interval (5 s), no probe and no further probe while the delay rises
(`InitiateProbing` sends nothing in a delay-limited state), and the extra probe traffic is reported
(`probe/recovery` event, `recovery_probes` and `recovery_saturated` counters; padding below).

Each gate was added because a 10-seed run of the whole suite showed cells behind libwebrtc without it
(see Results): the 5 s hold and the RTT gate stop probes into the standing queue a competing flow leaves
(`issue_204_cross_traffic_join`: a probe into a full drop-tail queue loses packets and the estimator,
which sees only the received ones, reads the rest as a clean probe), the delivered-rate level stops
probes on a link that never carried the start rate (`issue_466_undershoot_very_low_capacity`), and the
loss and ALR gates keep an audio-only call on a lossy link as libwebrtc (`issue_813_audio_padding_as_libwebrtc`).

`issue_120_rtx_payload_padding_capacity_drop` checks the RTX payload padding against libwebrtc's on the
`capacity_drop` link (`net.padding_kbps < 1`). Recovery probing adds its clusters' padding there (1.0-1.9
kbps on seeds 1-10, from about 0.5), so that scenario now runs with `recovery_probing off` and keeps
measuring what #120 compares; `issue_14_capacity_recovery` measures the same link with probing on.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_14_capacity_recovery.scn` | `cc.max_settle_ms` 13800 (expect < 10000), `net.loss_pct` 1.71 (expect < 1.7) | 1200, 1.34; passes on seeds 1-10 |
| `tests/unit/cc/probe_v2_test.cc` `ProbeControllerV2.RecoveryProbeAfterAHoldBelowHalfTheDeliveredRate` | does not build on main (no recovery probing) | passes |
| `ProbeControllerV2.RecoveryProbeNeverAboveTheDeliveredRate` | idem | passes |
| `ProbeControllerV2.NoRecoveryProbeUnlessEnabledAsLibwebrtc` | idem | passes |
| `ProbeControllerV2.RecoveryLevelIsTheDeliveredRateNotTheEstimate` | idem | passes |
| `ProbeControllerV2.NoRecoveryProbeWhileDelayLossOrRttLimited` | idem | passes |
| `ProbeControllerV2.NoRecoveryProbeWhileAQueueStands` | idem | passes |
| `ProbeControllerV2.NoRecoveryProbeWhenApplicationLimited` | idem | passes |
| `ProbeControllerV2.NoRecoveryProbeOnceTheDeliveredRateIsOld` | idem | passes |
| `ProbeBitrateEstimatorV2.RecoveryClusterCountsOnlyWhenTheLinkKeptUp` | idem | passes |
| `GccController.RecoveryProbingSwitch` | idem | passes |

## Results

Seeds 1-10, `capacity_drop` (the regression scenario's link), main dd070af -> this fix; libwebrtc M157 from
CI compare-webrtc on main dd070af (run published to `parity-data` 0fd0a6c):

| seed | max_settle_ms | unsettled | queue_p95_ms | loss_pct | freeze_total_ms | utilization_pct |
|---|---|---|---|---|---|---|
| 1 | 13800 -> 1200 | 0 -> 0 | 296 -> 296 | 1.71 -> 1.34 | 3034 -> 3034 | 57 -> 84 |
| 2 | 15000 -> 1400 | 1 -> 0 | 281 -> 281 | 1.88 -> 1.44 | 3177 -> 3177 | 53 -> 82 |
| 3 | 15000 -> 1300 | 1 -> 0 | 274 -> 274 | 1.80 -> 1.39 | 3013 -> 3013 | 54 -> 82 |
| 4 | 15000 -> 1400 | 1 -> 0 | 288 -> 288 | 1.87 -> 1.46 | 3238 -> 3238 | 55 -> 82 |
| 5 | 15000 -> 1600 | 1 -> 0 | 290 -> 290 | 1.83 -> 1.41 | 3015 -> 3015 | 54 -> 83 |
| 6 | 15000 -> 1200 | 1 -> 0 | 291 -> 291 | 1.86 -> 1.42 | 3116 -> 3116 | 54 -> 83 |
| 7 | 15000 -> 1500 | 1 -> 0 | 288 -> 288 | 1.81 -> 1.40 | 3104 -> 3104 | 54 -> 83 |
| 8 | 14900 -> 1200 | 0 -> 0 | 306 -> 306 | 1.69 -> 1.31 | 3325 -> 3325 | 55 -> 84 |
| 9 | 15000 -> 1200 | 1 -> 0 | 303 -> 303 | 1.98 -> 1.52 | 2458 -> 2458 | 53 -> 82 |
| 10 | 15000 -> 1100 | 1 -> 0 | 278 -> 278 | 1.89 -> 1.45 | 2785 -> 2785 | 54 -> 84 |
| mean | 14870 -> **1310** | 0.8 -> 0 | 289.5 -> 289.5 | 1.8 -> 1.4 | 3027 -> 3027 | 54.4 -> 82.8 |
| libwebrtc | 14870 | (not in per-seed data) | 289.4 | 1.8 | 3026 | 54.4 |

The issue's own target (`cc.max_settle_ms` < 5000) is reached as well. The probes' extra traffic on this
link: padding 0.5 -> 1.4 kbps, RTX 33.7 -> 33.9 kbps (means of 10 seeds); RTT p50 75.8 -> 79.9 ms,
audio mouth-to-ear p95 835 -> 835 ms, video e2e p95 473 -> 481 ms (libwebrtc 74.3, 835, 469).

The Owner's safety list (seeds 1-10 unless noted; queue p95 / loss / freeze total, main -> this fix):
`capacity_drop` 289.5 / 1.8% / 3027 -> 289.5 / 1.4% / 3027; `wifi_to_lte_handover` and `outage_3s`
unchanged (119.5 / 5.1% / 3945 and 39.1 / 5.9% / 3184); a shallow-buffer step (`capacity_drop`'s link
with `queue=20ms`, seeds 1-5) unchanged (12.4 ms / 10.3% / 4280 ms; settle 7320 ms, no probe goes out:
the loss is above 2%).

The Tester's variants on #14 (40 s, delay 40 ms, queue 300 ms, seeds 1-5, settle main -> this fix):
10x drop 3 Mbps -> 300 kbps -> 3 Mbps 15000 -> 3240 ms (unsettled 1 -> 0), 5x 14120 -> 1440, 4x 11080
-> 1200, queue and freezes unchanged. A rise from a link that was **always** low (300 kbps -> 3 Mbps
with no earlier drop: 21060 ms; the Tester's `rnd9`: 17000 ms) is unchanged: there is no delivered rate
to go back to, so recovery probing does not fire (see the PR for the follow-up question).

Comparison with libwebrtc: the whole suite on 10 seeds locally (macOS arm64; the local `main` run gives
exactly CI's parity report: 28 gaps, 0 behind), our per-seed values substituted into `main`'s
compare-webrtc.json and run through `tools/parity_report.py --base` (first 10 seeds of each scenario):
**0 cells behind libwebrtc**, 66 ahead (28 on main; 38 opened: `cc.max_settle_ms`, `cc.utilization_pct`
and `video.avg_kbps` on the `capacity_drop` family, the cross-traffic joins and the host-RTT scenarios;
0 closed). The closest any cell came to behind is 0.90 of its band (`issue_911_host_rtt_10s` /
`issue_921_host_rtt_under_limit` `video.e2e_delay_p95_ms` 310 -> 320 ms, libwebrtc 305, band 16), then
the cross-traffic joins' `cc.rtt_p50_ms` (0.68-0.77 of the band: a higher target after the cross traffic
leaves). Before the gates in the Fix section, the first designs had 16 behind cells (cross-traffic joins:
RTT, delay, freezes; `issue_466_undershoot_very_low_capacity` pauses).

The scenario suite against the baseline (seed 1, `tools/testbench_compare.py --markdown`). The flagged
"regressions" are the higher rate itself (`video.encoder_target_avg_kbps`, `video.encode_height_mean`,
`cc.send_over_target_*` as the encoder follows a faster-rising target), plus the network and e2e delay of
the larger frames in `issue_451/911/921_host_rtt_*` (`video.delay_network_p95_ms` 136 -> 157 ms) and
`issue_5_keyframe_flush` (`video.e2e_delay_p50_ms` 253 -> 284 ms), which stay within the parity band of
libwebrtc on 10 seeds (above):

33 regression(s), 100 improvement(s)
| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| capacity_drop | cc.goodput_kbps | 994.5 | 1471 | improved |
| capacity_drop | cc.max_settle_ms | 1.38e+04 | 1200 | improved |
| capacity_drop | cc.send_over_target_p95_pct | 14.21 | 18.42 | REGRESSED |
| capacity_drop | cc.target_avg_kbps | 1082 | 1591 | improved |
| capacity_drop | cc.utilization_pct | 56.83 | 84.04 | improved |
| capacity_drop | repair.overhead_pct | 3.409 | 2.295 | improved |
| capacity_drop | video.avg_kbps | 890.6 | 1351 | improved |
| capacity_drop | video.encode_height_mean | 339.7 | 372.2 | REGRESSED |
| capacity_drop | video.encoder_target_avg_kbps | 878.6 | 1327 | REGRESSED |
| capacity_drop | video.qp_mean | 26.41 | 23.71 | improved |
| issue_14_capacity_recovery | (new scenario) | - | - | new |
| issue_204_cross_traffic_join | cc.goodput_kbps | 1540 | 1908 | improved |
| issue_204_cross_traffic_join | cc.max_settle_ms | 6700 | 4200 | improved |
| issue_204_cross_traffic_join | cc.send_over_target_ms | 0 | 200 | REGRESSED |
| issue_204_cross_traffic_join | cc.send_over_target_p95_pct | 9.885 | 8.364 | improved |
| issue_204_cross_traffic_join | cc.target_avg_kbps | 1858 | 2463 | improved |
| issue_204_cross_traffic_join | cc.utilization_pct | 59.64 | 73.9 | improved |
| issue_204_cross_traffic_join | video.avg_kbps | 1419 | 1775 | improved |
| issue_204_cross_traffic_join | video.encoder_target_avg_kbps | 1398 | 1750 | REGRESSED |
| issue_204_cross_traffic_join | video.qp_mean | 25.28 | 23.5 | improved |
| issue_23_capacity_drop | cc.goodput_kbps | 994.5 | 1471 | improved |
| issue_23_capacity_drop | cc.max_settle_ms | 1.38e+04 | 1200 | improved |
| issue_23_capacity_drop | cc.send_over_target_p95_pct | 14.21 | 18.42 | REGRESSED |
| issue_23_capacity_drop | cc.target_avg_kbps | 1082 | 1591 | improved |
| issue_23_capacity_drop | cc.utilization_pct | 56.83 | 84.04 | improved |
| issue_23_capacity_drop | repair.overhead_pct | 3.409 | 2.295 | improved |
| issue_23_capacity_drop | video.avg_kbps | 890.6 | 1351 | improved |
| issue_23_capacity_drop | video.encode_height_mean | 339.7 | 372.2 | REGRESSED |
| issue_23_capacity_drop | video.encoder_target_avg_kbps | 878.6 | 1327 | REGRESSED |
| issue_23_capacity_drop | video.qp_mean | 26.41 | 23.71 | improved |
| issue_24_timing_v2_default | cc.send_over_target_p95_pct | 24.57 | 23.14 | improved |
| issue_24_timing_v2_render_fps | cc.send_over_target_p95_pct | 24.57 | 23.14 | improved |
| issue_346_reconfigure_capacity_drop | cc.goodput_kbps | 994.5 | 1471 | improved |
| issue_346_reconfigure_capacity_drop | cc.max_settle_ms | 1.38e+04 | 1200 | improved |
| issue_346_reconfigure_capacity_drop | cc.send_over_target_p95_pct | 14.21 | 18.42 | REGRESSED |
| issue_346_reconfigure_capacity_drop | cc.target_avg_kbps | 1082 | 1591 | improved |
| issue_346_reconfigure_capacity_drop | cc.utilization_pct | 56.83 | 84.04 | improved |
| issue_346_reconfigure_capacity_drop | repair.overhead_pct | 3.409 | 2.295 | improved |
| issue_346_reconfigure_capacity_drop | video.avg_kbps | 890.6 | 1351 | improved |
| issue_346_reconfigure_capacity_drop | video.encode_height_mean | 339.7 | 372.2 | REGRESSED |
| issue_346_reconfigure_capacity_drop | video.encoder_target_avg_kbps | 878.6 | 1327 | REGRESSED |
| issue_346_reconfigure_capacity_drop | video.qp_mean | 26.41 | 23.71 | improved |
| issue_346_reconfigure_cross_join | cc.goodput_kbps | 1540 | 1908 | improved |
| issue_346_reconfigure_cross_join | cc.max_settle_ms | 6700 | 4200 | improved |
| issue_346_reconfigure_cross_join | cc.send_over_target_ms | 0 | 200 | REGRESSED |
| issue_346_reconfigure_cross_join | cc.send_over_target_p95_pct | 9.885 | 8.364 | improved |
| issue_346_reconfigure_cross_join | cc.target_avg_kbps | 1858 | 2463 | improved |
| issue_346_reconfigure_cross_join | cc.utilization_pct | 59.64 | 73.9 | improved |
| issue_346_reconfigure_cross_join | video.avg_kbps | 1419 | 1775 | improved |
| issue_346_reconfigure_cross_join | video.encoder_target_avg_kbps | 1398 | 1750 | REGRESSED |
| issue_346_reconfigure_cross_join | video.qp_mean | 25.28 | 23.5 | improved |
| issue_356_capacity_drop_xr_rtt | cc.goodput_kbps | 994.5 | 1471 | improved |
| issue_356_capacity_drop_xr_rtt | cc.max_settle_ms | 1.38e+04 | 1200 | improved |
| issue_356_capacity_drop_xr_rtt | cc.send_over_target_p95_pct | 14.21 | 18.42 | REGRESSED |
| issue_356_capacity_drop_xr_rtt | cc.target_avg_kbps | 1082 | 1591 | improved |
| issue_356_capacity_drop_xr_rtt | cc.utilization_pct | 56.83 | 84.04 | improved |
| issue_356_capacity_drop_xr_rtt | repair.overhead_pct | 3.409 | 2.295 | improved |
| issue_356_capacity_drop_xr_rtt | video.avg_kbps | 890.6 | 1351 | improved |
| issue_356_capacity_drop_xr_rtt | video.encode_height_mean | 339.7 | 372.2 | REGRESSED |
| issue_356_capacity_drop_xr_rtt | video.encoder_target_avg_kbps | 878.6 | 1327 | REGRESSED |
| issue_356_capacity_drop_xr_rtt | video.qp_mean | 26.41 | 23.71 | improved |
| issue_390_cross_join_seed15 | cc.goodput_kbps | 1411 | 1908 | improved |
| issue_390_cross_join_seed15 | cc.max_settle_ms | 1.3e+04 | 1.04e+04 | improved |
| issue_390_cross_join_seed15 | cc.target_avg_kbps | 1735 | 2462 | improved |
| issue_390_cross_join_seed15 | cc.utilization_pct | 54.65 | 73.93 | improved |
| issue_390_cross_join_seed15 | video.avg_kbps | 1295 | 1777 | improved |
| issue_390_cross_join_seed15 | video.encoder_target_avg_kbps | 1276 | 1758 | REGRESSED |
| issue_390_cross_join_seed15 | video.qp_mean | 26.23 | 23.54 | improved |
| issue_451_host_rtt_near_limit | audio.jb_delay_p50_ms | 380 | 340 | improved |
| issue_451_host_rtt_near_limit | cc.goodput_kbps | 1327 | 1645 | improved |
| issue_451_host_rtt_near_limit | cc.max_settle_ms | 1.2e+04 | 4900 | improved |
| issue_451_host_rtt_near_limit | cc.target_avg_kbps | 1462 | 1797 | improved |
| issue_451_host_rtt_near_limit | cc.utilization_pct | 60.31 | 74.77 | improved |
| issue_451_host_rtt_near_limit | video.avg_kbps | 1213 | 1521 | improved |
| issue_451_host_rtt_near_limit | video.delay_network_p95_ms | 135.8 | 157.3 | REGRESSED |
| issue_451_host_rtt_near_limit | video.encoder_target_avg_kbps | 1189 | 1494 | REGRESSED |
| issue_451_host_rtt_near_limit | video.qp_mean | 25.93 | 23.89 | improved |
| issue_53_cwnd_pushback | cc.send_over_target_p95_pct | 24.57 | 23.14 | improved |
| issue_5_keyframe_flush | cc.goodput_kbps | 934.5 | 1440 | improved |
| issue_5_keyframe_flush | cc.max_settle_ms | 1.5e+04 | 1400 | improved |
| issue_5_keyframe_flush | cc.target_avg_kbps | 1022 | 1556 | improved |
| issue_5_keyframe_flush | cc.utilization_pct | 53.4 | 82.29 | improved |
| issue_5_keyframe_flush | repair.overhead_pct | 3.781 | 2.458 | improved |
| issue_5_keyframe_flush | video.avg_kbps | 835.8 | 1325 | improved |
| issue_5_keyframe_flush | video.e2e_delay_p50_ms | 253 | 284 | REGRESSED |
| issue_5_keyframe_flush | video.encoder_target_avg_kbps | 821.2 | 1311 | REGRESSED |
| issue_5_keyframe_flush | video.qp_mean | 27.29 | 23.47 | improved |
| issue_76_rtx_v2_keyframe_flush | cc.goodput_kbps | 916.5 | 1280 | improved |
| issue_76_rtx_v2_keyframe_flush | cc.max_settle_ms | 1.5e+04 | 3300 | improved |
| issue_76_rtx_v2_keyframe_flush | cc.send_over_target_p95_pct | 21.74 | 24.23 | REGRESSED |
| issue_76_rtx_v2_keyframe_flush | cc.target_avg_kbps | 1019 | 1407 | improved |
| issue_76_rtx_v2_keyframe_flush | cc.utilization_pct | 52.37 | 73.14 | improved |
| issue_76_rtx_v2_keyframe_flush | repair.overhead_pct | 6.716 | 5.661 | improved |
| issue_76_rtx_v2_keyframe_flush | video.avg_kbps | 816.3 | 1169 | improved |
| issue_76_rtx_v2_keyframe_flush | video.encoder_target_avg_kbps | 806.4 | 1151 | REGRESSED |
| issue_76_rtx_v2_keyframe_flush | video.qp_mean | 27.13 | 24.72 | improved |
| issue_911_host_rtt_10s | audio.jb_delay_p50_ms | 380 | 340 | improved |
| issue_911_host_rtt_10s | cc.goodput_kbps | 1327 | 1645 | improved |
| issue_911_host_rtt_10s | cc.max_settle_ms | 1.2e+04 | 4900 | improved |
| issue_911_host_rtt_10s | cc.target_avg_kbps | 1462 | 1797 | improved |
| issue_911_host_rtt_10s | cc.utilization_pct | 60.31 | 74.77 | improved |
| issue_911_host_rtt_10s | video.avg_kbps | 1213 | 1521 | improved |
| issue_911_host_rtt_10s | video.delay_network_p95_ms | 135.8 | 157.3 | REGRESSED |
| issue_911_host_rtt_10s | video.encoder_target_avg_kbps | 1189 | 1494 | REGRESSED |
| issue_911_host_rtt_10s | video.qp_mean | 25.93 | 23.89 | improved |
| issue_921_host_rtt_under_limit | audio.jb_delay_p50_ms | 380 | 340 | improved |
| issue_921_host_rtt_under_limit | cc.goodput_kbps | 1327 | 1645 | improved |
| issue_921_host_rtt_under_limit | cc.max_settle_ms | 1.2e+04 | 4900 | improved |
| issue_921_host_rtt_under_limit | cc.target_avg_kbps | 1462 | 1797 | improved |
| issue_921_host_rtt_under_limit | cc.utilization_pct | 60.31 | 74.77 | improved |
| issue_921_host_rtt_under_limit | video.avg_kbps | 1213 | 1521 | improved |
| issue_921_host_rtt_under_limit | video.delay_network_p95_ms | 135.8 | 157.3 | REGRESSED |
| issue_921_host_rtt_under_limit | video.encoder_target_avg_kbps | 1189 | 1494 | REGRESSED |
| issue_921_host_rtt_under_limit | video.qp_mean | 25.93 | 23.89 | improved |
| pacer_v2_capacity_drop | cc.goodput_kbps | 994.5 | 1471 | improved |
| pacer_v2_capacity_drop | cc.max_settle_ms | 1.38e+04 | 1200 | improved |
| pacer_v2_capacity_drop | cc.send_over_target_p95_pct | 14.21 | 18.42 | REGRESSED |
| pacer_v2_capacity_drop | cc.target_avg_kbps | 1082 | 1591 | improved |
| pacer_v2_capacity_drop | cc.utilization_pct | 56.83 | 84.04 | improved |
| pacer_v2_capacity_drop | repair.overhead_pct | 3.409 | 2.295 | improved |
| pacer_v2_capacity_drop | video.avg_kbps | 890.6 | 1351 | improved |
| pacer_v2_capacity_drop | video.encode_height_mean | 339.7 | 372.2 | REGRESSED |
| pacer_v2_capacity_drop | video.encoder_target_avg_kbps | 878.6 | 1327 | REGRESSED |
| pacer_v2_capacity_drop | video.qp_mean | 26.41 | 23.71 | improved |
| probe_v2_capacity_drop | cc.goodput_kbps | 994.5 | 1471 | improved |
| probe_v2_capacity_drop | cc.max_settle_ms | 1.38e+04 | 1200 | improved |
| probe_v2_capacity_drop | cc.send_over_target_p95_pct | 14.21 | 18.42 | REGRESSED |
| probe_v2_capacity_drop | cc.target_avg_kbps | 1082 | 1591 | improved |
| probe_v2_capacity_drop | cc.utilization_pct | 56.83 | 84.04 | improved |
| probe_v2_capacity_drop | repair.overhead_pct | 3.409 | 2.295 | improved |
| probe_v2_capacity_drop | video.avg_kbps | 890.6 | 1351 | improved |
| probe_v2_capacity_drop | video.encode_height_mean | 339.7 | 372.2 | REGRESSED |
| probe_v2_capacity_drop | video.encoder_target_avg_kbps | 878.6 | 1327 | REGRESSED |
| probe_v2_capacity_drop | video.qp_mean | 26.41 | 23.71 | improved |
