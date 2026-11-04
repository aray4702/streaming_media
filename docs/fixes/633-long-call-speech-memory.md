---
issue: 633
title: Test bench uses about 15 MB per simulated minute (900 MB at 60 min) because it builds the whole call's speech up front
severity: sev:3
status: fixed
area: area:testbench
regression_tests: testbench/scenarios/issue_633_long_call_speech_memory.scn, tests/unit/testbench/speech_pcm_test.cc
---

# #633: Test bench uses about 15 MB per simulated minute (900 MB at 60 min) because it builds the whole call's speech up front

## Issue

`sm_testbench` needed about 15 MB of RAM per simulated minute: 61 MB for a 5 min call, 901 MB for a 60 min call (the
longest the bench allows), on the native Release build. That limits the long-call (soak, coverage area 5) scenarios and
the browser demo, which builds the bench to WebAssembly. The memory is the bench's own, not the library's. The Tester
found it in run 65 on commit 82af33b: https://github.com/aray4702/streaming_media/issues/633.

## Root cause

Each send side built the whole call's scripted speech in its constructor, before the call started
(`testbench/call.cc:1023-1031` on dd070af5, `SendSide::SendSide`): `sim::SpeechLike(48000, duration + 1 s, seed + 7)`
returned a float vector for the whole call (4 B a sample), and the loop copied it into `opus_pcm_`, a 16-bit PCM vector
for the whole call (2 B a sample, grown by `push_back`, so up to twice that while it doubled). Both were alive at once
at the peak: 48 kHz x 6 B or more = about 17 MB per simulated minute, per send side (a two-way call has two, a mesh call
one per directed pair). The call itself only ever reads the frame it encodes (`CaptureAudio`, `SendAheadFrame`) and,
in a two-way call with turns, a frame the listeners hear a little later (`FramePcm`), so almost all of it sat unused.

## Fix

The bench makes the speech as the call reads it, 1 s at a time, and gives every reader the samples the whole signal
had:

- `include/sm/sim/signals.h`: `sim::SpeechLikeGenerator` is SpeechLike's loop with its state in members, so `Next()`
  gives the samples one at a time. `sim::SpeechLike` now fills its vector from it: the same statements in the same
  order, so the samples are bit for bit what they were (a unit test pins this against a copy of the old loop, on every
  platform, and the whole scenario suite's JSON is byte for byte unchanged).
- `testbench/speech_pcm.h` (new): `SpeechPcm` gives frame k of the call's 48 kHz PCM, fewer samples at the end of the
  signal and none past it, as the old `opus_pcm_` subspans did. It keeps the last 4 blocks of 1 s that were read
  (375 KB) and the generator's state at the start of the last 128 blocks (about 15 KB), so a frame read again a little
  later (the listeners, #960 and #961) costs nothing, an older one is remade from its block's start state, and one
  older than that (no reader lags 2 min) is remade from the signal's start.
- `testbench/call.cc`: `SendSide` holds a `SpeechPcm` instead of `opus_pcm_`; `CaptureAudio`, `SendAheadFrame` and
  `FramePcm` read their frame from it. `CaptureAudio` records whether a frame was speech from the speaking test
  itself rather than by comparing the frame's pointer with the silence buffer's (same result).
- The bench reports its own use as `bench.speech_pcm_peak_kb` in a new `Report::bench` map (all send sides' peaks
  added up). It is the bench's resource use, not the call's, so it stays out of `Report::metrics`: the JSON, the
  baseline and the comparison with libwebrtc never see it. `EvaluateExpectations` looks a name up there when
  `metrics` doesn't have it, so an `expect` line can check it; `--metrics` lists it.

`compare/webrtc/webrtc_call.cc` still builds libwebrtc's side's signal whole (`sim::SpeechLike`, unchanged samples);
`sm_compare` runs scenarios of up to 2 min, so it was left alone. `sm_compare` builds and runs with this change
(checked locally on macOS arm64 against the prebuilt libwebrtc).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_633_long_call_speech_memory.scn` (2 min on the issue's link, `expect bench.speech_pcm_peak_kb <= 375`) | fails: the bench has no bound to report (`<unknown metric>`); it held the call's 121 s of PCM, 11344 KB, and 22688 KB of float while building it | 375 KB on every seed (4 blocks of 1 s) |
| `tests/unit/testbench/speech_pcm_test.cc` `TestbenchSpeechPcm.GeneratorGivesTheWholeSignalsSamples` | (new API) | `SpeechLike` and the generator give the old loop's samples bit for bit, and a copied generator carries on |
| `TestbenchSpeechPcm.FramesAreTheWholeSignals` | (new API) | every frame read in order, ahead (send-ahead), 2.8 s behind, far behind, and at and past the end equals the whole signal's frame |
| `TestbenchSpeechPcm.HoldsAFewBlocksWhateverTheLength` | (new API) | a 60 min signal holds at most 4 blocks (375 KB, under 1/100 of the whole call's PCM) |
| `TestbenchSpeechPcm.OlderThanTheKeptStatesStillReadsTheSignal` | (new API) | a block older than the kept start states reads the signal's samples |

## Results

Peak RSS (`/usr/bin/time -l`, maximum resident set size) of the native Release `sm_testbench`, the issue's
`flat60.scn` link (`at 0s capacity=3Mbps delay=30ms jitter=10ms loss=1%`, audio and video, seed 1), macOS arm64,
before (dd070af5) and after; the two runs of each length ran side by side:

| simulated duration | peak RSS before | after | run time before | after |
|---|---|---|---|---|
| 5 min | 128 MB | 20 MB | 9.2 s | 9.4 s |
| 60 min | 936 MB | 100 MB | 119.6 s | 121.2 s |

The JSON report of each run is byte for byte the same before and after. What still grows after the fix (about
1.4 MB per simulated minute, 80 MB of the 100 MB at 60 min) is the bench's metric bookkeeping, not the speech: a live
heap snapshot of a 20 min call 16 min in (`malloc_history -allBySize`) shows `RepairMeter`'s per-video-packet
`video_send_ms_` map and `received_` set (`testbench/call_common.h`, about 1.2 MB per minute; it needs every packet to
compute `video.residual_loss_pct` at the end, and `sm_compare` shares it for libwebrtc's side), the per-frame
`VideoFrameTimes`, and the 100 ms time series. Bounding those would change how whole-call metrics are computed and is
not part of this issue's cause.

The regression scenario, seeds 1-5: `bench.speech_pcm_peak_kb` 375 on each (fails on dd070af5 on each: no such
measure, the whole call's signal held).

The scenario suite against the baseline (`tools/testbench_compare.py testbench/baseline r.json`): 0 regressions,
0 improvements, 0 changed metrics; the one new row is this scenario. The JSON of all 195 existing scenarios is byte for
byte the same as dd070af5's on seed 1, and every report value is the same on seeds 2 and 3 (each scenario's `seed` line changed, both binaries; the same 8 and 10 scenarios fail their expectations on those seeds before and after).
