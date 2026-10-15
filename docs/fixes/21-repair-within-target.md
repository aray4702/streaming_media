---
issue: 21
title: Repair traffic (RTX, FEC, keyframes) is not held to the target bitrate
severity: sev:2
status: partial
area: area:loss, area:cc
regression_tests: testbench/scenarios/issue_21_repair_within_target.scn, testbench/scenarios/issue_21_capacity_drop_repair.scn, tests/unit/loss/nack_rtx_test.cc, tests/unit/rtp/session_test.cc
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

Repair traffic ignored the target in two places that this PR fixes:

1. `loss::RtxSender::OnNack` (`src/loss/rtx.cc`) answered every NACK. Nothing
   bounded the resend rate, so resends took whatever the pacer allowed.
2. The FEC protection controller asked for up to 100% FEC regardless of the
   budget. On `low_bandwidth` the audio alone (85 kbps on the wire) is above
   the 30-45 kbps target, yet 51 kbps of FEC was sent, queued 1.4 s in the
   pacer behind video (useless on arrival) and still counted as loss on the
   full link. The loss then fed the loss-based estimator, which cut the target
   further.

A third place, `rtp::SendSideController::ComputeRates`, takes only the
*configured* FEC protection off the video budget and never RTX. A first version
of this PR replaced it with the measured RTX + FEC share; that is reverted (see
Remaining).

## Fix

- `RtxSender::SetMaxRate`: resends are limited to a rate over a 1 s window.
  `rtp::Session` sets it to `SendSideConfig::max_retransmission_factor` (3.0)
  times the current target whenever the target changes. Metric `rate_limited`.
  The factor was measured over seeds 1-20 (below): 1x and 2x cut recovery after
  a capacity drop (`issue_53_cwnd_pushback` `video.residual_loss_pct` mean 0.4 ->
  1.4 at 1x, 0.7 at 2x, failing its own `<= 1.0` on 17 and 3 of 20 seeds
  respectively); 3x holds main's 0.3 and still ends the flood
  (`low_bandwidth` `net.loss_pct` 21.8 -> 1.5). libwebrtc's pinned M157 sources
  contain no retransmission `RateLimiter` (the pacer bounds resends there), so
  there is no file to cite for the factor; the cap itself follows the Owner's
  direction on the issue.
- `Session::SetFecParams`: FEC protection is capped by what the target leaves
  above the video minimum: `avail / video_min - 1`, so no FEC when the audio
  takes the target.

The frame-dropper bucket cap that an earlier head of this PR carried is in
`main` through #56; this PR no longer touches `cc::FrameDropper`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_21_repair_within_target.scn` | `net.loss_pct` 26.1 (expect < 7), `repair.overhead_pct` 85.8 (< 16), `video.render_fps` 5.2 (> 18) | 20 of 20 seeds |
| `testbench/scenarios/issue_21_capacity_drop_repair.scn` | `net.loss_pct` 6.7 (expect < 5), `repair.overhead_pct` 13.8 (< 10) | 20 of 20 seeds |
| `tests/unit/loss/nack_rtx_test.cc` `Rtx.RateLimitCapsResendsPerSecond` | does not build (no limiter) | passes |
| `tests/unit/rtp/session_test.cc` `FecProtectionIsCappedByWhatTheTargetLeavesAboveTheVideoFloor`, `NoFecWhenTheAudioAloneTakesTheTarget` | does not build | passes |

`tests/e2e/cwnd_pushback_e2e_test.cc` (from #56) compares a run with pushback
against one without. The resend cap also lowers the data in flight of the run
without pushback (72.7 kB against 95.2 kB on `main`), which would make its
unchanged `with * 2 < without` bound fail (76.7 kB against 72.7 kB) although the
run with pushback is identical to `main` (38.3 kB). The test isolates pushback,
so it now sets `max_retransmission_factor = 1000` in both runs. No threshold is
changed.

## Results

Seeds 1-20, `main` (c88105d, with #56) against this PR. Mean (max) over the
seeds; "seeds worse" counts seeds where the PR value is worse than `main`'s.

| scenario | metric | main mean (max) | PR mean (max) | seeds worse | worst seed (main -> PR) |
|---|---|---|---|---|---|
| plain_shallow | longest freeze ms | 955.3 (1734) | 952.0 (1947) | 1/20 | seed 20: 1734 -> 1947 |
| plain_shallow | freeze total ms | 4921.4 (9345) | 4867.9 (9715) | 3/20 | seed 20: 9345 -> 9715 |
| plain_shallow | net loss % | 20.9 (36) | 20.2 (28) | 0/20 | seed 1: 10 -> 10 |
| plain_shallow | residual loss % | 0.1 (2) | 0.2 (2) | 4/20 | seed 20: 2 -> 2 |
| plain_shallow | send_over_target ms | 5890.0 (10500) | 5755.0 (10000) | 3/20 | seed 5: 6500 -> 6800 |
| plain_shallow | render fps | 24.7 (28) | 24.8 (28) | 3/20 | seed 4: 22 -> 22 |
| issue_21_shallow_buffer_no_stall | longest freeze ms | 1888.8 (3220) | 1987.7 (3299) | 9/20 | seed 16: 1360 -> 3299 |
| issue_21_shallow_buffer_no_stall | freeze total ms | 8214.4 (15099) | 8189.4 (15686) | 6/20 | seed 5: 7516 -> 15686 |
| issue_21_shallow_buffer_no_stall | net loss % | 28.3 (56) | 24.2 (41) | 3/20 | seed 16: 29 -> 41 |
| issue_21_shallow_buffer_no_stall | residual loss % | 2.4 (12) | 3.1 (13) | 12/20 | seed 16: 1 -> 11 |
| issue_21_shallow_buffer_no_stall | send_over_target ms | 7540.0 (14000) | 7285.0 (12200) | 4/20 | seed 5: 6800 -> 12200 |
| issue_21_shallow_buffer_no_stall | render fps | 21.2 (26) | 21.8 (26) | 6/20 | seed 8: 23 -> 12 |
| low_bandwidth | longest freeze ms | 3426.1 (7334) | 1551.2 (2622) | 1/20 | seed 3: 16 -> 20 |
| low_bandwidth | freeze total ms | 18721.3 (23224) | 1545.8 (2622) | 0/20 | seed 3: 0 -> 0 |
| low_bandwidth | net loss % | 21.8 (26) | 1.5 (2) | 0/20 | seed 3: 2 -> 1 |
| low_bandwidth | residual loss % | 5.3 (7) | 0.3 (1) | 0/20 | seed 3: 0 -> 0 |
| low_bandwidth | send_over_target ms | 34310.0 (38000) | 5280.0 (7200) | 0/20 | seed 3: 9000 -> 3700 |
| low_bandwidth | render fps | 6.0 (23) | 24.2 (27) | 0/20 | seed 3: 23 -> 27 |
| capacity_drop | longest freeze ms | 1392.4 (2682) | 1254.4 (2250) | 8/20 | seed 12: 1367 -> 2250 |
| capacity_drop | freeze total ms | 3708.6 (4696) | 3031.8 (4334) | 3/20 | seed 13: 4124 -> 4282 |
| capacity_drop | net loss % | 3.8 (6) | 2.5 (3) | 0/20 | seed 18: 2 -> 2 |
| capacity_drop | residual loss % | 0.1 (0) | 0.1 (0) | 5/20 | seed 1: 0 -> 0 |
| capacity_drop | send_over_target ms | 2515.0 (3100) | 2020.0 (2600) | 1/20 | seed 6: 2500 -> 2600 |
| capacity_drop | render fps | 26.9 (28) | 27.4 (28) | 0/20 | seed 2: 27 -> 27 |
| issue_53_cwnd_pushback | longest freeze ms | 1446.1 (2766) | 1559.7 (2744) | 8/20 | seed 11: 1818 -> 2744 |
| issue_53_cwnd_pushback | freeze total ms | 4459.8 (5413) | 3933.1 (4838) | 4/20 | seed 16: 3305 -> 4416 |
| issue_53_cwnd_pushback | net loss % | 8.8 (12) | 8.1 (10) | 4/20 | seed 16: 8 -> 8 |
| issue_53_cwnd_pushback | residual loss % | 0.4 (1) | 0.3 (1) | 10/20 | seed 9: 0 -> 1 |
| issue_53_cwnd_pushback | send_over_target ms | 2995.0 (3400) | 2635.0 (3100) | 2/20 | seed 7: 2500 -> 2700 |
| issue_53_cwnd_pushback | render fps | 24.5 (26) | 25.1 (26) | 5/20 | seed 5: 25 -> 24 |

`issue_21_shallow_buffer_no_stall` was dropped from this PR: it guarded the
frame-dropper stall that #56 fixed, it is not about repair traffic, and the
cap alone does not hold its `expect` lines on all 20 seeds (9 of 20 fail on
`net.loss_pct`, `encoder_dropped` or `render_fps`). The table keeps its numbers
for reference.

Where this PR is worse than `main` on a single seed (cause not found; these
are single-seed swings in runs that are chaotic after a capacity change):

- `capacity_drop` seed 12 longest freeze 1367 -> 2250 ms (8 of 20 seeds worse,
  mean 1392 -> 1254 ms);
- `issue_53_cwnd_pushback` seed 11 longest freeze 1818 -> 2744 ms (mean
  1446 -> 1560 ms, so the mean is also slightly worse; freeze total falls
  4460 -> 3933 ms) and `video.undecodable_frames` mean 7.8 -> 10.2;
- shallow buffer + 3x drop (`issue_21_shallow_buffer_no_stall`) seed 16 longest
  freeze 1360 -> 3299 ms (9 of 20 worse);
- `wifi_to_lte_handover` seed 1 longest freeze 2214 -> 3997 ms; over 20 seeds the
  mean is 2081 -> 2094 ms, freeze total falls 6370 -> 5350 ms and net loss
  19.0 -> 9.7%.

Suite against `main`'s baseline (`tools/testbench_compare.py`, seed 1): 44
metrics improved, 6 regressed; `testbench/baseline.json` is updated. The
regressions, all seed 1, with their 20-seed means:

- `capacity_drop` `video.freeze_count` 5 -> 7, `video.keyframe_requests` 6 -> 9,
  `video.undecodable_frames` 4 -> 7: 20-seed means 5.5 -> 5.0, 7.6 -> 5.7,
  5.8 -> 5.7.
- `issue_53_cwnd_pushback` `video.undecodable_frames` 3 -> 12: 20-seed mean
  7.8 -> 10.2. A small count that is worse; not explained.
- `wifi_to_lte_handover` `video.longest_freeze_ms` 2214 -> 3997 and
  `video.undecodable_frames` 3 -> 5: see above.

## Remaining

- **The video budget does not subtract what RTX actually sends.** The measured
  share (RTX + FEC bytes of the last second, capped at 50%, as in libwebrtc's
  `FecControllerDefault`) was in an earlier head of this PR and is reverted. With
  it `E2E_CongestionWindowPushback` failed: the run with pushback reached the
  full link rate before the drop (target 2407 kbps against 844 kbps without the
  measured share) and its data in flight at the drop (126 kB) was larger than the
  run without pushback (75 kB), so pushback was not compared on equal footing
  and the shallow-buffer scenario got worse on 9 of 20 seeds. Next step: decide
  how the measured share and pushback interact, probably together with #37.
- **Keyframe recovery is not held to the budget.** The #5 trigger (an RTX +
  keyframe burst into a 2 Mbps link, cutting the target by 53%) is unchanged. A
  keyframe is still released as one frame at the pacer rate. Next step: a keyframe
  budget in the send-side media controller (#37).
- **Shallow buffer + capacity drop** is not improved in freezes (table above):
  it is dominated by the start-up and capacity-drop bursts into a 20 ms queue
  (#53, #20).
