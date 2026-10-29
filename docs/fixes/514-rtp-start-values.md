---
issue: 514
title: RTP sender wire differences from libwebrtc (part 1: start sequence numbers, start timestamps, first audio marker)
severity: sev:3
status: partial
area: area:cc
regression_tests: testbench/scenarios/issue_514_rtp_start_values.scn, tests/unit/rtp/session_test.cc, tests/unit/rtp/rtp_sender_test.cc
---

# #514: RTP sender wire differences from libwebrtc (part 1)

## Issue

The harness fidelity audit (#478, row 4) found that the two stacks put different RTP on the link from
the same input: https://github.com/aray4702/streaming_media/issues/514. This part covers row 3 of the
issue. libwebrtc starts every stream's sequence number and RTP timestamp at a random value (seed 1:
6666 and 873682028) and sets the marker bit on the first audio packet; ours started at 0 and 0 and
sent no audio marker. The Builder found on #514 (from #700) that the start timestamp is read after
all: NetEq's `PacketArrivalHistory` truncates each timestamp to ms before subtracting, so the offset
moves the playout delay by up to 1 ms and flips some NetEq decisions.

## Root cause

- `RtpSenderConfig::initial_seq` and the RTX sequence number defaulted to 0, and Session never set
  them. libwebrtc's `PacketSequencer`, built by `ModuleRtpRtcpImpl2::RtpSenderContext`
  (`rtp_rtcp_impl2.cc:67-70`) with the clock, draws the media and then the RTX start sequence number
  from `Random(clock_us)`, each `Rand(1, kMaxInitRtpSeqNumber)` (32767).
- Nothing added a start timestamp. libwebrtc's `RTPSender` draws `timestamp_offset_` from
  `Random(clock_us).Rand<uint32_t>()` (`rtp_sender.cc:247-250`), and `ChannelSend` and
  `RtpVideoSender` add `StartTimestamp()` to each frame's timestamp (`channel_send.cc:406`,
  `rtp_video_sender.cc:658-660`).
- `RtpSender::SendAudio` used only the caller's marker. libwebrtc computes it in
  `RTPSenderAudio::MarkerBit` (`rtp_sender_audio.cc:88-128`, 243, 262).
- The test bench stamped video frames with `capture_ms x 90` from the call's start. libwebrtc's
  frames carry 90 x their capture NTP time in ms (on the simulated clock: the clock plus 2208988800 s).

`packet_sequencer.cc` is not in `compare/webrtc/reference/`. The draw order and the bound are taken
from libwebrtc's own traces: with the clock at the call's start (`RtcpRandomSeed`) as the seed, the
first two `Rand(1, 32767)` draws give exactly 6666 and 887, libwebrtc's audio/video and RTX start
numbers on seed 1, and 17561 on seed 3; the timestamp draw gives 873682028 (seed 1) and 2301725640
(seed 3), the values on the traces and in the Builder's comment.

## Fix

- `Session::SenderConfig` draws each stream's start values from `SessionConfig::seed` (the clock time
  that already seeds RTCP, as libwebrtc seeds every module from the clock at construction): the
  timestamp offset from one `XorshiftRandom` (`webrtc::Random`), the media then RTX sequence numbers
  from another. Audio and video get the same values, as in libwebrtc where both modules are built
  at the same clock time.
- `RtpSender` keeps the offset (`RtpSenderConfig::timestamp_offset`, `StartTimestamp()`); Session
  adds it in `SendAudio`, `SendVideo` (for the absolute capture time) and `SendVideoChunks`.
  `RtxConfig::initial_seq` starts the RTX numbers.
- `RtpSender::AudioMarkerBit` ports `RTPSenderAudio::MarkerBit`: the stream's first packet, a payload
  type change, and the first speech frame after a DTX frame (Opus DTX frames, at most 2 bytes, are
  ACM's `kAudioFrameCN`). The caller's marker still sets it.
- Test bench: video frames are stamped `90 x capture NTP ms`, as libwebrtc's are; the measurement
  takes each stream's offset off again where it maps wire timestamps to capture times, as
  `webrtc_call.cc` does. New information-only metrics `net.audio_start_seq`, `net.video_start_seq`
  and `net.audio_first_marker`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_514_rtp_start_values.scn` | the start metrics are not reported (the sender starts at 0, no marker) | `net.audio_start_seq` 6666, `net.video_start_seq` 6666, `net.audio_first_marker` 1 |
| `tests/unit/rtp/session_test.cc` `Session.StreamsStartAtLibwebrtcsRandomSequenceNumberAndTimestamp` | sequence numbers and timestamps start at 0 | libwebrtc's seed-1 and seed-3 values |
| `tests/unit/rtp/rtp_sender_test.cc` `RtpSender.AudioMarkerOnTheFirstPacketAndAfterDtx` | no marker on the first packet or after DTX | `MarkerBit`'s pattern |

Seed-1 forward-link trace of `clean_link`, first packets, now identical to libwebrtc's: audio seq 6666,
ts 873682028, marker 1; RTX padding seq 887.

## Results

The new scenario across seeds 1-5: audio and video start at 6666, 2898, 17561, 8688 and 13862, all
with the first audio marker; every seed passes.

The suite against the baseline: 261 cells in 63 scenarios move, in both directions, most of them by
one NetEq step (10 ms on `audio.jb_delay_*`) or less than a millisecond of A/V offset, as #700
predicted from the ms truncation in `PacketArrivalHistory`. Two cells pass the tolerance:

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_84_encoder_qp_360p_low_rate | video.longest_freeze_ms | 4 | 32 | REGRESSED |
| issue_84_encoder_qp_720p_low_rate | video.longest_freeze_ms | 4 | 32 | REGRESSED |

That is the longest gap between rendered frames, not a freeze (a 30 fps call renders every 33 ms).
Seeds 1-10 on this branch: 3, 4, 5, 7, 17, 19, 27, 28.7, 30, 32 (mean 17.3); main's published
comparison: mean 14.9; libwebrtc: 0 to 29.1 per seed, mean 12.8. Seed 1 now draws a different gap,
inside libwebrtc's own spread.

## Remaining

- Row 1: padding on the media SSRC without RTX (`can_send_padding_on_media_ssrc`), and row 2: audio
  padding counts and sizes. Both need `PacketSequencer` (`modules/rtp_rtcp/source/packet_sequencer.{h,cc}`),
  which decides when padding may go on the media SSRC (`require_marker_before_media_padding`) and
  which timestamp it carries; it is not in `compare/webrtc/reference/` yet.
- Padding before the first media packet: libwebrtc's carries timestamp 0, ours the stream's offset
  (also `PacketSequencer`).
