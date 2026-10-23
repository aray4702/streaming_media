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
out/native/testbench/sm_testbench --link-trace=DIR a.scn                  # every packet offered to the forward link, as CSV (tools/link_trace.py); also the video send trace and the GoogCC estimate trace (tools/bwe_trace.py)
ctest --preset native -L scenario                                         # the suite as CTest entries
python3 tools/testbench_compare.py testbench/baseline.json r.json         # regressions against main
```

It exits with 1 if any `expect` line fails and 2 on a usage or parse error.
The same seed gives the same result on a given platform and compiler.
Simulated time runs about 250x faster than real time, so a 30 s call takes
around 0.1 s.

## The call

Both ends are `rtp::Session` objects (`include/sm/rtp/session.h`, design in
`docs/design/rtp-session.md`). The bench passes encoded frames in and moves
datagram bytes between the two sessions over the emulated links, so every
packet crosses a link as bytes, in both directions:

```
sender                                   forward link                 receiver
speech -> Session::SendAudio (RED) ---\                               Session::OnPacket -> RED -> NetEq-lite jitter buffer -> 10 ms playout
video encoder model -> SendVideo (H.264) > pacer -> [capacity, queue, loss,  -> packet buffer -> keyframe requester ->
  (RTX history, FEC) in RtpSender    /            jitter, dup, outage,         jitter estimator + timing -> render scheduler -> freeze detector
SendSideController (GCC, probes,         cross traffic]
  outage monitor)  <-------- reverse link <-------- RtcpEngine: transport-cc (50 ms), NACK, PLI/FIR, SR/RR + SDES
```

```cpp
rtp::Session sender(sender_cfg), receiver(receiver_cfg);
auto audio = sender.AddAudioSend(audio_cfg);
auto video = sender.AddVideoSend(video_cfg);
receiver.AddAudioReceive(audio_cfg, &decoder);
auto video_in = receiver.AddVideoReceive(video_cfg);
for (each millisecond) {
  sender.SendAudio(audio, {rtp_timestamp, frame_bytes}, now);       // every 20 ms
  sender.SendVideo(video, {rtp_timestamp, annexb_frame}, now);       // every frame
  sender.Process(now);
  for (auto& p : sender.TakeOutgoing()) forward.Send(now, p.bytes, p.wire_size);
  forward.ReceiveEach(now, [&](auto bytes, auto at) { receiver.OnPacket(bytes, at); });
  receiver.Process(now);
  for (auto& f : receiver.TakeVideoFrames(video_in)) { /* render f */ }
  for (auto& p : receiver.TakeOutgoing()) reverse.Send(now, p.bytes, p.wire_size);
  reverse.ReceiveEach(now, [&](auto bytes, auto at) { sender.OnPacket(bytes, at); });
  for (auto& e : sender.TakeEvents()) { /* keyframe request, encoder pause, report block */ }
}
```

The bench itself owns only what the library leaves to the app: the media
sources, the video encoder model with its frame dropper, the FEC protection
decision, the audio device (a pull every 10 ms), the freeze detector, the
link emulation and the measurement.

- **Audio** is 20 ms frames at the configured bitrate (default 32 kbps), with
  RED distance 1. Frame k is sent at 20k + 10 ms, the phase libwebrtc's audio
  path has in `sm_compare` (10 ms ADM blocks from t = 0), so audio and the
  30 fps video share the delay detector's 5 ms send groups as there (#124). There is no audio codec in the library, so the payload
  bytes stand in for the encoded frame. The receiver plays the original
  speech samples for each frame it receives, which is what a lossless codec
  would decode.
- **Video** is an encoder model. Each frame's size follows the allocated
  rate (`Session::rates().video_target`: GCC target minus audio, divided by
  1 + FEC overhead) with ±20% variation, and keyframes are 4x larger. A
  keyframe is sent at the start, on PLI/FIR and on outage recovery. The
  frame dropper skips frames when the encoder overshoots. The bitstream is
  an Annex-B access unit from `rtp::MakeFakeH264Frame` (libwebrtc's
  `FakeH264Encoder`): SPS, PPS and an IDR slice on a keyframe, one non-IDR
  slice otherwise, packetized as RFC 6184 mode 1 (STAP-A for the parameter
  sets, FU-A for the slices). The receiver finds frames and keyframes from
  the NAL types and derives frame ids from sequence numbers, so each frame
  depends on the one before it, and one lost frame blocks decoding until
  it is repaired or the next keyframe arrives.
- **Feedback** is real RTCP, built and parsed by `rtp::RtcpEngine`:
  - Transport-wide congestion control feedback every 50 ms.
  - NACKs as soon as the NACK tracker asks.
  - PLI/FIR from the keyframe requester or on NACK list overflow. No loss
    notification is sent: it works on frame ids and dependencies from a
    descriptor, which H.264 has none of, and libwebrtc sends PLI and NACK
    only in that case (design 6.4).
  - Sender reports every second (first at 0.5 s) and receiver reports every
    second (first at 1 s), each compound with an SDES CNAME. These give the
    RTT, and the loss for the protection controller. An audio-only call
    reports every 5 s.
- **Simplifications:**
  - The receiver is given the sender's RTT estimate (`Session::SetRtt`) for
    NACK timing; a real receiver would measure it itself with RTCP XR.
  - Loss burstiness is read from the receiver directly; a real receiver
    would report it in XR.
  - There is no decoder, and decode time is fixed at 5 ms.

## Scenario format

One directive per line, and `#` starts a comment.

| Directive | Default | Meaning |
|---|---|---|
| `name <id>` | `unnamed` | Scenario name (also the CTest name) |
| `describe <text>` | | One line describing what the user experiences |
| `duration <time>` | `30s` | Call length (up to 1 hour) |
| `compare_seeds <N>` | `--seeds` | Read only by `sm_compare`: seeds to run for this scenario (1-200), overriding `--seeds`; `sm_testbench` ignores it |
| `seed <n>` | `1` | Seed for every random process |
| `audio on\|off`, `video on\|off` | on | Media types |
| `red on\|off`, `nack on\|off`, `fec on\|off` | on | Repair mechanisms |
| `audio_rate <rate>` | `32kbps` | Audio codec bitrate |
| `audio_sample_rate 8000\|12000\|16000\|24000\|32000\|44100\|48000` | 16000 | The capture rate. The codec, RTP clock and NetEq run at it at 8, 16, 32 and 48 kHz; at 12, 24 and 44.1 kHz at Opus's 48 kHz clock, as libwebrtc resamples the capture to the encoder (#298) |
| `video_fps <n>` | 30 | |
| `video_size <W>x<H>` | 1280x720 | The captured resolution, on both stacks of `sm_compare`. It feeds the encoder model's QP and encode time (`testbench/encoder_model.h`, #84); frame sizes don't depend on it |
| `video_complexity <x>` | 1 | Content for the encoder model's QP: 2 needs twice the bits for the same QP (#84) |
| `encoder_speed <x>` | 1 | CPU speed for the encoder model's encode time: 10 ms per 720p frame at 1, reported in the video trace (#84) |
| `encode_delay on\|off` | off | `on` makes the encode time real on both stacks: a frame reaches the library (`OnEncoded`) and the sender that long after its capture, which libwebrtc's CPU overuse detector measures (#84) |
| `video_max_payload <bytes>` | 1446 | `rtp::VideoStreamConfig::max_payload_size`, passed as is; the sender clamps it to a 100-1500 byte packet (#257). `sm_compare`'s libwebrtc side keeps its default |
| `video_max <rate>`, `video_min <rate>` | 2500/50 kbps | Encoder bitrate range |
| `start_rate`, `min_rate`, `max_rate <rate>` | 300/30/5000 kbps | GCC limits |
| `keyframe_interval <time>` | 0 (on request only) | Periodic keyframes |
| `link_headroom on\|off` | on | `on` gives the encoder bitrate adjuster libwebrtc's `link_allocation` (`VideoSendStreamImpl::OnBitrateUpdated`): the video allocation less overhead and protection, taken before the encoder target is capped at the maximum, so an allocation above the maximum is headroom the adjuster can let the encoder use. `off` passes the capped target, with no headroom (#326) |
| `send_rate_stats on\|off` | off | `on` takes the RTX/FEC share off the video encoder rate from per-type send rates as libwebrtc's `RtpSenderEgress::GetSendRates` keeps them (`RateStatistics`, 1 s): a repair stream that starts after 1.5 s without one counts at its rate since it started, not averaged over the second (#224). `off` averages every type over the last second |
| `bitrate_adjuster on\|off` | on | `on` runs libwebrtc's `EncoderBitrateAdjuster` (#5): the encoder is configured with a rate lowered by its measured overshoot |
| `quality_scaling on\|off` | off | `on` runs libwebrtc's quality scaling (#84): `QualityScalerResource` on the encoder model's QP and `EncodeUsageResource` (CPU overuse) on its encode time, with `MAINTAIN_FRAMERATE` and the initial frame drop; the library picks the encoded resolution (`video.encode_height_mean`) |
| `timing v1\|v2` | v2 (since #24) | v1 is the library's video receive timing (`VideoJitterEstimator`, `VideoTiming`, `FrameScheduler` with its 10 ms late-drop rule); v2 is libwebrtc's (`JitterEstimator`, `TimestampExtrapolator`, `VCMTiming`, and frame release as `VideoStreamBufferController`/`FrameDecodeTiming`: a late frame is skipped only when a later keyframe is decodable; #24) |
| `neteq v1\|v2` | v2 | v1 is the library's audio target delay (`DelayManager`: 95th percentile of each packet's relative delay, 10 ms buckets) and its own operations; v2 is libwebrtc M157's NetEq (`DelayManager` with `UnderrunOptimizer` over 500 ms maxima in 20 ms buckets and `ReorderOptimizer`, `PacketArrivalHistory` over 2 s, 80 ms until the first estimate; `DecisionLogic`, `Expand`, `Merge`, `Accelerate`/`PreemptiveExpand`, Opus in-band FEC; #62), the default since #62 |
| `av_sync on\|off` | off | `on` puts audio and video in one sync group on both stacks (#77): the audio stream sends its own RTCP SR (as libwebrtc's audio RtpRtcp module) and the receiver runs `RtpStreamsSynchronizer` every second, setting both streams' minimum playout delays; `sm_compare` sets libwebrtc's `sync_group` on both receive streams |
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
  end), `pause_count` and `pause_total_ms` (render gaps of 5 s or more,
  which libwebrtc's `VideoQualityObserver` counts as pauses, not freezes;
  a pause still in progress at the end is included), `render_fps`, `render_ratio_pct`, `e2e_delay_p50/p95_ms` (capture
  to render), `time_to_first_frame_ms`, `avg_kbps`, `qp_mean` (mean QP of encoded
  frames from the encoder model both stacks share,
  `testbench/encoder_model.h`), `keyframes`,
  `keyframe_requests`, `encoder_dropped`, `undecodable_frames`,
  `residual_loss_pct`.
  - Delay breakdown of each rendered frame: `delay_encode_ms` (mean
    capture to handed to the sender; the bench's encoder is
    instantaneous, so 0), `delay_pacer_p95_ms` (handed to the sender to
    the last media packet leaving the pacer), `delay_network_p95_ms` (last
    media packet sent to the frame complete at the receiver: propagation,
    queue, jitter and any wait for a retransmission) and
    `delay_jitter_buffer_p95_ms` (frame complete to rendered). The
    p95 values are separate percentiles, so they need not add up to
    `e2e_delay_p95_ms`.
- **A/V sync** (calls with audio and video, issue #63):
  `sync.av_offset_p50_ms` (signed median), `sync.av_offset_p95_ms` (95th
  percentile of the absolute value) and `sync.av_offset_max_ms`. At each
  rendered frame, the offset is the capture time of the audio playing
  then minus the frame's capture time, so a positive value means the video
  is late. It is libwebrtc's `RtpStreamsSynchronizer::GetStreamSyncOffsetInMs`
  (the `sync_offset_ms` stat) with exact capture times.
- **Congestion control:**
  - `cc.target_avg_kbps` and `cc.goodput_kbps`.
  - `cc.utilization_pct`: goodput divided by the usable capacity, which is
    the link capacity or the media's maximum demand, whichever is lower.
  - Queue delay: `cc.queue_p95_ms`, `cc.queue_max_ms` and
    `cc.queue_over_200ms_pct`.
  - `cc.max_settle_ms`: the time after each link change until the target
    is within [0.5, 1.0] x usable capacity. `cc.unsettled` counts link
    changes where it never got there.
  - `cc.target_max_drop_pct`: the deepest fall of the target below its
    peak of the preceding 5 s, outside the first 5 s and the 5 s after each
    link change (a cut on a link that did not change).
  - `cc.send_over_target_p95_pct`: the 95th percentile of how far the send
    rate (100 ms windows after the first 2 s, all traffic) is above the
    target, in % of the target. `cc.send_over_target_ms` counts the time
    above 1.5 x the target.
  - `cc.rtt_p50_ms`.
- **Network and repair:** `net.loss_pct`, `repair.overhead_pct`,
  `repair.nack_recovered`, `repair.fec_recovered`. Per kind on the forward
  link (information only, issue #117): `net.audio_kbps`, `net.video_kbps`,
  `net.rtx_kbps`, `net.padding_kbps`, `net.fec_kbps` (wire rate sent,
  averaged over the call) and `net.audio_loss_pct`, `net.video_loss_pct`,
  `net.rtx_loss_pct`, `net.padding_loss_pct` (random, burst, outage and
  queue drops). `sm_compare` counts libwebrtc's the same way, by SSRC;
  padding is a packet with no payload. Also per kind (audio, video, rtx,
  padding; issue #119, information only): `net.<kind>_size_p50_b`,
  `_size_p90_b`, `_size_max_b` (wire size of the packets offered to the
  link, nearest rank) and `net.<kind>_queue_drops`.

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
