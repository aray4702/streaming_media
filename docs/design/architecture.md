# Architecture

`streaming_media` is a C++20 library of the resilience algorithms from
sections 3-8 of [Problems WebRTC solves](../02-webrtc-problems-solved.md):
packet-loss recovery, jitter and timing, congestion control, audio
processing, video adaptation and synchronization. It deliberately
contains no transport (ICE/DTLS/SRTP/SCTP), no codecs and no platform
I/O. Those come from a light stack such as libdatachannel or libpeer, the
SoC's hardware codecs and the OS, as recommended in the
[library survey](../01-library-survey.md) and the
[footprint analysis](../03-webrtc-footprint.md).

## Layers

```
            +-----------------------------------------------------------+
 sections   |  loss (3)  timing (4)  cc (5)  audio (6)  video (7)  sync (8) |
            +-----------------------------------------------------------+
 core       |  units  seq_num  metrics  event_log  fft  dsp  rtp_packet  |
            +-----------------------------------------------------------+
 test-only  |  sim/network_link  sim/signals  (header-only, installed)  |
            +-----------------------------------------------------------+
```

Each section is its own static library (`sm::loss`, `sm::timing`,
`sm::cc`, `sm::audio`, `sm::video`, `sm::sync`) on top of `sm::core`;
`sm::sm` links everything that is enabled. Dependencies between sections
are minimal and explicit:

| Library | Depends on | Why |
|---|---|---|
| `sm::loss` | core | |
| `sm::timing` | loss | the audio jitter buffer conceals with PLC (3.4) |
| `sm::cc` | core | |
| `sm::audio` | core | |
| `sm::video` | core | render times come in as plain `Timestamp`s |
| `sm::sync` | timing | RTCP NTP time types |

Any section can be compiled out with `-DSM_WITH_<SECTION>=OFF`. The
section tests build and pass on their own (verified for cc-only,
audio-only, loss-only and loss+timing+video builds).

## Conventions that make the code portable

**Time is a parameter, never a clock read.** Every method that depends on
time takes `Timestamp now`. The library has no clock, timer, thread or
sleep, so the same code runs in real time on a device and in simulated
time in the tests (60 s of a congested call simulates in about 10 ms).
The application decides how to schedule: one network thread, a task
queue, or an RTOS task.

**Strong units.** `Timestamp`, `TimeDelta`, `DataRate` and `DataSize`
(int64 microseconds, bits per second and bytes) make unit mistakes
compile errors. The Wasm build caught one: adding two `Timestamp`s.

**No exceptions, RTTI or global state.** Errors are `std::optional` or
status enums. The whole suite passes with `-fno-exceptions -fno-rtti`.

**Deterministic numerics.** All randomness (tests, simulators, comfort
noise) uses an in-tree xoshiro256** PRNG instead of `<random>`, whose
distributions differ between standard libraries. Measured values match
between native ARM64 and WebAssembly to the last printed digit.

**Fixed-rate media frames.** Audio algorithms take 10 ms frames of
`float` PCM at 8, 16, 32 or 48 kHz; video algorithms take I420 frames or
frame metadata. Codec bitstreams are opaque payloads.

**Bounded memory.** Histories, windows and queues have explicit limits
(NACK list, RTX history, FEC window, jitter buffer packets, pacer queue
time, transport-cc history), so a hostile or broken peer cannot grow
memory without bound.

## Observability

Every algorithm has a plain stats struct (always on, cheap: the
equivalent of WebRTC `getStats()`), and implements
`MetricsSource::ExportMetrics()`:

```cpp
sm::MetricsRegistry registry;
registry.Register("receiver.neteq", &jitter_buffer);
registry.Register("sender.gcc", &gcc);
std::string prom = registry.ToPrometheus();  // or ToJson()
```

Decisions (NACK sent, overuse detected, keyframe requested, delay change,
frame dropped late...) go to an optional `EventSink*` in each config.
`EventLog` keeps a ring buffer with JSON-lines export and an optional
callback (for example to `os_log`, logcat or a trace file); this is the
lightweight counterpart of the RTC event log. Building with
`SM_ENABLE_EVENTS=0` removes event calls entirely, and
`SM_ENABLE_METRICS=0` turns histogram updates and export into no-ops for
the smallest targets.

## How the pieces form a call

```
 SENDER                                              RECEIVER
 capture --> HPF -> AEC(6.1,6.2) -> NS -> VAD -> TS -> AGC          (6.x)
         --> DTX/CNG (6.7) -> RED (3.3) ---------------------------> RED rx -> NetEq-lite (4.3-4.5) -> PLC (3.4) -> mixer (6.10)
 camera  --> content policy (7.4) -> adaptation (5.9, 7.8) -> scaler (7.6) -> encoder
         --> frame dropper (5.8) -> temporal layers (3.6) -> FEC (3.2) --> packet buffer (4.1) -> keyframe req (3.5)
         --> RTX history (3.1) -> pacer (5.5) --> network -->          -> video timing (4.6) -> scheduler (7.7) -> freeze (7.1)
 GCC (5.1-5.4, 5.6) <---- transport-cc feedback, RTCP SR/RR (4.2) <---- NACK (3.1), PLI (3.5), RTCP
 bitrate allocator (5.7), protection (3.7), outage (5.11)              A/V sync (8.1, 8.2), latency (8.3), demux (8.4)
```

`examples/simulated_call.cc` wires a subset of this together (G.711 +
RED + NetEq-lite audio, GCC-paced video with NACK/RTX, metrics and
events) over the network emulator.

## What is out of scope

- Transport, security and session setup (sections 1-2 of the problems
  document), data channels (section 9) and the signaling, SFU topology
  and recording items WebRTC itself leaves to applications.
- Codec internals: Opus LBRR/DRED, encoder rate control, intra refresh,
  LTR frames, screen-content tools. The algorithms here drive codecs
  through configuration (target rate, resolution, keyframe requests,
  QP feedback).
- Platform audio/video device modules.

See [docs/algorithms](../algorithms/README.md) for the per-algorithm
documents and [docs/results](../results/README.md) for verification
results.
