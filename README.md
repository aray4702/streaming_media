<p align="center"><img src="docs/assets/logo.svg" alt="streaming_media logo" width="160"></p>

# streaming_media

Human interacts with AI using streaming audio and video. This project will develop and maintain an audio/video streaming library that
targets human-AI experience on various platforms, devices and links.

streaming_media is a real-time audio/video streaming library, from servers
and browsers to phones and embedded devices, designed to keep calls working
over unstable links. It is developed by a program in which agents improve
it continuously.

## The program

The library evolves through a loop that [program.md](agents/program.md) defines,
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
  [baseline](testbench/baseline/) and "gaming" (loosened thresholds).
  A PR merges automatically once it has the Reviewer's approval and every
  platform in CI is green.
- **Owner** (a person) sets direction through `agents/program.md` and `roadmap`
  issues, and decides anything an agent labels `needs-human`.

Progress is measured, not asserted: a change has to improve the
user-experience metrics or leave them unchanged, and every problem found
becomes a scenario that keeps it fixed. The goals, labels, quality gates
and guardrails are in [program.md](agents/program.md), and the agents'
instructions are in [agents/](agents/).

## Scope

The library grows in two stages, both measured on the same test bench
([program.md section 0.1](agents/program.md#01-scope-and-stages)):

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

Baseline work comes before AI work. **Milestone 3
([#944](https://github.com/aray4702/streaming_media/issues/944)) ports sections 1, 2, 9 and 10** (connectivity,
security, data channels and observability) from libwebrtc too, so the
library no longer leaves them to a host stack. The Owner chose standard
WebRTC on the wire for both topologies: the AI server (client-server) and
P2P calls on web, Android and iOS, with 1:1 calls and small-group mesh, no
SFU. What is ported and how it differs:
[libwebrtc parity, section 10](docs/design/libwebrtc-parity.md#10-connectivity-milestone-3-944).

## The library

The library implements, in portable C++20, the
resilience algorithms from sections 3-8 of
[Problems WebRTC solves](docs/02-webrtc-problems-solved.md): 46 algorithms
covering packet loss, jitter and timing, congestion control, audio
processing, video adaptation and synchronization. Each one has unit tests,
an end-to-end test over a simulated network, speed and quality benchmarks,
metrics and structured events, and a design and results document.

Sections 1-8 have no dependencies. The connectivity layer (milestone 3)
needs two libraries, both fetched by CMake at configure time (see
`cmake/`): a TLS library behind one interface, BoringSSL (default) or
mbedTLS 3.6, chosen with `SM_TLS_BACKEND`, and libsrtp 2.8.0. Build with
`-DSM_WITH_TLS=OFF` to leave both out.

## Documents

Background research:

1. [Existing libraries](docs/01-library-survey.md): WebRTC stacks, SIP/RTP
   stacks, loss-tolerant transports, codecs and media frameworks, with a
   recommended starting point.
2. [Problems WebRTC solves](docs/02-webrtc-problems-solved.md): every problem
   area mapped to the mechanism that handles it, plus what WebRTC leaves to
   the application. Sections 3-8, and since milestone 3 sections 1, 2, 9 and 10, are implemented here.
3. [WebRTC footprint](docs/03-webrtc-footprint.md): binary and RAM size of
   libwebrtc and lighter alternatives.
4. [Call data flow](docs/04-webrtc-call-data-flow.md): how media moves through
   a one-on-one call, from setup through send, receive and the feedback loop.
5. [Streaming options](docs/05-streaming-options.md): passthrough, bitrate
   ladder, live re-encode and simulcast/SVC for streaming a video file, and
   when each one fits.

The library:

- [Program](agents/program.md): vision, goals, agent roles, quality gates and
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
- [libwebrtc parity](docs/design/libwebrtc-parity.md): what is ported from
  libwebrtc M157 and how it differs, area by area; section 10 covers the
  connectivity ports of milestone 3.
- [Self-hosted TURN server](docs/deploy/turn-server.md): coturn on Amazon
  Lightsail, for tests on real networks and production.
- [Fuzz targets](fuzz/README.md): libFuzzer targets for the parsers that
  read network input.

## Status

| Section | Algorithms |
|---|---|
| 3. Packet loss | NACK/RTX, XOR FEC, RED, PLC, keyframe requests (PLI/FIR/LNTF), NACK/FEC protection selection |
| 4. Jitter and timing | packet buffer, RTCP stats/RTT, delay manager, WSOLA time stretching, NetEq-lite audio jitter buffer, video jitter + render timing |
| 5. Congestion | transport-cc + REMB, GCC delay- and loss-based estimation, GCC controller, pacer, probing, bitrate allocator, frame dropper, resolution/CPU adaptation, outage handling |
| 6. Audio (APM) | echo canceller, echo delay estimation, noise suppression, AGC + limiter, high-pass filter, transient suppression, VAD, audio level (RFC 6464), resampler |
| 7. Video | freeze detection, HW->SW codec fallback, orientation (CVO), content hints + screen VFR, render scheduling, quality scaler |
| 8. Synchronization | RTP->NTP mapping, A/V lip sync with CNAME groups, absolute capture time / latency, header extensions + BUNDLE demux |
| 1. Connectivity (milestone 3) | STUN, ICE agent with controller and ICE-lite, TURN over UDP, TCP and TLS, SDP offer/answer and an endpoint that sets up ICE, DTLS-SRTP and RTP from SDP (#1062-#1064); small-group mesh of 3-4 (#1072) |
| 2. Security (milestone 3) | TLS and DTLS 1.2/1.3 on both backends (BoringSSL, mbedTLS), DTLS-SRTP, SRTP (libsrtp) |
| 9. Data channels (milestone 3) | dcSCTP over DTLS, DCEP and the data channel API, behind `SM_WITH_DATACHANNEL` (#1069, #1070) |
| 10. Observability (milestone 3) | build and runtime switches (`SM_ENABLE_STATS`, `SM_ENABLE_METRICS`, `SM_ENABLE_EVENTS`, `SM_LOG_MIN_LEVEL`); ICE and DTLS connection stats (#1059) |
| Apple devices (milestone 4) | Audio Units I/O and AVFoundation camera capture (`sm::device`, #1115), VideoToolbox H.264 (#1112); the library and its unit tests run on the iOS simulator in CI (#1107, #1108) |

The native build passes 590 of 590 ctest tests on macOS arm64 (main 8677716f
plus #1112, 2026-11-02; `interop.Coturn` skips without a local coturn), and
the iOS simulator build passes its 238 unit tests (Xcode 27.2, the CI job's
flags). The older full runs on WebAssembly (Node), ASan+UBSan and
`-fno-exceptions -fno-rtti` cover sections 3-8 only
([details](docs/results/README.md)). Android and embedded Linux ARM are
cross-built in CI.

**Stage 1 (parity with libwebrtc) has no open gaps** on main: see
[Benchmarks](#benchmarks).

## Benchmarks

All numbers below come from the [test bench](testbench/README.md): a whole
call through the library over a scripted emulated link, in simulated
time, so a run is repeatable.

### Classic WebRTC features vs libwebrtc

The `compare-webrtc` CI job runs every one-way scenario through this
library and through libwebrtc M157 over the same emulated links, 10 seeds
each, and publishes the [parity report](https://raw.githubusercontent.com/aray4702/streaming_media/parity-data/parity.md)
and the [side-by-side table](https://raw.githubusercontent.com/aray4702/streaming_media/parity-data/compare-webrtc.txt).
On main a78f737a (2026-11-02):

- **163 scenarios, 0 gaps:** no metric is behind libwebrtc, or ahead of
  it, by more than the parity band (the larger of the tie floor, 5% and
  one standard deviation of libwebrtc's seeds).
- Of the 3,540 metric/scenario pairs, 3,453 tie within the tie floor;
  42 lean towards this library and 45 towards libwebrtc, all inside the
  band. In the first comparison (2026-10-14), libwebrtc was better on 88
  pairs and this library on 51.
- Not compared: the 20 two-way (user and AI) scenarios, the 3 mesh
  scenarios and 7 that libwebrtc can't run the same way (`sm_compare`
  runs one-way calls).

A sample of the table (mean of 10 seeds, this library / libwebrtc):

| Scenario | Audio concealed | Audio mouth-to-ear p95 | Video freezes (total) | Video end-to-end p95 | Link utilization | Queue p95 |
|---|---|---|---|---|---|---|
| clean_link: 2.5 Mbps, 25 ms one way | 0.3% / 0.3% | 245 / 245 ms | 0 / 0 ms | 137 / 137 ms | 88.6% / 88.6% | 75.8 / 75.8 ms |
| random_loss_5pct: 2 Mbps, 40 ms | 0.3% / 0.3% | 236 / 228 ms | 415 / 479 ms | 253 / 253 ms | 72.2% / 72.2% | 29.9 / 30.2 ms |
| burst_loss: 3% in bursts of ~6 | 1.4% / 1.4% | 299 / 288 ms | 413 / 419 ms | 284 / 276 ms | 75.9% / 76.1% | 46.0 / 46.6 ms |
| high_jitter: 40 ms jitter, reordering | 0.2% / 0.2% | 247 / 249 ms | 0 / 0 ms | 181 / 181 ms | 22.9% / 22.9% | 4.7 / 4.7 ms |
| capacity_drop: 2.5 Mbps to 500 kbps and back | 0.9% / 0.9% | 824 / 824 ms | 3,076 / 3,076 ms | 543 / 540 ms | 54.8% / 54.8% | 287 / 287 ms |
| bufferbloat: 1 Mbps, 2 s router buffer | 0.1% / 0.1% | 191 / 191 ms | 0 / 0 ms | 115 / 113 ms | 90.1% / 90.1% | 44.1 / 44.2 ms |
| cross_traffic: 1.5 Mbps flow joins a 3 Mbps link | 0.5% / 0.5% | 494 / 494 ms | 0 / 0 ms | 226 / 226 ms | 72.9% / 72.9% | 164 / 164 ms |
| issue_23_outage_3s: 3 s outage | 10.1% / 10.1% | 199 / 199 ms | 3,184 / 3,185 ms | 113 / 113 ms | 85.8% / 85.8% | 39.1 / 39.1 ms |
| audio_only_lossy: 10% loss with jitter | 0.5% / 0.5% | 155 / 152 ms | | | 77.0% / 77.1% | 4.1 / 3.6 ms |

### Human-AI conversation features

libwebrtc has no counterpart for these (stage 2, milestone 2: #2, #3, #4),
so they are measured against the targets in
[program.md section 6.3](agents/program.md) instead. The values are the
test bench baseline on main (Linux x86_64, the CI `testbench` job). Each
call is two-way audio on a 100 ms RTT link with three scripted user turns:

- **Response delay** is what the transport adds between the end of the
  user's turn and the first AI sample heard, not counting the AI's own
  time (thinking, the 600 ms its endpointer waits, and generating the first
  chunk). Target: p95 under 500 ms.
- **User speech lost** is the share of the user's speech that the AI
  side concealed or never played. Target: at most 2%.
- **Barge-in stop** is the time from the user starting to talk over the
  AI to the AI's voice stopping at the user's ear. The user's device mutes
  the AI locally as soon as it hears its user, then the AI cancels the
  rest of its reply.

| Scenario | Response delay p50 / p95 | User speech lost | Barge-in stop | Turn-detection errors |
|---|---|---|---|---|
| conversation_clean | 290 / 308 ms | 0% | | 0 |
| conversation_loss_5pct | 320 / 338 ms | 0.37% | | 0 |
| conversation_jitter_50ms | 380 / 416 ms | 0.22% | | 0 |
| conversation_burst_loss (5% in bursts) | 310 / 310 ms | 5.7% | | 0 |
| conversation_barge_in | 270 / 306 ms | 0% | 190 ms | 0 |
| conversation_barge_in_capacity_drop | 360 / 360 ms | 0.96% | 210 ms | 0 |
| conversation_outage_user_turn (2 s outage in the user's turn) | 270 / 306 ms | 22% | 3,390 ms | 2 |

AI-generated media (#4): the AI side's source produces speech at 3x real
time in 1 s bursts, and the library sends it at real time or, with
`ai_send_ahead`, slightly early so the user's jitter buffer holds a
cushion.

| Scenario | Response delay p50 / p95 | Source ahead of real time | Underruns | User speech lost | Barge-in |
|---|---|---|---|---|---|
| ai_source_clean | 280 / 298 ms | up to 2.1 s | 0 | 0% | |
| ai_source_loss_5pct | 320 / 329 ms | up to 2.1 s | 0 | 0% | |
| ai_source_capacity_drop | 305 / 307 ms | up to 2.1 s | 0 | 0% | |
| ai_source_adaptive_loss_5pct (adaptive send-ahead) | 450 / 470 ms | up to 1.9 s | 0 | 0.27% | |
| ai_source_barge_in | 260 / 314 ms | up to 1.8 s | 0 | 0% | stopped in 210 ms; 5.7 s of queued reply dropped; 0 ms of AI audio heard after the stop |

Two scenarios are outside the conditions the targets cover (up to 5%
random loss), and their numbers are expected:

- `conversation_burst_loss` (5% loss in bursts of about 5 packets each way)
  loses 5.7% of the user's speech on the baseline seed (3.05% mean over 20
  seeds). The Owner decided the 2% target doesn't apply to it (#965).
  Audio NACK (`up_audio_nack on`, #971, off by default) brings it to a 1.1%
  mean, the option for native clients.
- `conversation_outage_user_turn` (the user-to-AI link down for 2 s in the
  middle of the user's turn) loses 22% of the user's speech, and the AI's
  turn detector sees a false end of turn and a false barge-in. Only the
  severity-1 target applies: the AI still hears the user and answers
  every turn.

### On real networks (milestone 4)

- The library's TURN client against the self-hosted coturn on Amazon
  Lightsail (Oregon), from a home network: allocations, permissions,
  channel binds and relayed data both ways pass over UDP, TCP and TLS
  (`interop.Coturn`, #1054).
- The AI server (`apps/ai_server`, #1103) and the calling app's controller
  (`apps/call`, #1117) are tested end to end over local sockets; calls over
  the Lightsail relay, with these metrics measured at the caller, are C12.

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
Sections can be disabled with `-DSM_WITH_LOSS|TIMING|CC|AUDIO|VIDEO|SYNC|RTP=OFF`. `SM_WITH_RTP` (the RTP/RTCP session layer) needs loss, timing, cc, sync and video.

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
include/sm/{rtp,p2p,tls,pc,dcsctp}/                      session and connectivity headers
src/{core,loss,timing,cc,audio,video,sync,rtp}/          implementation
src/{p2p,tls,pc,dcsctp}/                                 connectivity (milestone 3)
tests/{framework,unit,e2e}/                              tests and e2e simulations
tests/interop/                                           tests against real sockets and coturn
fuzz/                                                    libFuzzer targets for network parsers
bench/                                                   benchmarks
examples/                                                usage example
testbench/                                               call test bench, scenarios, baseline
web/                                                     WebAssembly bindings and demo page
agents/                                                  the program, the agents' instructions and their scripts (tools/)
tools/                                                   run_all.sh, gen_docs.py, catalog.py, testbench_compare.py
docs/                                                    research, design, algorithms, results
```

## Decisions

- **Wire formats:** everything the library sends must be standard
  RTP/RTCP that any WebRTC-compatible stack can parse: RTP with RFC 8285
  header extensions and abs-capture-time, RTCP SR/RR, generic NACK, PLI,
  FIR, loss notification, transport-cc, REMB, RTX, RED, and ULPFEC over
  RED, with the standard payload formats for Opus (RFC 7587) and H.264
  (RFC 6184, packetization mode 1). All of these are in place and checked
  against libwebrtc M157 in CI, except FEC, which still uses its own
  simple format ([#29](https://github.com/aray4702/streaming_media/issues/29)). H.264 is the only video format so far, and
  loss notification stays off for it, as in libwebrtc ([#47](https://github.com/aray4702/streaming_media/issues/47)).
  Connecting to a browser (ICE, DTLS-SRTP, SDP) is part of the library
  now (milestone 3, [#944](https://github.com/aray4702/streaming_media/issues/944)), also standard WebRTC on the wire;
  SDP offer/answer is in progress (#1063, #1064).
- **Build and runtime switches:** `SM_WITH_TLS` and `SM_TLS_BACKEND`
  (`boringssl` or `mbedtls`) select the TLS library, `SM_WITH_DATACHANNEL`
  adds data channels, `SM_ENABLE_STATS`, `SM_ENABLE_METRICS` and
  `SM_ENABLE_EVENTS` compile the stats, metrics and event log in or out,
  and `SM_LOG_MIN_LEVEL` sets the lowest log level compiled in.
- **RTP/RTCP in the library:** the library provides the RTP/RTCP session
  layer (sender, receiver, the codecs' standard RTP payload formats, RTCP
  scheduling and feedback dispatch), so an app passes encoded frames and
  packet bytes instead of wiring the building blocks together. Done: the
  test bench runs every call through it ([#31](https://github.com/aray4702/streaming_media/issues/31) design, [#32](https://github.com/aray4702/streaming_media/issues/32) implementation).
- **Codecs:** the library contains no audio or video encoder or decoder.
  The app passes encoded Opus frames and H.264 access units in, and
  decodes audio through an `AudioDecoder` it supplies. The test bench
  uses a synthetic encoder: correctly framed H.264 that hits its target
  size, with no real picture or sound behind it.
- **Send-side adaptation in the library:** frame dropping, quality
  scaling and rate allocation are part of the library too, so the app only
  runs its encoder and decoder. Done so far ([#37](https://github.com/aray4702/streaming_media/issues/37)):
  `rtp::VideoSendController` runs the frame dropper, keyframe policy and
  pause per captured frame (the encoder loop is in
  [architecture.md](docs/design/architecture.md)); the quality scaler and
  resolution adaptation exist as building blocks but aren't wired in yet.
- **Call topology:** one-to-one calls, plus small-group mesh (milestone 3,
  C9). No SFU: the SFU and multiparty pieces (layer selection, active
  speaker detection, the audio mixer) have been removed ([#27](https://github.com/aray4702/streaming_media/issues/27)).
  BUNDLE demultiplexing stays, because WebRTC calls bundle.

## Open questions

- Target hardware: MCU or embedded Linux, and how much RAM and CPU? The
  algorithms need an FPU; the packet-level sections could also run on
  Cortex-M4F/M7-class MCUs.
