# WebRTC footprint

Full libwebrtc is about **5–15 MB of stripped binary per CPU architecture**
and **tens to a few hundred MB of RAM per call**. It is far too big for MCUs
and heavy even for small embedded Linux devices.

All figures here are rough estimates that vary a lot with version,
configuration and platform. Measure the actual build before deciding.

## Approximate footprint

| Stack | Binary (stripped, release) | Runtime RAM (1:1 call) | Runs on |
|---|---|---|---|
| **libwebrtc** (Google, default config) | ~6–12 MB per ABI, e.g. Android `libjingle_peerconnection_so.so` on arm64 | ~30–150 MB, depending on resolution, simulcast and codecs | Phones, PCs, embedded Linux with ≥512 MB RAM in practice |
| libwebrtc, aggressively trimmed | ~3–5 MB | ~20–50 MB | Embedded Linux |
| **Pion** (Go) | ~10–20 MB static binary (Go runtime included) | ~10–50 MB | Embedded Linux |
| **libdatachannel** + libsrtp + usrsctp + TLS | ~1–3 MB (mostly the TLS library) | A few MB, plus codecs | Embedded Linux, small SBCs |
| **KVS WebRTC C SDK** | ~0.5–1.5 MB with mbedTLS | A few MB | Cameras, embedded Linux, some RTOSes |
| **libpeer** (ESP32 etc.) | A few hundred KB of flash | ~100–300 KB SRAM, plus PSRAM for video | MCUs |

The **source checkout** is a burden too. Google's WebRTC needs `depot_tools`
and `gclient sync`, which pulls ~10–20 GB including Chromium toolchains and
third-party code. It builds with GN/Ninja and Chromium's own clang and libc++.

## Why libwebrtc is big

### 1. It implements everything

Every row of [Problems WebRTC solves](02-webrtc-problems-solved.md) is a real
subsystem: ICE/STUN/TURN, DTLS, SRTP, SCTP, RTP/RTCP with dozens of feedback
types, GCC with the pacer and probing, NetEq, the video jitter buffer,
AEC3/NS/AGC2, simulcast/SVC, the stats engine and the event log. Each is tens
to hundreds of KB, and there are a lot of them.

### 2. It bundles its own codecs (the largest single factor)

- **libvpx** (VP8 + VP9 encoder and decoder): often the biggest chunk, several
  MB.
- **AV1**: libaom encoder and dav1d decoder, also MB-scale.
- **H.264**: OpenH264 encoder plus an FFmpeg decoder subset, when enabled.
- **Audio**: Opus, G.711, G.722, iLBC, and formerly iSAC.

Browsers must decode whatever the other side sends, so libwebrtc ships them
all. An embedded device usually needs exactly one video codec, often in
hardware.

### 3. It vendors its dependencies instead of using system libraries

BoringSSL (~1 MB+), usrsctp, libsrtp, libyuv, Abseil, protobuf (for event
logs), PFFFT/FFT code, RNNoise-style models, and its own libc++ in some
builds.

### 4. Multiple code paths for each CPU feature and platform

- SIMD variants (SSE2, SSSE3, AVX2, NEON) in libvpx, libyuv, AEC3 and
  resamplers, each compiled in and chosen at runtime.
- Both fixed-point and float implementations of some audio processing.
- Audio device and capture modules for Windows, macOS, iOS, Android, Linux
  (ALSA and PulseAudio), and wrappers for hardware codecs (MediaCodec,
  VideoToolbox).

### 5. It is built for browser robustness, not for size

- Legacy and compatibility code coexists with the modern paths: Plan B
  alongside Unified Plan, REMB alongside transport-cc, old jitter buffers
  alongside new ones.
- Heavy C++ (templates, Abseil, inlining, `std::function`) compiled for speed,
  not `-Os`.
- Extensive logging strings, field trials (runtime A/B experiment flags that
  keep both code paths alive), and the stats plumbing.
- Few compile-time switches to cut whole subsystems out. The library is
  designed as all-or-nothing.

### 6. RAM is high by design

- **Video frames are large.** One 720p I420 frame is about 1.4 MB, and the
  capture, scaling, encode, decode and render stages each keep frame pools.
- **Simulcast** runs 2–3 encoders at once, each with its own reference frames.
- **Buffers sized for bad networks:** jitter buffers, retransmission history
  for NACK, FEC buffers and pacer queues.
- **Threads:** signaling, worker and network threads, plus encoder, decoder,
  audio device and task-queue threads, each with its own stack.
- AEC3 keeps sizable filter and delay buffers.

## Making libwebrtc smaller

If libwebrtc must be used:

- **GN args:**
  - drop unused codecs: `rtc_libvpx_build_vp9=false`,
    `rtc_include_ilbc=false`, the dav1d/libaom options, `rtc_use_h264` as
    needed
  - leave out extras: `rtc_include_tests=false`, `rtc_build_examples=false`,
    `rtc_enable_protobuf=false`
  - `is_debug=false`, `symbol_level=0`
- **Linker and compiler:** `-Os`, LTO, `--gc-sections`, strip.
- **Hardware codecs:** inject the SoC's hardware encoder and decoder through
  `VideoEncoderFactory` and remove libvpx completely.
- **System TLS:** link against system OpenSSL or BoringSSL if the platform
  already ships it.
- **Measure:** run `bloaty` to see what is actually taking space before
  cutting anything.

## Implications for this library

The size comes from **generality** (every codec, platform and legacy path),
not from the core resilience algorithms. The parts that actually handle
unstable links are relatively compact: NACK, FEC, transport-cc with a
GCC-style controller, an adaptive jitter buffer, PLC and keyframe requests.
Each is a few thousand lines.

A sensible embedded design is a light transport such as libdatachannel or
libpeer, plus a hardware video codec and Opus, plus only the resilience
modules needed. Some can be taken straight from libwebrtc: AEC3/APM and NetEq
can be extracted and built on their own.
