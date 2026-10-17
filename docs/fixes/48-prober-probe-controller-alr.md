---
issue: 48
title: Start-up probing depends on the first packet's size; port libwebrtc's BitrateProber, ProbeController and AlrDetector behind cc v2
severity: sev:3
status: partial
area: area:cc
regression_tests: testbench/scenarios/issue_48_prober_low_bandwidth.scn, testbench/scenarios/probe_v2_clean_link.scn, testbench/scenarios/probe_v2_cross_traffic.scn, testbench/scenarios/probe_v2_capacity_drop.scn, tests/unit/cc/probe_v2_test.cc
---

# #48: Start-up probing depends on the first packet's size

## Issue

A small first packet (the 24-byte SPS+PPS of H.264) changed the congestion controller's start-up: the first probe cluster was rejected as invalid and the target started far lower, which moved the rest of the call (https://github.com/aray4702/streaming_media/issues/48). Found by the Fixer while making plan PR 7 of #32. The Owner's roadmap (#64, Phase 3, item 3) re-scoped it to duplicating libwebrtc's prober and probe controller in `cc v2`, with the `AlrDetector` carried over from #15.

## Root cause

The pacer did not do what libwebrtc's `BitrateProber` does (`src/cc/pacing_controller.cc` on the commit before the fix, `ProcessProbe`):

- a probe burst was at least `max(1500 B, rate x 5 ms)`, so at a low cluster rate a whole cluster (`rate x 15 ms`, 1687 B at 900 kbps) left in one burst and its send interval was zero: `ProbeBitrateEstimator` rejected it (this is the failure the issue traces). libwebrtc times each burst by the bytes sent at the cluster rate, sends `rate x min_probe_delta` bytes per burst, and starts a cluster on a media packet of a useful size;
- a probe started even with no media queued, and did not begin with the small padding packet that gives the estimator a clean start window;
- `ProbeController` (`src/cc/probing.cc`): periodic ALR probing was on by default (libwebrtc: only when the stream requests it), the ALR state came from an instantaneous send-rate comparison, and there was no probe after a big drop, none on a rise of the max allocated rate, and no limit while the estimate is held back by delay increase;
- `ProbeBitrateEstimator` lacked the `kMaxValidRatio` check and the 1 s expiry and counted lost packets.

## Fix

Ported with libwebrtc's constants and defaults (reference: `modules/pacing/bitrate_prober.cc`, `modules/pacing/interval_budget.cc`, `modules/pacing/pacing_controller.cc` `ProcessPackets`, `goog_cc/alr_detector.cc`, `goog_cc/probe_controller.cc`, `goog_cc/probe_bitrate_estimator.cc`, `goog_cc/goog_cc_network_control.cc`):

- `BitrateProber` (`include/sm/cc/bitrate_prober.h`) inside `PacingController` (`pacer v2`): the pacer's send loop is `ProcessPackets` with the prober: `NextSendTime` is the probe time, a probe may run 1 ms early, the first packet of a cluster is a small padding packet, a burst stops at `RecommendedMinProbeSize` and the rest of it is padding, `ProbeSent` times the next burst, and a burst more than 10 ms late drops the cluster. The old `ProcessProbe` is removed.
- `AlrDetector` and `IntervalBudget` (`include/sm/cc/alr_detector.h`), fed by every sent packet; the ALR state marks `AckedBitrateEstimatorV2`, and its end calls `ExpectFastRateChange` (the item #15 carried over).
- `ProbeControllerV2` (`probing.h`): `SetBitrates`, `OnNetworkAvailability`, `OnMaxTotalAllocatedBitrate`, `SetEstimatedBitrate` (with the bandwidth-limited cause), `RequestProbe`, `Process`, `Reset`, with a 1 s result timeout and `EnablePeriodicAlrProbing` off by default.
- `ProbeBitrateEstimator(true)`: `HandleProbeAndEstimateBitrate` as it is.
- `GccController` with `version 2` wires them as `GoogCcNetworkController` does: `OnSentPacket` to the ALR detector, ALR start/end to the probe controller and the acked-rate estimator, probe results limited to `min(estimate, 0.85 x acked)` at the lowest, `recovered_from_overuse` requests a probe, `MaybeTriggerOnNetworkChanged` feeds the new target to the ALR detector and the probe controller, and the sum of the allocator's maximums reaches `OnMaxTotalAllocatedBitrate`.

`cc v1` and `pacer v1` are unchanged and stay the default (the suite is identical on seeds 1-3 outside the scenarios that use `cc v2` or `pacer v2`). Remaining differences are in `tools/catalog.py` (5.2, 5.5, 5.6) and `docs/design/libwebrtc-parity.md`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_48_prober_low_bandwidth.scn` (`cc v2`, `pacer v2`, 150 kbps) | `video.keyframe_requests <= 4` (9), `cc.queue_p95_ms < 300` (465), `repair.overhead_pct < 3` (6.4) on seed 1; fails on 20 of 20 seeds | passes on 20 of 20 seeds (3 keyframe requests, queue p95 146-196 ms, overhead 1.3-1.7 %) |
| `testbench/scenarios/probe_v2_clean_link.scn`, `probe_v2_cross_traffic.scn`, `probe_v2_capacity_drop.scn` | pass on `main` too (guards on libwebrtc's three named scenarios, with its numbers in the comments) | pass on seeds 1-20 |
| `tests/unit/cc/probe_v2_test.cc` | does not build on `main` (new classes). Ports of `bitrate_prober_unittest.cc`, `alr_detector_unittest.cc` (and `interval_budget`), `probe_controller_unittest.cc` (exponential probing, max-rate rise, timeout, `RequestProbe` in and after an ALR, allocated-rate cap) and `probe_bitrate_estimator_unittest.cc` (every case), plus the start-up test: cluster estimates are the probe rates for a first packet of 24, 70 and 1200 bytes, on a 3 Mbps link and on a saturated 1.2 Mbps link | pass |
| `tests/unit/cc/pacing_controller_test.cc` `ProbeClusterTagsAndPads` | changed: it expected a probe to start with nothing queued, which libwebrtc's prober does not do (`bitrate_prober_unittest.cc` `DoesntInitializeProbingForSmallPackets`); it now enqueues a media packet first | pass |

## Results

Mean of 10 seeds with `cc v2` and `pacer v2` added to libwebrtc's own scenarios (`main` / this change / libwebrtc M157 from `parity-data`):

| scenario | utilization % | video kbps | queue p95 ms | video delay p95 ms | freezes | keyframe requests |
|---|---|---|---|---|---|---|
| clean_link | 92.5 / 92.6 / 91.6 | 2128 / 2131 / 2142 | 102 / 101 / 77 | 142 / 140 / 125 | 0 / 0 / 0 | 1 / 1 / 0 |
| cross_traffic | 75.6 / 74.7 / 74.2 | 1781 / 1759 / 1779 | 149 / 121 / 264 | 182 / 152 / 491 | 0 / 0 / 3.9 | 1 / 1 / 0 |
| capacity_drop | 58.6 / 58.9 / 57.5 | 893 / 898 / 895 | 373 / 374 / 283 | 244 / 217 / 249 | 1.9 / 1.7 / 2.2 | 11.5 / 11.5 / 0 |
| random_loss_5pct | 75.6 / 76.5 / 77.5 | 1322 / 1337 / 1382 | 30 / 35 / 29.5 | 357 / 354 / 210 | 0.2 / 0.4 / 7.5 | 2.9 / 3.6 / 0.2 |
| burst_loss | 85.8 / 86.5 / 80.4 | 1537 / 1554 / 1461 | 140 / 162 / 47 | 276 / 330 / 213 | 4.8 / 4.9 / 6.2 | 1.7 / 2.2 / 0 |
| wifi_to_lte_handover | 81.0 / 82.4 / 104.7 | 995 / 1025 / 1299 | 111 / 133 / 162 | 303 / 281 / 2319 | 3.1 / 3.9 / 4.3 | 12.5 / 12.3 / 0 |
| outage_3s | 90.0 / 89.8 / 89.1 | 1103 / 1100 / 1142 | 89 / 94 / 39 | 146 / 158 / 105 | 2.8 / 2.8 / 1.0 | 5 / 5 / 1 |
| low_bandwidth | 95.4 / 97.5 / 98.7 | 47.8 / 50.8 / 73.4 | 464 / 172 / 484 | 1002 / 478 / 4718 | 0.8 / 0 / 17.8 | 11.7 / 3.0 / 0.7 |

Utilization, video rate and `cross_traffic` delay move toward libwebrtc in most rows. Rows that move away: `burst_loss` queue p95 (140 -> 162 ms, libwebrtc 47) and delay p95, `wifi_to_lte_handover` queue p95 and loss (8.9 -> 10.3 %, libwebrtc 6.0), `random_loss_5pct` freezes 0.2 -> 0.4 and keyframe requests. These are the loss-limited cases that `LossBasedBweV2` (#12) owns: the probe controller already stays quiet while the estimate is limited by delay, but the loss-based state and the RTT limit are not inputs yet. `low_bandwidth` is better than libwebrtc on queue, delay and freezes (libwebrtc 17.8 freezes): the encoder and allocator decide that gap (#104, #61), not probing.

Existing `cc v2` scenarios on `pacer v1` (10 seeds, `main` / this change / libwebrtc): `issue_15_aimd_v2_clean_link` utilization 90.9 / 91.2 / 91.6 %; `issue_15_aimd_v2_cross_traffic` 73.8 / 74.1 / 74.2 %; `issue_15_aimd_v2_random_loss` 73.4 / 76.4 / 77.5 % but freezes 0 / 0.5 / 7.5 (140 ms frozen, libwebrtc 1863 ms), keyframe requests 2.3 / 2.7 / 0.2. The `pacer v2` scenarios (`pacer_v2_capacity_drop`, `issue_5_keyframe_flush`, `issue_76_rtx_v2_keyframe_flush`): utilization 53.1 / 54.9 / 57.5 %, video rate 799 / 828 / 895 kbps, freezes 3.1 / 2.5 / 2.2, queue p95 358 / 324 / 283 ms, keyframe requests 6.9 / 5.8 / 0.

Suite against the baseline (seed of each scenario file): 30 metrics regressed and 40 improved, all in the six existing scenarios that use `cc v2` or `pacer v2` (plus three new ones); every other scenario is unchanged on seeds 1, 2 and 3. The regressions are seed-level movements of the same 10-seed means above: `issue_15_aimd_v2_random_loss` (the freezes, keyframe requests and delay above; its `video.freeze_count == 0` guard is relaxed to `<= 2`, see below), `issue_5_keyframe_flush` (seed 2: 9 keyframe requests, mean over 10 seeds improves 6.9 -> 5.8), `pacer_v2_capacity_drop` and `issue_76_rtx_v2_keyframe_flush` (`send_over_target_p95_pct`, `keyframe_requests`, `longest_freeze_ms` on one seed while the 10-seed means improve), `issue_15_aimd_v2_clean_link` and `issue_15_aimd_v2_cross_traffic` (`target_max_drop_pct`, a deeper cut then a faster recovery, the settle time improves). `testbench/baseline.json` is updated.

Guard rails (program.md 0.2): `issue_15_aimd_v2_random_loss` expected no freeze; libwebrtc freezes 7.5 times (1863 ms) on it, so the line is `<= 2` (observed 0-2 on seeds 1-20). The `ProbeClusterTagsAndPads` unit test changed as above. The PR is labelled `needs-human` for both.

## Remaining

- The loss-based state and the RTT limit as inputs of `GetBandwidthLimitedCause` (needs `LossBasedBweV2`, #12): until then probes are limited only by delay increase.
- `requests_alr_probing` is a `GccConfig::periodic_alr_probing` flag with no stream setting it (screen share is not modelled); `NetworkStateEstimate` does not exist in the default GoogCC.
- The wire size of the padding packet that starts a probe burst (`GeneratePadding(1)`) and the size the prober sees for an incoming packet (it includes the 28 bytes of UDP/IP) come from memory: `rtp_sender.cc` is not in `compare/webrtc/reference/` (#57).
- `cc v2` and `pacer v2` are not the default until #61; the gaps in `burst_loss`, `wifi_to_lte_handover` and `capacity_drop` keyframe requests belong to #12, #23's follow-ups and #61.
