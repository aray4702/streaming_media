---
issue: 256
title: Audio session with a clock rate other than 8/16/32/48 kHz crashes NetEq concealment
severity: sev:1
status: fixed
area: area:timing, area:audio
regression_tests: testbench/scenarios/issue_256_audio_24khz.scn, tests/unit/timing/audio_jitter_buffer_test.cc, tests/e2e/rtp_session_e2e_test.cc
---
<!-- sm-agent: fixer -->

# #256: Audio session with a clock rate other than 8/16/32/48 kHz crashes NetEq concealment

## Issue

An `rtp::Session` audio stream whose clock rate is not 8, 16, 32 or 48 kHz
(12 or 24 kHz Opus, 44.1 kHz L16) corrupted memory once the device pulled
concealment: heap-buffer-overflow, heap-use-after-free or SEGV in
`NetEqExpand::Correlation`, with no complaint when the stream was added.
Found by the Tester (coverage area 6) on 58c9895:
https://github.com/aray4702/streaming_media/issues/256.

## Root cause

NetEq v2 (the session default since #62) is a port of libwebrtc's NetEq,
which runs only at 8, 16, 32 and 48 kHz. Its expand, merge and time stretch
(`src/timing/neteq_expand.cc`) pick their downsampling filter and windows
by rate (`DownsampleFor`, `WindowsFor`) and size their buffers with
`fs_mult = rate / 8000`. At any other rate the factor and the history
length disagree, so `NetEqExpand::Correlation` (`neteq_expand.cc:514`)
indexes before the start of `history`, and `NetEqTimeStretch::Analyze`
(`neteq_expand.cc:961`) reads past its input. Nothing upstream checked the
rate: `AudioJitterBuffer` took `sample_rate` from the RTP clock rate
(`src/rtp/rtp_receiver.cc:57`) and inserted every decoded packet.

libwebrtc never reaches this state. Its decoder factories refuse such a
format (`AudioDecoderOpus` needs a 48 kHz clock, `AudioDecoderL16` one of
the four rates), so no decoder is registered for the payload type, and
`NetEqImpl::InsertPacketInternal` drops its packets as
`kUnknownRtpPayloadType`
(`compare/webrtc/reference/modules/audio_coding/neteq/neteq_impl.cc:1692`).
`NetEqImpl::SetSampleRateAndChannels` `RTC_CHECK`s the four rates (line
1914), and the constructor replaces any other configured rate with 8 kHz
(line 163).

## Fix

`timing::NetEqSupportsSampleRate` names the four rates. With version 2 at
any other rate, `AudioJitterBuffer::InsertPacket` discards the packet,
counts it in the new `packets_discarded_unsupported_rate` stat and metric,
and records a `neteq/unsupported_rate` event. The receiver then plays
silence, as libwebrtc does when the payload type has no decoder. Version 1
does not use NetEq's DSP and keeps working at any rate (checked under ASan
at 4, 11.025, 12, 24, 44.1 and 96 kHz).

The test bench now accepts `audio_sample_rate` 12000, 24000 and 44100, so
the regression scenario can run the session path. `audio.time_to_first_ms`
now marks the first packet the jitter buffer takes, as its description
says, instead of the first audio packet to arrive. The two are the same
moment in every existing scenario: the baseline does not change.

No API is removed. `AudioJitterBufferStats` gains one field.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_256_audio_24khz.scn` | exit 2: the base test bench rejects `audio_sample_rate 24000` | passes on seeds 1-5 |
| `tests/unit/timing/audio_jitter_buffer_test.cc` `AudioJitterBuffer.NetEqDiscardsPacketsAtUnsupportedRates` | ASan heap-buffer-overflow in `NetEqTimeStretch::Analyze` (with the check disabled) | passes (native and ASan) |
| `tests/e2e/rtp_session_e2e_test.cc` `E2E_RtpSession.AudioAtAClockRateNetEqDoesNotRunAtIsDiscarded` | ASan failure (with the check disabled) | passes (native and ASan) |

The issue's driver against the jitter buffer alone (ASan+UBSan, 1 s of
packets then 2 s of concealment):

| rate | before | after |
|---|---|---|
| 8000 | ok | ok |
| 12000 | heap-buffer-overflow | ok, 50 packets discarded |
| 24000 | heap-buffer-overflow | ok, 50 packets discarded |
| 44100 | heap-use-after-free | ok, 50 packets discarded |
| 90000 | ok | ok, 50 packets discarded |

## Results

The reproduction scenario across seeds (before: the base test bench
refuses the rate, exit 2):

| seed | audio.time_to_first_ms | video.frames_rendered | video.freeze_count |
|---|---|---|---|
| 1 | 20000 (no audio) | 580 | 1 |
| 2 | 20000 | 588 | 0 |
| 3 | 20000 | 583 | 2 |
| 4 | 20000 | 591 | 0 |
| 5 | 20000 | 588 | 0 |

The scenario suite against the baseline (`tools/testbench_compare.py`): 0
regressions, 0 improvements. The only change is the new scenario.
