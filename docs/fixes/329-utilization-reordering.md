---
issue: 329
title: Test bench cc.utilization_pct reads 133% on a clean 8 Mbps link with reordering
severity: sev:3
status: fixed
area: area:cc, area:testbench
regression_tests: testbench/scenarios/issue_329_utilization_reordering.scn, tests/unit/testbench/testbench_metrics_test.cc
---

# #329: Test bench cc.utilization_pct reads 133% on a clean 8 Mbps link with reordering

## Issue

On an 8 Mbps link with 5 ms jitter, reordering and no loss, the test bench reported `cc.utilization_pct` at 130-134% (seeds 1-5) and `cc.goodput_kbps` 3431, against about 2560 kbps of new media. https://github.com/aray4702/streaming_media/issues/329. Found by the Tester (coverage area 5) on 1e2de24.

## Root cause

This is a test bench metric bug. The library is not at fault. `testbench/call.cc` added the wire bytes of every audio, video, RTX and FEC packet delivered to the receiver to `w_good`, and that included RTX copies of packets that had already arrived. With reordering, the receiver NACKs packets that are only late, as libwebrtc's `NackRequester` does, and their resends were counted as a second delivery of the same media. `cc.utilization_pct` divides that goodput by `min(capacity, demand)`, so it went above 100%. `sm_compare`'s libwebrtc side (`compare/webrtc/webrtc_call.cc`) counted the same way (every delivered packet with a payload), so both stacks over-read alike.

Is it a resend difference against libwebrtc? No. libwebrtc resends about as much on reordering: on `high_jitter` (40 ms jitter with reordering), the `main` comparison gives `net.rtx_kbps` 283 for us against 266 for libwebrtc, and `repair.overhead_pct` 40.5 against 43.7, within the parity band. So only the metric needed fixing.

## Fix

`testbench::FirstDelivery` (`testbench/call_common.h`) counts each media RTP sequence number once per stream (audio, and video including FEC on the video SSRC), at its first delivery as the original or as an RTX copy (the RFC 4588 original sequence number). A copy whose original had already arrived is not goodput: a resend after reordering, RTX payload padding, or a link duplicate. Both stacks use it: the test bench in `RunCall`, and `sm_compare`'s libwebrtc side by SSRC. An RTX copy of a lost packet is still counted, because it delivers the media for the first time. `testbench/README.md` describes the definition.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_329_utilization_reordering.scn` | `cc.utilization_pct` 133.7 (expect <= 100) | 99.14 |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.FirstDeliveryCountsEachSequenceNumberOnce` | new helper | passes |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.UtilizationExcludesResendsOfPacketsThatArrived` | utilization above 100 | passes |

## Results

The reproduction scenario across seeds (before -> after):

| seed | metric | before | after |
|---|---|---|---|
| 1 | cc.utilization_pct | 132.9 | 99.14 |
| 2 | cc.utilization_pct | 133.8 | 99.37 |
| 3 | cc.utilization_pct | 129.6 | 97.06 |
| 4 | cc.utilization_pct | 132.4 | 99.28 |
| 5 | cc.utilization_pct | 133.3 | 98.27 |

(The "before" values are the Tester's. Seed 1 measured here on `main` reads 133.7.)

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`). Only `cc.goodput_kbps` and `cc.utilization_pct` change. They move on 89 of the 97 existing scenarios, but beyond the compare tolerance only on the 18 with reordering, link duplication or RTX payload padding, which are listed below. Elsewhere the change is small (RTX payload padding, and resends of packets that arrived late). No other metric moves, because this is a measurement change. The lower values are the definition change and not a regression: goodput now equals the new media delivered (for example `high_jitter`: 888 kbps goodput, against 820 video + 70 audio sent).

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| high_jitter | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| high_jitter | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| issue_120_rtx_payload_padding | cc.goodput_kbps | 1796 | 1466 | REGRESSED |
| issue_120_rtx_payload_padding | cc.utilization_pct | 107.3 | 87.57 | REGRESSED |
| issue_127_all_v2_no_stall | cc.goodput_kbps | 941.6 | 713.9 | REGRESSED |
| issue_127_all_v2_no_stall | cc.utilization_pct | 78.46 | 59.49 | REGRESSED |
| issue_127_pacer_v2_jitter_reorder_loss | cc.goodput_kbps | 1147 | 831.9 | REGRESSED |
| issue_127_pacer_v2_jitter_reorder_loss | cc.utilization_pct | 44.42 | 32.22 | REGRESSED |
| issue_12_lossbwe_high_jitter_no_loss | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| issue_12_lossbwe_high_jitter_no_loss | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| issue_150_gcc_v2_high_jitter | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| issue_150_gcc_v2_high_jitter | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed10 | cc.goodput_kbps | 1214 | 834.9 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed10 | cc.utilization_pct | 47.04 | 32.34 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed5 | cc.goodput_kbps | 1080 | 749.7 | REGRESSED |
| issue_150_gcc_v2_high_jitter_seed5 | cc.utilization_pct | 41.85 | 29.04 | REGRESSED |
| issue_19_cc_v2_high_jitter | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| issue_19_cc_v2_high_jitter | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| issue_23_handover | cc.goodput_kbps | 1796 | 1466 | REGRESSED |
| issue_23_handover | cc.utilization_pct | 107.3 | 87.57 | REGRESSED |
| issue_24_timing_v2_high_jitter | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| issue_24_timing_v2_high_jitter | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| issue_329_utilization_reordering | (new scenario) | - | - | new |
| issue_62_neteq_v2_high_jitter | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| issue_62_neteq_v2_high_jitter | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| issue_76_packetization_v2 | cc.goodput_kbps | 2399 | 1875 | REGRESSED |
| issue_76_packetization_v2 | cc.utilization_pct | 92.92 | 72.64 | REGRESSED |
| issue_76_rtx_v2_handover | cc.goodput_kbps | 1796 | 1466 | REGRESSED |
| issue_76_rtx_v2_handover | cc.utilization_pct | 107.3 | 87.57 | REGRESSED |
| issue_76_session_layer_v2 | cc.goodput_kbps | 1991 | 1476 | REGRESSED |
| issue_76_session_layer_v2 | cc.utilization_pct | 77.11 | 57.18 | REGRESSED |
| issue_76_tcc_v2_high_jitter | cc.goodput_kbps | 1302 | 887.7 | REGRESSED |
| issue_76_tcc_v2_high_jitter | cc.utilization_pct | 50.43 | 34.38 | REGRESSED |
| satellite | cc.goodput_kbps | 1261 | 1097 | REGRESSED |
| satellite | cc.utilization_pct | 84.04 | 73.16 | REGRESSED |
| wifi_to_lte_handover | cc.goodput_kbps | 1796 | 1466 | REGRESSED |
| wifi_to_lte_handover | cc.utilization_pct | 107.3 | 87.57 | REGRESSED |
