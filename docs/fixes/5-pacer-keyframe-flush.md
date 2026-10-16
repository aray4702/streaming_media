---
issue: 5
title: Video freezes up to 1.5 s with 2% loss in 15-packet bursts (libwebrtc pacer keyframe flush, PR 3)
severity: sev:2
status: partial
area: area:loss, area:video
regression_tests: testbench/scenarios/issue_5_keyframe_flush.scn, tests/unit/cc/pacing_controller_test.cc, tests/unit/rtp/send_side_controller_test.cc
---

# #5: Video freezes up to 1.5 s with 2% loss in 15-packet bursts

## Issue

With 2% loss in ~15-packet bursts, video froze for up to 1.5 s (Tester). The
Owner re-scoped it as stage-1 parity work on the encoder side
(`VideoStreamEncoder`, `PacingController`). PR 1 ported the `FrameDropper`
(`5-frame-dropper-port.md`), PR 2 the `EncoderBitrateAdjuster`
(`5-encoder-bitrate-adjuster.md`). This PR 3 ports the pacer's keyframe flush
(Owner scope note from #70 on the issue).
https://github.com/aray4702/streaming_media/issues/5

## Root cause

`PacingController::EnqueuePacket` (`compare/webrtc/reference/modules/pacing/pacing_controller.cc`
lines 205-232) discards the queued packets of a stream when the first packet
of a keyframe arrives, "to get the new keyframe out as quickly as possible".
`pacer v2` (`src/cc/pacing_controller.cc`, `Enqueue`) did not: after a capacity
drop the queue holds stale delta-frame packets, and the keyframe the receiver
asked for waited behind them. The sender also did not mark keyframes, so the
pacer could not know.

## Fix

- `PacedPacket` carries `is_key_frame` and `first_packet_of_frame`, set from
  `SenderPacket`, which `RtpSender::SendVideoFrame(..., keyframe)` fills
  (`EncodedVideoFrame::keyframe`, default false; the test bench sets it).
- `PacingController::Enqueue`: a video packet that is the first of a keyframe,
  with no keyframe packet already queued (`HasKeyframePackets`), removes the
  queued video, FEC and retransmission packets (`FlushVideo`, levels 1 and 3 of
  `PrioritizedPacketQueue::RemovePacketsForSsrc`; audio and padding are other
  streams) before the queue-empty fast-forward, as libwebrtc orders it. The
  queue-time accounting is updated as `Pop` does.
- `Pacer::TakeFlushed()` hands the discarded packets to
  `SendSideController::Enqueue`, which drops what it kept for them
  (`stats.flushed`). They are never sent, so the receiver NACKs them or
  requests a keyframe, as with libwebrtc ("this can truncate a picture that has
  only been partially sent").
- Behind `pacer v2`; v1 (the default) is unchanged. Metrics `keyframe_flushes`
  and `flushed_packets`, event `pacer/keyframe_flush`.

Differences from libwebrtc: one video stream, so the "ssrc" is the video
stream (video, FEC and the RTX level); `retransmit_flushed_packets_settings`
(off by default) and the RTX-SSRC removal are not needed.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_5_keyframe_flush.scn` (seeds 1-5) | `video.freeze_total_ms` 3966 / 3197 / 4250 / 3680 / 4006 (expect < 3000) | 2157 / 2007 / 2227 / 1859 / 2402; longest freeze at most 1589 (< 1700) |
| `PacingController.KeyframeFlushesTheQueuedVideoPackets`, `...OnlyOnTheFirstPacketAndOnlyOnce`, `...QueueDrainsAfterAKeyframeFlush` | n/a (new API) | pass |
| `SendSideController.KeyframeFlushesQueuedVideoUnderPacerV2` | n/a (new API) | passes |

## Results

`pacer_v2_capacity_drop` (= `capacity_drop` with `pacer v2`), mean of 10 seeds,
main / this PR / libwebrtc (`parity-data`, `capacity_drop`):

| metric | main | this PR | libwebrtc |
|---|---|---|---|
| video.freeze_total_ms | 3770 | 2250 | 2538 |
| video.longest_freeze_ms | 1922 | 1385 | |
| video.e2e_delay_p95_ms | 263 | 303 | 249 |
| cc.queue_p95_ms | 387 | 361 | 283 |
| video.keyframe_requests | 7.5 | 7.2 | 0 |
| video.undecodable_frames | 17.0 | 24.7 | |
| video.render_fps | 27.1 | 28.0 | |

Freeze time moves to libwebrtc's value; e2e delay p95 moves away from it and
undecodable frames rise (the flush truncates the pictures that were in the
queue, which libwebrtc does too). Suite comparison against the baseline:
`testbench_compare.py` reports 14 improved and 1 regressed, all on
`pacer_v2_capacity_drop` (the only scenario with `pacer v2`); the regression is
`video.undecodable_frames` 24 -> 26 on seed 1, explained above. Every other
scenario is unchanged (the default is pacer v1). The baseline is updated.

## Remaining

- Keyframe budget as `VideoStreamEncoder`, a measured input frame rate, and
  flipping the `EncoderBitrateAdjuster` default: libwebrtc's `link_allocation`
  (`video_send_stream_impl.cc` `OnBitrateUpdated`) equals the encoder target
  rate (`max(encoder_target, encoder_target - protection)`), so there is no
  headroom beyond what `bandwidth_allocation - sum(allocation)` gives; our
  `link = max(target, rates.target - audio)` gives more. Needs a measured
  comparison of the encoder model, not done here.
- The issue's `freeze_count <= 2` is not reached (tied to #23).
- `pacer v2` becomes the default with #61.
