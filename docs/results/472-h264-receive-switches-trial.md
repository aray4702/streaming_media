# #472: `sps_pps_tracker` and `h264_missing_packets` off again (stage-1 exit trial)

**Result: both stay on.** Turning them off makes time to first frame faster, but
regresses 301 scenario metrics and fails two scenarios, so the decision rule on
#472 (keep off only if better with **no** regression on any scenario metric)
keeps them on, as libwebrtc M157 has them (#453).

## Method

- `main` @ 71169f0, Linux x86_64, GCC 13, Release (the baseline platform).
- The test bench defaults (`testbench/scenario.h`, which set
  `rtp::SessionConfig`) flipped to `false` for each configuration below, the
  whole suite (`testbench/scenarios/*.scn`, 164 scenarios, the seed each one
  runs) run with `sm_testbench --quiet --json`, and compared with
  `tools/testbench_compare.py testbench/baseline`. Scenarios that set a switch
  explicitly (`issue_453_*`, `issue_24_timing_v2_high_jitter`,
  `issue_76_tcc_v2_high_jitter`) keep their setting.
- `main` itself reproduces the baseline exactly (0 regressions, 0 improvements).

## Suite against the baseline

| configuration | regressed metrics | improved metrics | scenarios with a regression | failing scenarios |
|---|---|---|---|---|
| both on (`main`) | 0 | 0 | 0 | 0 |
| `sps_pps_tracker off` | 115 | 124 | 15 | 0 |
| `h264_missing_packets off` | 186 | 98 | 45 | 2 |
| both off | 301 | 222 | 60 | 2 |

Failing with both off (and with `h264_missing_packets off` alone):

- `issue_893_overshoot_seed9`: `video.pause_total_ms < 100` (actual 8897)
- `issue_897_libwebrtc_clock`: `repair.overhead_pct > 50` (36.91), `sync.av_offset_p95_ms > 65` (57.63)

Over the 151 scenarios with video (seed each scenario runs), suite totals (sum over scenarios) and the scenario count where the metric is worse / better than both on:

| metric | both on (main) | tracker off | missing_packets off | both off | both off: worse / better |
|---|---|---|---|---|---|
| video.time_to_first_frame_ms | 51931 | 46171 | 51777 | 46017 | 0 / 22 |
| video.keyframe_requests | 121 | 97 | 139 | 109 | 10 / 15 |
| video.freeze_total_ms | 502358 | 506238 | 512749 | 516629 | 13 / 3 |
| video.freeze_count | 513 | 517 | 531 | 535 | 14 / 2 |
| video.undecodable_frames | 41 | 24 | 2366 | 2343 | 41 / 9 |
| video.frames_rendered | 138841 | 138742 | 138860 | 138761 | 10 / 18 |
| video.e2e_delay_p95_ms | 156849 | 158100 | 155900 | 157152 | 15 / 13 |
| audio.jb_delay_p95_ms | 56998 | 56848 | 59948 | 59798 | 15 / 12 |
| sync.av_offset_p95_ms | 111586 | 112765 | 108671 | 109850 | 13 / 17 |
| cc.target_avg_kbps | 746192 | 745837 | 745116 | 744760 | 21 / 9 |

Most regressions are in outage, handover and very-low-capacity scenarios:
without `missing_packets`, delta frames that reference a lost frame are handed
to the decoder (undecodable frames 41 -> 2343 over the suite) and freezes grow
(`very_low_capacity` freeze total 24.75 -> 32.6 s, `wifi_to_lte_handover`
3.4 -> 4.4 s, `issue_76_session_layer_v2` 0 -> 232 ms). Without the tracker
alone, the regressions are fewer but still present (freeze total +3.9 s over
the suite).

## `high_jitter`, seeds 1-10 (context)

As on #453, the scenario where the switches were first measured is better
with both off:

| metric | both on, mean (per seed) | both off, mean (per seed) |
|---|---|---|
| video.time_to_first_frame_ms | 453.7 (814, 244, 231, 469, 584, 489, 429, 433, 463, 381) | 224.9 (199, 244, 231, 208, 246, 221, 213, 221, 235, 231) |
| video.keyframe_requests | 1.2 (2, 0, 0, 1, 1, 2, 2, 2, 1, 1) | 0.4 (0, 1, 1, 0, 1, 0, 0, 0, 0, 1) |
| video.undecodable_frames | 0.5 | 0.0 |
| video.freeze_total_ms | 0 | 0 |
| video.render_fps | 29.60 | 29.68 |

That gain does not hold across the suite, so it does not meet the rule.

## Full comparison (both off against the baseline)

<details><summary>tools/testbench_compare.py output (525 lines)</summary>

```
  improved  high_jitter                  audio.jb_delay_p95_ms                 150 -> 110
  improved  high_jitter                  audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  high_jitter                  cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  high_jitter                  cc.send_over_target_ms                100 -> 0
  improved  high_jitter                  cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  high_jitter                  cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  high_jitter                  cc.target_max_drop_pct              24.01 -> 33.52
  improved  high_jitter                  sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  high_jitter                  sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  high_jitter                  sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  high_jitter                  video.avg_kbps                      477.9 -> 450.5
  improved  high_jitter                  video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  high_jitter                  video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  high_jitter                  video.e2e_delay_p95_ms                361 -> 163.2
  improved  high_jitter                  video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  high_jitter                  video.keyframe_requests                 2 -> 0
  improved  high_jitter                  video.time_to_first_frame_ms          814 -> 199
  improved  high_jitter                  video.undecodable_frames                2 -> 0
 REGRESSED  issue_120_rtx_payload_padding audio.jb_delay_p95_ms                 260 -> 300
 REGRESSED  issue_120_rtx_payload_padding audio.late_discards                    25 -> 30
 REGRESSED  issue_120_rtx_payload_padding audio.mouth_to_ear_p95_ms           386.4 -> 550.9
 REGRESSED  issue_120_rtx_payload_padding audio.net_delay_p95_ms              126.4 -> 250.9
 REGRESSED  issue_120_rtx_payload_padding cc.max_settle_ms                      700 -> 800
 REGRESSED  issue_120_rtx_payload_padding cc.queue_p95_ms                     43.14 -> 174.5
 REGRESSED  issue_120_rtx_payload_padding cc.send_over_target_ms                  0 -> 1600
 REGRESSED  issue_120_rtx_payload_padding cc.send_over_target_p95_pct         21.65 -> 38.4
 REGRESSED  issue_120_rtx_payload_padding cc.target_max_drop_pct              70.68 -> 78.37
 REGRESSED  issue_120_rtx_payload_padding sync.av_offset_p50_ms               31.61 -> 65.34
 REGRESSED  issue_120_rtx_payload_padding sync.av_offset_p95_ms                1101 -> 1714
 REGRESSED  issue_120_rtx_payload_padding video.delay_jitter_buffer_p95_ms      533.2 -> 613.2
  improved  issue_120_rtx_payload_padding video.delay_pacer_p95_ms            892.1 -> 37.7
 REGRESSED  issue_120_rtx_payload_padding video.e2e_delay_p50_ms                358 -> 394.2
 REGRESSED  issue_120_rtx_payload_padding video.e2e_delay_p95_ms               1427 -> 2133
  improved  issue_120_rtx_payload_padding video.encode_height_mean            585.9 -> 488.4
 REGRESSED  issue_120_rtx_payload_padding video.encoder_dropped                  44 -> 69
 REGRESSED  issue_120_rtx_payload_padding video.freeze_total_ms                3404 -> 4374
  improved  issue_120_rtx_payload_padding video.time_to_first_frame_ms          276 -> 175
 REGRESSED  issue_123_v2_allocation_min  audio.jb_delay_p50_ms                 720 -> 880
 REGRESSED  issue_123_v2_allocation_min  audio.jb_delay_p95_ms                1740 -> 2330
 REGRESSED  issue_123_v2_allocation_min  audio.mouth_to_ear_p95_ms            2073 -> 2664
 REGRESSED  issue_123_v2_allocation_min  audio.stretch_pct                   6.608 -> 8.235
 REGRESSED  issue_123_v2_allocation_min  cc.send_over_target_ms               6200 -> 7000
 REGRESSED  issue_123_v2_allocation_min  repair.overhead_pct                 27.48 -> 32.9
 REGRESSED  issue_123_v2_allocation_min  sync.av_offset_max_ms                6814 -> 7457
 REGRESSED  issue_123_v2_allocation_min  sync.av_offset_p95_ms                5856 -> 6692
 REGRESSED  issue_123_v2_allocation_min  video.e2e_delay_p50_ms               4254 -> 4622
 REGRESSED  issue_123_v2_allocation_min  video.e2e_delay_p95_ms               7451 -> 8382
 REGRESSED  issue_123_v2_allocation_min  video.encoder_dropped                 436 -> 501
  improved  issue_123_v2_allocation_min  video.frames_rendered                 377 -> 439
 REGRESSED  issue_123_v2_allocation_min  video.freeze_count                     23 -> 27
 REGRESSED  issue_123_v2_allocation_min  video.freeze_total_ms           2.475e+04 -> 3.26e+04
  improved  issue_123_v2_allocation_min  video.pause_total_ms                 8461 -> 0
  improved  issue_123_v2_allocation_min  video.render_fps                    9.425 -> 10.97
  improved  issue_123_v2_allocation_min  video.render_ratio_pct              49.35 -> 62.8
 REGRESSED  issue_123_v2_allocation_min  video.undecodable_frames                0 -> 39
  improved  issue_127_all_v2_no_stall    cc.max_settle_ms                 1.47e+04 -> 1.2e+04
 REGRESSED  issue_127_all_v2_no_stall    cc.queue_max_ms                     56.13 -> 143
 REGRESSED  issue_127_all_v2_no_stall    cc.target_max_drop_pct              23.82 -> 29.84
 REGRESSED  issue_127_all_v2_no_stall    repair.overhead_pct                 32.95 -> 35.32
  improved  issue_127_all_v2_no_stall    sync.av_offset_max_ms               683.7 -> 601.8
 REGRESSED  issue_127_all_v2_no_stall    video.delay_network_p95_ms          336.6 -> 404.5
 REGRESSED  issue_127_all_v2_no_stall    video.encode_height_mean              234 -> 249.1
 REGRESSED  issue_127_all_v2_no_stall    video.freeze_count                      5 -> 9
 REGRESSED  issue_127_all_v2_no_stall    video.freeze_total_ms                2057 -> 2242
  improved  issue_127_all_v2_no_stall    video.longest_freeze_ms               695 -> 457
  improved  issue_127_pacer_v2_jitter_reorder_loss audio.jb_delay_p95_ms                 140 -> 100
 REGRESSED  issue_127_pacer_v2_jitter_reorder_loss audio.late_discards                    16 -> 43
  improved  issue_127_pacer_v2_jitter_reorder_loss audio.mouth_to_ear_p95_ms             251 -> 209.3
  improved  issue_127_pacer_v2_jitter_reorder_loss cc.max_settle_ms                 2.53e+04 -> 2.19e+04
  improved  issue_127_pacer_v2_jitter_reorder_loss cc.send_over_target_ms                100 -> 0
 REGRESSED  issue_127_pacer_v2_jitter_reorder_loss cc.send_over_target_p95_pct         20.25 -> 23.63
 REGRESSED  issue_127_pacer_v2_jitter_reorder_loss cc.target_max_drop_pct              38.25 -> 41.67
  improved  issue_127_pacer_v2_jitter_reorder_loss repair.overhead_pct                 47.25 -> 44.31
  improved  issue_127_pacer_v2_jitter_reorder_loss sync.av_offset_p95_ms               109.1 -> 78.99
  improved  issue_127_pacer_v2_jitter_reorder_loss video.e2e_delay_p50_ms              184.3 -> 158.9
 REGRESSED  issue_127_pacer_v2_jitter_reorder_loss video.freeze_total_ms                   0 -> 187
 REGRESSED  issue_127_pacer_v2_jitter_reorder_loss video.longest_freeze_ms              55.7 -> 187
  improved  issue_12_lossbwe_high_jitter_no_loss audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_12_lossbwe_high_jitter_no_loss audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_12_lossbwe_high_jitter_no_loss cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_12_lossbwe_high_jitter_no_loss cc.send_over_target_ms                100 -> 0
  improved  issue_12_lossbwe_high_jitter_no_loss cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_12_lossbwe_high_jitter_no_loss cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_12_lossbwe_high_jitter_no_loss cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_12_lossbwe_high_jitter_no_loss sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_12_lossbwe_high_jitter_no_loss sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_12_lossbwe_high_jitter_no_loss sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_12_lossbwe_high_jitter_no_loss video.avg_kbps                      477.9 -> 450.5
  improved  issue_12_lossbwe_high_jitter_no_loss video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_12_lossbwe_high_jitter_no_loss video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_12_lossbwe_high_jitter_no_loss video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_12_lossbwe_high_jitter_no_loss video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_12_lossbwe_high_jitter_no_loss video.keyframe_requests                 2 -> 0
  improved  issue_12_lossbwe_high_jitter_no_loss video.time_to_first_frame_ms          814 -> 199
  improved  issue_12_lossbwe_high_jitter_no_loss video.undecodable_frames                2 -> 0
 REGRESSED  issue_12_lossbwe_policer     video.undecodable_frames               17 -> 116
 REGRESSED  issue_133_all_v2_outage_15s  video.undecodable_frames                0 -> 45
 REGRESSED  issue_133_all_v2_outage_3s   video.undecodable_frames                0 -> 6
 REGRESSED  issue_133_all_v2_outage_8s   video.undecodable_frames                0 -> 73
 REGRESSED  issue_133_pacer_v2_outage_15s video.undecodable_frames                0 -> 45
 REGRESSED  issue_133_pacer_v2_outage_3s video.undecodable_frames                0 -> 6
 REGRESSED  issue_133_pacer_v2_outage_8s video.undecodable_frames                0 -> 73
  improved  issue_150_gcc_v2_high_jitter audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_150_gcc_v2_high_jitter audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_150_gcc_v2_high_jitter cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_150_gcc_v2_high_jitter cc.send_over_target_ms                100 -> 0
  improved  issue_150_gcc_v2_high_jitter cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_150_gcc_v2_high_jitter cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_150_gcc_v2_high_jitter cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_150_gcc_v2_high_jitter sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_150_gcc_v2_high_jitter sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_150_gcc_v2_high_jitter sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_150_gcc_v2_high_jitter video.avg_kbps                      477.9 -> 450.5
  improved  issue_150_gcc_v2_high_jitter video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_150_gcc_v2_high_jitter video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_150_gcc_v2_high_jitter video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_150_gcc_v2_high_jitter video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_150_gcc_v2_high_jitter video.keyframe_requests                 2 -> 0
  improved  issue_150_gcc_v2_high_jitter video.time_to_first_frame_ms          814 -> 199
  improved  issue_150_gcc_v2_high_jitter video.undecodable_frames                2 -> 0
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 audio.jb_delay_p95_ms                 110 -> 150
  improved  issue_150_gcc_v2_high_jitter_seed10 audio.late_discards                   116 -> 62
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 audio.mouth_to_ear_p95_ms           233.4 -> 269.3
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 cc.goodput_kbps                     842.2 -> 578.6
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 cc.max_settle_ms                  1.6e+04 -> 3e+04
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 cc.target_avg_kbps                   1294 -> 807.2
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 cc.target_max_drop_pct                  0 -> 16.9
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 cc.utilization_pct                  32.62 -> 22.41
  improved  issue_150_gcc_v2_high_jitter_seed10 repair.overhead_pct                 51.68 -> 36.91
  improved  issue_150_gcc_v2_high_jitter_seed10 sync.av_offset_p50_ms              -3.138 -> -31.2
 REGRESSED  issue_150_gcc_v2_high_jitter_seed10 video.avg_kbps                      742.5 -> 487.2
  improved  issue_150_gcc_v2_high_jitter_seed10 video.encode_height_mean            294.1 -> 198
  improved  issue_150_gcc_v2_high_jitter_seed10 video.encoder_target_avg_kbps       735.4 -> 485.9
  improved  issue_150_gcc_v2_high_jitter_seed10 video.longest_freeze_ms             31.83 -> 2.403
  improved  issue_150_gcc_v2_high_jitter_seed5 audio.late_discards                    77 -> 52
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 cc.goodput_kbps                       611 -> 473.9
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 cc.max_settle_ms                  2.5e+04 -> 3e+04
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 cc.send_over_target_ms                  0 -> 100
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 cc.target_avg_kbps                  858.8 -> 623
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 cc.target_max_drop_pct              31.21 -> 41.23
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 cc.utilization_pct                  23.67 -> 18.36
  improved  issue_150_gcc_v2_high_jitter_seed5 repair.overhead_pct                 41.92 -> 31.84
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 video.avg_kbps                      520.6 -> 387.5
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 video.delay_jitter_buffer_p95_ms      104.6 -> 126.8
  improved  issue_150_gcc_v2_high_jitter_seed5 video.encode_height_mean              198 -> 180
  improved  issue_150_gcc_v2_high_jitter_seed5 video.encoder_target_avg_kbps       518.1 -> 388
  improved  issue_150_gcc_v2_high_jitter_seed5 video.keyframe_requests                 2 -> 0
 REGRESSED  issue_150_gcc_v2_high_jitter_seed5 video.qp_mean                       25.27 -> 26.94
  improved  issue_150_gcc_v2_high_jitter_seed5 video.time_to_first_frame_ms          489 -> 221
 REGRESSED  issue_151_outage_3s_v2       video.undecodable_frames                0 -> 4
  improved  issue_19_cc_v2_high_jitter   audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_19_cc_v2_high_jitter   audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_19_cc_v2_high_jitter   cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_19_cc_v2_high_jitter   cc.send_over_target_ms                100 -> 0
  improved  issue_19_cc_v2_high_jitter   cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_19_cc_v2_high_jitter   cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_19_cc_v2_high_jitter   cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_19_cc_v2_high_jitter   sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_19_cc_v2_high_jitter   sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_19_cc_v2_high_jitter   sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_19_cc_v2_high_jitter   video.avg_kbps                      477.9 -> 450.5
  improved  issue_19_cc_v2_high_jitter   video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_19_cc_v2_high_jitter   video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_19_cc_v2_high_jitter   video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_19_cc_v2_high_jitter   video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_19_cc_v2_high_jitter   video.keyframe_requests                 2 -> 0
  improved  issue_19_cc_v2_high_jitter   video.time_to_first_frame_ms          814 -> 199
  improved  issue_19_cc_v2_high_jitter   video.undecodable_frames                2 -> 0
 REGRESSED  issue_23_handover            audio.jb_delay_p95_ms                 260 -> 300
 REGRESSED  issue_23_handover            audio.late_discards                    25 -> 30
 REGRESSED  issue_23_handover            audio.mouth_to_ear_p95_ms           386.4 -> 550.9
 REGRESSED  issue_23_handover            audio.net_delay_p95_ms              126.4 -> 250.9
 REGRESSED  issue_23_handover            cc.max_settle_ms                      700 -> 800
 REGRESSED  issue_23_handover            cc.queue_p95_ms                     43.14 -> 174.5
 REGRESSED  issue_23_handover            cc.send_over_target_ms                  0 -> 1600
 REGRESSED  issue_23_handover            cc.send_over_target_p95_pct         21.65 -> 38.4
 REGRESSED  issue_23_handover            cc.target_max_drop_pct              70.68 -> 78.37
 REGRESSED  issue_23_handover            sync.av_offset_p50_ms               31.61 -> 65.34
 REGRESSED  issue_23_handover            sync.av_offset_p95_ms                1101 -> 1714
 REGRESSED  issue_23_handover            video.delay_jitter_buffer_p95_ms      533.2 -> 613.2
  improved  issue_23_handover            video.delay_pacer_p95_ms            892.1 -> 37.7
 REGRESSED  issue_23_handover            video.e2e_delay_p50_ms                358 -> 394.2
 REGRESSED  issue_23_handover            video.e2e_delay_p95_ms               1427 -> 2133
  improved  issue_23_handover            video.encode_height_mean            585.9 -> 488.4
 REGRESSED  issue_23_handover            video.encoder_dropped                  44 -> 69
 REGRESSED  issue_23_handover            video.freeze_total_ms                3404 -> 4374
  improved  issue_23_handover            video.time_to_first_frame_ms          276 -> 175
 REGRESSED  issue_23_outage_3s           video.undecodable_frames                0 -> 4
 REGRESSED  issue_24_timing_v2_default   video.undecodable_frames                0 -> 2
  improved  issue_24_timing_v2_high_jitter audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_24_timing_v2_high_jitter audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_24_timing_v2_high_jitter cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_24_timing_v2_high_jitter cc.send_over_target_ms                100 -> 0
  improved  issue_24_timing_v2_high_jitter cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_24_timing_v2_high_jitter cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_24_timing_v2_high_jitter cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_24_timing_v2_high_jitter sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_24_timing_v2_high_jitter sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_24_timing_v2_high_jitter sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_24_timing_v2_high_jitter video.avg_kbps                      477.9 -> 450.5
  improved  issue_24_timing_v2_high_jitter video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_24_timing_v2_high_jitter video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_24_timing_v2_high_jitter video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_24_timing_v2_high_jitter video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_24_timing_v2_high_jitter video.keyframe_requests                 2 -> 0
  improved  issue_24_timing_v2_high_jitter video.time_to_first_frame_ms          814 -> 199
  improved  issue_24_timing_v2_high_jitter video.undecodable_frames                2 -> 0
 REGRESSED  issue_24_timing_v2_render_fps video.undecodable_frames                0 -> 2
 REGRESSED  issue_268_freeze_rule_outage video.undecodable_frames                0 -> 6
 REGRESSED  issue_29_ulpfec_over_red     video.undecodable_frames                0 -> 823
 REGRESSED  issue_329_utilization_reordering audio.late_discards                    26 -> 33
  improved  issue_329_utilization_reordering video.time_to_first_frame_ms          272 -> 188
 REGRESSED  issue_346_reconfigure_outage_8s video.undecodable_frames                0 -> 73
 REGRESSED  issue_368_tracker_outage_render video.undecodable_frames                0 -> 6
 REGRESSED  issue_372_outage_interruption_count video.undecodable_frames                0 -> 5
 REGRESSED  issue_411_nack_on_reorder    audio.late_discards                    26 -> 33
  improved  issue_411_nack_on_reorder    video.time_to_first_frame_ms          272 -> 188
 REGRESSED  issue_411_reconfigure_clears_cwnd_drop video.undecodable_frames                0 -> 5
 REGRESSED  issue_430_av_outage_4s       video.undecodable_frames                0 -> 94
 REGRESSED  issue_430_av_outage_6s       video.undecodable_frames                0 -> 7
 REGRESSED  issue_430_video_only_outage_4s video.undecodable_frames                0 -> 88
 REGRESSED  issue_437_default_flip_very_low_capacity audio.jb_delay_p50_ms                 720 -> 880
 REGRESSED  issue_437_default_flip_very_low_capacity audio.jb_delay_p95_ms                1740 -> 2330
 REGRESSED  issue_437_default_flip_very_low_capacity audio.mouth_to_ear_p95_ms            2073 -> 2664
 REGRESSED  issue_437_default_flip_very_low_capacity audio.stretch_pct                   6.608 -> 8.235
 REGRESSED  issue_437_default_flip_very_low_capacity cc.send_over_target_ms               6200 -> 7000
 REGRESSED  issue_437_default_flip_very_low_capacity repair.overhead_pct                 27.48 -> 32.9
 REGRESSED  issue_437_default_flip_very_low_capacity sync.av_offset_max_ms                6814 -> 7457
 REGRESSED  issue_437_default_flip_very_low_capacity sync.av_offset_p95_ms                5856 -> 6692
 REGRESSED  issue_437_default_flip_very_low_capacity video.e2e_delay_p50_ms               4254 -> 4622
 REGRESSED  issue_437_default_flip_very_low_capacity video.e2e_delay_p95_ms               7451 -> 8382
 REGRESSED  issue_437_default_flip_very_low_capacity video.encoder_dropped                 436 -> 501
  improved  issue_437_default_flip_very_low_capacity video.frames_rendered                 377 -> 439
 REGRESSED  issue_437_default_flip_very_low_capacity video.freeze_count                     23 -> 27
 REGRESSED  issue_437_default_flip_very_low_capacity video.freeze_total_ms           2.475e+04 -> 3.26e+04
  improved  issue_437_default_flip_very_low_capacity video.pause_total_ms                 8461 -> 0
  improved  issue_437_default_flip_very_low_capacity video.render_fps                    9.425 -> 10.97
  improved  issue_437_default_flip_very_low_capacity video.render_ratio_pct              49.35 -> 62.8
 REGRESSED  issue_437_default_flip_very_low_capacity video.undecodable_frames                0 -> 39
 REGRESSED  issue_437_render_smoothing_very_low_capacity audio.jb_delay_p50_ms                 720 -> 880
 REGRESSED  issue_437_render_smoothing_very_low_capacity audio.jb_delay_p95_ms                1740 -> 2330
 REGRESSED  issue_437_render_smoothing_very_low_capacity audio.mouth_to_ear_p95_ms            2073 -> 2664
 REGRESSED  issue_437_render_smoothing_very_low_capacity audio.stretch_pct                   6.608 -> 8.235
 REGRESSED  issue_437_render_smoothing_very_low_capacity cc.send_over_target_ms               6200 -> 7000
 REGRESSED  issue_437_render_smoothing_very_low_capacity repair.overhead_pct                 27.48 -> 32.9
 REGRESSED  issue_437_render_smoothing_very_low_capacity sync.av_offset_max_ms                6814 -> 7457
 REGRESSED  issue_437_render_smoothing_very_low_capacity sync.av_offset_p95_ms                5856 -> 6692
 REGRESSED  issue_437_render_smoothing_very_low_capacity video.e2e_delay_p50_ms               4254 -> 4622
 REGRESSED  issue_437_render_smoothing_very_low_capacity video.e2e_delay_p95_ms               7451 -> 8382
 REGRESSED  issue_437_render_smoothing_very_low_capacity video.encoder_dropped                 436 -> 501
  improved  issue_437_render_smoothing_very_low_capacity video.frames_rendered                 377 -> 439
 REGRESSED  issue_437_render_smoothing_very_low_capacity video.freeze_count                     23 -> 27
 REGRESSED  issue_437_render_smoothing_very_low_capacity video.freeze_total_ms           2.475e+04 -> 3.26e+04
  improved  issue_437_render_smoothing_very_low_capacity video.pause_total_ms                 8461 -> 0
  improved  issue_437_render_smoothing_very_low_capacity video.render_fps                    9.425 -> 10.97
  improved  issue_437_render_smoothing_very_low_capacity video.render_ratio_pct              49.35 -> 62.8
 REGRESSED  issue_437_render_smoothing_very_low_capacity video.undecodable_frames                0 -> 39
 REGRESSED  issue_446_cwnd_after_outage  video.undecodable_frames                0 -> 86
 REGRESSED  issue_466_scene_change_very_low_capacity audio.jb_delay_p50_ms                 930 -> 980
 REGRESSED  issue_466_scene_change_very_low_capacity audio.jb_delay_p95_ms                1660 -> 2050
 REGRESSED  issue_466_scene_change_very_low_capacity audio.mouth_to_ear_p95_ms            1992 -> 2383
 REGRESSED  issue_466_scene_change_very_low_capacity cc.send_over_target_ms               4000 -> 6000
 REGRESSED  issue_466_scene_change_very_low_capacity repair.overhead_pct                 24.46 -> 28.02
 REGRESSED  issue_466_scene_change_very_low_capacity sync.av_offset_max_ms                5273 -> 7432
 REGRESSED  issue_466_scene_change_very_low_capacity sync.av_offset_p50_ms                1818 -> 2063
 REGRESSED  issue_466_scene_change_very_low_capacity sync.av_offset_p95_ms                4682 -> 6522
 REGRESSED  issue_466_scene_change_very_low_capacity video.delay_jitter_buffer_p95_ms       2745 -> 5393
  improved  issue_466_scene_change_very_low_capacity video.delay_network_p95_ms           2456 -> 2165
 REGRESSED  issue_466_scene_change_very_low_capacity video.delay_pacer_p95_ms             2664 -> 2920
 REGRESSED  issue_466_scene_change_very_low_capacity video.e2e_delay_p50_ms               3104 -> 3553
 REGRESSED  issue_466_scene_change_very_low_capacity video.e2e_delay_p95_ms               6175 -> 8731
 REGRESSED  issue_466_scene_change_very_low_capacity video.encoder_dropped                 465 -> 500
  improved  issue_466_scene_change_very_low_capacity video.frames_rendered                 355 -> 387
  improved  issue_466_scene_change_very_low_capacity video.freeze_count                     21 -> 19
  improved  issue_466_scene_change_very_low_capacity video.freeze_total_ms           2.468e+04 -> 1.934e+04
 REGRESSED  issue_466_scene_change_very_low_capacity video.longest_freeze_ms              4101 -> 4610
 REGRESSED  issue_466_scene_change_very_low_capacity video.pause_total_ms                 6074 -> 1.294e+04
  improved  issue_466_scene_change_very_low_capacity video.render_fps                    8.875 -> 9.675
  improved  issue_466_scene_change_very_low_capacity video.render_ratio_pct               48.3 -> 55.29
 REGRESSED  issue_466_scene_change_very_low_capacity video.undecodable_frames                0 -> 70
  improved  issue_466_undershoot_very_low_capacity audio.jb_delay_p50_ms                1030 -> 760
  improved  issue_466_undershoot_very_low_capacity audio.jb_delay_p95_ms                1610 -> 1450
  improved  issue_466_undershoot_very_low_capacity audio.late_discards                    16 -> 13
  improved  issue_466_undershoot_very_low_capacity audio.mouth_to_ear_p95_ms            1944 -> 1783
  improved  issue_466_undershoot_very_low_capacity cc.queue_over_200ms_pct              99.1 -> 93.53
  improved  issue_466_undershoot_very_low_capacity cc.send_over_target_ms           1.13e+04 -> 7700
  improved  issue_466_undershoot_very_low_capacity cc.send_over_target_p95_pct         115.5 -> 101.3
  improved  issue_466_undershoot_very_low_capacity repair.overhead_pct                 39.88 -> 28.76
  improved  issue_466_undershoot_very_low_capacity sync.av_offset_max_ms                9002 -> 6860
  improved  issue_466_undershoot_very_low_capacity sync.av_offset_p50_ms                3695 -> 1774
  improved  issue_466_undershoot_very_low_capacity sync.av_offset_p95_ms                7480 -> 3978
  improved  issue_466_undershoot_very_low_capacity video.delay_jitter_buffer_p95_ms       5309 -> 2825
  improved  issue_466_undershoot_very_low_capacity video.delay_network_p95_ms           3535 -> 1193
  improved  issue_466_undershoot_very_low_capacity video.delay_pacer_p95_ms             3224 -> 2377
  improved  issue_466_undershoot_very_low_capacity video.e2e_delay_p50_ms               5401 -> 2888
  improved  issue_466_undershoot_very_low_capacity video.e2e_delay_p95_ms               8965 -> 5686
  improved  issue_466_undershoot_very_low_capacity video.encoder_dropped                 453 -> 391
 REGRESSED  issue_466_undershoot_very_low_capacity video.frames_rendered                 430 -> 210
  improved  issue_466_undershoot_very_low_capacity video.freeze_count                     24 -> 12
  improved  issue_466_undershoot_very_low_capacity video.freeze_total_ms           3.193e+04 -> 1.29e+04
 REGRESSED  issue_466_undershoot_very_low_capacity video.keyframe_requests                 3 -> 6
  improved  issue_466_undershoot_very_low_capacity video.longest_freeze_ms              3804 -> 3055
 REGRESSED  issue_466_undershoot_very_low_capacity video.pause_count                       0 -> 2
 REGRESSED  issue_466_undershoot_very_low_capacity video.pause_total_ms                    0 -> 1.966e+04
 REGRESSED  issue_466_undershoot_very_low_capacity video.render_fps                    10.75 -> 5.25
 REGRESSED  issue_466_undershoot_very_low_capacity video.render_ratio_pct              57.56 -> 25.96
 REGRESSED  issue_466_undershoot_very_low_capacity video.residual_loss_pct             4.647 -> 5.958
 REGRESSED  issue_466_undershoot_very_low_capacity video.undecodable_frames                0 -> 190
  improved  issue_484_audio_report_blocks audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_484_audio_report_blocks audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_484_audio_report_blocks cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_484_audio_report_blocks cc.send_over_target_ms                100 -> 0
  improved  issue_484_audio_report_blocks cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_484_audio_report_blocks cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_484_audio_report_blocks cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_484_audio_report_blocks sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_484_audio_report_blocks sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_484_audio_report_blocks sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_484_audio_report_blocks video.avg_kbps                      477.9 -> 450.5
  improved  issue_484_audio_report_blocks video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_484_audio_report_blocks video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_484_audio_report_blocks video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_484_audio_report_blocks video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_484_audio_report_blocks video.keyframe_requests                 2 -> 0
  improved  issue_484_audio_report_blocks video.time_to_first_frame_ms          814 -> 199
  improved  issue_484_audio_report_blocks video.undecodable_frames                2 -> 0
 REGRESSED  issue_514_padding_without_rtx video.undecodable_frames                0 -> 86
 REGRESSED  issue_522_outage_to_500kbps  video.undecodable_frames                0 -> 11
 REGRESSED  issue_53_cwnd_pushback       video.undecodable_frames                0 -> 2
  improved  issue_62_neteq_v2_high_jitter audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_62_neteq_v2_high_jitter audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_62_neteq_v2_high_jitter cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_62_neteq_v2_high_jitter cc.send_over_target_ms                100 -> 0
  improved  issue_62_neteq_v2_high_jitter cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_62_neteq_v2_high_jitter cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_62_neteq_v2_high_jitter cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_62_neteq_v2_high_jitter sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_62_neteq_v2_high_jitter sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_62_neteq_v2_high_jitter sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_62_neteq_v2_high_jitter video.avg_kbps                      477.9 -> 450.5
  improved  issue_62_neteq_v2_high_jitter video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_62_neteq_v2_high_jitter video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_62_neteq_v2_high_jitter video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_62_neteq_v2_high_jitter video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_62_neteq_v2_high_jitter video.keyframe_requests                 2 -> 0
  improved  issue_62_neteq_v2_high_jitter video.time_to_first_frame_ms          814 -> 199
  improved  issue_62_neteq_v2_high_jitter video.undecodable_frames                2 -> 0
 REGRESSED  issue_737_outage_15s_100ms   video.undecodable_frames                0 -> 23
 REGRESSED  issue_737_outage_3s_20ms     video.undecodable_frames                0 -> 3
 REGRESSED  issue_738_cwnd_ratio_min_clamp video.undecodable_frames                0 -> 66
 REGRESSED  issue_76_rtx_v2_handover     audio.jb_delay_p95_ms                 260 -> 300
 REGRESSED  issue_76_rtx_v2_handover     audio.late_discards                    25 -> 30
 REGRESSED  issue_76_rtx_v2_handover     audio.mouth_to_ear_p95_ms           386.4 -> 550.9
 REGRESSED  issue_76_rtx_v2_handover     audio.net_delay_p95_ms              126.4 -> 250.9
 REGRESSED  issue_76_rtx_v2_handover     cc.max_settle_ms                      700 -> 800
 REGRESSED  issue_76_rtx_v2_handover     cc.queue_p95_ms                     43.14 -> 174.5
 REGRESSED  issue_76_rtx_v2_handover     cc.send_over_target_ms                  0 -> 1600
 REGRESSED  issue_76_rtx_v2_handover     cc.send_over_target_p95_pct         21.65 -> 38.4
 REGRESSED  issue_76_rtx_v2_handover     cc.target_max_drop_pct              70.68 -> 78.37
 REGRESSED  issue_76_rtx_v2_handover     sync.av_offset_p50_ms               31.61 -> 65.34
 REGRESSED  issue_76_rtx_v2_handover     sync.av_offset_p95_ms                1101 -> 1714
 REGRESSED  issue_76_rtx_v2_handover     video.delay_jitter_buffer_p95_ms      533.2 -> 613.2
  improved  issue_76_rtx_v2_handover     video.delay_pacer_p95_ms            892.1 -> 37.7
 REGRESSED  issue_76_rtx_v2_handover     video.e2e_delay_p50_ms                358 -> 394.2
 REGRESSED  issue_76_rtx_v2_handover     video.e2e_delay_p95_ms               1427 -> 2133
  improved  issue_76_rtx_v2_handover     video.encode_height_mean            585.9 -> 488.4
 REGRESSED  issue_76_rtx_v2_handover     video.encoder_dropped                  44 -> 69
 REGRESSED  issue_76_rtx_v2_handover     video.freeze_total_ms                3404 -> 4374
  improved  issue_76_rtx_v2_handover     video.time_to_first_frame_ms          276 -> 175
 REGRESSED  issue_76_session_layer_v2    audio.jb_delay_p95_ms                  80 -> 130
 REGRESSED  issue_76_session_layer_v2    audio.late_discards                    20 -> 31
 REGRESSED  issue_76_session_layer_v2    audio.mouth_to_ear_p95_ms           170.7 -> 225.8
 REGRESSED  issue_76_session_layer_v2    cc.max_settle_ms                     7800 -> 8400
 REGRESSED  issue_76_session_layer_v2    cc.queue_max_ms                     57.23 -> 120
  improved  issue_76_session_layer_v2    cc.send_over_target_p95_pct         18.27 -> 17.07
  improved  issue_76_session_layer_v2    cc.target_max_drop_pct              29.94 -> 27.89
 REGRESSED  issue_76_session_layer_v2    sync.av_offset_max_ms               99.28 -> 175.1
 REGRESSED  issue_76_session_layer_v2    sync.av_offset_p95_ms               49.52 -> 77.96
  improved  issue_76_session_layer_v2    video.encode_height_mean            438.5 -> 414.4
 REGRESSED  issue_76_session_layer_v2    video.freeze_total_ms                   0 -> 232
 REGRESSED  issue_76_session_layer_v2    video.longest_freeze_ms                14 -> 232
  improved  issue_76_tcc_v2_high_jitter  audio.jb_delay_p95_ms                 150 -> 110
  improved  issue_76_tcc_v2_high_jitter  audio.mouth_to_ear_p95_ms           273.2 -> 230
  improved  issue_76_tcc_v2_high_jitter  cc.max_settle_ms                 2.85e+04 -> 2.64e+04
  improved  issue_76_tcc_v2_high_jitter  cc.send_over_target_ms                100 -> 0
  improved  issue_76_tcc_v2_high_jitter  cc.send_over_target_p95_pct         25.64 -> 23.46
 REGRESSED  issue_76_tcc_v2_high_jitter  cc.target_avg_kbps                  790.4 -> 748.2
 REGRESSED  issue_76_tcc_v2_high_jitter  cc.target_max_drop_pct              24.01 -> 33.52
  improved  issue_76_tcc_v2_high_jitter  sync.av_offset_max_ms               588.5 -> 286.3
 REGRESSED  issue_76_tcc_v2_high_jitter  sync.av_offset_p50_ms              -41.27 -> 2.626
  improved  issue_76_tcc_v2_high_jitter  sync.av_offset_p95_ms               229.2 -> 70.4
 REGRESSED  issue_76_tcc_v2_high_jitter  video.avg_kbps                      477.9 -> 450.5
  improved  issue_76_tcc_v2_high_jitter  video.delay_pacer_p95_ms            268.2 -> 53.15
 REGRESSED  issue_76_tcc_v2_high_jitter  video.e2e_delay_p50_ms              104.5 -> 152.7
  improved  issue_76_tcc_v2_high_jitter  video.e2e_delay_p95_ms                361 -> 163.2
  improved  issue_76_tcc_v2_high_jitter  video.encoder_target_avg_kbps       468.5 -> 443.5
  improved  issue_76_tcc_v2_high_jitter  video.keyframe_requests                 2 -> 0
  improved  issue_76_tcc_v2_high_jitter  video.time_to_first_frame_ms          814 -> 199
  improved  issue_76_tcc_v2_high_jitter  video.undecodable_frames                2 -> 0
  improved  issue_867_nack_reorder_loss  audio.jb_delay_p50_ms                 170 -> 140
  improved  issue_867_nack_reorder_loss  audio.late_discards                   104 -> 70
 REGRESSED  issue_867_nack_reorder_loss  cc.goodput_kbps                     332.3 -> 304.4
  improved  issue_867_nack_reorder_loss  cc.send_over_target_ms                200 -> 0
 REGRESSED  issue_867_nack_reorder_loss  cc.target_avg_kbps                  441.2 -> 402.8
 REGRESSED  issue_867_nack_reorder_loss  cc.target_max_drop_pct              3.788 -> 36.89
 REGRESSED  issue_867_nack_reorder_loss  cc.utilization_pct                  12.87 -> 11.79
  improved  issue_867_nack_reorder_loss  sync.av_offset_max_ms               494.1 -> 320
  improved  issue_867_nack_reorder_loss  sync.av_offset_p95_ms               119.4 -> 91.34
 REGRESSED  issue_867_nack_reorder_loss  video.avg_kbps                      254.1 -> 228.2
  improved  issue_867_nack_reorder_loss  video.e2e_delay_p50_ms                284 -> 262.1
  improved  issue_867_nack_reorder_loss  video.encoder_target_avg_kbps       253.2 -> 226.9
 REGRESSED  issue_867_nack_reorder_loss  video.longest_freeze_ms             10.99 -> 33.54
 REGRESSED  issue_893_overshoot_seed2    audio.late_discards                    10 -> 13
 REGRESSED  issue_893_overshoot_seed2    cc.queue_over_200ms_pct             93.03 -> 98.03
  improved  issue_893_overshoot_seed2    cc.send_over_target_ms               4900 -> 4300
  improved  issue_893_overshoot_seed2    cc.send_over_target_p95_pct            63 -> 59.82
  improved  issue_893_overshoot_seed2    repair.overhead_pct                 27.15 -> 25.45
  improved  issue_893_overshoot_seed2    sync.av_offset_p50_ms                2391 -> 2268
  improved  issue_893_overshoot_seed2    sync.av_offset_p95_ms                4907 -> 3864
 REGRESSED  issue_893_overshoot_seed2    video.delay_jitter_buffer_p95_ms       2878 -> 3077
  improved  issue_893_overshoot_seed2    video.delay_network_p95_ms           2209 -> 1966
  improved  issue_893_overshoot_seed2    video.e2e_delay_p50_ms               3938 -> 3658
 REGRESSED  issue_893_overshoot_seed2    video.frames_rendered                 377 -> 348
 REGRESSED  issue_893_overshoot_seed2    video.freeze_count                     23 -> 28
 REGRESSED  issue_893_overshoot_seed2    video.freeze_total_ms           3.277e+04 -> 3.531e+04
 REGRESSED  issue_893_overshoot_seed2    video.longest_freeze_ms              3545 -> 4303
 REGRESSED  issue_893_overshoot_seed2    video.render_fps                    9.425 -> 8.7
 REGRESSED  issue_893_overshoot_seed2    video.render_ratio_pct              64.44 -> 60
 REGRESSED  issue_893_overshoot_seed2    video.undecodable_frames                5 -> 35
  improved  issue_893_overshoot_seed9    audio.jb_delay_p50_ms                 740 -> 670
  improved  issue_893_overshoot_seed9    audio.jb_delay_p95_ms                2140 -> 1760
  improved  issue_893_overshoot_seed9    audio.mouth_to_ear_p95_ms            2472 -> 2092
  improved  issue_893_overshoot_seed9    audio.stretch_pct                   7.735 -> 6.083
  improved  issue_893_overshoot_seed9    cc.send_over_target_ms               9300 -> 8100
 REGRESSED  issue_893_overshoot_seed9    cc.send_over_target_p95_pct          98.4 -> 136.8
  improved  issue_893_overshoot_seed9    sync.av_offset_max_ms           1.029e+04 -> 7513
  improved  issue_893_overshoot_seed9    sync.av_offset_p50_ms                2846 -> 1314
  improved  issue_893_overshoot_seed9    sync.av_offset_p95_ms                9800 -> 5456
  improved  issue_893_overshoot_seed9    video.delay_jitter_buffer_p95_ms       8787 -> 3287
  improved  issue_893_overshoot_seed9    video.delay_network_p95_ms           2571 -> 2064
  improved  issue_893_overshoot_seed9    video.e2e_delay_p50_ms               3847 -> 2514
  improved  issue_893_overshoot_seed9    video.e2e_delay_p95_ms           1.23e+04 -> 7372
 REGRESSED  issue_893_overshoot_seed9    video.frames_rendered                 255 -> 200
  improved  issue_893_overshoot_seed9    video.freeze_total_ms           3.408e+04 -> 2.644e+04
 REGRESSED  issue_893_overshoot_seed9    video.pause_total_ms                    0 -> 8897
 REGRESSED  issue_893_overshoot_seed9    video.render_fps                    6.375 -> 5
 REGRESSED  issue_893_overshoot_seed9    video.render_ratio_pct              43.74 -> 33.28
 REGRESSED  issue_893_overshoot_seed9    video.residual_loss_pct             1.818 -> 4.396
 REGRESSED  issue_893_overshoot_seed9    video.undecodable_frames                0 -> 71
    FAILED  issue_893_overshoot_seed9    expectations                            - -> -
 REGRESSED  issue_897_libwebrtc_clock    audio.jb_delay_p95_ms                 110 -> 150
  improved  issue_897_libwebrtc_clock    audio.late_discards                   116 -> 62
 REGRESSED  issue_897_libwebrtc_clock    audio.mouth_to_ear_p95_ms           233.4 -> 269.3
 REGRESSED  issue_897_libwebrtc_clock    cc.goodput_kbps                     842.2 -> 578.6
 REGRESSED  issue_897_libwebrtc_clock    cc.max_settle_ms                  1.6e+04 -> 3e+04
 REGRESSED  issue_897_libwebrtc_clock    cc.target_avg_kbps                   1294 -> 807.2
 REGRESSED  issue_897_libwebrtc_clock    cc.target_max_drop_pct                  0 -> 16.9
 REGRESSED  issue_897_libwebrtc_clock    cc.utilization_pct                  32.62 -> 22.41
  improved  issue_897_libwebrtc_clock    repair.overhead_pct                 51.68 -> 36.91
  improved  issue_897_libwebrtc_clock    sync.av_offset_p50_ms              -3.138 -> -31.2
 REGRESSED  issue_897_libwebrtc_clock    video.avg_kbps                      742.5 -> 487.2
  improved  issue_897_libwebrtc_clock    video.encode_height_mean            294.1 -> 198
  improved  issue_897_libwebrtc_clock    video.encoder_target_avg_kbps       735.4 -> 485.9
  improved  issue_897_libwebrtc_clock    video.longest_freeze_ms             31.83 -> 2.403
    FAILED  issue_897_libwebrtc_clock    expectations                            - -> -
 REGRESSED  issue_942_dlsr_compact_ntp   video.undecodable_frames                0 -> 12
 REGRESSED  outage_3s                    video.undecodable_frames                0 -> 4
 REGRESSED  satellite                    video.undecodable_frames                0 -> 2
 REGRESSED  very_low_capacity            audio.jb_delay_p50_ms                 720 -> 880
 REGRESSED  very_low_capacity            audio.jb_delay_p95_ms                1740 -> 2330
 REGRESSED  very_low_capacity            audio.mouth_to_ear_p95_ms            2073 -> 2664
 REGRESSED  very_low_capacity            audio.stretch_pct                   6.608 -> 8.235
 REGRESSED  very_low_capacity            cc.send_over_target_ms               6200 -> 7000
 REGRESSED  very_low_capacity            repair.overhead_pct                 27.48 -> 32.9
 REGRESSED  very_low_capacity            sync.av_offset_max_ms                6814 -> 7457
 REGRESSED  very_low_capacity            sync.av_offset_p95_ms                5856 -> 6692
 REGRESSED  very_low_capacity            video.e2e_delay_p50_ms               4254 -> 4622
 REGRESSED  very_low_capacity            video.e2e_delay_p95_ms               7451 -> 8382
 REGRESSED  very_low_capacity            video.encoder_dropped                 436 -> 501
  improved  very_low_capacity            video.frames_rendered                 377 -> 439
 REGRESSED  very_low_capacity            video.freeze_count                     23 -> 27
 REGRESSED  very_low_capacity            video.freeze_total_ms           2.475e+04 -> 3.26e+04
  improved  very_low_capacity            video.pause_total_ms                 8461 -> 0
  improved  very_low_capacity            video.render_fps                    9.425 -> 10.97
  improved  very_low_capacity            video.render_ratio_pct              49.35 -> 62.8
 REGRESSED  very_low_capacity            video.undecodable_frames                0 -> 39
 REGRESSED  very_low_capacity_allocator_v2 audio.jb_delay_p50_ms                 720 -> 880
 REGRESSED  very_low_capacity_allocator_v2 audio.jb_delay_p95_ms                1740 -> 2330
 REGRESSED  very_low_capacity_allocator_v2 audio.mouth_to_ear_p95_ms            2073 -> 2664
 REGRESSED  very_low_capacity_allocator_v2 audio.stretch_pct                   6.608 -> 8.235
 REGRESSED  very_low_capacity_allocator_v2 cc.send_over_target_ms               6200 -> 7000
 REGRESSED  very_low_capacity_allocator_v2 repair.overhead_pct                 27.48 -> 32.9
 REGRESSED  very_low_capacity_allocator_v2 sync.av_offset_max_ms                6814 -> 7457
 REGRESSED  very_low_capacity_allocator_v2 sync.av_offset_p95_ms                5856 -> 6692
 REGRESSED  very_low_capacity_allocator_v2 video.e2e_delay_p50_ms               4254 -> 4622
 REGRESSED  very_low_capacity_allocator_v2 video.e2e_delay_p95_ms               7451 -> 8382
 REGRESSED  very_low_capacity_allocator_v2 video.encoder_dropped                 436 -> 501
  improved  very_low_capacity_allocator_v2 video.frames_rendered                 377 -> 439
 REGRESSED  very_low_capacity_allocator_v2 video.freeze_count                     23 -> 27
 REGRESSED  very_low_capacity_allocator_v2 video.freeze_total_ms           2.475e+04 -> 3.26e+04
  improved  very_low_capacity_allocator_v2 video.pause_total_ms                 8461 -> 0
  improved  very_low_capacity_allocator_v2 video.render_fps                    9.425 -> 10.97
  improved  very_low_capacity_allocator_v2 video.render_ratio_pct              49.35 -> 62.8
 REGRESSED  very_low_capacity_allocator_v2 video.undecodable_frames                0 -> 39
 REGRESSED  wifi_to_lte_handover         audio.jb_delay_p95_ms                 260 -> 300
 REGRESSED  wifi_to_lte_handover         audio.late_discards                    25 -> 30
 REGRESSED  wifi_to_lte_handover         audio.mouth_to_ear_p95_ms           386.4 -> 550.9
 REGRESSED  wifi_to_lte_handover         audio.net_delay_p95_ms              126.4 -> 250.9
 REGRESSED  wifi_to_lte_handover         cc.max_settle_ms                      700 -> 800
 REGRESSED  wifi_to_lte_handover         cc.queue_p95_ms                     43.14 -> 174.5
 REGRESSED  wifi_to_lte_handover         cc.send_over_target_ms                  0 -> 1600
 REGRESSED  wifi_to_lte_handover         cc.send_over_target_p95_pct         21.65 -> 38.4
 REGRESSED  wifi_to_lte_handover         cc.target_max_drop_pct              70.68 -> 78.37
 REGRESSED  wifi_to_lte_handover         sync.av_offset_p50_ms               31.61 -> 65.34
 REGRESSED  wifi_to_lte_handover         sync.av_offset_p95_ms                1101 -> 1714
 REGRESSED  wifi_to_lte_handover         video.delay_jitter_buffer_p95_ms      533.2 -> 613.2
  improved  wifi_to_lte_handover         video.delay_pacer_p95_ms            892.1 -> 37.7
 REGRESSED  wifi_to_lte_handover         video.e2e_delay_p50_ms                358 -> 394.2
 REGRESSED  wifi_to_lte_handover         video.e2e_delay_p95_ms               1427 -> 2133
  improved  wifi_to_lte_handover         video.encode_height_mean            585.9 -> 488.4
 REGRESSED  wifi_to_lte_handover         video.encoder_dropped                  44 -> 69
 REGRESSED  wifi_to_lte_handover         video.freeze_total_ms                3404 -> 4374
  improved  wifi_to_lte_handover         video.time_to_first_frame_ms          276 -> 175

301 regression(s), 222 improvement(s)
```

</details>
