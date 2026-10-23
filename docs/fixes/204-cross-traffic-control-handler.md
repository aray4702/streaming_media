---
issue: 204
title: Standing 300 ms queue and 6-9 s of video freezes when 2 Mbps of cross traffic joins a 3 Mbps link (all v2 switches on)
severity: sev:2
status: fixed
area: area:cc, stage:baseline
regression_tests: testbench/scenarios/issue_204_cross_traffic_join.scn, tests/unit/rtp/send_side_controller_test.cc
---

# #204: standing queue and freezes when cross traffic joins

## Issue

When 1.8-2 Mbps of non-responsive cross traffic joins a 3 Mbps link with a
300 ms queue, our queue stayed at its limit until the cross traffic left and
video froze for 6-9 s; libwebrtc drains the queue within a few seconds
(yardstick #294: freezes 8461/7709 ms against libwebrtc's 1639/3590).
https://github.com/aray4702/streaming_media/issues/204.

## Root cause

GoogCC agreed on both stacks (about 1350 kbps by 11.5 s), but our encoder
target didn't hold. libwebrtc fixes `encoder_target_rate_bps_` with its
protection share only in `RtpVideoSender::OnBitrateUpdated`
(`call/rtp_video_sender.cc:898`), which runs only when
`CongestionControlHandler::GetUpdate`
(`modules/congestion_controller/rtp/control_handler.cc`) passes a GoogCC
update on, so one allocation with a ~23 % share held for 1.25 s and the queue
drained. Ours re-read the live repair share on every captured frame
(`SendSideController::AllocatedRates` with `control_handler` off), so video
climbed back to about 1200 kbps and the queue never drained; with it full,
the delay gradient was flat and the estimate never came down. The share's
rates themselves are #224's part (`send_rate_stats`).

## Fix

`control_handler` (the #318 port) and `send_rate_stats` (#320) are on by
default in the library and the test bench; see
[224-send-rate-stats-control-handler-default.md](224-send-rate-stats-control-handler-default.md),
which records the guard-rail lines this flip moves.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_204_cross_traffic_join.scn` | `video.freeze_total_ms` 7547 (expect < 6000) | 1849 |
| `tests/unit/rtp/send_side_controller_test.cc` `SendSideController.RateStatisticsAndControlHandlerAreTheDefault` | both defaults false | passes |

## Results

`video.freeze_total_ms` over seeds 1-10, before -> after (libwebrtc from
`parity-data-pr/pr/294`, means only):

| scenario | before | after | libwebrtc |
|---|---|---|---|
| `yardstick_204_cross_1800` | 9493 8814 6922 5852 7148 8454 6356 5273 9536 9293 (mean 7714) | 2387 7517 1232 1764 4585 1998 1222 1111 4674 1869 (mean 2836) | 1639 |
| `yardstick_204_cross_2000` (= the regression scenario) | 7547 2203 8396 5545 7703 2071 7359 7773 7478 2116 (mean 5819) | 1849 1379 1382 5292 2540 1886 8100 1843 1808 846 (mean 2693) | 3590 |

`cc.rtt_p50_ms` moves less: 368 -> 284 ms (1800) and 370 -> 353 ms (2000),
against libwebrtc's 90 and 105. On most seeds our queue still fills to its
limit for part of the cross traffic; seeds 3, 4 and 7 of 1800 drain it as
libwebrtc does. The remaining RTT difference is in the PR's parity table.

Suite against the baseline: see the #224 record (the same change).
