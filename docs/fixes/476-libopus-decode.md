---
issue: 476
title: "Harness: our side doesn't decode audio, libwebrtc decodes with libopus"
severity: sev:2
status: fixed
area: area:audio, area:testbench
regression_tests: testbench/scenarios/issue_476_libopus_decode.scn, tests/unit/testbench/opus_decoder_test.cc
---

# #476: Harness: our side doesn't decode audio, libwebrtc decodes with libopus

## Issue

A difference in the test harness, found by the harness fidelity audit (#478)
under the Owner's rule "one test environment for both stacks" (PR #475):
https://github.com/aray4702/streaming_media/issues/476. On libwebrtc's side of
`sm_compare`, NetEq decodes every packet with libopus 55513e81 through
`AudioDecoderOpusImpl` (`builtin_audio_decoder_factory`, decoder map
`opus/48000/2`). Our side decoded nothing: a packet "decoded" to the original
20 ms of the `SpeechLike` signal for its RTP timestamp, and an LBRR copy to the
same clean samples. So our NetEq concealed, merged and time-stretched the clean
source while libwebrtc's worked on lossy decoded audio, and every `audio.*`
cell of the comparison carried that difference.

## Root cause

The test bench, not the library: `SpeechDecoder` in `testbench/call.cc`
(lines 99-125 on 9e436cb) modelled the decoder after #460 had replaced the
encoder model with libopus. The library has no codec; its jitter buffer takes
decoded samples from an `rtp::AudioDecoder` the app provides.

## Fix

- `testbench/opus_decoder.h` `OpusReceiveDecoder`: libopus called as
  `AudioDecoderOpusImpl` calls it (compare/webrtc/reference/
  `modules/audio_coding/codecs/opus/audio_decoder_opus.cc`,
  `opus_interface.cc`): one decoder state at 48 kHz, one channel (the
  `opus/48000/2` format without `stereo=1`); `WebRtcOpus_Decode` with room for
  120 ms; `DecodeRedundantInternal` -> `WebRtcOpus_DecodeFec` (the packet's
  samples per frame, `decode_fec = 1`) for a packet with an LBRR copy, a plain
  decode for a RED block; `WebRtcOpus_FecDurationEst`.
- `OpusAudioDecoder` (the bench's `rtp::AudioDecoder`): our receiver decodes
  on arrival, NetEq at playout, so it keeps libopus's call sequence to NetEq's.
  Each frame is decoded once (duplicates, RED copies and LBRR copies of frames
  that arrived get the earlier samples and leave libopus alone); an LBRR copy is
  decoded only for a frame not seen yet; frames are decoded in timestamp order
  (libopus's state is saved per frame, and a late frame is decoded from the
  state after the frame before it, the later frames again after it); a primary
  replaces its LBRR copy. At a bench rate other than 48 kHz the output is
  resampled with `audio::PushSincResampler`.
- The test bench links `sm::audio` (and needs `SM_WITH_AUDIO`). The library
  stays codec-free; `compare/webrtc/webrtc_call.cc` is unchanged.
- `tools/parity_owners.json`: #476 and #477 own the `audio.*` cells until both
  land (the coordinator's brief on the issue).

The decoded samples go to the jitter buffer as floats, `int16 / 32767`, the
inverse of the encoder input's `OpusSendEncoder::ToPcm`.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_476_libopus_decode.scn` | `audio.concealed_pct` 0.4229 (expect > 0.47) | 0.5183 |
| `tests/unit/testbench/opus_decoder_test.cc` `OpusDecoder.InOrderSamplesAreLibwebrtcs` | no decoder (the model returns the source) | 100 frames, 0 samples differ from libwebrtc's calls |
| `OpusDecoder.LostFrameDecodesTheLbrrCopyAsLibwebrtc` | as above | LBRR copies and the next frames equal `WebRtcOpus_DecodeFec` / `WebRtcOpus_Decode` |
| `OpusDecoder.DuplicatesDoNotTouchTheDecoder`, `ReorderedFrameIsDecodedInTimestampOrder`, `LatePrimaryReplacesTheLbrrCopy`, `OtherBenchRatesAreResampled`, `ConfiguredAsAudioDecoderOpusImpl` | as above | pass |

## Results

`audio_only_lossy` (the regression scenario's link), seeds 1-5, before -> after:

| seed | metric | before | after |
|---|---|---|---|
| 1 | `audio.concealed_pct` | 0.4229 | 0.5183 |
| 2 | `audio.concealed_pct` | 0.3240 | 0.3369 |
| 3 | `audio.concealed_pct` | 0.5012 | 0.5409 |
| 4 | `audio.concealed_pct` | 0.4905 | 0.5231 |
| 5 | `audio.concealed_pct` | 0.3506 | 0.4753 |

libwebrtc on the same scenario, seeds 1-10: 0.28-0.96 %, mean 0.509 (ours with
the model: mean 0.459).

`issue_62_neteq_v2_high_jitter`, seeds 1-10: `audio.jb_delay_p95_ms` mean 133
-> 132 (libwebrtc 129), `audio.concealed_pct` mean 0.129 -> 0.142 (libwebrtc
0.163). Seed 1's p95 moves 170 -> 180, one 10 ms step past its guard-rail line
(`<= 170`, set at libwebrtc's worst seed by #457); libwebrtc's own seeds reach
180 (seed 3). The line moves to 180 in its own commit, for the Owner's
sign-off (needs-human): the Owner's harness rule doesn't let an agent loosen a
line for a harness change.

The whole suite on seed 1 against libwebrtc's seed 1 (compare-webrtc on
9e436cb): of the 184 `audio.*` cells that changed, 103 moved closer to
libwebrtc and 81 farther. The 10-seed parity on the PR's `compare-webrtc` run
decides.

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`),
before the baseline update:

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_150_gcc_v2_high_jitter_seed10 | audio.late_discards | 0 | 3 | REGRESSED |
| issue_29_ulpfec_over_red | cc.target_max_drop_pct | 20.16 | 21.23 | REGRESSED |
| issue_29_ulpfec_over_red | video.frames_rendered | 114 | 101 | REGRESSED |
| issue_29_ulpfec_over_red | video.render_ratio_pct | 12.72 | 11.27 | REGRESSED |
| issue_415_cross_80pct_standing_queue | audio.longest_interruption_ms | 52 | 119 | REGRESSED |
| issue_5_burst15_freeze | audio.interruption_ms | 233 | 401 | REGRESSED |

These are seed-1 moves of a harness change, inside libwebrtc's per-seed range
on the same scenarios: `issue_415_cross_80pct_standing_queue` longest
interruption 119 (libwebrtc seed 1 97, seeds 1-10 29-160);
`issue_5_burst15_freeze` interruption 401 ms (libwebrtc's longest
interruption per seed reaches 410). The video and cc cells move through lip
sync, which follows the audio playout delay.
