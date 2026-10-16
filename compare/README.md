# Comparisons with other libraries

`compare/` runs the [test bench](../testbench/README.md) scenarios through
other real-time media stacks, over the same emulated links, and reports
the same user-experience metrics side by side. It measures how far this
library is from the stage 1 bar in
[program.md section 0.1](../program.md#01-scope-and-stages): parity with
libwebrtc on the problems in sections 3-8.

| Library | Status | Directory |
|---|---|---|
| libwebrtc (M157, prebuilt) | Working on macOS arm64; Ubuntu x86_64/arm64 builds are published but not yet tried | [webrtc/](webrtc/) |

Latest results and the parity report: [docs/results/compare-webrtc.md](../docs/results/compare-webrtc.md).

## libwebrtc

```sh
compare/webrtc/fetch.sh                      # ~300 MB download into out/webrtc-deps
cmake -S compare/webrtc -B out/compare -DCMAKE_BUILD_TYPE=Release
cmake --build out/compare -j
out/compare/sm_compare testbench/scenarios/*.scn                  # mean of 3 seeds (CI uses --seeds=10)
out/compare/sm_compare --seeds=5 --json=r.json --dump=runs/ a.scn # JSON summary (means, per-seed values), full per-run reports
python3 tools/parity_report.py r.json --owners tools/parity_owners.json   # gaps with libwebrtc, in both directions
```

The build needs clang 21 or newer (Apple clang 21 works): the libc++ the
binary was built with rejects older compilers. On Ubuntu, install it with
`wget -qO- https://apt.llvm.org/llvm.sh | sudo bash -s -- 21` and configure
with `CXX=clang++-21`.

The whole suite (13 scenarios, 3 seeds, both libraries) runs in about
20 s, and the same seed gives the same result.

### How it works

- **The binary.** [fetch.sh](webrtc/fetch.sh) downloads a release from
  [shiguredo-webrtc-build](https://github.com/shiguredo-webrtc-build/webrtc-build),
  which publishes `libwebrtc.a` with headers for macOS and Ubuntu. That
  library is built against Chromium's own libc++ (`std::__Cr`), so the
  script also fetches the libc++ revision and `__config_site` named in
  the release's `VERSIONS` file. Everything in `sm_compare`, including
  this library's sources, is compiled against that libc++, which is why
  it is a separate CMake project.
- **The call.** [webrtc_call.cc](webrtc/webrtc_call.cc) creates two
  `webrtc::Call`s, the layer under `PeerConnection`, as sender and
  receiver. Signaling and ICE are not involved. It configures them the
  way Chrome negotiates a call:
  - **Audio:** Opus at the scenario's bitrate, with in-band FEC, plus
    RED when the scenario has `red on`.
  - **Video:** NACK + RTX and ULPFEC (via RED), each following the
    scenario's `nack` and `fec` switches.
  - **Congestion control:** transport-wide sequence numbers on every
    packet, with GoogCC at the scenario's start, minimum and maximum
    rates.
  - **Field trials:** none, so the result reflects libwebrtc's defaults.
- **The same links.** Packets travel over this repository's
  `sim::NetworkLink`, which applies the scenario's timeline through
  `testbench::LinkTimeline`, with the same seeds and the same cross
  traffic as `sm_testbench`.
- **Simulated time.** Release builds of libwebrtc leave out its
  simulated-time test harness, so [sim_time.cc](webrtc/sim_time.cc)
  provides one: a clock plus task queues that jump from one due task to
  the next. It also handles the global clock and `rtc::Event` waits.
- **The same encoder.** The video encoder plugged into libwebrtc is the
  test bench's encoder model (frame sizes follow the allocated rate,
  ±20%, keyframes 4x), so video differences come from transport, repair
  and buffering, not from codec quality. libwebrtc's own frame dropper
  is active, as it is for software encoders.
- **The same metrics.** Metric names and formulas are the test bench's.
  `testbench::AddCcMetrics` computes the congestion-control summary for
  both libraries. Freezes use the same `video::FreezeDetector`.

### Differences to keep in mind

- **Audio codecs differ.** libwebrtc runs real Opus with in-band FEC.
  The test bench sends payload stand-ins and plays the original samples.
  Audio concealment therefore also reflects Opus FEC, which this library
  does not have.
- **Some metrics are approximations for libwebrtc**, because it does not
  expose the per-packet value. `sm_compare` prints the list at the end of
  every run (`WebrtcMetricNotes()` in
  [webrtc_call.cc](webrtc/webrtc_call.cc)). The main ones:
  - the audio jitter-buffer delay percentiles come from 10 ms averages;
  - `video.residual_loss_pct` ignores FEC recoveries;
  - `repair.fec_recovered` is not reported.
- **The video delay breakdown is this library's only.**
  `video.delay_encode_ms`, `video.delay_pacer_p95_ms`,
  `video.delay_network_p95_ms` and `video.delay_jitter_buffer_p95_ms` need the
  time each frame's packets left the sender's pacer and arrived, per frame.
  libwebrtc's stats do not expose that, and `sm_compare` does not compute
  them for it yet. `cc.target_max_drop_pct` and
  `cc.send_over_target_p95_pct` come from `testbench::AddCcMetrics`, so
  `sm_compare` reports them for both libraries.
- **`cc.utilization_pct` can exceed 100%** for either library. Link jitter
  bunches deliveries into some 100 ms windows, and a queue built before a
  capacity drop drains after it.
- **No decoder, no capture device.** Both sides use fakes: the video
  decoder only returns a frame, and audio goes in and out through
  `AudioTransport` every 10 ms.
