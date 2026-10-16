---
issue: 53
title: Send-side queue limit: stop encoding while the data in flight is too large (congestion-window pushback)
severity: roadmap
status: partial
area: area:cc, stage:baseline
regression_tests: testbench/scenarios/issue_53_cwnd_pushback.scn, tests/unit/cc/congestion_window_pushback_test.cc, tests/e2e/cwnd_pushback_e2e_test.cc, tests/unit/cc/allocation_adaptation_test.cc
---

# #53: send-side queue limit (congestion-window pushback)

## Issue

After a capacity drop the encoder keeps producing frames at the old rate while
the estimate catches up. The frames overflow the bottleneck, the receiver loses
video, asks for keyframes, and the video freezes. Issue:
https://github.com/aray4702/streaming_media/issues/53 (filed by the Owner, split
out of #5 and #37). Measured on commit 83b4420 (`main`).

## Root cause

Two things, both on the encoder side of the loop:

1. The encoder only saw the GCC target. Nothing looked at the data already in
   flight, so frames kept coming at the old rate for the 0.3-0.5 s the delay-based
   estimate needs to fall. libwebrtc closes this with
   `CongestionWindowPushbackController` (goog_cc) and the encoder's
   `cwnd_frame_drop_interval_` (`video_stream_encoder.cc`), which this library did not have.
2. `cc::FrameDropper` (`src/cc/frame_dropper.cc`): keyframes bypass `ShouldDrop()` but
   their bytes enter the bucket, which had no cap. After a run of keyframe
   requests (31-47 per call on the shallow-buffer scenario) the bucket stayed over its
   limit and the dropper dropped every delta frame for seconds, which looked
   like a pacer problem in the measurements (the Owner's note on #53). Another
   contributor: `cc::SendHistory` kept the packets of a lost transport feedback
   "in flight" for 60 s, which would have held the congestion window shut
   (`lossy_feedback`, `outage_3s` and `wifi_to_lte_handover` failed their
   expectations until this was fixed).

## Fix

- `cc::CongestionWindowPushback` (`include/sm/cc/congestion_window_pushback.h`): a port of
  `congestion_window_pushback_controller.cc` and of the data-window arithmetic in
  `goog_cc_network_control.cc` (`UpdateCongestionWindowSize`): window = target x (smallest of
  the last 32 per-feedback maximum RTTs + 350 ms), averaged with the previous window, at least
  3000 bytes; the ratio moves by 0.9 / 0.95 / 1.05 per update; minimum pushback rate 30 kbps.
  `FrameDropInterval()` is `VideoStreamEncoder::UpdateTargetBitrate` (at most every second frame).
- It runs in `rtp::SendSideController` where GoogCc runs it: after feedback, on the 25 ms process
  interval and for every sent packet. It uses the drop-frame-only mode: the target and the pacing
  rate stay, `SendRates::frame_drop_interval` and a lowered `video_target` go to the encoder, and
  `SendSideController::DropCapturedFrame()` says which captured frame to skip. The test bench encoder
  calls it before the frame dropper, as `VideoStreamEncoder::OnFrame` does.
- `GccController::last_feedback_max_rtt()` feeds the RTT window.
- `SendHistory`: a packet below the highest reported sequence number that was never reported was in a
  lost feedback (feedbacks cover contiguous ranges); it leaves the in-flight count.
- `FrameDropper`: the bucket and the pending keyframe bytes are capped at 1.5 s of target
  (`max_hold`; libwebrtc's `FrameDropper::CapAccumulator` caps at 3 s, `kAccumulatorCapBufferSizeSecs`).
  PR #55 has the same cap for #21; it can drop its copy.
- Test bench: new metric `cc.send_over_target_ms` (time the 100 ms send rate exceeds 1.5 x the target
  after the first 2 s).
- Default: pushback is on (`SendSideConfig::cwnd.enabled`). libwebrtc turns it on through the
  `WebRTC-CongestionWindow` field trial; `rtc_base/experiments/rate_control_settings.cc`, which has the
  default, is not in `compare/webrtc/reference`, so I could not confirm whether M157 has it on without
  the trial. The Owner can ask for it off by default.
- Tried and not kept: draining the pacer queue by its average queue time (libwebrtc's
  `MaybeUpdateMediaRateDueToLongQueue`) instead of the oldest packet's. On `capacity_drop` it made
  freezes and delay worse (total freeze 4120 -> 4430 ms, p95 delay 253 -> 301 ms over 20 seeds) because the
  retransmission backlog (below) then lingers. Counting the pacer queue in the window
  (`add_pacing`, an option in libwebrtc) changes nothing: the ratio is already at its floor. A window
  of 500 ms instead of 350 ms loses most of the gain on the shallow buffer (residual loss 0.4 -> 2.3 %).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_53_cwnd_pushback.scn` (seed 1) | `video.residual_loss_pct <= 1.0`: 2.52 %; `video.render_fps >= 22`: 20.96 | 0.26 %, 25.0 fps; passes on seeds 1-20 (residual loss 0-0.79 %, fps 23.4-26.2) |
| `tests/unit/cc/congestion_window_pushback_test.cc` (`CongestionWindowPushback.*`, 11 cases; the first three are libwebrtc's unit tests) | does not build (no such class) | passes |
| `tests/e2e/cwnd_pushback_e2e_test.cc` `E2E_CongestionWindowPushback.EncoderBacksOffWhenTheBottleneckFills` (sessions over `sim::NetworkLink`, 2.5 Mbps to 500 kbps) | does not build | in flight peaks at 38 kB vs 95 kB without pushback; 813 vs 1166 bottleneck drops; 472 vs 865 kbps sent after the drop |
| `tests/unit/cc/allocation_adaptation_test.cc` `FrameDropper.KeyframeRunDoesNotStallDeltaFramesForSeconds` | 144 of 300 frames dropped (expect < 60); 44 with the cap | passes |

## Results

Issue scenario `issue_53_cwnd_pushback` (2.5 Mbps to 500 kbps at 8 s, 100 ms buffer), seeds 1-20:

| metric | main mean (min-max) | this PR mean (min-max) | seeds better |
|---|---|---|---|
| `video.residual_loss_pct` | 2.683 (1.422-4.76) | 0.3637 (0-0.7922) | 20/20 |
| `net.loss_pct` | 14.05 (9.849-19.87) | 8.812 (6.684-12.39) | 20/20 |
| `video.freeze_total_ms` | 7501 (5272-8819) | 4460 (2899-5413) | 20/20 |
| `video.longest_freeze_ms` | 2933 (1650-3466) | 1446 (636-2766) | 20/20 |
| `video.e2e_delay_p95_ms` | 360.1 (201-580.1) | 449.4 (294-589.1) | 2/20 |
| `video.render_fps` | 20.41 (18.4-23.44) | 24.55 (23.44-26.2) | 20/20 |
| `video.keyframe_requests` | 39.25 (30-52) | 17.3 (8-23) | 20/20 |
| `cc.send_over_target_ms` | 4570 (3400-5400) | 2995 (2200-3400) | 20/20 |

Delay p95 is worse on this scenario: the video that used to be lost is now recovered, but late.
Total freeze and longest freeze improve on all 20 seeds.

`capacity_drop` (the issue's scenario, 400 ms buffer), seeds 1-20:

| metric | main mean (min-max) | this PR mean (min-max) | seeds better |
|---|---|---|---|
| `video.residual_loss_pct` | 0.7971 (0.3894-1.151) | 0.1194 (0-0.3784) | 20/20 |
| `net.loss_pct` | 6.304 (3.237-10.38) | 3.766 (1.976-6.097) | 20/20 |
| `video.freeze_total_ms` | 4897 (3515-8127) | 3709 (2456-4696) | 19/20 |
| `video.longest_freeze_ms` | 1997 (1065-3248) | 1392 (739-2682) | 14/20 |
| `video.e2e_delay_p95_ms` | 392.6 (147-716) | 252.6 (178-329) | 16/20 |
| `video.render_fps` | 25.9 (23.93-26.68) | 26.88 (26.27-27.88) | 20/20 |
| `video.keyframe_requests` | 15.45 (9-34) | 7.6 (3-13) | 20/20 |
| `cc.send_over_target_ms` | 3530 (3000-4600) | 2515 (1800-3100) | 20/20 |

Same suite, mean of seeds 1-5 (only metrics that moved by more than 5 %; main -> this PR):

| scenario | metric | main (mean of 5 seeds) | this PR |
|---|---|---|---|
| capacity_drop | `video.freeze_total_ms` | 5608 | 3565 | (better)
| capacity_drop | `video.longest_freeze_ms` | 1897 | 1362 | (better)
| capacity_drop | `video.e2e_delay_p95_ms` | 411.6 | 260 | (better)
| capacity_drop | `video.render_fps` | 25.5 | 26.97 | (better)
| capacity_drop | `net.loss_pct` | 7.618 | 3.832 | (better)
| high_jitter | `video.freeze_total_ms` | 401.2 | 360.6 | (better)
| issue_53_cwnd_pushback | `video.freeze_total_ms` | 7488 | 4147 | (better)
| issue_53_cwnd_pushback | `video.longest_freeze_ms` | 2663 | 1213 | (better)
| issue_53_cwnd_pushback | `video.e2e_delay_p95_ms` | 337.2 | 466.9 | (worse)
| issue_53_cwnd_pushback | `video.render_fps` | 20.42 | 24.9 | (better)
| issue_53_cwnd_pushback | `net.loss_pct` | 13.62 | 8.593 | (better)
| low_bandwidth | `video.freeze_total_ms` | 2.087e+04 | 1.471e+04 | (better)
| low_bandwidth | `video.longest_freeze_ms` | 3135 | 2459 | (better)
| low_bandwidth | `video.e2e_delay_p95_ms` | 1860 | 1413 | (better)
| low_bandwidth | `video.render_fps` | 4.955 | 9.056 | (better)
| low_bandwidth | `net.loss_pct` | 26.89 | 18.06 | (better)
| low_bandwidth | `audio.mouth_to_ear_p95_ms` | 890.7 | 950.3 | (worse)
| outage_3s | `video.freeze_total_ms` | 8343 | 4894 | (better)
| outage_3s | `video.render_fps` | 21.84 | 25.21 | (better)
| outage_3s | `net.loss_pct` | 15.64 | 6.081 | (better)
| outage_3s | `audio.mouth_to_ear_p95_ms` | 613 | 290.2 | (better)
| wifi_to_lte_handover | `video.freeze_total_ms` | 8796 | 6403 | (better)
| wifi_to_lte_handover | `video.longest_freeze_ms` | 4318 | 2116 | (better)
| wifi_to_lte_handover | `video.e2e_delay_p95_ms` | 179 | 257.8 | (worse)
| wifi_to_lte_handover | `video.render_fps` | 19.66 | 25.14 | (better)
| wifi_to_lte_handover | `net.loss_pct` | 38.13 | 18.71 | (better)

Seed-1 comparison against the old baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| audio_only_lossy | cc.send_over_target_ms | - | 0 | added |
| bufferbloat | cc.send_over_target_ms | - | 0 | added |
| burst_loss | cc.send_over_target_ms | - | 500 | added |
| capacity_drop | cc.queue_over_200ms_pct | 8.875 | 7.3 | improved |
| capacity_drop | cc.send_over_target_ms | - | 2400 | added |
| capacity_drop | repair.overhead_pct | 13.77 | 9.973 | improved |
| capacity_drop | video.e2e_delay_p50_ms | 153.5 | 212 | REGRESSED |
| capacity_drop | video.e2e_delay_p95_ms | 225 | 315 | REGRESSED |
| capacity_drop | video.encoder_dropped | 101 | 112 | REGRESSED |
| capacity_drop | video.freeze_count | 7 | 5 | improved |
| capacity_drop | video.freeze_total_ms | 4787 | 3951 | improved |
| capacity_drop | video.keyframe_requests | 19 | 6 | improved |
| capacity_drop | video.longest_freeze_ms | 1774 | 1177 | improved |
| capacity_drop | video.undecodable_frames | 31 | 4 | improved |
| clean_link | cc.send_over_target_ms | - | 0 | added |
| cross_traffic | cc.send_over_target_ms | - | 0 | added |
| high_jitter | cc.send_over_target_ms | - | 1.89e+04 | added |
| issue_18_random_loss_keyframes | cc.send_over_target_ms | - | 0 | added |
| issue_53_cwnd_pushback | (new scenario) | - | - | new |
| lossy_feedback | cc.send_over_target_ms | - | 0 | added |
| low_bandwidth | audio.concealed_pct | 3.329 | 4.881 | REGRESSED |
| low_bandwidth | audio.jb_delay_p95_ms | 352 | 416 | REGRESSED |
| low_bandwidth | audio.mouth_to_ear_p95_ms | 890.4 | 954.9 | REGRESSED |
| low_bandwidth | audio.stretch_pct | 6.783 | 8.786 | REGRESSED |
| low_bandwidth | cc.queue_over_200ms_pct | 99.97 | 91.95 | improved |
| low_bandwidth | cc.send_over_target_ms | - | 3.61e+04 | added |
| low_bandwidth | repair.overhead_pct | 85.75 | 71.03 | improved |
| low_bandwidth | video.e2e_delay_p50_ms | 1552 | 965.5 | improved |
| low_bandwidth | video.e2e_delay_p95_ms | 1896 | 1378 | improved |
| low_bandwidth | video.frames_rendered | 210 | 226 | improved |
| low_bandwidth | video.freeze_count | 16 | 19 | REGRESSED |
| low_bandwidth | video.keyframe_requests | 85 | 66 | improved |
| low_bandwidth | video.longest_freeze_ms | 2561 | 3522 | REGRESSED |
| low_bandwidth | video.render_ratio_pct | 25.18 | 27.16 | improved |
| low_bandwidth | video.residual_loss_pct | 7.186 | 4.756 | improved |
| low_bandwidth | video.undecodable_frames | 465 | 388 | improved |
| outage_3s | audio.jb_delay_p95_ms | 288 | 240 | improved |
| outage_3s | audio.mouth_to_ear_p95_ms | 612.9 | 295.9 | improved |
| outage_3s | audio.net_delay_p95_ms | 324.9 | 55.9 | improved |
| outage_3s | cc.goodput_kbps | 809.4 | 857.5 | improved |
| outage_3s | cc.queue_over_200ms_pct | 7.133 | 1.267 | improved |
| outage_3s | cc.queue_p95_ms | 294.6 | 24.18 | improved |
| outage_3s | cc.send_over_target_ms | - | 700 | added |
| outage_3s | cc.target_avg_kbps | 829 | 930.5 | improved |
| outage_3s | cc.utilization_pct | 59.84 | 63.4 | improved |
| outage_3s | repair.overhead_pct | 26.94 | 7.733 | improved |
| outage_3s | video.avg_kbps | 682.1 | 716.8 | improved |
| outage_3s | video.e2e_delay_p50_ms | 80 | 164 | REGRESSED |
| outage_3s | video.e2e_delay_p95_ms | 178 | 220 | REGRESSED |
| outage_3s | video.encoder_dropped | 190 | 103 | improved |
| outage_3s | video.frames_rendered | 637 | 757 | improved |
| outage_3s | video.freeze_total_ms | 8861 | 4992 | improved |
| outage_3s | video.keyframe_requests | 11 | 7 | improved |
| outage_3s | video.render_fps | 21.23 | 25.23 | improved |
| outage_3s | video.render_ratio_pct | 89.72 | 94.98 | improved |
| outage_3s | video.residual_loss_pct | 7.831 | 3.476 | improved |
| outage_3s | video.undecodable_frames | 25 | 13 | improved |
| random_loss_5pct | cc.send_over_target_ms | - | 0 | added |
| satellite | cc.goodput_kbps | 571.4 | 530.7 | REGRESSED |
| satellite | cc.send_over_target_ms | - | 400 | added |
| satellite | cc.utilization_pct | 38.09 | 35.38 | REGRESSED |
| satellite | video.avg_kbps | 366.8 | 334 | REGRESSED |
| satellite | video.e2e_delay_p50_ms | 612 | 658 | REGRESSED |
| satellite | video.e2e_delay_p95_ms | 658 | 700 | REGRESSED |
| satellite | video.encoder_dropped | 4 | 28 | REGRESSED |
| satellite | video.freeze_count | 13 | 16 | REGRESSED |
| satellite | video.freeze_total_ms | 5190 | 5705 | REGRESSED |
| wifi_to_lte_handover | audio.jb_delay_p95_ms | 176 | 208 | REGRESSED |
| wifi_to_lte_handover | audio.mouth_to_ear_p95_ms | 435.6 | 466 | REGRESSED |
| wifi_to_lte_handover | cc.send_over_target_ms | - | 4200 | added |
| wifi_to_lte_handover | repair.overhead_pct | 99.71 | 58.44 | improved |
| wifi_to_lte_handover | video.e2e_delay_p50_ms | 63 | 178 | REGRESSED |
| wifi_to_lte_handover | video.e2e_delay_p95_ms | 158 | 247 | REGRESSED |
| wifi_to_lte_handover | video.encoder_dropped | 437 | 177 | improved |
| wifi_to_lte_handover | video.frames_rendered | 692 | 978 | improved |
| wifi_to_lte_handover | video.freeze_count | 2 | 4 | REGRESSED |
| wifi_to_lte_handover | video.freeze_total_ms | 5956 | 7295 | REGRESSED |
| wifi_to_lte_handover | video.keyframe_requests | 30 | 33 | REGRESSED |
| wifi_to_lte_handover | video.longest_freeze_ms | 4339 | 2214 | improved |
| wifi_to_lte_handover | video.render_fps | 17.3 | 24.45 | improved |
| wifi_to_lte_handover | video.render_ratio_pct | 90.69 | 95.6 | improved |
| wifi_to_lte_handover | video.residual_loss_pct | 7.323 | 2.212 | improved |


Seed-1 regressions, explained (the 5-seed means above show the direction; seed 1 is noisy):

- `capacity_drop` delay p50/p95 (225 -> 315 ms): seed 1 only; mean over 20 seeds 393 -> 253 ms, better on 16/20.
- `wifi_to_lte_handover` freezes and delay: seed 1 is 5956 -> 7295 ms; mean of 5 seeds 8796 -> 6403 ms, longest
  freeze 4318 -> 2116 ms, loss 38 % -> 19 %. Delay p95 is worse (179 -> 258 ms) for the same reason as above.
- `outage_3s` delay p50/p95: higher on seed 1 (the video that survives the outage arrives late); freeze total
  8343 -> 4894 ms, mouth-to-ear p95 613 -> 290 ms, loss 16 % -> 6 % (5 seeds).
- `low_bandwidth` (150 kbps link): audio concealment and mouth-to-ear +60 ms on seed 1, freeze count 16 -> 19;
  mean of 5 seeds: total freeze 20.9 -> 14.7 s, render fps 5.0 -> 9.1, loss 27 % -> 18 %, mouth-to-ear
  891 -> 950 ms. The audio delay grows because the sender now keeps more audio+video in flight on that link.
- `satellite` (600 ms RTT): pushback fires twice (probe at 1.4 s, 34.5 s) and drops 18 frames; 20-seed means:
  total freeze 4244 -> 4485 ms (+6 %), utilization 54.2 -> 51.5 %. This is the 350 ms window on a long
  RTT link with FEC and retransmission overhead, as in libwebrtc; libwebrtc freezes 16 s here (compare-webrtc.md).

## Remaining

The issue's first acceptance criterion is not met: the send rate after the drop is still above 1.5 x the target
for about 2.5-3 s on `capacity_drop` (`cc.send_over_target_ms` 3530 -> 2515 ms over 20 seeds), and the freeze totals
are still above libwebrtc's (3.7 s vs 2.4 s, delay p95 253 vs 149 ms; `docs/results/compare-webrtc.md`).

Cause: after the encoder backs off (the ratio reaches its floor within 0.5 s) the pacer queue still holds a
backlog of RTX packets, 150-215 kB at 11 s on seed 1, nothing else: the NACKs for the packets the bottleneck dropped
before the target fell. The pacer drains it at up to 2 Mbps (`drain_boosts`) into a 500 kbps link and the queue
overflows again. Holding retransmissions to the target is issue #21 / PR #55 (on hold until this merges); with
this PR merged its measurements are no longer confused by the dropper stall. The pacer's drain rule
(oldest packet vs libwebrtc's average queue time) should be revisited together with it.

Still open for the other criteria: unit tests exist for the decision and an e2e over `sim::NetworkLink`;
no baseline regressions beyond the explained seed noise above.
