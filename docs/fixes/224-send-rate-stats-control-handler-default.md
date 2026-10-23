---
issue: 224
title: Send side: video e2e delay p95 808 ms vs libwebrtc 420 ms on congestive links once late frames are rendered
severity: sev:2
status: fixed
area: area:cc, stage:baseline
regression_tests: testbench/scenarios/issue_224_congestive_e2e_flip.scn, tests/unit/rtp/send_side_controller_test.cc
---

# #224: video e2e delay on congestive links (the `send_rate_stats` + `control_handler` flip)

## Issue

On a 2.5 Mbps link with a 60 ms queue that drops to 800 kbps at 15 s
(`issue_61_googcc_glue_congestive`, `issue_12_lossbwe_congestive`), our video
end-to-end delay p95 was far above libwebrtc's: 697 ms against 420 ms over
seeds 1-10 on `main` fabadc4, with the lip-sync offset and freezes following
it. Split out of #216; https://github.com/aray4702/streaming_media/issues/224.

## Root cause

The encoder produced 25-35 KB more video in the second after the drop than
libwebrtc's (1229-1400 against 1022-1137 kbps on seeds 1, 2, 4 and 8), which
filled the pacer queue (866 ms at 17 s against 66 ms). Two send-side
differences, ported behind switches in #320 and #318 and judged together here:

- **The protection share's rates.** `FecControllerDefault::UpdateFecRates`
  takes `(nack + fec) / total` off the encoder rate. libwebrtc measures those
  rates with one `RateStatistics` per media type
  (`RtpSenderEgress::GetSendRates`, `modules/rtp_rtcp/source/rtp_sender_egress.cc:340`,
  `rtc_base/rate_statistics.cc`), whose window restarts when RTX sends again
  after 1.5 s without any, so RTX that starts at the drop reaches the 0.5 cap
  within about 100 ms. Ours averaged bytes over a fixed second
  (`SendSideController::ComputeMeasuredRates`, `send_rate_stats` off).
- **When the share is read.** libwebrtc fixes the encoder target and its
  protection share only in `RtpVideoSender::OnBitrateUpdated`, when
  `CongestionControlHandler::GetUpdate`
  (`modules/congestion_controller/rtp/control_handler.cc`) passes a GoogCC
  update on. Ours re-read the live share on every captured frame
  (`control_handler` off).

## Fix

`rtp::SendSideConfig::send_rate_stats` and `control_handler` default to on
(`include/sm/rtp/send_side_controller.h`), and so do the test bench's
`send_rate_stats` and `control_handler` (`testbench/scenario.h`), as the
Owner approved on #224 (2026-10-22, 9:58 PM PT) once #318, #320 and #217's
RTX fix (#315) were on `main`. Both stay selectable with `off`.
`testbench/scenario.cc` now writes `control_handler` when it differs from the
default. No library code changed; the ports are #318 and #320.

Guard-rail lines moved by the Owner's rule (just above our seed-1 value, never
above libwebrtc's worst seed), each with libwebrtc's values in a comment:
`issue_12_lossbwe_policer` `video.freeze_total_ms < 14000` (ours 13372;
libwebrtc 14859 on its seed 1, worst 15478) and `issue_12_lossbwe_random_loss`
`video.freeze_total_ms < 460` (ours 447; libwebrtc worst 1028). One more,
`issue_120_rtx_payload_padding` `net.rtx_kbps >= 360` (ours 365), can't be
shown against libwebrtc's worst seed (only its seed 1, 402, is published) and
waits for the Owner's sign-off (needs-human).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_224_congestive_e2e_flip.scn` | `video.e2e_delay_p95_ms` 1052 (expect < 650), `sync.av_offset_p95_ms` 939 (expect < 520) | 574, 466 |
| `tests/unit/rtp/send_side_controller_test.cc` `SendSideController.RateStatisticsAndControlHandlerAreTheDefault` | both defaults false | passes |

## Results

`issue_61_googcc_glue_congestive` (same link as the regression scenario),
seeds 1-10, before -> after (libwebrtc from `parity-data`):

| seed | e2e p95 before | e2e p95 after | libwebrtc | av offset p95 before | after | libwebrtc |
|---|---|---|---|---|---|---|
| 1 | 1052 | 574 | 231 | 939 | 466 | 118 |
| 2 | 754 | 610 | 491 | 651 | 502 | 388 |
| 3 | 670 | 430 | 390 | 570 | 323 | 264 |
| 4 | 723 | 526 | 487 | 612 | 424 | 382 |
| 5 | 574 | 413 | 285 | 473 | 312 | 172 |
| 6 | 633 | 530 | 426 | 532 | 429 | 326 |
| 7 | 455 | 394 | 478 | 355 | 293 | 358 |
| 8 | 739 | 448 | 557 | 627 | 336 | 445 |
| 9 | 607 | 467 | 414 | 497 | 364 | 306 |
| 10 | 586 | 450 | 442 | 473 | 336 | 318 |
| mean | 679 | 484 | 420 | 573 | 379 | 307 |

Freezes on the same scenario: 4.4 -> 3.8 (total 1355 -> 1289 ms; libwebrtc
3.5, 1301 ms).

The regression scenario passes on seeds 1-5. The `issue_133_*_outage_8s`
settle time the Owner asked about (2.2 -> 5.0 s before #315) no longer
regresses on this `main`: `cc.max_settle_ms` over seeds 1-10 is 2940 -> 2880 ms
(libwebrtc 3170), and `video.pause_total_ms` 9540 -> 9529 (libwebrtc 9766).

Suite against `testbench/baseline.json` (`tools/testbench_compare.py`): 229
metrics worse and 272 better, all from the two switches; the baseline is
updated. The judgment is the parity report on the PR head (more gaps closed
than opened), which is in the PR.
