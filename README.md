<p align="center"><img src="docs/assets/logo.svg" alt="streaming_media logo" width="160"></p>

# streaming_media

**Vision:** people interact with AI using streaming audio and video.

**Mission:** develop and maintain an audio/video streaming library that
targets user experience on various platforms, devices and links.

streaming_media is a real-time audio/video streaming library, from servers
and browsers to phones and embedded devices, designed to keep calls working
over unstable links. It is developed by a program in which agents improve
it continuously.

## The program

The library evolves through a loop that [program.md](program.md) defines,
run by three AI agents with GitHub as the shared record:

```
 Tester ──files issues──> Fixer ──opens PRs──> CI + Reviewer ──approve──> main
   ^                                                                       │
   └──────────────── next run tests the improved main ─────────────────────┘
```

- **Tester** runs scripted calls in the [test bench](testbench/README.md)
  over emulated bad links (loss, jitter, outages, bandwidth drops, queues,
  cross traffic) and measures what users would notice: audio gaps, video
  freezes, delay and recovery time. It also runs the same scenarios
  through libwebrtc to find where this library falls short of it. Each
  problem it finds is filed as an issue that reproduces it in one command.
- **Fixer** turns each issue into a failing test, fixes the root cause and
  opens a PR. It also builds the `roadmap` features the Owner files.
- **Reviewer**, a separate GitHub App identity, checks every PR for
  correctness, tests, regressions against the
  [baseline](testbench/baseline.json) and "gaming" (loosened thresholds).
  A PR merges automatically once it has the Reviewer's approval and every
  platform in CI is green.
- **Owner** (a person) sets direction through `program.md` and `roadmap`
  issues, and decides anything an agent labels `needs-human`.

Progress is measured, not asserted: a change has to improve the
user-experience metrics or leave them unchanged, and every problem found
becomes a scenario that keeps it fixed. The goals, labels, quality gates
and guardrails are in [program.md](program.md), and the agents'
instructions are in [agents/](agents/).

## Scope

The library grows in two stages, both measured on the same test bench
([program.md section 0.1](program.md#01-scope-and-stages)):

1. **Baseline: what WebRTC handles** (current stage). These are the
   problems in sections 3-8 of
   [Problems WebRTC solves](docs/02-webrtc-problems-solved.md): packet
   loss, jitter and timing, congestion control, audio, video and
   synchronization. The bar is parity with libwebrtc's current
   mechanisms, not older or simpler variants. Parity is measured by
   running every test bench scenario through both libraries over the
   same emulated links ([comparison](docs/results/compare-webrtc.md)).
   Each gap is an issue labelled `stage:baseline`.
2. **Human-AI streaming.** Features for problems that arise because the
   other side of the call is an AI, added one at a time on top of the
   baseline without regressing it. Examples are a two-way conversation
   test bench, metrics for response delay, barge-in and how cleanly the
   user's speech reaches the AI, and AI media sent in faster-than-real-time
   bursts and cancelled on barge-in. These issues are labelled `stage:ai`.

Baseline work comes before AI work. Connectivity, security, data channels
and observability APIs (sections 1, 2, 9 and 10 of that document) are left
to the stack that hosts the library.

## The library

The library implements, in portable C++20 with no dependencies, the
resilience algorithms from sections 3-8 of
[Problems WebRTC solves](docs/02-webrtc-problems-solved.md): 46 algorithms
covering packet loss, jitter and timing, congestion control, audio
processing, video adaptation and synchronization. Each one has unit tests,
an end-to-end test over a simulated network, speed and quality benchmarks,
metrics and structured events, and a design and results document.

## Documents

Background research:

1. [Existing libraries](docs/01-library-survey.md): WebRTC stacks, SIP/RTP
   stacks, loss-tolerant transports, codecs and media frameworks, with a
   recommended starting point.
2. [Problems WebRTC solves](docs/02-webrtc-problems-solved.md): every problem
   area mapped to the mechanism that handles it, plus what WebRTC leaves to
   the application. Sections 3-8 are implemented here.
3. [WebRTC footprint](docs/03-webrtc-footprint.md): binary and RAM size of
   libwebrtc and lighter alternatives.
4. [Call data flow](docs/04-webrtc-call-data-flow.md): how media moves through
   a one-on-one call, from setup through send, receive and the feedback loop.

The library:

- [Program](program.md): vision, goals, agent roles, quality gates and
  guardrails of the self-evolution loop.
- [Test bench](testbench/README.md): a whole call through the library over
  a scripted unreliable link (loss, bursts, jitter, outages, capacity
  changes, bufferbloat, cross traffic), measured as user-experience metrics.
  The scenario suite and its baseline are the regression gate.
- [Fix records](docs/fixes/README.md): every problem found and fixed, with
  its root cause, fix, regression test and before/after results.
- [Browser port](web/): the library and test bench as a WebAssembly ES
  module with a JS API, plus a demo page.
- [Architecture](docs/design/architecture.md): layers, conventions,
  observability, and how the modules form a call.
- [Portability](docs/design/portability.md): platforms, presets, what was
  verified and how, performance across hardware, footprint.
- [Testing and benchmarking](docs/design/testing.md): test framework,
  network emulator, pass criteria, how to regenerate the results.
- [Algorithms](docs/algorithms/README.md): one document per algorithm
  (design, API skeleton, metrics, test methods, benchmarks, results,
  verification notes).
- [Verification results](docs/results/README.md): pass/fail and benchmark
  numbers per platform.
- [Comparison with libwebrtc](docs/results/compare-webrtc.md): every test
  bench scenario run through this library and a prebuilt libwebrtc over
  the same links ([how](compare/README.md)).

## Status

| Section | Algorithms |
|---|---|
| 3. Packet loss | NACK/RTX, XOR FEC, RED, PLC, keyframe requests (PLI/FIR/LNTF), temporal layers, NACK/FEC protection selection |
| 4. Jitter and timing | packet buffer, RTCP stats/RTT, delay manager, WSOLA time stretching, NetEq-lite audio jitter buffer, video jitter + render timing, clock drift |
| 5. Congestion | transport-cc + REMB, GCC delay- and loss-based estimation, GCC controller, pacer, probing, bitrate allocator, frame dropper, resolution/CPU adaptation, outage handling |
| 6. Audio (APM) | echo canceller, echo delay estimation, noise suppression, AGC + limiter, high-pass filter, transient suppression, DTX + comfort noise, VAD, audio level (RFC 6464), resampler |
| 7. Video | freeze detection, HW->SW codec fallback, orientation (CVO), content hints + screen VFR, codec negotiation, scaling, render scheduling, quality scaler |
| 8. Synchronization | RTP->NTP mapping, A/V lip sync with CNAME groups, absolute capture time / latency, header extensions + BUNDLE demux |

Verified locally on macOS arm64 (native), WebAssembly (Node), ASan+UBSan
and `-fno-exceptions -fno-rtti`: 213/213 test cases and 79/79 benchmarks
pass ([details](docs/results/README.md)). iOS, Android and embedded Linux
ARM are cross-built in CI. All six sections plus core compile to about
350 KiB of code.

**Stage 1 (parity with libwebrtc) is in progress.** In the first
[comparison](docs/results/compare-webrtc.md) (2026-10-14), libwebrtc was
better on 88 metric/scenario pairs, this library on 51, with 100 ties.
The largest gaps are needless keyframe requests, repair traffic beyond
the target bitrate, utilization under random loss, high jitter, and
recovery after outages. Each one is a `stage:baseline` issue.

## Building

```sh
cmake --preset native            # or: cmake -S . -B build
cmake --build --preset native
ctest --preset native            # unit + e2e tests + benchmark gates
out/native/bench/sm_bench        # all benchmarks; --filter=audio/ --json=out.json
out/native/examples/simulated_call
out/native/testbench/sm_testbench testbench/scenarios/*.scn   # call scenarios
```

The `wasm` preset also builds `out/wasm/web/` (`sm_web.js`, `sm_web.wasm`
and `index.html`). Serve that directory over HTTP to use the demo, or
`import createSmModule from './sm_web.js'` in your own page or in Node.

Other presets: `native-asan`, `embedded` (size-optimized, no
exceptions/RTTI, metrics and events compiled out), `wasm` (set
`EMSCRIPTEN_ROOT`), `ios`, `ios-simulator`, `android-arm64`,
`android-armv7` (set `ANDROID_NDK_HOME`), `linux-aarch64`, `linux-armv7`.
Sections can be disabled with `-DSM_WITH_LOSS|TIMING|CC|AUDIO|VIDEO|SYNC=OFF`.

`tools/run_all.sh all` rebuilds every configuration, reruns everything and
regenerates the algorithm and results documents.

## Using it

```cmake
find_package(streaming_media CONFIG REQUIRED)
target_link_libraries(app PRIVATE sm::sm)   # or sm::cc, sm::audio, ...
```

```cpp
#include "sm/cc/send_side_bwe.h"
#include "sm/timing/audio_jitter_buffer.h"

sm::cc::GccController gcc;                       // bandwidth estimation
auto update = gcc.Start(now);                    // target + pacing rate + probes
update = gcc.OnTransportFeedback(feedback, now); // on every transport-cc packet

sm::timing::AudioJitterBuffer jb;                // adaptive audio playout
jb.InsertPacket(rtp_timestamp, std::move(pcm), now);
jb.GetAudio(ten_ms_out, now);                    // every 10 ms from the audio device

sm::MetricsRegistry metrics;                     // observability
metrics.Register("send.gcc", &gcc);
metrics.Register("recv.neteq", &jb);
std::string prometheus = metrics.ToPrometheus();
```

Every API takes the current time as an argument and never reads a clock
or starts a thread, so the application chooses the threading model, and
the same code runs in real time on a device and in simulated time in
tests. See [examples/simulated_call.cc](examples/simulated_call.cc) for a
complete sender/receiver over an emulated unstable link.

## Layout

```
include/sm/{core,loss,timing,cc,audio,video,sync,sim}/   public headers
src/{core,loss,timing,cc,audio,video,sync}/              implementation
tests/{framework,unit,e2e}/                              tests and e2e simulations
bench/                                                   benchmarks
examples/                                                usage example
testbench/                                               call test bench, scenarios, baseline
web/                                                     WebAssembly bindings and demo page
agents/                                                  instructions for the Tester, Fixer and Reviewer agents
tools/                                                   run_all.sh, gen_docs.py, catalog.py, testbench_compare.py
docs/                                                    research, design, algorithms, results
```

## Decisions

- **Wire formats:** everything the library sends must be standard
  RTP/RTCP that any WebRTC-compatible stack can parse: RTP with RFC 8285
  header extensions and abs-capture-time, RTCP SR/RR, generic NACK, PLI,
  FIR, loss notification, transport-cc, REMB, RTX, RED, and ULPFEC over
  RED. Two gaps remain: FEC still uses its own simple format
  ([#29](https://github.com/aray4702/streaming_media/issues/29)), and NACK, PLI, FIR and loss notification have no wire
  format yet ([#28](https://github.com/aray4702/streaming_media/issues/28)). Connecting to a browser (ICE, DTLS-SRTP,
  SDP) is left to the stack that hosts the library.
- **RTP/RTCP in the library:** the library provides the RTP/RTCP session
  layer (sender, receiver, the codecs' standard RTP payload formats, RTCP
  scheduling and feedback dispatch), so an app passes encoded frames and
  packet bytes instead of wiring the building blocks together
  ([#31](https://github.com/aray4702/streaming_media/issues/31) design, [#32](https://github.com/aray4702/streaming_media/issues/32) implementation).
- **Send-side adaptation in the library:** frame dropping, quality
  scaling and rate allocation are part of the library too, so the app only
  runs its encoder and decoder ([#37](https://github.com/aray4702/streaming_media/issues/37)).
- **Call topology:** one-to-one calls only. The SFU and multiparty pieces
  (layer selection, active speaker detection, the audio mixer) have been
  removed ([#27](https://github.com/aray4702/streaming_media/issues/27)). BUNDLE demultiplexing stays, because one-to-one WebRTC
  calls bundle too.

## Open questions

- Target hardware: MCU or embedded Linux, and how much RAM and CPU? The
  algorithms need an FPU; the packet-level sections could also run on
  Cortex-M4F/M7-class MCUs.
