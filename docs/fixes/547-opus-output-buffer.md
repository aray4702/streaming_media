---
issue: 547
title: libopus payloads part mid-call between the stacks with the same encoder inputs
severity: sev:2
status: fixed
area: area:audio, area:testbench
regression_tests: testbench/scenarios/issue_547_opus_output_buffer.scn, tests/unit/testbench/opus_encoder_test.cc
---

# #547: libopus payloads part mid-call between the stacks with the same encoder inputs

## Issue

The comparison must give both stacks the same encoder input (`agents/program.md`
0.2). On `issue_533_opus_decoder_frame_older_than_window`, though, the two stacks'
Opus payloads parted at frame 240 (4.81 s) with the same loss input on both sides
(20 %, clamped). Ours was 1 byte larger, and 185 of the first 682 frames differed.
The product audit #542 found it (row 1 of `docs/design/harness-fidelity.md`), on
`main` at 89384ba: https://github.com/aray4702/streaming_media/issues/547.

## Root cause

Yardstick #550 added per-packet encode rows on both stacks (PCM hash, bitrate, and
the allocation, overhead and fraction lost each encoder was given). On every traced
scenario they show the same PCM and the same 32 kbps on both sides, and on
`issue_533` the same loss fraction up to packet 302. So at frame 240 every traced
input matched, and the payloads still differed.

The input that differed is the output buffer. `AudioEncoderOpusImpl::EncodeImpl`
passes `SufficientOutputBufferSize()` to `WebRtcOpus_Encode` as `max_encoded_bytes`
(`compare/webrtc/reference/modules/audio_coding/codecs/opus/audio_encoder_opus.cc:574-587`,
`:617-624`). That is twice the bytes the configured bitrate gives a packet, rounded up
per millisecond: 200 bytes at 32 kbps. `OpusSendEncoder::Encode`
(`testbench/opus_encoder.h`, before the fix) passed a fixed 1500 bytes. libopus's
rate control codes to `max_data_bytes`, so on some frames the two sizes code
different bits.

Offline, re-encoding the scenario's PCM with libopus 55513e81 (the SIMD build, as in
libwebrtc.a) and libwebrtc's loss schedule:
- with 1500 bytes, 335 of 1000 frames differ from libwebrtc's;
- with 200 bytes, 2 differ, the same 2 that differ between libopus builds on our own
  side.

## Fix

`OpusSendEncoder::SufficientOutputBufferSize()` ports libwebrtc's formula, and
`Encode` hands libopus that buffer. The encode rows and the libwebrtc-side
pass-through wrapper stay, so the drift check can follow the encoder's inputs.
`issue_533_opus_decoder_frame_older_than_window` joins `sm_compare`'s `kDefaultTraced`.

Payloads change on the frames where the buffer matters, and everything downstream of
audio packet sizes moves with them (the pacer, GoogCC and video on the mixed
scenarios). So the PR regenerates `testbench/baseline/` and is judged on the 10-seed
parity report, as the Coordinator set out on #547.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_547_opus_output_buffer.scn` (seed 1) | `net.audio_kbps` 64.92 (expect < 64.915): 34291 payload bytes in 6 s, libwebrtc 34283 | 64.91: 34283 bytes, every payload hash equal to libwebrtc's |
| `tests/unit/testbench/opus_encoder_test.cc` `OpusEncoder.CodesLibwebrtcsFramesOnIssue533` | frames 240, 242 and 244 are 55, 82 and 66 bytes (libwebrtc 54, 81, 67) | frames 236-244 are libwebrtc's sizes |
| `tests/unit/testbench/opus_encoder_test.cc` `OpusEncoder.OutputBufferIsLibwebrtcsSufficientSize` | (new function) | 200, 40 and 2560 bytes at 32, 6 and 510 kbps |

The scenario's threshold is libwebrtc's byte count for seed 1, the seed its trace
comes from. Other seeds code other speech and have no libwebrtc trace, so they are
not compared. Seeds 2-5 before -> after: 68.95 -> 68.89, 66.94 -> 66.94,
75.69 -> 75.65, 76.02 -> 75.91 kbps.

## Results

On `issue_533` (seed 1, payloads up to the frame where the loss inputs part,
frame 682), against libwebrtc's trace on #550:

| | payloads differing from libwebrtc's |
|---|---|
| `main` (CI) | 185 of 682 |
| this fix (local, plain-C libopus) | 2 of 682 (frames 572-573, the plain-C vs SIMD build difference that `main` shows locally too; `sm_compare` links libwebrtc's libopus on both sides) |

Suite on seed 1: 63 metrics regressed and 49 improved against the old baseline. They
are all downstream of the changed audio packet sizes, and the baseline is
regenerated. The parity report on the PR judges the change across 10 seeds.
