---
issue: 410
title: "Loss bursts: behind libwebrtc on 8-10 of 10 seeds under the 7/10 line (burst15 queue p95, burst_loss audio JB p95, ULPFEC freeze total) (from #406)"
severity: sev:3
status: partial
area: area:testbench, area:cc
regression_tests: testbench/scenarios/issue_410_queue_sample_phase.scn, tests/unit/testbench/testbench_metrics_test.cc
---

# #410: Loss bursts behind libwebrtc (first cluster: burst15 queue p95)

## Issue

On `issue_5_burst15_freeze` (2 Mbps, 2 % loss in 15-packet bursts) our bottleneck queue p95 was 50.5 ms against
libwebrtc's 36.7 ms (mean of 10 seeds, `parity-data` @ fe3f555), higher on 10 of 10 seeds. #410 groups it with
two other loss-burst cells (`burst_loss` audio jitter-buffer p95, `issue_29_ulpfec_over_red` freeze total); the
coordinator asked for one PR per cluster. This record covers the queue cluster:
https://github.com/aray4702/streaming_media/issues/410.

## Root cause

The difference was in the test bench's measurement, not in the library. Both harnesses sample the
bottleneck queue every 10 ms with `QueueDelayMs` (`testbench/call_common.h`), but at different points of the
millisecond:

- `testbench/call.cc` sampled it in the tick's "Measurement" block, after `put_on_link` had put the call's
  packets of that millisecond on the forward link.
- `compare/webrtc/webrtc_call.cc` samples it inside `tick(t)` on the network task queue. libwebrtc's encoder,
  pacer and transport run in tasks of their own that the simulated clock runs after `tick(t)`, so the sample
  sees the link before that millisecond's packets (the tick's cross traffic is sent synchronously and is in it).

Opus packets leave at 10, 30, 50 ms, all on sample points, and so does every third 30 fps frame (0, 100, 200
ms). Our samples counted those packets' own bytes and libwebrtc's did not.

The published seed-1 forward-link traces (`traces/issue_5_burst15_freeze.7.{sm,webrtc}.trace.csv`) show this
exactly. Rebuilding the 10 ms samples from each trace at a fixed 2 Mbps capacity gives:

| trace | packets of the sample's ms included | excluded | metric reported |
|---|---|---|---|
| ours | **37.96** | 32.75 | 37.96 |
| libwebrtc | 40.54 | **35.64** | 35.64 |

Measured the same way, libwebrtc's queue on that seed is the higher one.

## Fix

`testbench/call.cc` samples the queue after the tick's cross traffic and before `sender.Process` /
`put_on_link`, the same phase as sm_compare (the order of the cross traffic and the call's packets was matched
in #390). The 100 ms timeseries `queue_ms` uses the same sample. Nothing in the library changes. The only
metrics that move are `cc.queue_p95_ms`, `cc.queue_max_ms` and `cc.queue_over_200ms_pct`. They go down in 245
places and up in none.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_410_queue_sample_phase.scn` (audio-only call on an idle 200 kbps link) | `cc.queue_p95_ms` 8.64 (expect < 2): one Opus packet's serialization time | 0 |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.QueueIsSampledBeforeTheCallsPacketsOfTheSameMillisecond` | the p95 rebuilt from the link trace without the sample's own millisecond differs from the metric, and the metric is not < 0.5 | equal to 1e-6; 0 |

## Results

`issue_5_burst15_freeze`, `cc.queue_p95_ms` per seed (7-16, the seeds `sm_compare` runs), our side re-run
locally, and libwebrtc's from `parity-data` @ fe3f555:

| seed | before | after | libwebrtc |
|---|---|---|---|
| 7 | 38.0 | 32.8 | 35.6 |
| 8 | 67.2 | 60.4 | 28.2 |
| 9 | 40.0 | 34.0 | 26.9 |
| 10 | 62.5 | 56.2 | 47.8 |
| 11 | 61.5 | 59.4 | 37.7 |
| 12 | 51.0 | 47.4 | 36.4 |
| 13 | 46.7 | 42.0 | 38.3 |
| 14 | 50.3 | 42.7 | 35.7 |
| 15 | 40.6 | 33.4 | 33.5 |
| 16 | 47.5 | 44.0 | 47.0 |
| mean | 50.5 | **45.2** | 36.7 (band ±10: in band) |

The parity report, rebuilt from `main`'s `compare-webrtc.json` with our `cc.queue_p95_ms` re-run on every
scenario's seeds (1360 runs; the other shown metrics reproduced the published values exactly):

- `cc.queue_p95_ms` gets closer to libwebrtc's on 97 of 113 scenarios and further on 16. The mean |ours -
  libwebrtc| goes from 10.3 to 7.4 ms. The mean signed difference goes from +3.1 to -1.8 ms.
- Gaps: 55 -> 57, 1 closed, 3 opened. The burst15 queue cell closes. `low_bandwidth`,
  `issue_23_low_bandwidth` and `issue_48_prober_low_bandwidth` `cc.queue_p95_ms` open as **ahead** (288 vs
  389, band ±97.8). Before, they were 297 vs 389, 1 ms inside the band. #411 owns them (the ahead clusters).
  CI's `compare-webrtc` job for this PR gives the authoritative report.

The scenario suite against the baseline (`tools/testbench_compare.py`): 0 regressions. Every
`cc.queue_p95_ms` change of 3 ms or more:

| scenario | cc.queue_p95_ms baseline | this PR |
|---|---|---|
| audio_only_lossy | 3.489 | 0 |
| bufferbloat | 56.14 | 51.85 |
| burst_loss | 44.5 | 38.57 |
| capacity_drop | 287.2 | 277.7 |
| clean_link | 76.26 | 73.06 |
| high_jitter | 7.897 | 3.713 |
| issue_120_rtx_payload_padding | 183.8 | 174 |
| issue_120_rtx_payload_padding_capacity_drop | 287.2 | 277.7 |
| issue_123_v2_allocation_min | 296.5 | 286.7 |
| issue_124_bufferbloat_v2 | 56.14 | 51.85 |
| issue_124_burst_loss_v2 | 44.5 | 38.57 |
| issue_124_clean_link_v2 | 76.26 | 73.06 |
| issue_126_audio_v2 | 17.25 | 13.48 |
| issue_127_all_v2_no_stall | 25.24 | 20.7 |
| issue_127_pacer_v2_jitter_reorder_loss | 4.882 | 1.198 |
| issue_12_lossbwe_burst_loss | 44.5 | 38.57 |
| issue_12_lossbwe_congestive | 47.5 | 39.55 |
| issue_12_lossbwe_high_jitter_no_loss | 7.897 | 3.713 |
| issue_12_lossbwe_policer | 5.174 | 0 |
| issue_12_lossbwe_random_loss | 35.98 | 32.02 |
| issue_12_lossbwe_shallow | 16.39 | 12.25 |
| issue_12_lossbwe_shallow_step | 16.75 | 12.32 |
| issue_133_all_v2_outage_15s | 29.17 | 25.94 |
| issue_133_all_v2_outage_3s | 30.11 | 26.78 |
| issue_133_pacer_v2_outage_15s | 29.17 | 25.94 |
| issue_133_pacer_v2_outage_3s | 30.11 | 26.78 |
| issue_150_gcc_v2_high_jitter | 7.897 | 3.713 |
| issue_150_gcc_v2_high_jitter_seed10 | 10.89 | 7.213 |
| issue_150_gcc_v2_high_jitter_seed5 | 3.848 | 0 |
| issue_151_outage_3s_v2 | 50.01 | 44.98 |
| issue_154_shallow_drop_repair_v2 | 16.17 | 10.66 |
| issue_15_aimd_v2_clean_link | 76.26 | 73.06 |
| issue_15_aimd_v2_random_loss | 35.98 | 32.02 |
| issue_18_random_loss_keyframes | 35.98 | 32.02 |
| issue_19_cc_v2_high_jitter | 7.897 | 3.713 |
| issue_21_shallow_drop_repair | 16.17 | 10.66 |
| issue_224_congestive_e2e_flip | 47.5 | 39.55 |
| issue_23_capacity_drop | 287.2 | 277.7 |
| issue_23_handover | 183.8 | 174 |
| issue_23_low_bandwidth | 346.1 | 336.4 |
| issue_23_outage_3s | 50.01 | 44.98 |
| issue_23_outage_500ms | 49.44 | 45.8 |
| issue_24_timing_v2_clean_link | 76.26 | 73.06 |
| issue_24_timing_v2_default | 83.73 | 75.99 |
| issue_24_timing_v2_high_jitter | 7.897 | 3.713 |
| issue_24_timing_v2_render_fps | 83.73 | 75.99 |
| issue_256_audio_24khz | 35.17 | 31.71 |
| issue_268_freeze_rule_outage | 30.11 | 26.78 |
| issue_283_longest_freeze_no_metrics | 54.25 | 50.1 |
| issue_298_audio_24khz_plays | 35.17 | 31.71 |
| issue_29_ulpfec_over_red | 37.54 | 28.72 |
| issue_331_low_bandwidth_av_offset_seed4 | 436 | 426 |
| issue_335_fps1_keyframe_packets | 55.13 | 45.13 |
| issue_346_reconfigure_capacity_drop | 287.2 | 277.7 |
| issue_348_scaling_outage_500ms | 49.44 | 45.8 |
| issue_355_rate_tracker_default | 31.76 | 28.64 |
| issue_355_rate_tracker_ramp | 31.76 | 28.64 |
| issue_356_capacity_drop_xr_rtt | 287.2 | 277.7 |
| issue_368_tracker_outage_render | 30.11 | 26.78 |
| issue_48_prober_low_bandwidth | 346.1 | 336.4 |
| issue_53_cwnd_pushback | 83.73 | 75.99 |
| issue_5_bitrate_adjuster | 43.35 | 35.24 |
| issue_5_burst15_freeze | 37.96 | 32.75 |
| issue_5_keyframe_flush | 295.7 | 287.1 |
| issue_61_googcc_glue_congestive | 47.5 | 39.55 |
| issue_62_neteq_default | 3.489 | 0 |
| issue_62_neteq_v2_audio_only_lossy | 3.489 | 0 |
| issue_62_neteq_v2_high_jitter | 7.897 | 3.713 |
| issue_76_packetization_v2 | 21.1 | 17.24 |
| issue_76_rtx_v2_handover | 183.8 | 174 |
| issue_76_rtx_v2_keyframe_flush | 287.7 | 277.9 |
| issue_76_tcc_v2_high_jitter | 7.897 | 3.713 |
| issue_77_av_sync_clean_link | 76.26 | 73.06 |
| issue_7_no_fec_beside_nack | 35.98 | 32.02 |
| issue_84_encoder_qp_360p_low_rate | 68.43 | 63.42 |
| issue_84_encoder_qp_720p_low_rate | 68.43 | 63.42 |
| issue_84_quality_scaling_720p_low_rate | 66.74 | 62.16 |
| issue_84_quality_scaling_rate_rise | 52.92 | 47.9 |
| issue_85_video_pause | 58.63 | 53.3 |
| lossy_feedback | 50.98 | 47.19 |
| low_bandwidth | 346.1 | 336.4 |
| low_fps_clean_link | 88.68 | 82.97 |
| outage_3s | 50.01 | 44.98 |
| pacer_v2_capacity_drop | 287.2 | 277.7 |
| probe_v2_capacity_drop | 287.2 | 277.7 |
| probe_v2_clean_link | 76.26 | 73.06 |
| random_loss_5pct | 35.98 | 32.02 |
| satellite | 42.38 | 37.62 |
| very_low_capacity | 296.5 | 286.7 |
| very_low_capacity_allocator_v2 | 296.5 | 286.7 |
| wifi_to_lte_handover | 183.8 | 174 |

## Remaining

- **burst15 queue, per seed:** ours is still higher on 7 of 10 seeds. The largest are seeds 8, 10 and 11. Our
  traces of seeds 8 and 11 show an overuse late in the call:
  - Seed 8: a 15-packet loss burst at 20.9 s lowers the acked rate just before an overuse at 21.6 s. The
    decrease lands below the link-capacity estimate's lower bound, and `AimdRateControl` resets the estimate.
    The next ramp is multiplicative (about 8 %/s instead of about 20 kbps/s) and overshoots the 2 Mbps link to
    2.17 Mbps at 27 s.
  - Seed 11: an additive climb to 2.09 Mbps, then an overuse at 26.9 s. libwebrtc's seed-7 trace has the same
    kind of late overuse, at 25.3 s.

  Our `AimdRateControl` and `LinkCapacityEstimator` match `remote_bitrate_estimator/aimd_rate_control.cc` and
  `goog_cc/link_capacity_estimator.cc` line for line. The loss bursts also land on different packets in the two
  stacks (`net.loss_pct` differs per seed). So whether this is seed sampling needs libwebrtc's bwe traces for
  seeds 8, 10 and 11, which are not published (only the first seed is traced), or a many-seed check as in #424.
- **`burst_loss` audio jitter-buffer p95** (194 vs 174) and **ULPFEC freeze total** (22.6 vs 19.2 s): not
  touched here. These are separate PRs, as the coordinator asked on #410.
