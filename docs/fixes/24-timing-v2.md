---
issue: 24
title: Video delay p95 1.5 s under 40 ms jitter with reordering while network queueing is under 100 ms; libwebrtc 142 ms
severity: sev:2
status: partial
area: area:timing, area:video
regression_tests: testbench/scenarios/issue_24_timing_v2_high_jitter.scn, testbench/scenarios/issue_24_timing_v2_render_fps.scn, tests/unit/timing/vcm_timing_test.cc
---

# #24: Video delay p95 1.5 s under 40 ms jitter with reordering; libwebrtc 142 ms

## Issue

On the high-jitter link (40 ms jitter with reordering, no loss) video arrived
1.5 s late at p95, against libwebrtc's 142 ms
(https://github.com/aray4702/streaming_media/issues/24, split from #19). The
Owner widened the item on 2026-10-20 to porting libwebrtc's video receive
timing as a whole, behind a `timing v1|v2` switch. Its targets:
`issue_53_cwnd_pushback` render fps (15-17 with every v2 switch on, against
libwebrtc's 27.5), `issue_76_tcc_v2_high_jitter` e2e delay p95 (restore
`< 300`), `issue_151_outage_3s_v2` seeds 2 and 3, and `clean_link` video e2e
delay (78 against 120 ms).

## Root cause

Two receive-side mechanisms differ from libwebrtc M157:

- **Late-frame dropping.** `video::FrameScheduler::Process`
  (`src/video/frame_scheduler.cc:34-44`) skips rendering a frame that is more
  than 10 ms late whenever a newer frame is due. libwebrtc's
  `VideoStreamBufferController::MaybeScheduleFrameForRelease`
  (`video/video_stream_buffer_controller.cc`) asks
  `FrameDecodeTiming::OnFrameBufferUpdated` (`video/frame_decode_timing.cc:45`),
  which fast-forwards past a frame only if it is 5 ms late **and** it is not
  the last decodable temporal unit. In a stream where every delta frame
  references the one before it (H.264 without a descriptor here, the generic
  codec in `sm_compare`), `FrameBuffer::FindNextAndLastDecodableTemporalUnit`
  (`api/video/frame_buffer.cc`) never finds a later decodable unit unless a
  keyframe is queued, so libwebrtc decodes and renders late frames.
- **Delay model.** `timing::VideoJitterEstimator` uses 3 sigma with no offset
  and `VideoTiming` moves its current delay toward the target at 10% per
  second. libwebrtc's `JitterEstimator` (`video/timing/jitter_estimator.cc`)
  uses 2.33 sigma - 30 ms plus 10 ms OS jitter, an RTT term after three
  NACKed frames, a frame-rate scale, and `VCMTiming::UpdateCurrentDelay`
  (`modules/video_coding/timing/timing.cc`), whose current delay only grows,
  by how late a frame was released. Render times come from
  `TimestampExtrapolator`'s least-squares fit, not a lower envelope.

## Fix

`timing.version = 2` in `rtp::RtpReceiverConfig` (the test bench's
`timing v2`, default v1) replaces both with ports that keep libwebrtc's
constants and defaults:

- `timing::VcmTiming` (`include/sm/timing/vcm_timing.h`): `JitterEstimator`,
  `FrameDelayVariationKalmanFilter`, `InterFrameDelayVariationCalculator`,
  `RttFilter`, `TimestampExtrapolator`, `DecodeTimePercentileFilter` and
  `VCMTiming` with `DefaultVideoJitterTiming` folded in. Every call takes
  `now`. libwebrtc's millisecond rounding (`TimeDelta::ms()`) is kept.
- `timing::FrameReleaseController` (`include/sm/timing/frame_release.h`): the
  release half of `VideoStreamBufferController` and `FrameDecodeTiming`,
  including the bad-render-timing reset, the 3 s frame time-out bound on the
  wait, the low-latency path, and the keyframe-required release at the start.
- `loss::NackTracker::OnReceivedPacket` returns how often the packet was
  NACKed (as `NackRequester::OnReceivedPacket`), so a frame knows it was
  `delayed_by_retransmission`. Such frames skip the jitter update (they count
  as NACKed frames instead) and the current-delay update, as in libwebrtc.
- RTT reaches the jitter estimator once a second, as `CallStats` calls
  `VideoReceiveStream2::OnRttUpdate`.

Remaining differences, all listed in `include/sm/timing/frame_release.h`:
`FrameHasBadRenderTiming` and `TargetVideoDelayIsTooLarge`
(`video/frame_helpers.cc`) are not in `compare/webrtc/reference/`, so their
10 s bounds follow `kMaxVideoDelay` from memory (noted on #57); the decoder
is modelled as ready again as soon as a frame is released; field-trial
overrides are not ported.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_24_timing_v2_render_fps.scn` | base commit: `timing v2` is not a switch (exit 1). With `timing v1`: `video.render_fps` 16.0-20.8 on seeds 1-10 (expect >= 28) | 29.0-29.4 on seeds 1-10 |
| `testbench/scenarios/issue_24_timing_v2_high_jitter.scn` | base commit: exit 1. With `timing v1`: `video.e2e_delay_p50_ms` 158-245 (expect < 120), p95 162-313 (expect < 250) | p50 83-98, p95 130-199 on seeds 1-10 |
| `tests/unit/timing/vcm_timing_test.cc` `VcmTiming.*` (28 cases) | new | pass. Ported from libwebrtc's `jitter_estimator`, `rtt_filter`, `timestamp_extrapolator`, `timing` and `decode_time_percentile_filter` unit tests (`JitterSteadyStateConvergence` gives libwebrtc's 54 ms), plus release-rule tests |

## Results

Seeds 1-10, local runs on this branch; libwebrtc from `parity-data`
`compare-webrtc.json` (main be4bfbc, 10 seeds).

| scenario (switches) | metric | timing v1 | timing v2 | libwebrtc |
|---|---|---|---|---|
| `issue_53_cwnd_pushback` link, all v2 | `video.render_fps` mean | 18.2 | 29.1 | 27.5 |
| | `video.freeze_total_ms` mean | 4679 | 3339 | 2830 |
| | `video.e2e_delay_p95_ms` mean | 1607 | 2256 | 1868 |
| | `video.e2e_delay_p50_ms` mean | 184 | 922 | 416 |
| `high_jitter` link, all v2 | `video.e2e_delay_p95_ms` mean | 202 | 143 | 135 |
| | `video.e2e_delay_p50_ms` mean | 175 | 91 | 92 |
| `issue_151_outage_3s_v2` | `video.freeze_total_ms` seed 3 | 3965 | 3201 | 3202 |
| | `video.freeze_total_ms` seed 5 | 4251 | 4244 | 3256 |
| | `repair.overhead_pct` mean | 1.0 | 0.8 | 0.5 |
| `clean_link` (v1 send side) | `video.e2e_delay_p95_ms` mean | 68.1 | 73.8 | 122.4 |
| | `video.e2e_delay_p50_ms` mean | 60.1 | 69.8 | 74.9 |
| `issue_76_tcc_v2_high_jitter` (cc v1) | `video.e2e_delay_p95_ms` mean | 737 | 762 | 135 |

The scenario suite against the baseline: no metric changed (0 regressions,
0 improvements); the default stays `timing v1`. The two new scenarios are
added to `testbench/baseline.json`.

## Remaining

- **Default.** `timing v2` stays off until the flip (#61), as the other v2
  switches.
- **`issue_76_tcc_v2_high_jitter` `< 300`.** That scenario runs `cc v1`; its
  1.2-1.5 s tails on seeds 1, 2, 5, 7 and 10 are pacer delay from the v1
  target collapse (Tester run 14 on this issue), unchanged by the receive
  timing. With every v2 switch on the same link meets libwebrtc's level
  (above), so the line can be restored when the flip turns `cc v2` on.
- **`issue_53` delay.** With late frames now rendered, the median delay on
  the all-v2 `issue_53` link is 922 ms against libwebrtc's 416 ms, and render
  fps is 1.6 above libwebrtc's. The delay sits in the pacer queue after the
  drop (`video.delay_pacer_p95_ms` about 1.5 s), which is send-side work
  (#152, #21), and in a jitter estimate fed by those delayed frames.
- **`issue_151_outage_3s_v2` seed 5** still freezes 1 s longer than
  libwebrtc (4244 against 3256 ms), with 4.1% repair overhead.
- **`clean_link`** delay rises toward libwebrtc's but stays 48 ms below at
  p95; the send side (v1 there) is the rest.
