---
issue: 594
title: AEC3 and the noise suppressor corrupt memory when built for a rate other than 16/32/48 kHz; AGC2 asserts at 0 Hz
severity: sev:2
status: fixed
area: area:audio
regression_tests: testbench/scenarios/issue_594_apm_rate_8000.scn, testbench/scenarios/issue_594_apm_rate_44100.scn, tests/unit/audio/aec3_test.cc, tests/unit/audio/ns_test.cc, tests/unit/audio/agc2_test.cc
---

# #594: AEC3 and the noise suppressor corrupt memory at an unsupported rate

## Issue

An app that built the ported echo canceller (`aec3::EchoCanceller3`) or noise
suppressor (`ns::NoiseSuppressor`) for a sample rate other than 16, 32 or
48 kHz got silent memory corruption instead of an error: a UBSan null
reference at 8/12 kHz and 0 Hz, an ASan heap-buffer-overflow at 44.1 kHz, an
uncaught `std::length_error` at -16 kHz (AEC3), and a heap-buffer-overflow
read at 8 and 11.025 kHz (NS). `agc2::GainController2` asserted at 0 Hz in the
VAD's resampler. Found by the Tester while probing PR #585, on `main` at
ad495fd: https://github.com/aray4702/streaming_media/issues/594.

## Root cause

Nothing checked the rate. libwebrtc's `EchoCanceller3` and its parts
`RTC_DCHECK(ValidFullBandRate(sample_rate_hz))`
(`compare/webrtc/reference/modules/audio_processing/aec3/echo_canceller3.cc:786`,
`block_processor.cc:99`, `echo_remover.cc:282`), the noise suppressor
DCHECKs 16/32/48 kHz in `NumBandsForRate` (`ns/noise_suppressor.cc:34`), and
`AudioProcessingImpl` only ever hands them a processing rate it picked from
those three. Our port defined `aec3::ValidFullBandRate` but never called it:

- `EchoCanceller3::EchoCanceller3` (`src/audio/aec3.cc:535`) sized its state
  with `NumBandsForRate(rate) = rate / 16000`: 0 bands below 16 kHz (empty
  blocks, the null reference in `Block::View`), and a negative count at
  -16 kHz (`std::length_error`); at 44.1 kHz the render queue's frames had
  fewer bands than the views (`FillSubFrameView`, the overflow).
- `ns::NoiseSuppressor` (`src/audio/ns.cc:650`) mapped every rate below
  32 kHz to one 160-sample band and read `num_channels * 160` samples from
  whatever span the caller passed, so a caller that sized the frame as
  10 ms at 8 kHz (80 samples) was read past its end (`ns.cc:715`).
- `agc2::GainController2` built its VAD resampler and limiter from
  `sample_rate / 100` with no floor, so 0 Hz reached
  `assert(request_frames_ > 0)`.
- The mono 16 kHz convenience overloads of `EchoCanceller3` copied a span of
  any length into a 160-sample frame (`aec3.cc:630`).

## Fix

Each module validates its configuration in the constructor and reports it
through `valid()`. libwebrtc has DCHECKs here, which our library can't use (no
exceptions, no aborts in a release build), so an invalid module leaves the
audio untouched on every call instead, as the issue and the coordinator's
comment asked; there is no resampling fallback, which libwebrtc doesn't have
at this layer either.

- `EchoCanceller3`: `valid_ = ValidFullBandRate(rate)`; the internal state is
  built at 16 kHz when it is not valid, so it is consistent, and
  `AnalyzeRender`, `AnalyzeCapture` and `ProcessCapture` return at once. The
  mono convenience calls also need one band, one capture channel and a
  160-sample span.
- `NoiseSuppressor`: valid at 16, 32 or 48 kHz only; `Analyze` and `Process`
  also ignore a span shorter than `num_channels() * frame_samples()`.
- `GainController2`: valid from 8 kHz (APM's lowest supported rate,
  `ValidateAudioFormat` in `audio_processing_impl.cc`) to 48 kHz (the
  limiter's `kMaximalNumberOfSamplesPerChannel` = 480 samples per 10 ms,
  `agc2/limiter.cc:99`); `Process` also needs a frame of `sample_rate / 100`
  samples.

Nothing changes at a valid rate with well-formed frames. The test bench gets
an `apm_rate <hz>` key that builds the three modules at that rate before the
call and reports `apm.*` metrics, so the regression scenarios can check it;
the bench call itself still runs no APM, so no other scenario and no parity
cell moves.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_594_apm_rate_8000.scn` | the base `sm_testbench` rejects the scenario (`apm_rate` doesn't exist there); the issue's driver shows the UBSan null reference in AEC3 and the ASan overflow in NS at 8 kHz on the base | `apm.aec3_valid` 0, `apm.ns_valid` 0, `apm.agc2_valid` 1, `apm.untouched` 1 |
| `testbench/scenarios/issue_594_apm_rate_44100.scn` | as above (AEC3's heap-buffer-overflow at 44.1 kHz) | the same |
| `tests/unit/audio/aec3_test.cc` `Aec3.EchoCanceller3RejectsUnsupportedRates` (0, 8000, 11025, 12000, 24000, 44100, -16000), `Aec3.EchoCanceller3RunsAtFullBandRates`, `Aec3.EchoCanceller3MonoCallsNeed16kHzFrames` | doesn't compile on the base (no `valid()`); the same calls corrupt memory there | pass, also under ASan+UBSan |
| `tests/unit/audio/ns_test.cc` `Ns.RejectsUnsupportedRates`, `Ns.RejectsShortFrames`, `Ns.RunsAtFullBandRates` | as above | pass, also under ASan+UBSan |
| `tests/unit/audio/agc2_test.cc` `GainController2.RejectsUnsupportedRatesAndFrameLengths` (0, -16000, 4000, 96000, a 20 ms frame), `GainController2.RunsAtApmRates` (8000 to 48000) | as above | pass, also under ASan+UBSan |

## Results

Deterministic (no seeds apply). Both scenarios pass on seeds 1-5.

The scenario suite against the baseline: no metric changed (the bench call
runs no APM); the two new scenarios add their own baseline files.
