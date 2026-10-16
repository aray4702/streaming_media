---
issue: 5
title: Video freezes up to 1.5 s with 2% loss in 15-packet bursts (libwebrtc EncoderBitrateAdjuster port, PR 2)
severity: sev:2
status: partial
area: area:loss, area:video
regression_tests: testbench/scenarios/issue_5_bitrate_adjuster.scn, tests/unit/cc/encoder_bitrate_adjuster_test.cc, tests/unit/rtp/video_send_controller_test.cc
---

# #5: Video freezes up to 1.5 s with 2% loss in 15-packet bursts

## Issue

With 2% loss in ~15-packet bursts, video froze for up to 1.5 s with 4-7
freezes in 30 s (Tester). The Owner re-scoped it as stage-1 parity work: port
the encoder side of libwebrtc's `VideoStreamEncoder` into
`VideoSendController`. PR 1 (fix record `5-frame-dropper-port.md`) ported the
`FrameDropper`. This PR 2 ports `EncoderOvershootDetector` and
`EncoderBitrateAdjuster`.
https://github.com/aray4702/streaming_media/issues/5

## Root cause

The library had a simplified `EncoderOvershootDetector` (actual/target over a
2 s window, not wired into anything). libwebrtc's `VideoStreamEncoder` runs
`EncoderBitrateAdjuster` by default (`rate_control_settings.h`:
`bitrate_adjuster = true`, `adjuster_use_headroom = true`), so the encoder is
configured with a rate lowered by its overshoot, measured by virtual
network and media buffers, with link headroom paying for part of it. Without
it, keyframe bursts and noisy frame sizes go to the pacer at the target rate
and queue there.

## Fix

- `cc::EncoderOvershootDetector` is a line-by-line port of
  `compare/webrtc/reference/video/encoder_overshoot_detector.cc` (3 s window,
  network and media utilization factors, `kMaxMediaUnderrunFrames` 5).
- `cc::EncoderBitrateAdjuster` ports
  `video/encoder_bitrate_adjuster.cc` for one stream with one temporal layer
  (the only shape this library sends): `kMinFramesSinceLayoutChange` 30,
  `kDefaultUtilizationFactor` 1.2, link factor clamped to 1.0..2.0, wanted
  overshoot granted from `bandwidth_allocation - target`, minimum bitrate,
  `OnFrameDropped` for dropper drops, `Reset`.
- `VideoSendController` calls it as `VideoStreamEncoder` does: the dropper keeps
  the unadjusted target, the encoder gets the adjusted rate in
  `EncodeDecision::target`; encoded sizes and dropper drops are reported back.
  It is behind `VideoSendControllerConfig::bitrate_adjuster` and the scenario
  line `bitrate_adjuster on|off`, **off by default** (libwebrtc: on), see
  Remaining.
- The old simplified detector, its test and its bench use are replaced.

Differences from libwebrtc (all in `tools/catalog.py` 5.8 and the parity
inventory 5.6): the media factor comes from the detector instead of
`RateUtilizationTracker` (`rtc_base/rate_utilization_tracker.*`, not in
`reference/`); rates are adjusted per captured frame, not per bitrate update;
`bandwidth_allocation` is the session target minus audio, because
`VideoSendStreamImpl`, which computes libwebrtc's `link_allocation`, is not in
`reference/`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_5_bitrate_adjuster.scn` | the base `sm_testbench` rejects `bitrate_adjuster on` (unknown scenario line); there is no behaviour to lock in on the base | passes on seeds 1-5 (freeze 0 ms, queue max < 200 ms, e2e p95 < 250 ms) |
| `EncoderOvershootDetector.*` (5 cases) | n/a (new API) | first-frame size/ideal, on-target factor 1.0, 1.5x overshoot penalized, culling after 3 s, drops leave no update |
| `EncoderBitrateAdjuster.*` (8 cases) | n/a (new API) | 1.2 start, on-target left alone, clamp at half, headroom pays, no-headroom option, minimum bitrate, zero target, reset |
| `VideoSendController.BitrateAdjusterLowersTheEncoderRateNotTheDropperTarget` | n/a (new) | passes |

## Results

With `bitrate_adjuster` off (the default) every existing scenario is
unchanged: `testbench_compare.py` reports 0 regressions, 0 improvements, plus
the new scenario.

With it **on**, mean of 10 seeds, main / adjuster on / libwebrtc (`parity-data`,
`compare-webrtc.json` of the latest `main` run):

| scenario | metric | main | adjuster on | libwebrtc |
|---|---|---|---|---|
| clean_link | cc.utilization_pct | 84.8 | 78.0 | 91.6 |
| clean_link | video.avg_kbps | 1943 | 1780 | 2141 |
| burst_loss | cc.utilization_pct | 69.2 | 63.6 | 80.4 |
| satellite | cc.utilization_pct | 54.2 | 52.6 | 82.4 |
| low_bandwidth | cc.utilization_pct | 96.3 | 94.6 | 98.7 |
| capacity_drop | cc.utilization_pct | 49.9 | 54.5 | 57.5 |
| capacity_drop | video.freeze_total_ms | 2929 | 2812 | 2538 |
| outage_3s | cc.utilization_pct | 66.8 | 70.0 | 89.1 |
| outage_3s | video.freeze_total_ms | 3600 | 3153 | 3222 |
| wifi_to_lte_handover | video.keyframe_requests | 19 | 14 | 0 |
| cross_traffic | cc.utilization_pct | 67.1 | 68.9 | 74.2 |
| high_jitter | send_over_target_ms | 16900 | 25200 | 10 |

Of the 100 (scenario x metric) cells compared, 38 move toward libwebrtc and the
rest away or are unchanged. The outage / capacity-drop family improves; clean
and lossy links lose 4 to 8% of video rate, which moves away from libwebrtc.

## Remaining

- **Why it is off.** On a clean link our `link_allocation` has no headroom:
  `video_target = (target - audio) x (1 - repair share)`, so the 4.7% link
  factor the model encoder produces (+-20% frame sizes, keyframes 4x) lowers the
  rate. In libwebrtc the allocation to the stream includes the per-packet
  overhead rate and the headroom covers it, so the same factor leaves the
  rate alone. That needs `video/video_send_stream_impl.cc` (computes
  `link_allocation`) and `rtc_base/rate_utilization_tracker.*`, which are not
  in `reference/`; requested on #57. The flip of the default is a follow-up
  once they are there and the per-packet overhead is modelled (#76).
- Keyframe budget as `VideoStreamEncoder`, the pacer's keyframe flush
  (`pacer v2`) and a measured input frame rate stay open in this item.
- The issue's `freeze_count <= 2` is not reached (tied to #23).
