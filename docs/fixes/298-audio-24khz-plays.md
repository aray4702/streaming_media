---
issue: 298
title: Audio jitter-buffer delay and concealment read 0 at 24 kHz
severity: sev:3
status: fixed
area: area:audio, area:testbench
regression_tests: testbench/scenarios/issue_298_audio_24khz_plays.scn, tests/unit/testbench/testbench_metrics_test.cc
---

# #298: Audio jitter-buffer delay and concealment read 0 at 24 kHz

## Issue

With `audio_sample_rate 24000` the test bench reported audio jitter-buffer
delay 0 ms and concealment 0 % on a link with 3 % loss, and mouth-to-ear
equal to the network delay, where libwebrtc reports 129 ms, 0.39 % and
219 ms on the same scenario (`issue_256_audio_24khz`, 10-seed means on
`parity-data`). Found by the Fixer while writing #296, on commit ccf6930:
https://github.com/aray4702/streaming_media/issues/298.

## Root cause

Not a counter: the call had no audio at all. The test bench used the
capture rate as the codec's rate, RTP clock and NetEq rate
(`testbench/call.cc:435` and `:486`, `audio_cfg.clock_rate = sr`). Since
#256, NetEq v2 discards packets at a rate it does not decode at (12, 24,
44.1 kHz), so every audio packet was dropped and the receiver played
silence (`audio.time_to_first_ms` 20000 on a 20 s call).

libwebrtc never sees such a clock. The capture is resampled to the encoder's
rate before encoding (`audio/audio_state.cc`
`UpdateAudioTransportWithSendingStreams`, the maximum of the sending
streams' encoder rates), and Opus stamps its packets with a 48 kHz RTP
clock whatever it encodes at
(`modules/audio_coding/codecs/opus/audio_encoder_opus.cc:70`
`kRtpTimestampRateHz`, `RtpTimestampRateHz()`), so NetEq decodes the call at
48 kHz (`audio_decoder_opus.cc:36`: 16 or 48 kHz only). `sm_compare`
runs libwebrtc at 48 kHz on every scenario.

## Fix

`AudioCodecRate()` (`testbench/call.cc`, declared in `call.h`): the bench's
stand-in Opus, its RTP clock, NetEq and the playout run at the capture rate
at 8, 16, 32 and 48 kHz (unchanged, so no other scenario moves) and at Opus's
48 kHz clock for 12, 24 and 44.1 kHz captures. NetEq v2 still discards a
clock it does not decode at (#256's unit and e2e tests keep that covered).

`issue_256_audio_24khz`'s `expect audio.time_to_first_ms > 19000` encoded
the discard, behaviour libwebrtc does not have on this call (libwebrtc 53 ms
there). It becomes `< 1000` (program.md 0.2, guard rails during a port;
`needs-human`).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_298_audio_24khz_plays.scn` | `audio.time_to_first_ms` 20000 (expect < 1000), `audio.jb_delay_p95_ms` 0 (> 40), `audio.concealed_pct` 0 (> 0), `audio.mouth_to_ear_p95_ms` 82.2 (> 120) | 52, 100, 0.25, 182.2 |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.AudioCodecRateIsOpusClockForCaptureRatesNetEqDoesNotRun` | does not build (no `AudioCodecRate`); a 24 kHz call has no audio | passes |

## Results

`issue_298_audio_24khz_plays` (the `issue_256_audio_24khz` link), after the
fix; before it, seed 1 gave 20000 ms / 0 ms / 0 % / 82.2 ms, as every packet
was discarded:

| seed | time to first (ms) | jb p95 (ms) | concealed % | mouth-to-ear p95 (ms) |
|---|---|---|---|---|
| 1 | 52 | 100 | 0.25 | 182.2 |
| 2 | 52 | 120 | 0.20 | 200.4 |
| 3 | 52 | 120 | 0.45 | 200.4 |
| 4 | 52 | 140 | 0.30 | 227.0 |
| 5 | 52 | 80 | 0.05 | 159.1 |
| mean | 52 | 112 | 0.25 | 193.8 |
| libwebrtc (10 seeds) | 53 | 129 | 0.39 | 219.0 |

The same scenario at 16 and 48 kHz gives identical numbers on every seed, so
what remains against libwebrtc is the jitter-buffer gap the 16 kHz calls
already have (owned by #62 / #63), not the rate.

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_256_audio_24khz | audio.jb_delay_p50_ms | 0 | 90 | REGRESSED |
| issue_256_audio_24khz | audio.jb_delay_p95_ms | 0 | 100 | REGRESSED |
| issue_256_audio_24khz | audio.mouth_to_ear_p95_ms | 82.17 | 182.2 | REGRESSED |
| issue_256_audio_24khz | audio.time_to_first_ms | 2e+04 | 52 | improved |
| issue_256_audio_24khz | sync.av_offset_* | - | 33.15 / 58.18 / 296.1 | added |
| issue_298_audio_24khz_plays | (new scenario) | - | - | new |

The three "regressions" are the call gaining its audio: a jitter buffer and
mouth-to-ear above the network delay, toward libwebrtc's 129 / 219 ms. No
other scenario changed.
