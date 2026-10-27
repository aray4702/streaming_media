---
issue: 437
title: Video freezes behind libwebrtc on very_low_capacity (count, 61/80 seeds)
severity: sev:3
status: partial
area: area:loss, area:video
regression_tests: testbench/scenarios/issue_437_render_smoothing_very_low_capacity.scn, tests/unit/video/video_test.cc
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

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_437_render_smoothing_very_low_capacity.scn` | base `sm_testbench` doesn't know `render_smoothing`. With the switch off, seed 1 has `video.render_fps` 13.4 (expect < 13) | 11.72 |
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

## Remaining

- The rest of the freeze-count gap is upstream of this stage. On seed 1,
  libwebrtc decodes 445 frames and we decode 536, because its frames reach the
  frame buffer later and in larger bursts. The next step is the packet buffer
  and frame completion (#453's `PacketBuffer` `missing_packets_` port touches
  the same path).
- libwebrtc's seconds 25-29 on seed 1 (102 frames decoded, 4 rendered, then a
  5.5 s pause) come from frames whose render time is seconds ahead. Our timing
  doesn't produce that, so its cause sits in the frame release or the timing,
  not in this queue.
- Turning `render_smoothing` on by default is the Owner's call, after an 80-seed
  measurement against libwebrtc.
