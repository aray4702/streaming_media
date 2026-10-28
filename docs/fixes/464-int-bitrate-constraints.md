---
issue: 464
title: Sender crashes (bad_alloc) or hangs building padding when the start rate is 1 Tbps or more
severity: sev:3
status: fixed
area: area:cc
regression_tests: testbench/scenarios/issue_464_start_rate_above_int_bps.scn, tests/unit/cc/goog_cc_network_control_test.cc
---

# #464: Sender crashes (bad_alloc) or hangs building padding when the start rate is 1 Tbps or more

## Issue

With a start rate of about 1 Tbps or more, the sender tried to build that much padding in one pacer
step and aborted with `std::bad_alloc`, or ran for minutes for a 10 s call. Between about 20 Gbps
and 1 Tbps the call completed but never settled on the link: 9-14 video freezes in 10 s on a
2.5 Mbps link. Found by the Fixer while working on #451, confirmed by the Coordinator on `main`
b998c6b: https://github.com/aray4702/streaming_media/issues/464. It also made #463 fail on arm64,
macOS and wasm: there `std::llround` saturates the 3x start probe of `issue_451_extreme_start_rate`
(9 Ebps) to INT64_MAX instead of returning INT64_MIN as on x86, so the probe ran at the 9 Ebps max
and hit this `bad_alloc`.

## Root cause

`GccController` (`src/cc/send_side_bwe.cc:50`) took the host's min, start and max `DataRate`
unchanged. `ProbeControllerV2::SetBitrates` then asked for probes at 3x and 6x the start rate,
capped only by the max rate, and `PacingController::ProcessPackets` asks the padding generator for
`rate x min_probe_delta` bytes per burst (`BitrateProber::RecommendedMinProbeSize`), which
`RtpSender::GeneratePadding` (`src/rtp/rtp_sender.cc:217`) fills with packets of at most 255 bytes:
billions of packets at a terabit rate. libwebrtc never sees such a rate: its `BitrateConstraints`
carry `int` bps, and `ConvertConstraints` (`call/rtp_transport_controller_send.cc:68-83`) builds
GoogCC's `TargetRateConstraints` from those ints, so the largest finite rate a host can configure is
INT_MAX bps (2,147,483,647 bps, about 2.1 Gbps). Its padding loop and prober are the same as ours
and are not bounded otherwise.

## Fix

`cc::ConvertConstraints(GccConfig)` saturates a finite min, start or max rate at INT_MAX bps, the
range of libwebrtc's `BitrateConstraints`; an infinite max stays infinite (libwebrtc's "no max").
`GccController`, `rtp::SendSideController` (its initial targets now come from the controller) and
`rtp::Session` (whose receive-side feedback interval reads the start rate before sending starts)
apply it to the configuration they are given. Rates in range are unchanged, so no scenario of the
suite moves. The deviation inventory (5.7) and `tools/catalog.py` (5.4) record it.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_464_start_rate_above_int_bps.scn` (20 Gbps start, 9 Ebps max, 2.5 Mbps link) | `cc.unsettled` 1, `cc.max_settle_ms` 10000, `video.freeze_count` 13, `video.longest_freeze_ms` 1499 | 0, 1300, 1, 422 |
| `tests/unit/cc/goog_cc_network_control_test.cc` `GccController.RatesAboveIntBpsSaturateAsLibwebrtcConstraints` | does not compile (no `ConvertConstraints`); before, a 1 Tbps start gave probes at 3 and 6 Tbps | min, start, max and both start probes are INT_MAX bps; rates in range and an infinite max are kept |

The scenario uses a 20 Gbps start so that the base commit fails in under a second; at 1 Tbps and
above the base commit runs out of memory (3-17 s with a 4 GB address-space cap), and with the fix
every start rate above INT_MAX bps gives the same results as 20 Gbps (checked at 9 Ebps, seeds 1-5).

## Results

The reproduction scenario across seeds (before -> after):

| seed | `cc.unsettled` | `cc.max_settle_ms` | `video.freeze_count` | `video.longest_freeze_ms` |
|---|---|---|---|---|
| 1 | 1 -> 0 | 10000 -> 1300 | 13 -> 1 | 1499 -> 422 |
| 2 | 1 -> 0 | 10000 -> 1300 | 9 -> 1 | 1845 -> 416 |
| 3 | 1 -> 0 | 10000 -> 1300 | 14 -> 1 | 1022 -> 819 |
| 4 | 1 -> 0 | 10000 -> 1400 | 13 -> 1 | 1806 -> 390 |
| 5 | 1 -> 0 | 10000 -> 1400 | 11 -> 1 | 1798 -> 428 |

The issue's repro (30 Tbps start, 9 Ebps max, 10 s): `std::bad_alloc` after 3 s under a 4 GB cap
before; completes in 0.1 s after, with the same metrics as the 20 Gbps start.

The scenario suite against the baseline (`tools/testbench_compare.py`): 0 regressions,
0 improvements; the only change to `testbench/baseline.json` is the new scenario.

The one freeze that remains is the start at 2.1 Gbps into a 2.5 Mbps link with a 400 ms queue;
`main` shows the same freeze with a 2 Gbps start, which was always configurable.
