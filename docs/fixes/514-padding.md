---
issue: 514
title: RTP sender wire differences from libwebrtc (part 2: padding without RTX, audio padding, padding timestamps)
severity: sev:3
status: partial
area: area:cc
regression_tests: testbench/scenarios/issue_514_padding_without_rtx.scn, testbench/scenarios/issue_514_audio_padding.scn, tests/unit/rtp/rtp_sender_test.cc, tests/unit/rtp/packet_router_test.cc, tests/unit/rtp/send_side_controller_test.cc
---

# #514: RTP sender wire differences from libwebrtc (part 2)

## Issue

The harness fidelity audit (#478, row 4) found that the two stacks put different RTP on the link from
the same input: https://github.com/aray4702/streaming_media/issues/514. Part 1 (#704) ported the
start values. This part covers rows 1 and 2 and the timestamp of padding sent before the first media
packet, with `PacketSequencer` now in the reference tree (#705). On `issue_29_ulpfec_over_red` (NACK
off) libwebrtc sent 383 padding packets on the media SSRCs and ours 4, on an RTX SSRC; on
`audio_only_lossy` libwebrtc's audio padding came in 25 sizes and ours in fixed 224-byte packets.

## Root cause

- **Padding without RTX.** `SendSideController` padded on one stream only, and with no RTX it
  installed no padding generator: the pacer then sent fixed-size placeholders (224 B, 102 B for a
  probe's first packet) that `RtpSender::MakePadding` filled on the media SSRC at any time.
  libwebrtc's `RTPSender::GeneratePadding` (`rtp_sender.cc:418-459`) pads on the media SSRC only while
  `PacketSequencer::CanSendPaddingOnMediaSsrc()` (`packet_sequencer.cc:139-154`): a media packet has
  been sent and, on video (`require_marker_before_media_padding = !audio`, `rtp_rtcp_impl2.cc:69`),
  it ended a frame. When the video stream can't pad, `PacketRouter::GeneratePadding`
  (`packet_router.cc:255-297`) asks the next stream, video first, then audio. FEC packets carry no
  marker, so on a NACK-off call with FEC most padding goes on the audio SSRC.
- **The test bench gave video an RTX stream with NACK off** (`testbench/call.cc`), which sm_compare's
  libwebrtc call does not have (`compare/webrtc/webrtc_call.cc`, RTX only `if (s.nack)`), so ours
  padded on that RTX SSRC. A harness difference: our side now gets the same configuration.
- **Audio padding sizes.** `PaddingBytesInPacket` (`rtp_sender.cc:150-160`) sizes audio padding as the
  bytes asked for, clamped to [50, min(max payload, 255)]; the placeholders were all 224 B.
- **Padding fields.** `PacketSequencer::Sequence` / `PopulatePaddingFields` (`packet_sequencer.cc:58-137`)
  set them when the packet is sent: on the media SSRC the last media packet's timestamp and payload
  type (the RED block's on ULPFEC, `is_red`); on the RTX SSRC empty padding gets the last media
  timestamp plus 90 ticks per ms since it was sent, and 0 before any media. Ours stamped the last
  *queued* media timestamp at generation, never advanced it, and used the stream's offset before media.
  `ModuleRtpRtcpImpl2::CanSendPacket` (`rtp_rtcp_impl2.cc:337-350`) also drops media-SSRC padding that
  a new mid-frame media packet preempted.

## Fix

- `RtpSender`: `GeneratePadding(target, now)` ports `RTPSender::GeneratePadding` whole (RTX payload
  padding, then empty padding on the RTX SSRC, or on the media SSRC while `CanSendPaddingOnMediaSsrc()`);
  `AssignSequenceNumber(p, now)` ports `PacketSequencer::Sequence` (padding fields, media-SSRC numbers,
  the last-packet state); `SupportsPadding()` / `SupportsRtxPayloadPadding()` as `rtp_sender.cc:401-412`.
  "Media has been sent" now means released by the pacer (`RtpSenderEgress::MediaHasBeenSent`).
- `SendSideController`: `AddPaddingSender()` keeps PacketRouter's module list (video at the front,
  audio at the back) and `last_send_module_`; the padding generator is always installed and follows
  `PacketRouter::GeneratePadding`; preempted media-SSRC padding is dropped at release
  (`padding_dropped` counter). The pacer's placeholder padding is no longer used by a session.
- `Session` registers every send stream with `AddPaddingSender()`.
- Test bench: RTX only with NACK, as sm_compare's libwebrtc call. New information-only metrics
  `net.padding_media_ssrc_packets`, `net.padding_rtx_ssrc_packets` and `net.padding_distinct_sizes`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_514_padding_without_rtx.scn` | metrics absent; padding on the RTX SSRC | 39 padding packets on the media SSRCs, none on RTX (seed 1; seeds 1-5 pass) |
| `testbench/scenarios/issue_514_audio_padding.scn` | metrics absent; 2 padding sizes (224, 102 B) | 21 sizes (seed 1; 13-28 on seeds 1-8, all pass) |
| `RtpSender.VideoWithoutRtxPadsOnTheMediaSsrcAfterAFrameEnds` | no padding without RTX | media-SSRC padding after the marker only, last timestamp and payload type, next numbers |
| `RtpSender.MediaSsrcPaddingAfterRedTakesTheBlockPayloadType` | no padding without RTX | the RED block's payload type |
| `RtpSender.AudioWithoutRtxPadsOnItsSsrcAfterItsFirstPacket` | no padding without RTX | audio SSRC, [50, 255] sizing taken once per call |
| `RtpSender.RtxPaddingTimestampFollowsTheLastMediaPacket` | the caller's timestamp | 0 before media, +90/ms after, payload padding unchanged |
| `PacketRouter.MidFrameVideoWithoutRtxLeavesThePaddingToAudio` | video placeholders mid-frame | audio pads until the frame ends, then video |
| `SendSideController.PaddingTimestampFollowsTheSequencerAndFitsItsAccountedSize` (was `PaddingCarriesTheLastMediaTimestamp...`, which encoded the old fixed timestamp) | fixed last-queued timestamp | `PopulatePaddingFields`' timestamps |

## Results

Seed-1 forward-link trace of `issue_29_ulpfec_over_red`, first 190 ms: now the same packets as
libwebrtc's (`parity-data/traces/issue_29_ulpfec_over_red.1.webrtc.trace.csv`), sizes, kinds, sequence
numbers and timestamps: the probe's first packet is 98 B of padding on the audio SSRC (seq 6673), then
303 B padding on the video SSRC after each frame's marker (6670, 6672-6682). Whole call: 402 padding
packets (libwebrtc 383, before 4), none on RTX. `audio_only_lossy` seed 1: padding bytes per second
equal to libwebrtc's for seconds 3-8 (917, 13883/14597, 30940, ...); 21 sizes against libwebrtc's 25.

Ten seeds against libwebrtc (means; before = #704's branch, libwebrtc from `parity-data` on main):

| scenario | metric | before | after | libwebrtc |
|---|---|---|---|---|
| issue_29_ulpfec_over_red | video.freeze_total_ms | 22397 | 19677 | 19722 |
| issue_29_ulpfec_over_red | video.render_fps | 3.31 | 2.66 | 2.71 |
| issue_29_ulpfec_over_red | video.frames_rendered | 99.4 | 79.7 | 81.1 |
| issue_29_ulpfec_over_red | video.keyframe_requests | 6.6 | 7.0 | 7.0 |
| issue_29_ulpfec_over_red | video.e2e_delay_p95_ms | 209.9 | 230.7 | 251.0 |
| issue_29_ulpfec_over_red | sync.av_offset_p95_ms | 91.6 | 103.1 | 126.8 |
| issue_29_ulpfec_over_red | repair.overhead_pct | 46.7 | 43.3 | 47.2 |
| issue_29_ulpfec_over_red | video.time_to_first_frame_ms | 205.7 | 303.2 | 242.9 |
| audio_only_lossy | cc.target_avg_kbps | 217.3 | 223.9 | 235.0 |
| audio_only_lossy | audio.jb_delay_p95_ms | 81 | 78 | 78 |
| issue_533_opus_decoder_frame_older_than_window | cc.target_avg_kbps | 130.1 | 138.9 | 138.3 |
| issue_533_opus_decoder_frame_older_than_window | cc.rtt_p50_ms | 1805.6 | 1410.0 | 1444.9 |
| issue_62_opus_fec_low_rate | audio.longest_interruption_ms | 40.3 | 46.3 | 45.8 |

`video.time_to_first_frame_ms` is equal to libwebrtc's within 1 ms on 9 of 10 seeds (201-204); seed 7
loses the first keyframe on both stacks (ours 1208, libwebrtc 608, before 224). `repair.overhead_pct`
is inside libwebrtc's per-seed spread (37.1-59.9; ours 33.0-52.5).

Suite against the baseline (seed 1; every baseline file also gains the new information metrics):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| audio_only_lossy (and its 3 copies) | audio.mouth_to_ear_p95_ms | 175.5 | 154.2 | improved |
| audio_only_lossy (and its 3 copies) | cc.target_avg_kbps | 344.4 | 308.8 | REGRESSED |
| issue_29_ulpfec_over_red | repair.overhead_pct | 60.14 | 45.38 | improved |
| issue_29_ulpfec_over_red | video.pause_total_ms | 6224 | 0 | improved |
| issue_29_ulpfec_over_red | video.e2e_delay_p95_ms | 186 | 208.7 | REGRESSED |
| issue_29_ulpfec_over_red | video.encoder_target_avg_kbps | 839.6 | 926.9 | REGRESSED |
| issue_29_ulpfec_over_red | video.encode_height_mean | 324.2 | 354.4 | REGRESSED |
| issue_29_ulpfec_over_red | video.frames_rendered | 113 | 98 | REGRESSED |
| issue_29_ulpfec_over_red | video.render_ratio_pct | 12.61 | 10.94 | REGRESSED |
| issue_533_opus_decoder_frame_older_than_window | cc.target_avg_kbps | 118.3 | 182.5 | improved |
| issue_533_opus_decoder_frame_older_than_window | audio.jb_delay_p95_ms | 780 | 820 | REGRESSED |
| issue_62_opus_fec_low_rate | cc.send_over_target_p95_pct | 9.249 | 4.286 | improved |

Each regressed cell moves toward libwebrtc's 10-seed mean (table above: `audio_only_lossy` target
224 against 235, `issue_29` frames rendered 79.7 against 81.1, encoder target 967.5 against 955.5,
e2e p95 230.7 against 251.0; `issue_533` jitter buffer p95 887 against 996).

## Remaining

- Audio padding *amount*: on `audio_only_lossy` seed 1 the padding is equal to libwebrtc's to the
  byte for seconds 3-8; from second 9 libwebrtc stops and ours pads until 11 s, and later phases
  differ (ours 1795 packets, libwebrtc 851). What asks for padding there (probes, the padding rate)
  is not this sender; it needs its own trace.
- `issue_29_ulpfec_over_red` seed 7: the first keyframe is lost on both stacks; ours renders the first
  frame at 1208 ms, libwebrtc at 608 ms.
