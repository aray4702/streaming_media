---
issue: 21
title: Repair traffic (RTX, FEC, keyframes) is not held to the target bitrate
severity: sev:2
status: partial
area: area:loss, area:cc
regression_tests: testbench/scenarios/issue_21_repair_within_target.scn, testbench/scenarios/issue_21_capacity_drop_repair.scn, tests/unit/loss/nack_rtx_test.cc, tests/unit/rtp/send_side_controller_test.cc, tests/unit/rtp/session_test.cc
---

# #21: Repair traffic (RTX, FEC, keyframes) is not held to the target bitrate

## Issue

On a 150 kbps link the congestion-control target collapsed to about 40 kbps
but the link stayed full and lost 27% of its packets, because FEC,
retransmissions and keyframes were not counted against the target: video
rendered at 5 fps and audio had an interruption. `capacity_drop` showed the
same pattern after the 5x drop. Found by the libwebrtc comparison (Tester) and
widened by the Owner (curation note at the top of the issue).
https://github.com/aray4702/streaming_media/issues/21

## Root cause

Three places let repair traffic ignore the target:

1. `loss::RtxSender::OnNack` (`src/loss/rtx.cc`) answered every NACK. Nothing
   bounded the resend rate, so resends took whatever the pacer allowed.
2. `rtp::SendSideController::ComputeRates` (`src/rtp/send_side_controller.cc`)
   took only the *configured* FEC protection off the video budget
   (`(target - audio) / (1 + protection)`). RTX was never subtracted, and the
   FEC share was the controller's wish, not what was sent.
3. The FEC protection controller asked for up to 100% FEC regardless of the
   budget. On `low_bandwidth` the audio alone (85 kbps on the wire) is above
   the 30-45 kbps target, yet 51 kbps of FEC was sent, queued 1.4 s in the
   pacer behind video (useless on arrival) and still counted as loss on the
   full link. The loss then fed the loss-based estimator, which cut the target
   further.

## Fix

- `RtxSender::SetMaxRate`: resends are limited to a rate over a 1 s window
  (libwebrtc's `RateLimiter` for retransmissions). `rtp::Session` sets it to the
  current target on every target change. Metric `rate_limited`.
- `SendSideController::ComputeMeasuredRates` (used by `Session::rates()`):
  the share taken off the video budget is what RTX and FEC took of the bytes
  released in the last second, capped at 50% (`FecControllerDefault`
  `overhead_threshold_`, `fec_controller_default.cc`). The configured FEC rate
  stays as a floor so a rising FEC rate is anticipated.
- `Session::SetFecParams`: FEC protection is capped by what the target leaves
  above the video minimum: `avail / video_min - 1`, so no FEC when the audio
  takes the target.

libwebrtc source checked in `compare/webrtc/reference/`: the protection
subtraction is `call/rtp_video_sender.cc` `OnBitrateUpdated` and
`modules/video_coding/fec_controller_default.cc` `UpdateFecRates`. The pinned
M157 sources contain no retransmission `RateLimiter` (it was removed from
`RtpSenderEgress`; the pacer bounds resends there), so the limiter follows the
Owner's direction in the issue rather than a file we can cite.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_21_repair_within_target.scn` | `net.loss_pct` 26.1 (expect < 7), `repair.overhead_pct` 85.8 (< 16), `video.render_fps` 5.2 (> 18) | 1.6, 6.1, 26.1 |
| `testbench/scenarios/issue_21_capacity_drop_repair.scn` | `net.loss_pct` 6.7 (expect < 5), `repair.overhead_pct` 13.8 (< 10) | 2.1, 4.8 |
| `tests/unit/loss/nack_rtx_test.cc` `Rtx.RateLimitCapsResendsPerSecond` | does not build (no limiter) | passes |
| `tests/unit/rtp/send_side_controller_test.cc` `VideoBudgetShrinksByTheMeasuredRepairShare`, `RepairNeverTakesMoreThanHalfOfTheBudget` | does not build | passes |
| `tests/unit/rtp/session_test.cc` `FecProtectionIsCappedByWhatTheTargetLeavesAboveTheVideoFloor`, `NoFecWhenTheAudioAloneTakesTheTarget` | does not build | passes |

## Results

Means over seeds 1-10 (`low_bandwidth`, `capacity_drop`) and 1-20 (shallow
buffer), main -> this change. libwebrtc from the issue table.

| scenario | metric | main | after | libwebrtc |
|---|---|---|---|---|
| low_bandwidth | net.loss_pct | 27.2 | 2.0 | 6.6 |
| low_bandwidth | repair.overhead_pct | 90.3 | 6.7 | 16 |
| low_bandwidth | video.render_fps | 4.9 | 25.7 | 21.7 |
| low_bandwidth | video.keyframe_requests | 83 (5 seeds) | 17 (5 seeds) | 0.3 |
| low_bandwidth | video.freeze_total_ms | 20658 | 1117 | |
| low_bandwidth | audio.interruptions | 1.0 | 1.0 | 0 |
| low_bandwidth | cc.target_avg_kbps | 43 (5 seeds) | 121 (5 seeds) | 136 |
| capacity_drop | net.loss_pct | 6.9 | 2.2 | 1.4 |
| capacity_drop | repair.overhead_pct | 14.2 | 4.5 | 2.9 |
| capacity_drop | video.freeze_total_ms | 5188 | 4325 | 2355 |
| capacity_drop | video.longest_freeze_ms | 2038 | 2160 | |
| capacity_drop | cc.queue_p95_ms | 388 | 290 | 271 |
| shallow buffer (1500 kbps, 20 ms queue) | send rate / target (5 seeds) | 1.27 | 1.07 | |
| shallow buffer | net.loss_pct (20 seeds) | 22.2 | 13.2 | |
| shallow buffer | repair.overhead_pct (20 seeds) | 83.7 | 40.0 | |
| shallow buffer | video.freeze_total_ms (20 seeds) | 6071 | 7588 | |
| shallow buffer | video.longest_freeze_ms (20 seeds) | 1564 | 2820 | |
| #5 reproduction (2% loss, burst=15, 2 Mbps; seeds 1-8) | largest target fall in 1 s (from the time series) | 53% | 51% | |
| #5 reproduction | video.longest_freeze_ms / freeze_total_ms | 366 / 993 | 303 / 758 | |

The scenario suite against the baseline (`tools/testbench_compare.py
--markdown`, seed 1): 62 metrics improved, 30 regressed; the baseline is
updated and the full table is in the PR description. Regressions and why they are
accepted (checked over seeds 1-10 against `main`):

- `burst_loss`, `satellite`, `high_jitter`, `wifi_to_lte_handover`:
  `video.avg_kbps` and `cc.goodput_kbps` fall 5-24% (burst_loss 887 -> 781
  kbps). The RTX and FEC share is now taken off the encoder's budget instead of
  being sent on top of it. `repair.overhead_pct` is a ratio to media bytes, so it
  rises slightly when media shrinks (burst_loss 37.8 -> 42.1) and falls where
  repair really fell (handover 113 -> 33). Freezes and render fps do not get
  worse: burst_loss freeze total 221 -> 174 ms, satellite 4886 -> 4036 ms,
  handover 9190 -> 5278 ms and 20.4 -> 25.7 fps.
- `wifi_to_lte_handover` `video.e2e_delay_p95_ms` 171 -> 284 ms: with 10% loss
  instead of 39% many more frames are played, including ones that waited for
  repair after the handover.
- `random_loss_5pct` `video.longest_freeze_ms` 21 -> 86 ms (freeze total 0 ->
  74 ms): repair at 5% loss now competes with the smaller video budget; the
  scenario's own bound is 2500 ms.
- `capacity_drop` seed 1 `video.longest_freeze_ms` 1774 -> 2492 ms; over 10 seeds
  2038 -> 2160 ms, within noise; freeze total falls 17%.

## Remaining

- **Keyframe recovery is not held to the budget.** The #5 trigger (an RTX +
  keyframe burst into a 2 Mbps link, cutting the target by 53%) is unchanged
  (51%). A keyframe is still released as one frame at the pacer rate. Next step:
  a keyframe budget in the send-side media controller (#37), which owns frame
  size and rate allocation.
- **Shallow buffer: freezes are worse than main** (total 6071 -> 7588 ms,
  longest 1564 -> 2820 ms over 20 seeds) although loss (22.2 -> 13.2%), repair
  overhead (83.7 -> 40.0%) and the send rate (1.27x -> 1.07x target) improved.
  When the target dips, the cap at the target blocks resends of packets sent
  earlier at a higher rate. A cap of 2x the target (measured, not
  adopted) gives 5132 ms / 2027 ms on the shallow buffer but loosens
  `low_bandwidth` (loss 2.0 -> 3.9%, overhead 6.7 -> 12.0%), so the choice between
  the libwebrtc-style cap and a looser one is left to the Owner. The start-up burst
  into a 20 ms queue (#53) is the other contributor.
