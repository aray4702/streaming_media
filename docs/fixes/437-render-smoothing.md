---
issue: 437
title: Video freezes behind libwebrtc on very_low_capacity (count, 61/80 seeds)
severity: sev:3
status: fixed
area: area:loss, area:video
regression_tests: testbench/scenarios/issue_437_render_smoothing_very_low_capacity.scn, testbench/scenarios/issue_437_default_flip_very_low_capacity.scn, tests/unit/video/video_test.cc, tests/unit/rtp/libwebrtc_defaults_test.cc
---

# #437: Video freezes behind libwebrtc on very_low_capacity

## Issue

On the 100 kbps `very_low_capacity` link (and its two copies,
`very_low_capacity_allocator_v2` and `issue_123_v2_allocation_min`), our video
freezes more often than libwebrtc's: `video.freeze_count` 25.3 against 20.3 on
80 seeds (band ±4.2), behind on 61 of 80 seeds. The Coordinator filed it after
#435 and placed it on the roadmap #64:
https://github.com/aray4702/streaming_media/issues/437.

## Root cause

Yardstick #448 split the gap into stages on seed 1. libwebrtc decodes 445
frames and renders 322, but we render all 536 frames we decode. Half of the gap
comes from libwebrtc's render stage, which we didn't have. With
`enable_prerenderer_smoothing` on (its default,
`video/video_receive_stream2.cc:400`), libwebrtc's decoded frames pass through
`IncomingVideoStream` and `VideoRenderFrames`
(`compare/webrtc/reference/video/render/`) before they are rendered. That queue
drops some frames:

- a frame whose render time is earlier than the last one queued (out of order);
- a frame more than 500 ms late while others wait;
- a frame more than 10 s ahead;
- every frame but the newest when several are due at once.

Our `RtpReceiver::Process` (`src/rtp/rtp_receiver.cc`, the `emit` lambda) handed
every decoded frame straight to the renderer.

## Fix

`video::RenderFrames` (`include/sm/video/render_frames.h`,
`src/video/render_frames.cc`) ports both files with the same constants:
`kOldRenderTimestampMS` 500, `kFutureRenderTimestampMS` 10000, render delay 10
ms clamped to [10, 500]. It also keeps the dequeue rule: a frame added to an
empty queue is dequeued at once, and while frames wait, the next dequeue runs at
the front frame's release time.

`RtpReceiver` runs the queue when the new `render_smoothing` switch is on
(`RtpReceiverConfig`, `VideoStreamConfig`, and the scenario key
`render_smoothing on|off`). It passes decoded frames through the queue and
returns the frames it drops with `render` false. The switch is off by default,
so the baseline doesn't move; turning it on is the Owner's call.

**Default flip (the last step of #437).** The Coordinator decided on #480
(2026-10-29) that every "flip" row of the configuration inventory goes to
libwebrtc's default, rows 1-2 through this item's parity-judged PR. Row 1's
default is now confirmed from M157's headers (shipped with the prebuilt
libwebrtc, not in `compare/webrtc/reference/`):
`bool enable_prerenderer_smoothing = true;` in `call/video_receive_stream.h:291`
and `media/base/media_config.h:62`. `compare/webrtc/webrtc_call.cc` leaves the
receive config at that default, so `sm_compare`'s libwebrtc side has always run
the pre-render queue. Row 2 is `PacingController::EnqueuePacket`'s
`RemovePacketsForSsrc(rtx_ssrc)` (`modules/pacing/pacing_controller.cc:227-231`),
which runs unconditionally. Both switches are now on by default:
`RtpReceiverConfig::render_smoothing`, `VideoStreamConfig::render_smoothing`,
`PacingControllerConfig::keyframe_flush_padding`, and the test bench's
`render_smoothing` / `keyframe_flush_padding` keys.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_437_render_smoothing_very_low_capacity.scn` | base `sm_testbench` doesn't know `render_smoothing`. With the switch off, seed 1 has `video.render_fps` 13.4 (expect < 13) | 11.72 |
| `testbench/scenarios/issue_437_default_flip_very_low_capacity.scn` (no switch lines: the defaults) | base `sm_testbench` (e54062b): seed 1 `video.render_fps` 12.55 (expect < 12), FAIL | seeds 1-5: 10.75, 11.05, 11.25, 5.25, 11.10, PASS |
| `tests/unit/rtp/libwebrtc_defaults_test.cc` `LibwebrtcDefaults.DecodedFramesPassThePreRenderQueue`, `.AKeyframeFlushAlsoDropsQueuedPadding` | the defaults are off | pass |
| `tests/unit/video/video_test.cc` `RenderFrames.*` (6 cases: immediate render, wait for the release time, only the newest of several due, old-frame drop only with others waiting, out-of-order and far-future drops, render-delay clamp) | the class doesn't exist | pass |

## Results

The regression scenario across seeds 1-5, with `render_smoothing` off -> on:

| seed | frames rendered | render fps | freezes | freeze total ms |
|---|---|---|---|---|
| 1 | 536 -> 469 | 13.40 -> 11.72 | 26 -> 25 | 29698 -> 29525 |
| 2 | 506 -> 465 | 12.65 -> 11.62 | 23 -> 23 | 28225 -> 28470 |
| 3 | 429 -> 401 | 10.72 -> 10.03 | 25 -> 25 | 23166 -> 23166 |
| 4 | 435 -> 381 | 10.88 -> 9.53 | 30 -> 29 | 31421 -> 31101 |
| 5 | 527 -> 508 | 13.18 -> 12.70 | 26 -> 24 | 27874 -> 27336 |

On `very_low_capacity`, seeds 1-10, the mean frames rendered fall from 466 to
421 (libwebrtc 369) and the mean freezes from 25.3 to 24.6 (libwebrtc 21.3). On
seed 1, 67 of the 536 decoded frames are dropped, all of them as out of order.
The freeze count is never worse on any seed.

On seed 1, with the switch on for the whole suite, 52 of 121 scenarios render
slightly fewer frames, and no scenario's freeze count goes up.
`issue_127_all_v2_no_stall` renders 1780 -> 1772 frames, and its
`sync.av_offset_p95_ms` moves from 169.4 to 164.0 ms (libwebrtc 167.9).

The baseline doesn't change: the switch is off, and the only addition is the
new scenario's entry.

### Default flip

Both switches off -> on, the whole suite on seeds 1-10 with the local
`sm_testbench` (GCC 13, Release), against libwebrtc's per-seed values for the
same seeds from `parity-data`'s `compare-webrtc.json` (main e54062b), with
`tools/parity_report.py`'s band (the tie floor, 5 %, one standard deviation of
libwebrtc's seeds). `sm_compare` needs clang 21, which this sandbox can't
install, so our side is `sm_testbench`'s, not `sm_compare`'s; the PR's
`compare-webrtc` run gives the CI numbers.

**Gaps: 49 -> 32.** 20 close, 3 open, 5 move by more than the band but stay gaps.

Closed (selection; all 20 in the PR):

| scenario | metric | off | on | libwebrtc | band |
|---|---|---|---|---|---|
| very_low_capacity (and its 3 copies) | `video.longest_freeze_ms` | 3864 | 4138 | 4431 | ±465 |
| low_bandwidth (and 4 copies) | `video.render_fps` | 27.76 | 25.88 | 25.86 | ±1.39 |
| wifi_to_lte_handover (and 3 copies) | `audio.mouth_to_ear_p95_ms` | 428.3 | 462.4 | 519 | ±88.9 |
| issue_466_undershoot_very_low_capacity | `video.render_fps` | 12.14 | 10.83 | 9.41 | ±1.67 |
| issue_466_undershoot_very_low_capacity | `video.freeze_count` | 24.2 | 23.4 | 20.7 | ±2.87 |
| issue_76_rtx_v2_keyframe_flush | `video.render_fps` | 28.58 | 27.04 | 27.14 | ±1.43 |

Opened (each just past the band, all with an open owner):

| scenario | metric | off | on | libwebrtc | band | owner |
|---|---|---|---|---|---|---|
| issue_12_lossbwe_policer | `sync.av_offset_p95_ms` | 3181 | 2980 | 3383 | ±381 | #411 (#741) |
| issue_150_gcc_v2_high_jitter_seed5 | `audio.mouth_to_ear_p95_ms` | 243.5 | 249.1 | 232.3 | ±12.5 | #783 |
| issue_150_gcc_v2_high_jitter_seed5 | `repair.overhead_pct` | 39.51 | 37.31 | 42.77 | ±5.39 | #731 (new rule) |

Moved, still a gap: `very_low_capacity` family `video.render_fps` 11.07 ->
9.66 against 8.30 (±1.25, closer; #411 D2, #739/#740), and
`issue_466_overshoot_very_low_capacity` `video.pause_total_ms` 2445 -> 4280
against 561 (±1770, further; #466's rule).

The baseline (seed 1) is regenerated: 178 metrics worse and 197 better than
before on seed 1, which is one draw of the seed-to-seed spread these
scenarios have; the 10-seed means above are the judgement.

## Remaining

None on #437. The `very_low_capacity` render fps cell (9.66 vs 8.30) stays with
#411 D2 (#739, #740).
