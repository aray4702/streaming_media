# Test bench

`sm_testbench` runs a complete one-way audio + video call through the
library over emulated links whose impairments change on a script. It
reports what a user of the call would experience. It is the tool the
agents in [program.md](../program.md) use to find, reproduce and guard
against problems.

```sh
cmake --preset native && cmake --build --preset native -j
out/native/testbench/sm_testbench testbench/scenarios/outage_3s.scn
out/native/testbench/sm_testbench --quiet testbench/scenarios/*.scn       # PASS/FAIL per scenario
out/native/testbench/sm_testbench --json=r.json --timeseries a.scn       # machine-readable, 100 ms samples
out/native/testbench/sm_testbench --library a.scn --json=-               # plus every module's own metrics
out/native/testbench/sm_testbench --metrics                               # metric names and meanings
ctest --preset native -L scenario                                         # the suite as CTest entries
python3 tools/testbench_compare.py testbench/baseline.json r.json         # regressions against main
```

It exits with 1 if any `expect` line fails and 2 on a usage or parse error.
The same seed gives the same result on a given platform and compiler.
Simulated time runs about 250x faster than real time, so a 30 s call takes
around 0.1 s.

## The call

```
sender                                   forward link                 receiver
speech 16 kHz -> RED ---------\                                       RED -> NetEq-lite jitter buffer -> 10 ms playout
video encoder model -> RTX     > pacer -> [capacity, queue, loss,  -> packet buffer -> keyframe requester ->
  history, FEC (adaptive)    /            jitter, dup, outage,         jitter estimator + timing -> render scheduler -> freeze detector
GCC (delay + loss BWE, probes)            cross traffic]
outage monitor, frame dropper  <-------- reverse link <-------- transport-cc feedback (50 ms), NACK, PLI/FIR, RTCP RR (1 s)
RTT (RTCP SR/RR), protection controller
```

- **Audio** is 20 ms frames at the configured bitrate (default 32 kbps), with
  RED distance 1. There is no audio codec in the library, so the payload
  bytes stand in for the encoded frame. The receiver plays the original
  speech samples for each frame it receives, which is what a lossless codec
  would decode.
- **Video** is an encoder model. Each frame's size follows the allocated
  rate (GCC target minus audio, divided by 1 + FEC overhead) with ±20%
  variation, and keyframes are 4x larger. A keyframe is sent at the start,
  on PLI/FIR and on outage recovery. The frame dropper skips frames when the
  encoder overshoots. Each frame references the previous one, so one lost
  frame blocks decoding until the next keyframe arrives.
- **Feedback:**
  - Transport-wide congestion control feedback every 50 ms.
  - NACKs as soon as the NACK tracker asks.
  - PLI/FIR from the keyframe requester or on NACK list overflow.
  - RTCP SR/RR every second; these give the RTT, and the loss for the
    protection controller.
- **Simplifications:**
  - The receiver uses the sender's RTT estimate for NACK timing; a real
    receiver would measure it itself with RTCP XR.
  - Loss burstiness is passed to the protection controller directly; a
    real receiver would report it in XR.
  - There is no decoder, and decode time is fixed at 5 ms.

## Scenario format

One directive per line, and `#` starts a comment.

| Directive | Default | Meaning |
|---|---|---|
| `name <id>` | `unnamed` | Scenario name (also the CTest name) |
| `describe <text>` | | One line describing what the user experiences |
| `duration <time>` | `30s` | Call length (up to 1 hour) |
| `seed <n>` | `1` | Seed for every random process |
| `audio on\|off`, `video on\|off` | on | Media types |
| `red on\|off`, `nack on\|off`, `fec on\|off` | on | Repair mechanisms |
| `audio_rate <rate>` | `32kbps` | Audio codec bitrate |
| `audio_sample_rate 8000\|16000\|32000\|48000` | 16000 | |
| `video_fps <n>` | 30 | |
| `video_max <rate>`, `video_min <rate>` | 2500/50 kbps | Encoder bitrate range |
| `start_rate`, `min_rate`, `max_rate <rate>` | 300/30/5000 kbps | GCC limits |
| `keyframe_interval <time>` | 0 (on request only) | Periodic keyframes |
| `at <time> <key=value...> [down\|up]` | | Change the forward link from `<time>` on |
| `reverse at <time> ...` | mirrors forward delay | Change the feedback link |
| `expect <metric> <op> <number>` | | Pass criterion; `op` is one of `< <= > >= ==` |

Units: times are `us`, `ms`, `s` or `min`; rates are `bps`, `kbps`, `Mbps`
or `inf`; fractions are `%` or a plain number from 0 to 1.

Link keys (any subset; unset keys keep their previous value):

| Key | Meaning |
|---|---|
| `capacity=` | Bottleneck rate (`inf` = unlimited, the default) |
| `delay=` | One-way propagation delay (default 20 ms) |
| `jitter=` | Standard deviation of added delay (half-normal) |
| `reorder=on\|off` | Whether jitter may reorder packets (default on) |
| `loss=` | Average loss rate |
| `burst=` | Mean loss burst length in packets. Above 1, loss follows a Gilbert-Elliott model with the same average rate |
| `dup=` | Duplication rate |
| `queue=` | Bottleneck buffer as time at the current capacity (`0` = unlimited) |
| `cross=` | Competing constant-rate traffic sharing the bottleneck |
| `down` / `up` | Link outage: everything sent is lost |

## Metrics

`sm_testbench --metrics` prints the authoritative list. In summary:

- **Audio:** `concealed_pct`, `interruptions` (concealment runs over
  150 ms), `longest_interruption_ms`, jitter buffer delay
  (`jb_delay_p50/p95_ms`), `net_delay_p95_ms`, `mouth_to_ear_p95_ms`,
  `stretch_pct`, `late_discards`, `time_to_first_ms`.
- **Video:** `freeze_count`, `freeze_total_ms` and `longest_freeze_ms`
  (WebRTC freeze definition, including a freeze still in progress at the
  end), `render_fps`, `render_ratio_pct`, `e2e_delay_p50/p95_ms` (capture
  to render), `time_to_first_frame_ms`, `avg_kbps`, `keyframes`,
  `keyframe_requests`, `encoder_dropped`, `undecodable_frames`,
  `residual_loss_pct`.
- **Congestion control:**
  - `cc.target_avg_kbps` and `cc.goodput_kbps`.
  - `cc.utilization_pct`: goodput divided by the usable capacity, which is
    the link capacity or the media's maximum demand, whichever is lower.
  - Queue delay: `cc.queue_p95_ms`, `cc.queue_max_ms` and
    `cc.queue_over_200ms_pct`.
  - `cc.max_settle_ms`: the time after each link change until the target
    is within [0.5, 1.0] x usable capacity. `cc.unsettled` counts link
    changes where it never got there.
  - `cc.rtt_p50_ms`.
- **Network and repair:** `net.loss_pct`, `repair.overhead_pct`,
  `repair.nack_recovered`, `repair.fec_recovered`.

## The suite and the baseline

- `scenarios/*.scn` is the regression suite. Every scenario runs in CI
  (`ctest -L scenario`). Their `expect` lines are guard rails that hold on
  `main` across seeds. They are not targets: lines marked `# Known gap`
  describe problems that are not fixed yet.
- `baseline.json` holds every metric of every scenario on `main`, produced
  by the canonical platform (Linux x86_64, GCC 13, Release; CI job
  `testbench`). `tools/testbench_compare.py` flags any metric that got worse
  by more than 5% (with a per-unit floor). A PR that changes metrics updates
  the baseline and explains why.
- To add a scenario that reproduces an issue, name it after the issue
  (`issue_123_<slug>.scn`) and give it the `expect` lines that fail before
  the fix.

## In the browser

[web/](../web/) builds the same test bench to WebAssembly. Its demo page
edits scenarios, runs them and plots the time series.
