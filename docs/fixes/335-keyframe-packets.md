---
issue: 335
title: Audio delayed 1.7-1.9 s and 4% concealed next to 1 fps video on a clean link
severity: sev:2
status: fixed
area: area:cc, area:video
regression_tests: testbench/scenarios/issue_335_fps1_keyframe_packets.scn, tests/unit/loss/keyframe_test.cc, tests/unit/rtp/h264_test.cc
---

# #335: Audio delayed 1.7-1.9 s and 4% concealed next to 1 fps video on a clean link

## Issue

With 1 fps video (a static screen share) next to audio on a clean 2 Mbps,
80 ms RTT link, mouth-to-ear p95 was 1.7-1.9 s, the jitter buffer p95 1.1-1.2 s
and 3.6-4.3 % of the audio was concealed, with no loss on the link; the
bottleneck queue p95 was about 620 ms. Found by the Tester on commit 2ffc33c:
https://github.com/aray4702/streaming_media/issues/335.

libwebrtc on the same scenario (yardstick PR #338, `sm_compare`, 10 seeds) does
not do this: mouth-to-ear p95 890 ms (mean), jitter buffer p95 730 ms,
concealed 0.35 %, queue p95 114 ms, **no keyframe requests**, render fps 0.99.
Ours made 2-3 keyframe requests on every seed and rendered 0.7 fps. At 2 fps
the two stacks already matched, so the gap was specific to frames that take
longer than 200 ms to arrive.

## Root cause

The receiver's decode time-out (`src/loss/keyframe_request.cc`,
`KeyframeRequester::Process`) waits `max_wait_for_keyframe` (200 ms) before the
first frame and does not fire while a keyframe is arriving: a keyframe packet
came within those 200 ms (libwebrtc's `VideoReceiveStream2::IsReceivingKeyFrame`).

Which packets count as keyframe packets differed. `RtpReceiver::OnVideoMedia`
passed `info.idr || info.sps || info.pps`, and the H.264 parser sets `idr` only
on the first FU-A fragment of an IDR slice. So only the first two packets of
a keyframe counted. libwebrtc counts every one:

- `VideoRtpDepacketizerH264::ParseFuaNalu`
  (`modules/rtp_rtcp/source/video_rtp_depacketizer_h264.cc:282-286`) reads the
  original NAL type from every fragment and marks each fragment of an IDR as
  `kVideoFrameKey`; `ProcessStapAOrSingleNalu` marks an SPS or IDR NAL unit key.
- `RtpVideoStreamReceiver2::UpdatePacketReceiveTimestamps`
  (`video/rtp_video_stream_receiver2.cc:1697-1704`) also counts any packet with
  the last keyframe's RTP timestamp, and runs only for packets that are not
  `recovered()` (`:816-819`).

At 1 fps the first keyframe (4x a frame, 91 KB) takes about 0.5 s at the start
rate. The receiver saw no keyframe packet after 48 ms and asked for a keyframe
at 400 ms. The encoder then sent a 742 KB keyframe at the new 1.5 Mbps target.
It took 3 s to cross the 2 Mbps link, so the 3 s frame time-out fired again
(3561 ms). The large keyframes filled the bottleneck queue the audio shares,
and the frame dropper spread each one over the next frames, so frames were
dropped (0.7 fps).

## Fix

- `H264PayloadInfo::keyframe` / `VideoPayloadInfo::keyframe`: the
  depacketizer's frame type, as libwebrtc sets it (an SPS or IDR in a single
  NAL unit or STAP-A, and every FU-A fragment of an IDR slice).
- `KeyframeRequester::OnMediaPacket(now, keyframe_packet, rtp_timestamp)`: a
  packet is a keyframe packet if its frame type is key or it has the last
  keyframe's RTP timestamp (`last_received_keyframe_rtp_timestamp_`).
- `RtpReceiver::OnVideoMedia` feeds it the frame type and the RTP timestamp,
  and only for packets received as sent, not for RTX-restored or FEC-recovered
  ones, as `UpdatePacketReceiveTimestamps`.

PPS packets no longer count by themselves (libwebrtc doesn't mark them key);
a PPS in the keyframe's access unit still counts through its RTP timestamp.
No other scenario in the suite changes: their metrics are bit-identical.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_335_fps1_keyframe_packets.scn` (seed 1) | `video.keyframe_requests` 2 (expect == 0), `video.render_fps` 0.7 (>= 0.99), `audio.mouth_to_ear_p95_ms` 1869 (< 910), `audio.jb_delay_p95_ms` 1210 (< 750), `audio.concealed_pct` 4.01 (< 0.7), `cc.queue_p95_ms` 625 (< 195) | 0, 1.0, 862.8, 740, 0.033, 87.9 |
| `tests/unit/loss/keyframe_test.cc` `KeyframeRequest.NoRequestWhileALongKeyframeIsArriving` | without the RTP-timestamp rule, a PLI at 600 ms while the keyframe is still arriving | no PLI while the keyframe arrives; one 200 ms after it ends |
| `tests/unit/rtp/h264_test.cc` `H264.EveryPacketOfAnIdrIsAKeyframePacket` | `keyframe` field did not exist (continuation fragments had `idr` false) | every keyframe packet key, no delta packet |

The bounds follow the tight rule: just above ours on seeds 1-5, and not above
libwebrtc's worst seed (mouth-to-ear 1040, jitter buffer 780, concealed 1.1 %,
queue 302 ms, render fps 0.93). The Tester's target (mouth-to-ear < 400 ms,
jitter buffer < 300 ms) is kept as a comment. libwebrtc doesn't meet it at
1 fps either.

## Results

The reproduction (`issue_335_fps1_keyframe_packets`), before -> after, with
libwebrtc's value on the same seed (yardstick #338):

| seed | metric | before | after | libwebrtc |
|---|---|---|---|---|
| 1 | audio.mouth_to_ear_p95_ms | 1869 | 862.8 | 876.3 |
| 2 | audio.mouth_to_ear_p95_ms | 1895 | 788.3 | 829.4 |
| 3 | audio.mouth_to_ear_p95_ms | 1713 | 847.1 | 858.3 |
| 4 | audio.mouth_to_ear_p95_ms | 1913 | 798.5 | 919.3 |
| 5 | audio.mouth_to_ear_p95_ms | 1896 | 905.6 | 910.5 |
| 1 | cc.queue_p95_ms | 625 | 87.9 | 80.9 |
| 2 | cc.queue_p95_ms | 623 | 74.3 | 64.3 |
| 3 | cc.queue_p95_ms | 577 | 72.0 | 62.5 |
| 4 | cc.queue_p95_ms | 665 | 62.2 | 104.5 |
| 5 | cc.queue_p95_ms | 615 | 189.7 | 184.4 |
| 1-5 | audio.concealed_pct | 3.57-4.31 | 0.03-0.60 | 0.03-0.60 |
| 1-5 | audio.jb_delay_p95_ms | 1100-1240 | 680-740 | 680-770 |
| 1-5 | video.keyframe_requests | 2-3 | 0 | 0 |

The scenario suite against the baseline (`tools/testbench_compare.py`):
0 regressions, 0 improvements; every metric of the 98 existing scenarios is
bit-identical. The baseline gains the new scenario.
