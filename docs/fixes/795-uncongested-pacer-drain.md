---
issue: 795
title: Audio 0.6-2.1 s late at 20% random loss and during a 45 s feedback outage (libwebrtc M157 too)
severity: sev:2
status: fixed
area: area:cc, area:sync
regression_tests: testbench/scenarios/issue_795_loss20_mouth_to_ear.scn, testbench/scenarios/issue_795_reverse_outage_mouth_to_ear.scn, tests/unit/cc/pacing_controller_test.cc
---

# #795: Audio 0.6-2.1 s late at 20% random loss and during a 45 s feedback outage

## Issue

At 20% random loss on a clean 2 Mbps link, and during a 45 s feedback-only outage on a healthy 1 Mbps link,
audio reached the listener 0.6-2.1 s late (mouth-to-ear p95). libwebrtc M157 does the same. Split from the
Tester's #536; the Owner decided on 2026-11-03 to go beyond libwebrtc here:
https://github.com/aray4702/streaming_media/issues/795.

## Root cause

The delay is not in the network. Audio arrives in 64-93 ms (`audio.net_delay_p95_ms`), then waits in
NetEq because A/V sync (`src/sync/stream_sync.cc`, `StreamSynchronization::ComputeDelays`) raises its
minimum delay to match video, and video waits about 2 s in the pacer. In both scenarios the media the
encoders commit (the video at its 50 kbps minimum plus audio, or video plus 20% retransmissions and
FEC) is more than the pacing rate (`GccController::PacingRate`, target x 1.1). So the video queue grows
until libwebrtc's long-queue drain holds it at `queue_time_limit` (2 s, `kMaxExpectedQueueLength`,
`PacingController::MaybeUpdateMediaRateDueToLongQueue`). The target there was cut by the loss-based
estimate or by the RTT backoff of the feedback outage. The path itself had no queue (`cc.queue_p95_ms`
2 ms; the delay detector stayed normal).

Capping how much A/V sync may delay audio would leave audio and video 2 s apart (`sync.av_offset_p95_ms`
worse than libwebrtc's), so the fix goes after the cause, the pacer queue.

## Fix

Beyond libwebrtc M157, behind `PacingControllerConfig::uncongested_queue_time_limit` (150 ms, on;
zero is libwebrtc's behaviour) and `uncongested_max_boost` (1.25):

- `SendSideController::ApplyPacer` marks the path uncongested (`Pacer::SetPathUncongested`) when the
  delay detector isn't overusing, the delay-based limit is at least 1.5 x the target, the last feedback
  RTT is within 100 ms of the smallest seen, and all of this has held for 5 s. A capacity drop or a
  standing queue fails one of these checks, so the pacer keeps libwebrtc's 2 s limit there.
- On such a path the pacer drains its queue within 150 ms instead of 2 s, at no more than 1.25 x the
  pacing rate (event `pacer/fast_drain`, counter `fast_drains`). Video no longer waits seconds, and A/V
  sync (unchanged, as libwebrtc) no longer holds audio back.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_795_loss20_mouth_to_ear.scn` | m2e p95 985.3, video e2e 2521, pacer 2356 (expect < 400 / 1000 / 500) | 303, 596, 230 |
| `testbench/scenarios/issue_795_reverse_outage_mouth_to_ear.scn` | m2e p95 2049, video e2e 2288 (expect < 400 / 1000) | 293, 445 |
| `tests/unit/cc/pacing_controller_test.cc` `PacingController.UncongestedPathDrainsWithinTheShortLimit` | no such option | passes |

## Results

Seeds 1-10, main -> this fix (libwebrtc M157 mean from #792):

| scenario | metric | main | fix | libwebrtc mean |
|---|---|---|---|---|
| loss20 | audio.mouth_to_ear_p95_ms | 985 274 1063 175 264 1051 325 147 1473 914 (mean 667) | 303 254 272 195 243 192 205 147 333 254 (mean 240) | 728 |
| loss20 | video.e2e_delay_p95_ms | mean 1567 | mean 512 | 1776 |
| loss20 | video.delay_pacer_p95_ms | mean 1315 | mean 171 | 1529 |
| loss20 | sync.av_offset_p95_ms | mean 1031 | mean 355 | 1173 |
| loss20 | video.freeze_count / total_ms | 25.8 / 7653 | 23.9 / 7174 | 27.4 / 8103 |
| reverse outage | audio.mouth_to_ear_p95_ms | 2041-2105 (mean 2070) | 293 354 296 367 296 312 331 325 358 298 (mean 323) | 2070 |
| reverse outage | video.e2e_delay_p95_ms | mean 2307 | mean 348 | 2307 |
| reverse outage | sync.av_offset_p95_ms | mean 1593 | mean 148 | ~1590 |
| reverse outage | video.longest_freeze_ms | mean 98 | mean 248 | 98 |
| reverse outage | audio.concealed_pct | mean 0.06 | mean 0.15 | 0.06 |
| policer (issue_12) | audio.mouth_to_ear_p95_ms | mean 1005 | mean 343 | |
| policer (issue_12) | video.freeze_count / total_ms | 5.6 / 9344 | 7.4 / 6587 | |

Suite against the baseline (seed 1): 92 improved, 17 regressed:

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_12_lossbwe_policer | cc.send_over_target_ms | 6200 | 7100 | REGRESSED |
| issue_12_lossbwe_policer | video.delay_jitter_buffer_p95_ms | 585.1 | 702.9 | REGRESSED |
| issue_12_lossbwe_policer | video.delay_network_p95_ms | 37.74 | 131.4 | REGRESSED |
| issue_12_lossbwe_policer | video.encoder_dropped | 125 | 152 | REGRESSED |
| issue_12_lossbwe_policer | video.freeze_count | 4 | 7 | REGRESSED |
| issue_12_lossbwe_policer | video.pause_total_ms | 1.201e+04 | 1.481e+04 | REGRESSED |
| issue_154_shallow_drop_repair_v2 | video.encoder_target_avg_kbps | 456.8 | 491.2 | REGRESSED |
| issue_21_shallow_drop_repair | video.encoder_target_avg_kbps | 456.8 | 491.2 | REGRESSED |
| issue_536_loss20_av_sync | video.freeze_total_ms | 7284 | 7661 | REGRESSED |
| issue_536_reverse_outage_45s | audio.late_discards | 36 | 69 | REGRESSED |
| issue_536_reverse_outage_45s | cc.send_over_target_ms | 400 | 700 | REGRESSED |
| issue_536_reverse_outage_45s | cc.send_over_target_p95_pct | 39.68 | 42.06 | REGRESSED |
| issue_536_reverse_outage_45s | sync.av_offset_p50_ms | -74.83 | -36.08 | REGRESSED |
| issue_536_reverse_outage_45s | video.freeze_total_ms | 0 | 250 | REGRESSED |
| issue_536_reverse_outage_45s | video.longest_freeze_ms | 16.98 | 250 | REGRESSED |
| issue_737_outage_15s_100ms | cc.send_over_target_p95_pct | 8.349 | 9.616 | REGRESSED |
| issue_737_outage_15s_100ms | video.e2e_delay_p50_ms | 151.5 | 176 | REGRESSED |

Why the regressions are accepted (for the Owner and the window to judge):
- `issue_536_reverse_outage_45s`: when feedback returns at 65 s, GoogCC's target jumps to the delay-based
  estimate (1.46 Mbps on a 1 Mbps link, as on main and in libwebrtc) and the link queue spikes to ~270 ms.
  Before, the 2 s of audio and video buffering hid it; now it shows as one ~250 ms freeze and 0.1% more
  concealment. Two expect lines move (`needs-human`). `send_over_target` is the drain itself.
- `issue_12_lossbwe_policer` seed 1: one trajectory. On 10 seeds m2e falls from 1005 to 343 ms and the freeze
  total from 9344 to 6587 ms; the freeze count rises from 5.6 to 7.4.
- `issue_536_loss20_av_sync` seed 1 freeze total: on 10 seeds it falls (7653 -> 7174 ms).
- `issue_21/154_shallow_drop_repair` encoder target average is higher, not a user-facing loss.
  `issue_737_outage_15s_100ms` e2e p50 151 -> 176 ms on seed 1; its 10-seed p95 is unchanged (553 -> 546).
