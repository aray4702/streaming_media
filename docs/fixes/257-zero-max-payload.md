---
issue: 257
title: Video stream with max_payload_size = 0 hangs Session::Process in padding generation until bad_alloc
severity: sev:1
status: fixed
area: area:loss, area:cc
regression_tests: testbench/scenarios/issue_257_zero_max_payload.scn, tests/unit/rtp/rtp_sender_test.cc
---

# #257: Video stream with max_payload_size = 0 hangs Session::Process in padding generation

## Issue

A video stream configured with `rtp::VideoStreamConfig::max_payload_size = 0`
was accepted, and the first time the pacer asked for padding
`Session::Process` never returned: it allocated padding packets until
`std::bad_alloc` (or the OOM killer). Found by the Tester (coverage area 6,
configuration edge cases) on 58c9895:
https://github.com/aray4702/streaming_media/issues/257.

## Root cause

`RtpSender::GeneratePadding` (`src/rtp/rtp_sender.cc:207-225` on 7d41f00)
sizes each empty padding packet as `min(max_payload_size, kMaxPaddingLength)`
and loops `while (bytes_left > 0)`, subtracting that size each time. With
`max_payload_size = 0` it subtracts 0, so the loop never ends. For an audio
sender the same value made `std::clamp(bytes_left, 50, 0)` undefined
(`lo > hi`). Small non-zero values (1-20) left no room for media either:
`Session::SendVideo` packetized every frame to nothing and only counted
`empty_video_frames`.

libwebrtc cannot reach this state. `RTPSender::SetMaxRtpPacketSize`
(`compare/webrtc/reference/modules/rtp_rtcp/source/rtp_sender.cc:307-312`)
requires a whole packet of 100 to `IP_PACKET_SIZE` (1500) bytes, and its
`GeneratePadding` (`rtp_sender.cc:418-459`, `PaddingBytesInPacket` at
151-165) is the loop we ported. We took the value without that range.

## Fix

`RtpSender`'s constructor clamps `max_payload_size` so that the whole packet
(with the 12-byte fixed header) is 100 to 1500 bytes, the range libwebrtc
accepts: 88 to 1488 payload bytes. Every padding packet then carries at
least 88 bytes and the loop ends. `Session::SendVideo` reads the clamped
value from the sender, so a frame is packetized into 88-byte payloads instead
of nothing. The default (1188) and the test bench's value (1446) are inside
the range, so no scenario changes. The header comments of
`RtpSenderConfig::max_payload_size` and `VideoStreamConfig::max_payload_size`
document the clamp. No public API change.

The test bench gains `video_max_payload <bytes>` (default 1446, the value it
always used), so a scenario can configure the value.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_257_zero_max_payload.scn` | the base commit's `sm_testbench` rejects `video_max_payload`; with only the bench change, the old library hangs (killed after 20 s, exit 124) | passes on seeds 1-5: `audio.interruptions` 0, `video.frames_rendered` 298-474 |
| `tests/unit/rtp/rtp_sender_test.cc` `RtpSender.MaxPayloadSizeIsClampedToLibwebrtcPacketSizeRange` | `config().max_payload_size` 0, 20, 5000 (expect 88, 88, 1488) | passes |
| `tests/unit/rtp/rtp_sender_test.cc` `RtpSender.GeneratePaddingWithAZeroMaxPayloadSizeReachesTheTarget` | `config().max_payload_size` 0 (expect 88); asserted before the padding call, which hangs on the old code | 7 packets of 88 bytes for a 600-byte target, video and audio |

## Results

The issue's `repro_pad.cc`, before -> after:

| max_payload_size | before | after |
|---|---|---|
| 0 | `std::bad_alloc` (3 GB `ulimit -v`) | ok |
| 1, 8, 20 | ok, every frame empty | ok, frames packetized at 88 bytes |
| 1200, 5000 | ok | ok (5000 clamped to 1488) |

`issue_257_zero_max_payload`, seeds 1-5: passes on all; `video.frames_rendered`
328 / 474 / 298 / 473 / 384 (on main 5fd7807, with `timing v2` the default since #216).

The scenario suite against the baseline: 0 regressions, 0 improvements; the
only change is the new scenario. Native `ctest` 251/251 and ASan `ctest`
243/243 pass.
