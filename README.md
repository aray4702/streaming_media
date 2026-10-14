# streaming_media

A real-time audio/video streaming library for embedded systems and internet
video conferencing, designed to keep working over unstable data links.

The library implements, in portable C++20 with no dependencies, the
resilience algorithms from sections 3-8 of
[Problems WebRTC solves](docs/02-webrtc-problems-solved.md): 48 algorithms
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

The library:

- [Program](program.md): how the library evolves itself. Tester, Fixer and
  Reviewer agents find problems with the test bench, fix them and review
  the fixes through GitHub issues and PRs.
- [Test bench](testbench/README.md): a whole call through the library over
  a scripted unreliable link (loss, bursts, jitter, outages, capacity
  changes, bufferbloat, cross traffic), measured as user-experience metrics.
  The scenario suite and its baseline are the regression gate.
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

## Status

| Section | Algorithms |
|---|---|
| 3. Packet loss | NACK/RTX, XOR FEC, RED, PLC, keyframe requests (PLI/FIR/LNTF), temporal layers, NACK/FEC protection selection |
| 4. Jitter and timing | packet buffer, RTCP stats/RTT, delay manager, WSOLA time stretching, NetEq-lite audio jitter buffer, video jitter + render timing, clock drift |
| 5. Congestion | transport-cc + REMB, GCC delay- and loss-based estimation, GCC controller, pacer, probing, bitrate allocator, frame dropper, resolution/CPU adaptation, SFU layer selection, outage handling |
| 6. Audio (APM) | echo canceller, echo delay estimation, noise suppression, AGC + limiter, high-pass filter, transient suppression, DTX + comfort noise, VAD, audio level + active speaker, mixer, resampler |
| 7. Video | freeze detection, HW->SW codec fallback, orientation (CVO), content hints + screen VFR, codec negotiation, scaling, render scheduling, quality scaler |
| 8. Synchronization | RTP->NTP mapping, A/V lip sync with CNAME groups, absolute capture time / latency, header extensions + BUNDLE demux |

Verified locally on macOS arm64 (native), WebAssembly (Node), ASan+UBSan
and `-fno-exceptions -fno-rtti`: 213/213 test cases and 79/79 benchmarks
pass ([details](docs/results/README.md)). iOS, Android and embedded Linux
ARM are cross-built in CI. All six sections plus core compile to about
350 KiB of code.

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

## Open questions

- Target hardware: MCU or embedded Linux, and how much RAM and CPU? The
  algorithms need an FPU; the packet-level sections could also run on
  Cortex-M4F/M7-class MCUs.
- Must browsers join calls directly (which requires WebRTC compatibility)?
  The wire formats here (RTCP SR/RR, transport-cc, REMB, RED, RTX, RFC 8285
  extensions, abs-capture-time) are the standard ones; FEC uses its own
  simple format.
- One-to-one calls only, or group calls through an SFU? The SFU-side pieces
  (layer selection, active speaker, BUNDLE demux, mixer) are included.
