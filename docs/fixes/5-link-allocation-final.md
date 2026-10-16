---
issue: 5
title: Video freezes up to 1.5 s with 2% loss in 15-packet bursts (link_allocation, adjuster on by default, measured input frame rate; final PR)
severity: sev:2
status: fixed
area: area:loss, area:video
regression_tests: testbench/scenarios/issue_5_adjuster_default.scn, tests/unit/rtp/video_send_controller_test.cc
---

# #5: Video freezes up to 1.5 s with 2% loss in 15-packet bursts

## Issue

With 2% loss in ~15-packet bursts video froze for up to 1.5 s (Tester). The Owner
re-scoped the issue as stage-1 parity work on the encoder side
(`VideoStreamEncoder`, `PacingController`). PR 1 ported the `FrameDropper`
(`5-frame-dropper-port.md`), PR 2 the `EncoderBitrateAdjuster`
(`5-encoder-bitrate-adjuster.md`), PR 3 the pacer's keyframe flush
(`5-pacer-keyframe-flush.md`). This final PR does the three remaining items from
the Owner's comment of 2026-10-16 15:14.
https://github.com/aray4702/streaming_media/issues/5

## Root cause

1. `link_allocation`: ours was `max(target, rates.target - audio)`, which gave the
   adjuster headroom that libwebrtc's does not have. `VideoSendStreamImpl::OnBitrateUpdated`
   (`compare/webrtc/reference/video/video_send_stream_impl.cc` lines 896-920)
   computes `link_allocation = max(encoder_target, encoder_target - protection)`,
   which is the encoder target, and `VideoStreamEncoder::UpdateBitrateAllocation`
   (`video_stream_encoder.cc` ~1728) only raises it to `sum(allocation)`.
   The extra headroom changed how the adjuster shares the overshoot between the encoder rate and the link, so PR 2
   (which measured on one seed) saw clean links lose 4-8% of video rate with it on; over 10 seeds with the libwebrtc
   allocation the clean-link change is within noise.
2. Input frame rate: the frame dropper and the adjuster got the configured rate.
   libwebrtc passes `GetInputFramerateFps()` (`video_stream_encoder.cc` 1746), the
   rate `FrameCadenceAdapter` measures with `RateStatistics` over 2970 ms
   (`frame_cadence_adapter.cc` 78-100, `frame_cadence_adapter.h` 39).
3. Keyframe budget: `VideoStreamEncoder` has none of its own. `SendKeyFrame`
   (`video_stream_encoder.cc` 2204) only sets `next_frame_types_`, which stays
   set until a keyframe is encoded, and the keyframe goes through the frame
   dropper like any frame (`RunPostEncode`: `Fill(size, !keyframe)`). That is
   what `VideoSendController` already does (sticky `keyframe_needed_`, keyframes
   through the dropper since PR 1), so nothing changes; the only throttle of keyframe requests is in
   `RTCPReceiver` (17 ms between PLI/FIR), which the session layer does not
   need yet (#23, #76).

## Fix

- `VideoSendController`: `link_allocation = encoder target`; input rate measured
  as `RateStatistics` does (`Rate()` before `Update()` per captured frame,
  rounded, configured maximum while unavailable or zero) and given to the
  dropper, the adjuster and `EncodeDecision::fps`; `bitrate_adjuster` defaults to
  on (`RateControlSettings`, M157). The scenario switch stays: `bitrate_adjuster off`.
- `FrameDropper::incoming_frame_rate()` accessor (tests).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_5_adjuster_default.scn` (seed 1; 10 seeds checked) | `cc.queue_p95_ms` 231 (expect < 200) | 167; seeds 1-10 117-196 |
| `VideoSendController.BitrateAdjusterIsOnByDefaultAsInM157`, `...InputFrameRateIsMeasuredNotConfigured`, `...LinkAllocationEqualsTheEncoderTarget` | n/a (new behaviour) | pass |
| `VideoSendController.BitrateAdjusterLowersTheEncoderRateNotTheDropperTarget` | unchanged except that the "off" controller now says `bitrate_adjuster = false` (the default changed) | passes |

## Results

Mean of 10 seeds of the 21 scenarios of the comparison, `main` (adjuster off) / this PR /
libwebrtc (`parity-data` `compare-webrtc.json`), selected cells:

| scenario | metric | main | this PR | libwebrtc |
|---|---|---|---|---|
| cross_traffic | `cc.queue_p95_ms` | 218.9 | 147.7 | 263.5 |
| cross_traffic | `video.e2e_delay_p95_ms` | 255.4 | 185.2 | 490.8 |
| cross_traffic | `video.avg_kbps` | 1611 | 1546 | 1779 |
| cross_traffic | `cc.utilization_pct` | 68.69 | 66.05 | 74.21 |
| capacity_drop | `video.freeze_total_ms` | 2922 | 2802 | 2538 |
| capacity_drop | `video.longest_freeze_ms` | 1810 | 1440 | 1875 |
| capacity_drop | `cc.queue_p95_ms` | 384 | 383.8 | 283.1 |
| random_loss_5pct | `video.avg_kbps` | 941.1 | 1043 | 1382 |
| random_loss_5pct | `cc.utilization_pct` | 55.25 | 60.84 | 77.5 |
| burst_loss | `video.freeze_total_ms` | 257.6 | 215.2 | 1432 |
| clean_link | `cc.utilization_pct` | 82.66 | 81.93 | 91.56 |
| clean_link | `video.avg_kbps` | 1892 | 1874 | 2142 |
| satellite | `video.longest_freeze_ms` | 915.5 | 583 | 837.8 |
| outage_3s | `video.freeze_total_ms` | 3497 | 3325 | 3222 |
| wifi_to_lte_handover | `video.freeze_total_ms` | 4132 | 4074 | 4528 |
| wifi_to_lte_handover | `video.freeze_count` | 3 | 2.4 | 4.3 |
| issue_21_shallow_drop_repair | `video.freeze_total_ms` | 5796 | 4350 | 7143 |
| low_bandwidth | `cc.rtt_p50_ms` | 283.2 | 197.2 | 505.1 |
| high_jitter | `cc.max_settle_ms` | 2.701e+04 | 3e+04 | 2.315e+04 |
| issue_18_lntf_not_pli | `video.e2e_delay_p95_ms` | 310.8 | 327.4 | 209.5 |
| wifi_to_lte_handover | `video.keyframe_requests` | 15 | 16.7 | 0 |

Counting every shown metric of the parity report where the mean moves by more than half its band: 62 cells move toward
libwebrtc and 25 away. Most of the 25 are cells where we were already ahead of libwebrtc and move further
(`cross_traffic` delays and queue, lower is better); the ones that really leave libwebrtc's value
are `cross_traffic` video rate (1611 -> 1546, libwebrtc 1779) and utilization (68.7 -> 66.1, 74.2),
`high_jitter` settle time (27.0 -> 30.0 s, libwebrtc 23.2 s), `issue_18_lntf_not_pli`/`issue_7`/`random_loss_5pct`
video delay p95 (311 -> 327 ms, libwebrtc 210) and the keyframe requests of `low_bandwidth`, `issue_53`
and `wifi_to_lte_handover` (+2 to +3 each, libwebrtc 0; owned by #23). With the adjuster off but the other two changes
the suite is unchanged (0 cells toward, 3 away, all within noise), so the movement is the adjuster's.
Turning it on therefore does not move the suite away from libwebrtc as a whole, which was the Owner's condition.

Suite against the baseline (`testbench_compare.py`, seed as in each scenario): 107 improved, 83 regressed; the
regressions are the same cells as above (single seed), plus `clean_link` and `burst_loss` goodput/utilization on seed 1,
which over 10 seeds are unchanged within noise (`clean_link` utilization 82.7 -> 81.9, libwebrtc 91.6). The baseline is updated.

## Remaining

None for the Owner's three items. Differences from libwebrtc that stay (PR list "Remaining differences"):
`RateUtilizationTracker` for the media factor (the detector's is used); rates are adjusted per captured frame,
not per bitrate update; one stream, one temporal layer; `RateStatistics` and `ExpFilter` written from the upstream
classes (not in `reference/`, #57). The issue's `freeze_count <= 2` is not reached; the freezes left are NACK/keyframe repair (#23).
