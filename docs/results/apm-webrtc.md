# Audio processing vs libwebrtc's APM (#63, section 6)

`sm_apm_compare` (`compare/webrtc/apm_compare.cc`) runs this library's
audio processing modules and libwebrtc M157's APM on the same test signals
and reads both outputs with the same metric code. The signals are the
section 6 end-to-end scenarios (`tests/e2e/audio_sims.h`, built from
`include/sm/sim/signals.h`): a speech-like far end played into a room
(`RoomImpulseResponse`, 40 ms tail, RT60 100 ms), a speech-like near-end
talker, white noise, DC and 50 Hz hum. Everything is 16 kHz mono in 10 ms
frames, one seed (the signals are deterministic).

```sh
compare/webrtc/fetch.sh
CXX=clang++-21 cmake -S compare/webrtc -B out/compare -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/compare -j 4 --target sm_apm_compare   # prints the table after the build
```

## What runs on each side

| Module | Ours | libwebrtc (settings) |
|---|---|---|
| High-pass filter | `HighPassFilter`: 4th-order Butterworth at 80 Hz | `HighPassFilter` (`high_pass_filter.cc`): three cascaded biquads with zeros near 35 and 46 Hz |
| Echo canceller | `EchoCanceller`: two-filter PBFDAF NLMS + residual suppressor, `EchoDelayEstimator` | AEC3 (`aec3/`), `set_stream_delay_ms(0)` |
| Noise suppressor | `NoiseSuppressor`: Wiener filter, decision-directed SNR, 18 dB floor | `ns/` at `kHigh` (18 dB), the level `WebRtcVoiceEngine::ApplyOptions` sets |
| Gain control | `AutomaticGainControl` (AGC2-style adaptive digital) + limiter | AGC1 `kAdaptiveAnalog`, the desktop default in `ApplyOptions`, with APM's analog mic gain emulation (no device volume in a simulation); and AGC2 adaptive digital at its defaults |
| VAD | `Vad` | `webrtc::CreateVad(kVadNormal)` (`common_audio/vad`); APM itself no longer runs a VAD |
| Transient suppressor | `TransientSuppressor` | none: M157's `audio_processing_impl.cc` has no transient suppression (the config field is deprecated) |

The chains match the e2e tests in `tests/e2e/audio_e2e_test.cc`: the
high-pass filter runs first on every case, the double-talk case adds noise
suppression after the echo canceller, and the VAD reads the high-passed
capture on both sides. libwebrtc's APM is driven the way
`AudioTransportImpl` drives it: render frame, then capture frame, with the
analog level fed back for AGC1.

## Results

`main` @ c40f9e3 plus this tool. ERLE is the echo's energy over the
output's in the window. SNRs are against the clean talker through the same
side's high-pass filter, at the output delay (0-40 ms) that gives the best
SNR. "Pauses" are frames more than 250 ms after the talker stopped.

| module | scenario | metric | ours | libwebrtc | note |
|---|---|---|---|---|---|
| high-pass filter | dc + 50 Hz hum | hum attenuation (dB) | 16.4 | 67.2 |  |
| high-pass filter | dc + 50 Hz hum | DC left (x1000) | -0.0 | 0.0 |  |
| high-pass filter | talker only | speech level change (dB) | -0.0 | -0.2 |  |
| echo canceller | far end only, route change | ERLE 1-3 s (dB) | 15.3 | 33.2 | convergence |
| echo canceller | far end only, route change | ERLE 6-12 s (dB) | 29.5 | 50.1 |  |
| echo canceller | far end only, route change | ERLE 12-14 s (dB) | 2.6 | 8.0 | just after the delay change |
| echo canceller | far end only, route change | ERLE 18-24 s (dB) | 25.6 | 49.0 |  |
| echo canceller + NS | full duplex | ERLE far end alone 6-15 s (dB) | 30.2 | 67.0 |  |
| echo canceller + NS | full duplex | near-end level change in double talk (dB) | -0.5 | -5.3 |  |
| echo canceller + NS | full duplex | near-end SNR in double talk (dB) | 1.9 | -0.2 |  |
| noise suppressor | white noise, ~7 dB SNR | SNR improvement (dB) | 5.3 | -0.1 | libwebrtc kHigh (18 dB), ours 18 dB |
| noise suppressor | white noise, ~7 dB SNR | noise attenuation in pauses (dB) | 16.9 | 17.9 |  |
| noise suppressor | white noise, ~7 dB SNR | level change in speech (dB) | -1.3 | -2.9 |  |
| noise suppressor | white noise, ~7 dB SNR | algorithmic delay (ms) | 10.0 | 6.0 |  |
| gain control | quiet then loud talker, AGC1 (default) | quiet talker in -> out, 12-20 s (dBFS) | -16.8 | -20.6 | input -32 dBFS |
| gain control | quiet then loud talker, AGC1 (default) | loud talker level change, 32-40 s (dB) | -2.0 | -3.2 |  |
| gain control | quiet then loud talker, AGC1 (default) | noise level in pauses, 12-20 s (dBFS) | -58.2 | -63.0 |  |
| gain control | quiet then loud talker, AGC1 (default) | peak (dBFS) | -1.0 | 0.0 |  |
| gain control | quiet then loud talker, AGC2 digital | quiet talker in -> out, 12-20 s (dBFS) | -16.8 | -21.0 | input -32 dBFS |
| gain control | quiet then loud talker, AGC2 digital | loud talker level change, 32-40 s (dB) | -2.0 | -1.0 |  |
| gain control | quiet then loud talker, AGC2 digital | noise level in pauses, 12-20 s (dBFS) | -58.2 | -62.9 |  |
| gain control | quiet then loud talker, AGC2 digital | peak (dBFS) | -1.0 | 0.0 |  |
| VAD | white noise, ~10 dB SNR | speech frames detected (%) | 99.4 | 99.9 | libwebrtc kVadNormal |
| VAD | white noise, ~10 dB SNR | noise frames flagged as speech (%) | 0.0 | 0.0 |  |

## Reading it

There is no parity band for these metrics yet (`tools/parity_report.py`
covers the call metrics only). Read with about 1 dB as a tie:

- **High-pass filter: a gap.** libwebrtc's filter is a different design
  (three biquads with zeros just below 50 Hz), so it takes 50 Hz hum down
  by 67 dB against our 16 dB. Both remove DC and leave speech alone.
- **Echo canceller: a gap in both directions.** AEC3 removes 20-37 dB more
  echo, converges faster and recovers sooner after the echo path changes.
  In double talk it also takes about 5 dB off the near-end talker, where
  ours keeps the level (-0.5 dB) and its output has a slightly better
  near-end SNR (1.9 vs -0.2 dB).
- **Noise suppressor: a gap.** At the same 18 dB floor both remove about
  the same noise in pauses, but libwebrtc's output is a worse copy of the
  speech waveform (SNR unchanged, against +5.3 dB for ours), with 1.6 dB
  more speech attenuation, and it is 4 ms earlier.
- **Gain control: a gap.** libwebrtc's default (AGC1 adaptive analog) and
  AGC2 both lift the quiet talker to about -21 dBFS; ours targets -18 and
  reaches -16.8. Ours keeps peaks at -1 dBFS (limiter ceiling) and its
  pauses 5 dB louder. The default mode is a different controller too:
  libwebrtc's desktop default drives the microphone volume (AGC1 analog);
  ours is digital only.
- **VAD: no gap** on this signal.
- **Transient suppressor: a difference in kind.** libwebrtc M157 has none
  in APM; ours is extra.

## Caveats

- One signal set and one seed per case. The signals are synthetic
  (harmonic speech-like source, white noise), so perceptual metrics
  (PESQ/POLQA-like) are out of scope.
- The table above was produced in the agent sandbox, which can't reach
  googlesource for Chromium's libc++ headers: it was built with clang 22
  and the llvm-project libc++ headers of 2026-09-30 (`a187057df`), the
  newest revision that links against the prebuilt libwebrtc. The
  `compare-webrtc` CI job builds it with the exact headers and prints the
  same table in its log (the target's post-build step).
