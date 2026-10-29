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
| High-pass filter | `hpf::HighPassFilter`, libwebrtc's (#239) | `HighPassFilter` (`high_pass_filter.cc`): three cascaded biquads with zeros near 35 and 46 Hz |
| Echo canceller | `aec3::EchoCanceller3`, libwebrtc's AEC3 (#240), fed as APM feeds it | AEC3 (`aec3/`), `set_stream_delay_ms(0)` |
| Noise suppressor | `ns::NoiseSuppressor`, libwebrtc's NS at kHigh (#241) | `ns/` at `kHigh` (18 dB), the level `WebRtcVoiceEngine::ApplyOptions` sets |
| Gain control | AGC1 as a call runs it (`AgcManagerDirect` with the emulated mic volume, #242); AGC2 (`agc2::GainController2`) for its row | AGC1 `kAdaptiveAnalog`, the desktop default in `ApplyOptions`, with APM's analog mic gain emulation (no device volume in a simulation); and AGC2 adaptive digital at its defaults |
| VAD | `Vad` | `webrtc::CreateVad(kVadNormal)` (`common_audio/vad`); APM itself no longer runs a VAD |
| Transient suppressor | `TransientSuppressor`, off by default since #243 (`enabled = false`, as libwebrtc's deprecated config) | none: M157's `audio_processing_impl.cc` has no transient suppression (the config field is deprecated) |

The chains match the e2e tests in `tests/e2e/audio_e2e_test.cc`: the
high-pass filter runs first on every case, the double-talk case adds noise
suppression after the echo canceller, and the VAD reads the high-passed
capture on both sides. libwebrtc's APM is driven the way
`AudioTransportImpl` drives it: render frame, then capture frame, with the
analog level fed back for AGC1.

## Results

Since #565 the library's own v1 modules (Butterworth high-pass filter,
PBFDAF echo canceller, STFT-Wiener noise suppressor, AGC2-style gain
control and limiter, Kaiser resampler) are retired, and every row compares
libwebrtc's port with libwebrtc. The libwebrtc column is the last
`sm_apm_compare` run (`main` @ c40f9e3). The ours column is this library
on #565's branch, from the e2e tests that measure each case the way
`sm_apm_compare` does (`E2E_HighPassFilter.LibwebrtcHpfAsLibwebrtc`,
`E2E_EchoDelay.Aec3FollowsAudioRouteChangeAsLibwebrtc`,
`E2E_EchoCanceller.Aec3FullDuplexCallAsLibwebrtc`,
`E2E_NoiseSuppressor.LibwebrtcNsAsLibwebrtc`,
`E2E_GainControl.Agc*LiftsQuietTalkerAsLibwebrtc`) and the last
`sm_apm_compare` run for the VAD. ERLE is the echo's energy over the
output's in the window. SNRs are against the clean talker through the same
side's high-pass filter, at the output delay (0-40 ms) that gives the best
SNR. "Pauses" are frames more than 250 ms after the talker stopped.

| module | scenario | metric | ours | libwebrtc | note |
|---|---|---|---|---|---|
| high-pass filter | dc + 50 Hz hum | hum attenuation (dB) | 67.2 | 67.2 | ours is libwebrtc's filter (#239) |
| high-pass filter | dc + 50 Hz hum | DC left (x1000) | 0.0 | 0.0 |  |
| high-pass filter | talker only | speech level change (dB) | -0.2 | -0.2 |  |
| echo canceller | far end only, route change | ERLE 1-3 s (dB) | 33.2 | 33.2 | ours is libwebrtc's AEC3 (#240) |
| echo canceller | far end only, route change | ERLE 6-12 s (dB) | 50.1 | 50.1 |  |
| echo canceller | far end only, route change | ERLE 12-14 s (dB) | 8.0 | 8.0 | just after the delay change |
| echo canceller | far end only, route change | ERLE 18-24 s (dB) | 49.0 | 49.0 |  |
| echo canceller + NS | full duplex | ERLE far end alone 6-15 s (dB) | 67.0 | 67.0 |  |
| echo canceller + NS | full duplex | near-end level change in double talk (dB) | -5.4 | -5.3 |  |
| echo canceller + NS | full duplex | near-end SNR in double talk (dB) | -0.2 | -0.2 |  |
| noise suppressor | white noise, ~7 dB SNR | SNR improvement (dB) | -0.1 | -0.1 | both kHigh (18 dB) |
| noise suppressor | white noise, ~7 dB SNR | noise attenuation in pauses (dB) | 17.9 | 17.9 |  |
| noise suppressor | white noise, ~7 dB SNR | level change in speech (dB) | -2.9 | -2.9 |  |
| noise suppressor | white noise, ~7 dB SNR | algorithmic delay (ms) | 6.0 | 6.0 |  |
| gain control | quiet then loud talker, AGC1 (default) | quiet talker in -> out, 12-20 s (dBFS) | -20.6 | -20.6 | input -32 dBFS |
| gain control | quiet then loud talker, AGC1 (default) | loud talker level change, 32-40 s (dB) | -3.2 | -3.2 |  |
| gain control | quiet then loud talker, AGC1 (default) | noise level in pauses, 12-20 s (dBFS) | -63.0 | -63.0 |  |
| gain control | quiet then loud talker, AGC1 (default) | peak (dBFS) | 0.0 | 0.0 |  |
| gain control | quiet then loud talker, AGC2 digital | quiet talker in -> out, 12-20 s (dBFS) | -21.0 | -21.0 | input -32 dBFS |
| gain control | quiet then loud talker, AGC2 digital | loud talker level change, 32-40 s (dB) | -1.0 | -1.0 |  |
| gain control | quiet then loud talker, AGC2 digital | noise level in pauses, 12-20 s (dBFS) | -62.9 | -62.9 |  |
| gain control | quiet then loud talker, AGC2 digital | peak (dBFS) | 0.0 | 0.0 |  |
| VAD | white noise, ~10 dB SNR | speech frames detected (%) | 99.4 | 99.9 | libwebrtc kVadNormal; ours measured behind the retired v1 high-pass filter (the next `compare-webrtc` log prints it behind libwebrtc's) |
| VAD | white noise, ~10 dB SNR | noise frames flagged as speech (%) | 0.0 | 0.0 |  |

## Reading it

There is no parity band for these metrics yet (`tools/parity_report.py`
covers the call metrics only). Read with about 1 dB as a tie: every row is
a tie. The high-pass filter, echo canceller, noise suppressor and gain
control are libwebrtc's code (#239-#242), and the v1 modules that showed
gaps here (16 dB less hum attenuation, 20-37 dB less ERLE, a different NS
and AGC) are gone (#565).

- **VAD: no gap** on this signal. Since #671 ours is libwebrtc's
  `webrtc::Vad` (6.8, kVadNormal) over the bit-exact `WebRtcVad` port
  (`agc1_vad.h`), so the next `compare-webrtc` log should show the same
  decisions on both sides; the v1 energy/flatness detector is retired.
- **Transient suppressor: off by default, as libwebrtc** (#243). libwebrtc
  M157 has none in APM; ours stays as an explicit opt-in
  (`TransientSuppressorConfig::enabled`).

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
