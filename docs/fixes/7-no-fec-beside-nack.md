---
issue: 7
title: Adaptive FEC plus RTX spends 50-83% extra bandwidth at 5% loss (no video FEC beside NACK for H.264, as libwebrtc)
severity: sev:3
status: partial
area: area:loss
regression_tests: testbench/scenarios/issue_7_no_fec_beside_nack.scn, tests/unit/rtp/session_test.cc
---

# #7: Adaptive FEC plus RTX spends 50-83% extra bandwidth at 5% loss

## Issue

At 5% random loss and about 100 ms RTT, adaptive FEC plus RTX spent 50-83% extra bandwidth, so about half of the target went to repair. Found by the Tester; re-scoped by the Owner (2026-10-16) to duplicate libwebrtc's `ShouldDisableRedAndUlpfec`. https://github.com/aray4702/streaming_media/issues/7

## Root cause

The session ran the protection controller and the FEC encoder whenever `fec` was on, next to NACK. libwebrtc (`compare/webrtc/reference/call/rtp_video_sender.cc`, `ShouldDisableRedAndUlpfec`, lines 112-160) turns video RED and ULPFEC off when NACK is on and the payload cannot skip FEC packets (`PayloadTypeSupportsSkippingFecPackets`: VP8, VP9, and Generic only with `WebRTC-GenericPictureId`), because FEC packets would be retransmitted too. For H.264 with NACK (the bench, and the `compare/webrtc` harness's Generic) libwebrtc sends no video FEC. `Session::AddVideoSend` and `AddVideoReceive` had no such rule.

## Fix

- `rtp::ShouldDisableRedAndUlpfec` (`include/sm/rtp/session.h`, `src/rtp/session.cc`) ports the rule with the same four conditions: the disabling experiment, FlexFEC priority, NACK with a codec that cannot skip FEC packets, and only one of RED/ULPFEC configured. `PayloadTypeSupportsSkippingFecPackets` ports the codec check.
- `VideoStreamConfig` gets a `codec` (default H.264); `rtp::VideoFecEnabled(config)` applies the rule. The session uses it for the sender, the receiver and the demultiplexer payload types; the test bench uses it to decide whether the protection controller runs.
- `tools/catalog.py` 3.7 and the inventory row in `docs/design/libwebrtc-parity.md` (3.2) record it.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_7_no_fec_beside_nack.scn` | seed 1: `repair.overhead_pct` 42.75 (expect < 15), `repair.fec_recovered` 163 (expect == 0); seeds 1-5 overhead 42.8 / 36.7 / 57.0 / 58.0 / 57.4 | seeds 1-5 overhead 8.0 / 8.4 / 9.6 / 7.6 / 7.5, no FEC |
| `tests/unit/rtp/session_test.cc` `Session.VideoFec*` (5 cases, the applicable cases of `rtp_video_sender_unittest.cc`) | do not compile (no decision function) | pass |

## Results

Mean of 10 seeds, main / this PR / libwebrtc (`parity-data` branch, M157):

| scenario | metric | main | this PR | libwebrtc |
|---|---|---|---|---|
| burst_loss | repair.overhead_pct | 41.8 | 5.4 | 4.7 |
| burst_loss | video.avg_kbps | 784.8 | 996.6 | 1460.7 |
| burst_loss | video.freeze_total_ms | 157.7 | 257.6 | 1432.5 |
| burst_loss | video.residual_loss_pct | 0.0 | 0.0 | 0.0 |
| burst_loss | cc.utilization_pct | 62.1 | 57.7 | 80.4 |
| burst_loss | video.e2e_delay_p95_ms | 199.1 | 199.7 | 213.0 |
| burst_loss | audio.concealed_pct | 2.5 | 2.6 | 1.5 |
| low_bandwidth | repair.overhead_pct | 70.5 | 7.8 | 16.9 |
| low_bandwidth | video.avg_kbps | 45.0 | 50.6 | 73.4 |
| low_bandwidth | video.freeze_total_ms | 21449.3 | 1111.4 | 12936.1 |
| low_bandwidth | video.residual_loss_pct | 5.3 | 0.3 | 0.3 |
| low_bandwidth | cc.utilization_pct | 98.7 | 96.7 | 98.7 |
| low_bandwidth | video.e2e_delay_p95_ms | 1619.0 | 844.8 | 4717.7 |
| low_bandwidth | audio.concealed_pct | 4.0 | 1.6 | 1.6 |
| random_loss_5pct | repair.overhead_pct | 47.0 | 8.4 | 8.6 |
| random_loss_5pct | video.avg_kbps | 677.7 | 950.3 | 1382.4 |
| random_loss_5pct | video.freeze_total_ms | 66.5 | 98.7 | 1862.8 |
| random_loss_5pct | video.residual_loss_pct | 0.0 | 0.0 | 0.0 |
| random_loss_5pct | cc.utilization_pct | 55.3 | 55.8 | 77.5 |
| random_loss_5pct | video.e2e_delay_p95_ms | 197.0 | 311.1 | 209.5 |
| random_loss_5pct | audio.concealed_pct | 1.6 | 1.5 | 0.4 |
| satellite | repair.overhead_pct | 38.2 | 14.0 | 18.0 |
| satellite | video.avg_kbps | 414.5 | 549.6 | 964.4 |
| satellite | video.freeze_total_ms | 5346.0 | 7095.9 | 17933.3 |
| satellite | video.residual_loss_pct | 0.0 | 0.0 | 0.0 |
| satellite | cc.utilization_pct | 47.6 | 49.7 | 82.4 |
| satellite | video.e2e_delay_p95_ms | 653.6 | 796.5 | 932.0 |
| satellite | audio.concealed_pct | 0.7 | 0.8 | 0.2 |
| wifi_to_lte_handover | repair.overhead_pct | 69.4 | 61.7 | 33.4 |
| wifi_to_lte_handover | video.avg_kbps | 772.5 | 810.2 | 1298.8 |
| wifi_to_lte_handover | video.freeze_total_ms | 6144.3 | 5665.0 | 4528.4 |
| wifi_to_lte_handover | video.residual_loss_pct | 1.5 | 1.4 | 0.0 |
| wifi_to_lte_handover | cc.utilization_pct | 66.7 | 66.5 | 104.7 |
| wifi_to_lte_handover | video.e2e_delay_p95_ms | 226.7 | 224.5 | 2319.3 |
| wifi_to_lte_handover | audio.concealed_pct | 4.7 | 4.7 | 4.2 |

The suite against the baseline (`tools/testbench_compare.py --markdown`, seed 1):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| burst_loss | cc.send_over_target_ms | 100 | 200 | REGRESSED |
| burst_loss | cc.target_max_drop_pct | 45.07 | 41.24 | improved |
| burst_loss | repair.overhead_pct | 29.15 | 2.927 | improved |
| burst_loss | video.avg_kbps | 992.6 | 1225 | improved |
| capacity_drop | audio.jb_delay_p95_ms | 288 | 352 | REGRESSED |
| capacity_drop | audio.mouth_to_ear_p95_ms | 700.9 | 764.8 | REGRESSED |
| capacity_drop | repair.overhead_pct | 10 | 8.697 | improved |
| capacity_drop | video.delay_jitter_buffer_p95_ms | 213.6 | 172.6 | improved |
| capacity_drop | video.e2e_delay_p50_ms | 171 | 150 | improved |
| capacity_drop | video.e2e_delay_p95_ms | 252 | 215 | improved |
| capacity_drop | video.encoder_dropped | 122 | 112 | improved |
| capacity_drop | video.longest_freeze_ms | 1762 | 2132 | REGRESSED |
| issue_18_lntf_not_pli | audio.stretch_pct | 2.972 | 1.302 | improved |
| issue_18_lntf_not_pli | cc.goodput_kbps | 1267 | 1096 | REGRESSED |
| issue_18_lntf_not_pli | cc.queue_max_ms | 27.25 | 80.02 | REGRESSED |
| issue_18_lntf_not_pli | cc.send_over_target_ms | 0 | 300 | REGRESSED |
| issue_18_lntf_not_pli | cc.send_over_target_p95_pct | 21.99 | 23.21 | REGRESSED |
| issue_18_lntf_not_pli | cc.target_avg_kbps | 1255 | 1090 | REGRESSED |
| issue_18_lntf_not_pli | cc.target_max_drop_pct | 30.16 | 36.29 | REGRESSED |
| issue_18_lntf_not_pli | cc.utilization_pct | 63.35 | 54.81 | REGRESSED |
| issue_18_lntf_not_pli | repair.overhead_pct | 42.75 | 7.97 | improved |
| issue_18_lntf_not_pli | video.avg_kbps | 810.9 | 935.8 | improved |
| issue_18_lntf_not_pli | video.delay_jitter_buffer_p95_ms | 135.1 | 297 | REGRESSED |
| issue_18_lntf_not_pli | video.delay_network_p95_ms | 138.7 | 166.2 | REGRESSED |
| issue_18_lntf_not_pli | video.e2e_delay_p50_ms | 140 | 331 | REGRESSED |
| issue_18_lntf_not_pli | video.e2e_delay_p95_ms | 198.6 | 354 | REGRESSED |
| issue_18_lntf_not_pli | video.encoder_dropped | 0 | 7 | REGRESSED |
| issue_18_lntf_not_pli | video.keyframe_requests | 1 | 3 | REGRESSED |
| issue_18_lntf_not_pli | video.longest_freeze_ms | 33 | 5 | improved |
| issue_21_shallow_drop_repair | cc.send_over_target_ms | 2700 | 3400 | REGRESSED |
| issue_21_shallow_drop_repair | cc.send_over_target_p95_pct | 61.53 | 91.89 | REGRESSED |
| issue_21_shallow_drop_repair | repair.overhead_pct | 44.28 | 23.01 | improved |
| issue_21_shallow_drop_repair | video.avg_kbps | 412.3 | 485 | improved |
| issue_21_shallow_drop_repair | video.delay_jitter_buffer_p95_ms | 653 | 889.7 | REGRESSED |
| issue_21_shallow_drop_repair | video.delay_network_p95_ms | 207.9 | 236.5 | REGRESSED |
| issue_21_shallow_drop_repair | video.e2e_delay_p50_ms | 619 | 832 | REGRESSED |
| issue_21_shallow_drop_repair | video.e2e_delay_p95_ms | 694 | 937 | REGRESSED |
| issue_21_shallow_drop_repair | video.encoder_dropped | 125 | 161 | REGRESSED |
| issue_21_shallow_drop_repair | video.freeze_total_ms | 4094 | 4400 | REGRESSED |
| issue_21_shallow_drop_repair | video.keyframe_requests | 19 | 26 | REGRESSED |
| issue_21_shallow_drop_repair | video.undecodable_frames | 7 | 11 | REGRESSED |
| issue_53_cwnd_pushback | audio.stretch_pct | 3.806 | 2.804 | improved |
| issue_53_cwnd_pushback | cc.send_over_target_ms | 2900 | 2400 | improved |
| issue_53_cwnd_pushback | repair.overhead_pct | 25.33 | 18.45 | improved |
| issue_53_cwnd_pushback | video.avg_kbps | 559.3 | 603.5 | improved |
| issue_53_cwnd_pushback | video.delay_jitter_buffer_p95_ms | 431.3 | 285.1 | improved |
| issue_53_cwnd_pushback | video.e2e_delay_p50_ms | 366 | 254 | improved |
| issue_53_cwnd_pushback | video.e2e_delay_p95_ms | 475 | 329 | improved |
| issue_53_cwnd_pushback | video.encoder_dropped | 99 | 93 | improved |
| issue_53_cwnd_pushback | video.freeze_total_ms | 3509 | 3830 | REGRESSED |
| issue_53_cwnd_pushback | video.longest_freeze_ms | 1510 | 2469 | REGRESSED |
| issue_53_cwnd_pushback | video.undecodable_frames | 5 | 8 | REGRESSED |
| issue_7_no_fec_beside_nack | (new scenario) | - | - | new |
| low_bandwidth | audio.concealed_pct | 4.481 | 1.302 | improved |
| low_bandwidth | audio.net_delay_p95_ms | 538.8 | 503.4 | improved |
| low_bandwidth | cc.queue_over_200ms_pct | 92.42 | 13.93 | improved |
| low_bandwidth | cc.queue_p95_ms | 497.6 | 462.3 | improved |
| low_bandwidth | cc.send_over_target_ms | 3.62e+04 | 4300 | improved |
| low_bandwidth | cc.send_over_target_p95_pct | 932.6 | 117.7 | improved |
| low_bandwidth | cc.target_avg_kbps | 46.65 | 126.5 | improved |
| low_bandwidth | cc.target_max_drop_pct | 84.07 | 78.78 | improved |
| low_bandwidth | repair.overhead_pct | 74.57 | 10.09 | improved |
| low_bandwidth | video.delay_jitter_buffer_p95_ms | 942.5 | 767.7 | improved |
| low_bandwidth | video.delay_network_p95_ms | 1146 | 244 | improved |
| low_bandwidth | video.delay_pacer_p95_ms | 577.8 | 0 | improved |
| low_bandwidth | video.e2e_delay_p50_ms | 1357 | 455 | improved |
| low_bandwidth | video.e2e_delay_p95_ms | 1783 | 839 | improved |
| low_bandwidth | video.encoder_dropped | 387 | 168 | improved |
| low_bandwidth | video.frames_rendered | 202 | 1009 | improved |
| low_bandwidth | video.freeze_count | 20 | 1 | improved |
| low_bandwidth | video.freeze_total_ms | 2.139e+04 | 1580 | improved |
| low_bandwidth | video.keyframe_requests | 69 | 14 | improved |
| low_bandwidth | video.longest_freeze_ms | 3613 | 1580 | improved |
| low_bandwidth | video.render_fps | 5.05 | 25.23 | improved |
| low_bandwidth | video.render_ratio_pct | 24.85 | 97.77 | improved |
| low_bandwidth | video.residual_loss_pct | 5.031 | 0.3953 | improved |
| low_bandwidth | video.undecodable_frames | 422 | 9 | improved |
| outage_3s | cc.send_over_target_p95_pct | 18.67 | 14.57 | improved |
| outage_3s | repair.overhead_pct | 8.505 | 2.746 | improved |
| outage_3s | video.avg_kbps | 736.6 | 788.3 | improved |
| outage_3s | video.encoder_dropped | 90 | 84 | improved |
| random_loss_5pct | audio.stretch_pct | 2.972 | 1.302 | improved |
| random_loss_5pct | cc.goodput_kbps | 1267 | 1096 | REGRESSED |
| random_loss_5pct | cc.queue_max_ms | 27.25 | 80.02 | REGRESSED |
| random_loss_5pct | cc.send_over_target_ms | 0 | 300 | REGRESSED |
| random_loss_5pct | cc.send_over_target_p95_pct | 21.99 | 23.21 | REGRESSED |
| random_loss_5pct | cc.target_avg_kbps | 1255 | 1090 | REGRESSED |
| random_loss_5pct | cc.target_max_drop_pct | 30.16 | 36.29 | REGRESSED |
| random_loss_5pct | cc.utilization_pct | 63.35 | 54.81 | REGRESSED |
| random_loss_5pct | repair.overhead_pct | 42.75 | 7.97 | improved |
| random_loss_5pct | video.avg_kbps | 810.9 | 935.8 | improved |
| random_loss_5pct | video.delay_jitter_buffer_p95_ms | 135.1 | 297 | REGRESSED |
| random_loss_5pct | video.delay_network_p95_ms | 138.7 | 166.2 | REGRESSED |
| random_loss_5pct | video.e2e_delay_p50_ms | 140 | 331 | REGRESSED |
| random_loss_5pct | video.e2e_delay_p95_ms | 198.6 | 354 | REGRESSED |
| random_loss_5pct | video.encoder_dropped | 0 | 7 | REGRESSED |
| random_loss_5pct | video.keyframe_requests | 1 | 3 | REGRESSED |
| random_loss_5pct | video.longest_freeze_ms | 33 | 5 | improved |
| satellite | audio.stretch_pct | 1.638 | 3.099 | REGRESSED |
| satellite | cc.goodput_kbps | 622.4 | 511.8 | REGRESSED |
| satellite | cc.send_over_target_ms | 1100 | 0 | improved |
| satellite | cc.send_over_target_p95_pct | 44.05 | 27.92 | improved |
| satellite | cc.target_avg_kbps | 601.9 | 498.8 | REGRESSED |
| satellite | cc.target_max_drop_pct | 65.92 | 59.48 | improved |
| satellite | cc.utilization_pct | 41.49 | 34.12 | REGRESSED |
| satellite | repair.overhead_pct | 45.46 | 12.71 | improved |
| satellite | video.avg_kbps | 332.9 | 358.7 | improved |
| satellite | video.delay_jitter_buffer_p95_ms | 361.8 | 453.9 | REGRESSED |
| satellite | video.e2e_delay_p50_ms | 632 | 723 | REGRESSED |
| satellite | video.e2e_delay_p95_ms | 673.1 | 770.4 | REGRESSED |
| satellite | video.encoder_dropped | 51 | 22 | improved |
| satellite | video.freeze_count | 13 | 26 | REGRESSED |
| satellite | video.freeze_total_ms | 5756 | 7865 | REGRESSED |
| wifi_to_lte_handover | cc.send_over_target_ms | 3100 | 2800 | improved |
| wifi_to_lte_handover | cc.send_over_target_p95_pct | 265.5 | 182.5 | improved |
| wifi_to_lte_handover | repair.overhead_pct | 65.25 | 59.65 | improved |
| wifi_to_lte_handover | video.avg_kbps | 843.7 | 895 | improved |
| wifi_to_lte_handover | video.delay_jitter_buffer_p95_ms | 108 | 137.5 | REGRESSED |
| wifi_to_lte_handover | video.e2e_delay_p95_ms | 181 | 209 | REGRESSED |
| wifi_to_lte_handover | video.encoder_dropped | 167 | 130 | improved |
| wifi_to_lte_handover | video.freeze_total_ms | 6993 | 5796 | improved |
| wifi_to_lte_handover | video.keyframe_requests | 16 | 19 | REGRESSED |

Overhead now matches libwebrtc on `burst_loss`, `random_loss_5pct` and `satellite`, and is below it on `low_bandwidth`. Video bitrate rises 20-40% and freezes stay below libwebrtc's on the loss scenarios. Metrics that regressed against the baseline are those that FEC had masked: with no FEC, recovery waits for NACK, so `random_loss_5pct` video delay p95 rises 197 -> 311 ms (libwebrtc 210 ms), `satellite` freeze total 5.3 -> 7.1 s (libwebrtc 17.9 s), and the congestion controller reads the retransmission bursts (utilization stays far under libwebrtc's 77-82%, which is the GoogCC work in Phase 3). The `issue_18`, `issue_21` and `issue_53` regressions are the same cause on scenarios libwebrtc was not run on. No threshold or seed was changed; the baseline is updated.

## Remaining

- The FEC amount where FEC does run (NACK off, VP8/VP9) is still our binomial controller, not `FecControllerDefault`/`media_opt_util`; no stage-1 scenario exercises it.
- `wifi_to_lte_handover` repair overhead is 61.7% against libwebrtc's 33.4%: not FEC, it is RTX and keyframes (#23, #70).
- The ULPFEC wire format is #29.
