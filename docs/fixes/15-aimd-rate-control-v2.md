---
issue: 15
title: Start-up decrease seeds the AIMD link-capacity estimate from probe-inflated acked rates, trapping the target in additive increase
severity: sev:2
status: partial
area: area:cc
regression_tests: testbench/scenarios/issue_15_aimd_v2_clean_link.scn, testbench/scenarios/issue_15_aimd_v2_cross_traffic.scn, testbench/scenarios/issue_15_aimd_v2_random_loss.scn, tests/unit/cc/aimd_rate_control_test.cc
---

# #15: Start-up decrease seeds the AIMD link-capacity estimate from probe-inflated acked rates

## Issue

At start-up the first overuse decrease seeded the AIMD link-capacity estimate from a probe-inflated acknowledged rate, and the delay-based controller then stayed in slow additive increase for about 17 s (https://github.com/aray4702/streaming_media/issues/15). Found by the Fixer while measuring #12; the Owner's roadmap (#64, Phase 3, item 2) turned it into a port of libwebrtc's acked-rate estimator and `AimdRateControl` into `cc v2`.

## Root cause

`cc v1` differs from libwebrtc on the whole overuse path (`src/cc/delay_based_bwe.cc` on the commit before the fix):

- the acknowledged rate is a plain 500 ms sliding window that is valid after 100 ms (libwebrtc: `BitrateEstimator`, a 500 ms first window and then 150 ms Bayesian windows). The first overuse, at about 0.4 s, therefore decreased to `0.85 x` a probe-inflated rate and seeded `link_kbps_` from it;
- `AimdRateController::Update` seeds and updates the link capacity with its own filter, measures the reduce-further interval from the last decrease, has no `bitrate_is_initialized_`, uses a different additive increase and decrease rule, and never acts on overuse without an acked rate;
- `DelayBasedBwe::OnPacketResults` adopts a probe result only if it is above the estimate, where libwebrtc calls `SetEstimate(probe)`.

## Fix

`cc v2` (`GccConfig::version`, `cc v2` in a scenario) now runs libwebrtc's code, ported with its constants (reference: `goog_cc/bitrate_estimator.cc`, `acknowledged_bitrate_estimator.cc`, `link_capacity_estimator.cc`, `remote_bitrate_estimator/aimd_rate_control.cc`, `goog_cc/delay_based_bwe.cc` `MaybeUpdateEstimate`):

- `include/sm/cc/aimd_rate_control.h`, `src/cc/aimd_rate_control.cc`: `BitrateEstimator` (no estimate until one full 500 ms window, then 150 ms windows, uncertainty scale 10), `AckedBitrateEstimatorV2` (updates per received packet in receive-time order), `LinkCapacityEstimator` (alpha 0.05 on overuse, deviation clamp 0.4..2.5) and `AimdRateControl` (hold/increase/decrease, `beta 0.85`, decrease to `beta x throughput - 5 kbps`, fall back to `beta x link capacity`, `TimeToReduceFurther` from the last change of any kind, near-max additive increase with a 4000 bps/s floor, increase limit `1.5 x throughput + 10 kbps`, `bitrate_is_initialized_`, `InitialTimeToReduceFurther`).
- `DelayBasedBwe::MaybeUpdateEstimateV2`: overusing with an acked rate reduces only when `TimeToReduceFurther`; overusing without one halves the estimate every `clamp(rtt, 10, 200)` ms; otherwise a probe result is set as is, else the rate control updates.

`cc v1` is unchanged and stays the default until the flip (#61). Remaining differences from libwebrtc are listed in `tools/catalog.py` (5.2) and `docs/design/libwebrtc-parity.md`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_15_aimd_v2_clean_link.scn` | `cc.utilization_pct >= 86`: 78-84 on seeds 1-5 with the v1 rate control behind `cc v2` | 90.1-90.8 |
| `testbench/scenarios/issue_15_aimd_v2_cross_traffic.scn` | `cc.utilization_pct >= 68`: 60-65 | 70.5-79.4 |
| `testbench/scenarios/issue_15_aimd_v2_random_loss.scn` | `cc.utilization_pct >= 60` and `video.avg_kbps >= 1050` fail on seeds 1, 2, 5 (59.6 / 50.9 / 53.1 %) | 64-79 % on all seeds |
| `tests/unit/cc/aimd_rate_control_test.cc` (`AimdRateControl.*`, `BitrateEstimatorV2.*`, `DelayBweV2.*`, `SendSideBwe.V2UsesTheWindowedAckedRate`) | do not build on `main` (the classes are new). Ports of libwebrtc's `aimd_rate_control_unittest.cc` values (4 kbps floor, 5 kbps at 90 kbps, `TypicalDrop` 216 kbps / 12 kbps, `BweLimitedByAckedBitrate`, ...), plus no estimate before the first 500 ms window, halving at most every RTT, and the unconditional probe estimate | pass |

## Results

Mean of 20 seeds, `cc v1` (main) / `cc v2` (this PR) / libwebrtc M157 (`parity-data`, 10 seeds):

| scenario | utilization % | video kbps | queue p95 ms | video frozen ms | video delay p95 ms |
|---|---|---|---|---|---|
| clean_link | 82.3 / 90.8 / 91.6 | 1883 / 2087 / 2142 | 22 / 29 / 77 | 0 / 0 / 0 | 67 / 73 / 125 |
| cross_traffic | 66.2 / 74.0 / 74.2 | 1550 / 1743 / 1779 | 117 / 114 / 264 | 0 / 0 / 1906 | 156 / 150 / 491 |
| random_loss_5pct | 63.1 / 72.9 / 77.5 | 1084 / 1262 / 1382 | 16.5 / 19.8 / 29.5 | 20 / 0 / 1863 | 315 / 317 / 209 |
| capacity_drop | 54.3 / 58.8 / 57.5 | 813 / 895 / 895 | 381 / 386 / 283 | 2820 / 3010 / 2538 | 318 / 275 / 249 |
| low_bandwidth | 94.9 / 95.1 / 98.7 | 47.8 / 47.5 / 73.4 | 471 / 463 / 484 | 343 / 126 / 12936 | 987 / 975 / 4718 |

Utilization, video rate, `capacity_drop` delay and `max_settle` (14.6 s -> 13.4 s) move toward libwebrtc on every row. Two metrics move away or stay far: `capacity_drop` frozen time (2820 -> 3010 ms, libwebrtc 2538; the sender still overshoots the target after the drop, a send-path/pacer item) and keyframe requests (capacity_drop 8.2 -> 14.7, libwebrtc 0; owned by #23's follow-ups and the loss/repair path, not by the delay controller).

Suite against the baseline: 0 regressions; 9 improvements, all on `issue_19_cc_v2_high_jitter` (the only existing `cc v2` scenario: video delay p95 1519 -> 944 ms, freezes 220 -> 0 ms; libwebrtc 142 ms, so #24 still owns that gap). Three new scenarios are added to `testbench/baseline.json`.

## Remaining

- `AlrDetector` (#48): libwebrtc calls `ExpectFastRateChange` on the acked estimator when ALR ends, and sets `in_alr` on the rate control (default-off trial). Neither happens here.
- `prior_unacked_data` is always zero; no `NetworkStateEstimate`.
- The `cc v2` combination with `LossBasedBweV2` (#12), the probe controller (#48) and the flip that makes `v2` the default and removes v1 (#61, where the guard-rail tests `CombinesDelayAndLossControllers`, `ResolutionFollowsBandwidth` and `Q_Adaptation_bandwidth_drop` are judged against libwebrtc).
- The issue's "time for the target to first reach 1 Mbps on #6's scenario with the loss limit lifted" needs #12's estimator and is measured there.
