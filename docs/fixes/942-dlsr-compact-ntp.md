---
issue: 942
title: issue_466_overshoot seed 3 pauses video 6.4 s where libwebrtc doesn't (the frame dropper's choice at 4.2 s)
severity: sev:2
status: fixed
area: area:timing, area:cc
regression_tests: testbench/scenarios/issue_942_dlsr_compact_ntp.scn, tests/unit/timing/rtcp_stats_test.cc
---

# #942: issue_466_overshoot seed 3 pauses video 6.4 s where libwebrtc doesn't

## Issue

On `issue_466_overshoot_very_low_capacity` seed 3 (100 kbps link, encoder overshoot 1.3), video paused
for 6435 ms (17.2-23.6 s) on main 63b80da; libwebrtc doesn't pause on that seed. Split from #893 by
the window (Owner decision 5:12 PM PT 2026-10-31): https://github.com/aray4702/streaming_media/issues/942.
libwebrtc's traces for this seed come from yardstick #940 (`issue_893_overshoot_seed3`, run 41247876306,
`compare-webrtc` artifact `traces`). The first visible difference was the frame dropper's choice at
4.2 s: libwebrtc encoded the frame captured at 4200 ms, we encoded 4233.

## Root cause

The frame dropper itself and its per-frame inputs were identical to libwebrtc's (sizes, input frame
rate, BWE and loss-based estimates). Its decision at 4200 ms was marginal: the accumulator was 12.573
kbit against a 12.5 kbit max. An offline float32 replay of `FrameDropper` over our run's inputs
reproduces our decisions exactly, and reproduces libwebrtc's (through 6.6 s) if the video target of
24969 bps reaches the dropper after the 4066 ms frame instead of before it.

That target came from an allocation at **4.05 s** that libwebrtc doesn't make. It was GoogCC's
RTT-change trigger (`MaybeTriggerOnNetworkChanged`, `goog_cc_network_control.cc:614-619`, compares
`round_trip_time` in microseconds) on the process interval, after the RR at 4.031 s gave an RTT of
345.505 ms instead of the 345.490 ms of the reports around it.

The 15 us comes from our receive side's report block. `ReceiveStatistics::MakeReportBlock`
(`src/timing/rtcp_stats.cc:295` before the fix) wrote DLSR as the interval since the SR, truncated once:
`((now - last_sr_arrival).us() << 16) / 1000000`. libwebrtc's `RTCPSender::CreateReportBlocks`
(`modules/rtp_rtcp/source/rtcp_sender.cc:739-748`) takes the difference of two compact NTP times, each
truncated: `CompactNtp(clock.CurrentNtpTime()) - CompactNtp(feedback_state.last_rr)`. The two differ by
one 1/65536 s unit for about half the arrival phases, so our RTT samples flipped between two values a
unit apart where libwebrtc's didn't (or flipped at other reports), and each flip is an RTT change that
GoogCC passes on as an allocation (`CongestionControlHandler::GetUpdate` compares the RTT too).

## Fix

`ReceiveStatistics::MakeReportBlock` computes DLSR as libwebrtc does:
`NtpTime::FromTimestamp(now).Compact() - NtpTime::FromTimestamp(last_sr_arrival_).Compact()`.
`NtpTime::FromTimestamp` truncates the fraction as libwebrtc's clock does (`TimeMicrosToNtp`), so the
report's DLSR is now the same number libwebrtc's receiver writes for the same SR and report times.

This is a receive-side change that every scenario with video or audio RTCP sees: the sender's RTT
samples, and so GoogCC's RTT-triggered allocations, the NACK/RTX timing that reads the RTT, and the
protection share, move on many scenarios. They move toward libwebrtc overall (Results).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_942_dlsr_compact_ntp.scn` (seed 3) | `video.pause_total_ms` 6435 (expect < 1) | 0 |
| `tests/unit/timing/rtcp_stats_test.cc` `RtcpStats.DelaySinceLastSrIsTheDifferenceOfCompactNtpTimes` | DLSR 13107 (expect 13108), and 8090 vs 8091 at other phases | passes |

## Results

`issue_466_overshoot_very_low_capacity`, `video.pause_total_ms` per seed (libwebrtc from parity-data
`compare-webrtc.json`):

| seed | before | after | libwebrtc |
|---|---|---|---|
| 1 | 0 | 0 | 0 |
| 2 | 0 | 0 | 0 |
| 3 | **6435** | **0** | 0 |
| 4 | 0 | 0 | 0 |
| 5 | 5611 | 5611 | 5611 |
| 6 | 0 | 0 | 0 |
| 7 | 0 | 0 | 0 |
| 8 | 0 | 0 | 0 |
| 9 | 6412 | 6412 | 0 (the window's #941) |
| 10 | 0 | 0 | 0 |

Seed 3's forward-link trace now matches libwebrtc's (yardstick #940) packet for packet through row 1057
(14.59 s); before, it split at row 363 (5.47 s).

Against libwebrtc's seed-1 values (parity-data `per_seed`), over every scenario of the suite:
of the cells that changed, **154 move closer to libwebrtc and 28 farther** (3266 unchanged); the cells equal to libwebrtc's value go from **2545 to 2650**. The 28 farther are small or scattered: `cc.rtt_p50_ms` on the high-jitter family (99 -> 103 against 101, 10 scenarios), and some audio and sync cells on `issue_76_session_layer_v2`, `issue_150_gcc_v2_high_jitter_seed5` and `issue_127_pacer_v2_jitter_reorder_loss`, where the RTT samples now flip at libwebrtc's reports and the run takes another path. The PR's `compare-webrtc` parity report judges the 10-seed means.

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_127_all_v2_no_stall | cc.queue_max_ms | 113.1 | 56.13 | improved |
| issue_127_all_v2_no_stall | video.delay_network_p95_ms | 411.2 | 336.6 | improved |
| issue_127_all_v2_no_stall | video.freeze_total_ms | 2478 | 2057 | improved |
| issue_127_pacer_v2_jitter_reorder_loss | cc.goodput_kbps | 695.1 | 730.7 | improved |
| issue_127_pacer_v2_jitter_reorder_loss | cc.send_over_target_p95_pct | 22.61 | 21.29 | improved |
| issue_127_pacer_v2_jitter_reorder_loss | cc.target_avg_kbps | 984.1 | 1048 | improved |
| issue_127_pacer_v2_jitter_reorder_loss | cc.utilization_pct | 26.93 | 28.3 | improved |
| issue_127_pacer_v2_jitter_reorder_loss | repair.overhead_pct | 40.27 | 43.36 | REGRESSED |
| issue_127_pacer_v2_jitter_reorder_loss | video.avg_kbps | 604.1 | 639.2 | improved |
| issue_127_pacer_v2_jitter_reorder_loss | video.encoder_target_avg_kbps | 595.1 | 630.2 | REGRESSED |
| issue_127_pacer_v2_jitter_reorder_loss | video.longest_freeze_ms | 59.41 | 119.2 | REGRESSED |
| issue_12_lossbwe_high_jitter_no_loss | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_12_lossbwe_high_jitter_no_loss | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_12_lossbwe_high_jitter_no_loss | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_12_lossbwe_high_jitter_no_loss | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_150_gcc_v2_high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_150_gcc_v2_high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_150_gcc_v2_high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_150_gcc_v2_high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed5 | cc.goodput_kbps | 545.8 | 576.7 | improved |
| issue_150_gcc_v2_high_jitter_seed5 | cc.max_settle_ms | 3e+04 | 2.84e+04 | improved |
| issue_150_gcc_v2_high_jitter_seed5 | cc.target_avg_kbps | 752.1 | 814 | improved |
| issue_150_gcc_v2_high_jitter_seed5 | cc.utilization_pct | 21.14 | 22.34 | improved |
| issue_150_gcc_v2_high_jitter_seed5 | repair.overhead_pct | 38.45 | 40.75 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed5 | sync.av_offset_p95_ms | 36.66 | 81.08 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed5 | video.avg_kbps | 457.9 | 487.8 | improved |
| issue_150_gcc_v2_high_jitter_seed5 | video.encoder_target_avg_kbps | 455.9 | 483 | REGRESSED |
| issue_19_cc_v2_high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_19_cc_v2_high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_19_cc_v2_high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_19_cc_v2_high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_24_timing_v2_high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_24_timing_v2_high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_24_timing_v2_high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_24_timing_v2_high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_453_sps_pps_tracker_high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_453_sps_pps_tracker_high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_453_sps_pps_tracker_high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_453_sps_pps_tracker_high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_484_audio_report_blocks | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_484_audio_report_blocks | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_484_audio_report_blocks | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_484_audio_report_blocks | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_62_neteq_v2_high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_62_neteq_v2_high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_62_neteq_v2_high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_62_neteq_v2_high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_76_session_layer_v2 | audio.jb_delay_p95_ms | 80 | 110 | REGRESSED |
| issue_76_session_layer_v2 | audio.late_discards | 8 | 10 | REGRESSED |
| issue_76_session_layer_v2 | audio.mouth_to_ear_p95_ms | 166.4 | 200.3 | REGRESSED |
| issue_76_session_layer_v2 | cc.queue_max_ms | 37.03 | 76.18 | REGRESSED |
| issue_76_session_layer_v2 | cc.send_over_target_p95_pct | 18.16 | 16.88 | improved |
| issue_76_session_layer_v2 | cc.target_avg_kbps | 1749 | 1841 | improved |
| issue_76_session_layer_v2 | repair.overhead_pct | 35.05 | 38.84 | REGRESSED |
| issue_76_session_layer_v2 | sync.av_offset_max_ms | 126 | 105.6 | improved |
| issue_76_session_layer_v2 | video.freeze_total_ms | 206 | 0 | improved |
| issue_76_session_layer_v2 | video.longest_freeze_ms | 206 | 15 | improved |
| issue_76_tcc_v2_high_jitter | cc.max_settle_ms | 2.64e+04 | 2.85e+04 | REGRESSED |
| issue_76_tcc_v2_high_jitter | cc.send_over_target_ms | 0 | 100 | REGRESSED |
| issue_76_tcc_v2_high_jitter | repair.overhead_pct | 38.53 | 40.49 | REGRESSED |
| issue_76_tcc_v2_high_jitter | video.longest_freeze_ms | 2.995 | 32.47 | REGRESSED |
| issue_867_nack_reorder_loss | cc.send_over_target_ms | 100 | 200 | REGRESSED |
| issue_867_nack_reorder_loss | video.freeze_total_ms | 271 | 0 | improved |
| issue_867_nack_reorder_loss | video.longest_freeze_ms | 271 | 10.99 | improved |
| issue_942_dlsr_compact_ntp | (new scenario) | - | - | new |

48 regression(s), 20 improvement(s)

Every flagged regression is the RTT change above; none comes from anything else in the PR. The baseline is regenerated.
