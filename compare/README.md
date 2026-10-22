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
out/compare/sm_compare --trace=NAME --trace-dir=tr/ a.scn          # per-packet forward-link trace, ours and libwebrtc's (below)
python3 tools/parity_report.py r.json --owners tools/parity_owners.json   # gaps with libwebrtc, in both directions
out/compare/sm_apm_compare                                         # audio processing vs libwebrtc's APM (docs/results/apm-webrtc.md)
out/compare/sm_video_compare                                       # freeze detection, quality scaler, frame timing vs libwebrtc (docs/results/video-webrtc.md)
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
  - **Receiver RTT:** the video receiver measures the round trip with
    RTCP XR (RRTR in its reports, DLRR in the sender's), libwebrtc's
    path for an endpoint that only receives. Its jitter estimator and
    NACK requester then see the RTT, as in a two-way call, and as the
    test bench's receiver does (issue #24).
  - **Congestion control:** transport-wide sequence numbers on every
    packet, with GoogCC at the scenario's start, minimum and maximum
    rates.
  - **Field trials:** none, so the result reflects libwebrtc's defaults.
- **The same links.** Packets travel over this repository's
  `sim::NetworkLink`, which applies the scenario's timeline through
  `testbench::LinkTimeline`, with the same seeds and the same cross
  traffic as `sm_testbench`. A link change at time t applies to every
  packet sent at t or later on both stacks: each transport applies the
  changes due before it sends, because a libwebrtc task due in the same
  millisecond can run before the harness tick (issue #151).
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
  - `sync.av_offset_*` reads libwebrtc's audio playout point from
    `Syncable::GetPlayoutRtpTimestamp`, the input its own A/V sync uses.
    Neither stack synchronizes in the comparison: libwebrtc's receive
    streams have no `sync_group`, and the test bench has no A/V sync yet
    (#77).
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

## Forward-link packet trace (issue #125)

For the first seed of the scenarios named by `--trace` (default
`very_low_capacity_allocator_v2`; `--trace=none` disables it), `sm_compare`
records every packet either stack offers to the forward link, in the same
CSV: `time_us,size,kind,seq,queue_bytes,outcome` (wire size, kind as the
per-kind counters tag it, RTP sequence number or -1 (for `rtx`, the original
sequence number from the RTX payload, #217), bytes in the bottleneck
queue before the packet, and whether the link delivered, dropped or lost it).
The traces are in `compare-webrtc.json` under `"traces"`, so CI publishes them
with the other results (`parity-data`, and `parity-data-pr/pr/<N>/`).
`--trace-dir=DIR` also writes them as CSV files. `sm_testbench --link-trace=DIR`
writes our side alone.

`--trace-all-dir=DIR` traces the first seed of **every** scenario and writes
only the CSV files to DIR (the JSON keeps the `--trace` ones). CI runs it on
`main` and publishes DIR as `traces/` on the `parity-data` branch, with
`traces/index.txt` listing the files, so libwebrtc's trace of any scenario is
readable without a PR that adds it to `--trace`. PR runs don't, because
`parity-data-pr` keeps every PR's files in its history.

```sh
python3 tools/link_trace.py compare-webrtc.json         # per kind, ours and libwebrtc's side by side
python3 tools/link_trace.py sm.trace.csv webrtc.trace.csv --duration 40
```

The summary shows, per kind, the offered kbps, the wire-size percentiles and
histogram, the gaps between sends, the queue at arrival, and the drops (by
cause, and as a share of all drops).

## Video send trace (issue #131)

The same runs that write the packet trace also write one row per simulated
second of the sender's video decisions, for this library and for libwebrtc, in
one CSV: `t_s,encoder_target_bps,alloc_target_bps,captured,encoded,`
`dropped_capturer,dropped_encoder_queue,dropped_rate_limiter,`
`dropped_congestion_window,dropped_encoder,paused,encoded_bytes,pacer_queue_ms`.
They are in `compare-webrtc.json` under `"sm_video"` and `"webrtc_video"` of
each `"traces"` entry (so CI publishes them to `parity-data` and
`parity-data-pr/pr/<N>/`), and `--trace-dir` writes
`<scenario>.<seed>.{sm,webrtc}.video.csv`. `sm_testbench --link-trace=DIR`
writes our side alone.

For libwebrtc the counts are deltas of `VideoSendStream::Stats`
(`frames_dropped_by_*`, `target_media_bitrate_bps`, `suspended`) and of
`Call::Stats::pacer_delay_ms`; the encoder target is the last `SetRates` of the
model encoder. This library maps its drop points onto the same reasons:

| libwebrtc reason | this library |
|---|---|
| `rate_limiter` (`kMediaOptimization`, the frame dropper) | `DropReason::kFrameDropper` |
| `congestion_window` | `DropReason::kCongestionWindow` |
| `encoder_queue` (`EncoderPaused`, `DropDueToSize`, a newer pending frame) | `DropReason::kPaused` only |
| `capturer`, `encoder` | no counterpart (always 0) |

```sh
python3 tools/video_trace.py compare-webrtc.json --series    # totals by reason, then the per-second series
python3 tools/video_trace.py sm.video.csv webrtc.video.csv
```

### The bandwidth-estimate trace (issue #149)

The same runs record the sender's GoogCC estimates in one CSV for both stacks:
`time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps`.
libwebrtc's side comes from an `RtcEventLog` sink on the sender's `Environment`
(`BweTraceLog` in `webrtc_call.cc`):

| event | libwebrtc | this library (`cc v2` only) |
|---|---|---|
| `delay_based` | `RtcEventBweUpdateDelayBased` (`delay_based_bwe.cc`): AIMD estimate and trendline state, when either changes | `GccController::TraceDelayBased`, same condition |
| `loss_based` | `RtcEventBweUpdateLossBased` (`send_side_bandwidth_estimation.cc`): the send-side target, fraction lost and packets, when the target or fraction changes, and every 5 s | `GccController::TraceLossBased`, same condition |
| `target` | `Call::Stats::send_bandwidth_bps` every 100 ms, when it changes | the controller's target, when it changes |

`loss_state`, `loss_limit_bps` and `instant_ub_bps` are `LossBasedBweV2`'s state,
loss-limited estimate and instant upper bound. libwebrtc logs no event for them, so
`DetectorTraceController` reads its `LossBasedBweV2` after each GoogCC call and fills
`loss_state` and `loss_limit_bps` on the `loss_based` rows that the call logged
(issue #217). `instant_ub_bps` has no accessor, so it stays empty on libwebrtc's side. The files are `"sm_bwe"` and `"webrtc_bwe"` in
each `"traces"` entry, `<scenario>.<seed>.{sm,webrtc}.bwe.csv` with `--trace-dir` and
`--trace-all-dir` (so `main` publishes them in `parity-data/traces/`), and
`sm_testbench --link-trace=DIR` writes ours alone.

```sh
python3 tools/bwe_trace.py compare-webrtc.json --scenario issue_124_burst_loss_v2 --from 4 --to 16
python3 tools/bwe_trace.py sm.bwe.csv webrtc.bwe.csv
```

For each second it shows both stacks' delay-based estimate and detector state, the
send-side target, the loss fraction, and whether the target is held by the
delay-based estimate or by the loss side.

### Audio jitter buffer trace (issue #62)

Both stacks also record NetEq's state after every 10 ms audio pull, read as
`AudioReceiveStream::GetStats` reports it for libwebrtc and from
`AudioJitterBuffer` for this library:

| column | libwebrtc | this library |
|---|---|---|
| `buffer_ms` | `jitter_buffer_ms` (NetEq's `current_buffer_size_ms`: packet buffer + sync buffer) | `AudioJitterBuffer::CurrentLevelMs` |
| `target_ms` | `jitter_buffer_preferred_ms` (`DecisionLogic::TargetLevelMs`) | `AudioJitterBuffer::TargetDelayMs` |
| `jb_delay_ms` | `jitterBufferDelay / jitterBufferEmittedCount` over the pull, empty when nothing was emitted | the same counters; `audio.jb_delay_*` are their percentiles |
| `received_ms`, `concealed_ms`, `accelerate_ms`, `decelerate_ms` | `totalSamplesReceived`, `concealedSamples`, `removedSamplesForAcceleration`, `insertedSamplesForDeceleration` over the pull, in ms | the same stats |

The files are `"sm_audio"` and `"webrtc_audio"` in each `"traces"` entry,
`<scenario>.<seed>.{sm,webrtc}.audio.csv` with `--trace-dir` and
`--trace-all-dir` (so `main` publishes them for every scenario in
`parity-data/traces/`), and `sm_testbench --link-trace=DIR` writes ours alone.
Yardstick #266 used them to show that the two NetEqs take the same decisions at
the same ticks, and that the remaining jitter buffer differences come from the
packets' arrivals.
