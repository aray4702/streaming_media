# Harness fidelity: both stacks in the same test environment

`sm_compare` runs every scenario through this library (`testbench/call.cc`)
and through libwebrtc (`compare/webrtc/webrtc_call.cc`). A parity gap means
something only when the two runs differ in the stack under test and in
nothing else (Owner's rule, 2026-10-27; issue #478). This document is the
audit of that rule, one row per part of the harness. Each row says whether the
two sides are:

- **shared code**: both call the same function;
- **identical by construction**: separate code that does the same thing, with
  the test or trace check that would fail if the sides drifted apart;
- **different**: with the item that fixes it.

Rows are audited one per Fixer run, in the order 2, 5, 7, 4, 6, 8 (rows 1 and
3 already have items). A row not yet audited says so.

| # | area | status | evidence | item |
|---|---|---|---|---|
| 1 | Captured media | audio: **different** (ADM 10 ms blocks through the ACM vs our direct 20 ms encode); video: not audited | #477 | #477 |
| 2 | Encoders | Opus: **identical by construction**, packet for packet on 60 of 104 traced scenarios, checked on the lossless ones by `tools/check_harness_drift.py`; where the sides part, the input that differs is the stack's own RTCP, not the harness. Video: **identical by construction** (the same size formula, seed and QP/encode-time model), the formula is still written twice | [Row 2](#row-2-encoders) | #484 (stack, not harness) |
| 3 | Decoders | **different**: our `SpeechDecoder` returns the source signal, libwebrtc decodes with libopus | #476 | #476 |
| 4 | Packetization and wire | not audited | | |
| 5 | The link | Link model, seeds, timeline, cross traffic and delivery times: **shared code** (`sim::NetworkLink`, `testbench::LinkTimeline`), each packet arrives at the same simulated time on both since #467 / #453. When the stack is handed a packet and sends its response: **different**: ours on the 1 ms tick, libwebrtc at the packet's arrival time (60 % of libwebrtc's RTX off the grid, none of ours) | [Row 5](#row-5-the-link) | #507 |
| 6 | Clock and tasks | not audited | | |
| 7 | Measurement | Link, congestion-control, goodput, concealment, A/V sync and freeze metrics: **shared code** (`call_common.h`, `AddCcMetrics`, `AddLinkKindMetrics`, `video::FreezeDetector`); the end-of-call summary formulas are **identical by construction** but written twice. Repair metrics (`repair.overhead_pct`, `repair.nack_recovered`, `video.residual_loss_pct`): **different** rules per side (`high_jitter` NACK recovered 672 vs 12 at the same RTX rate). Audio stretch, playout start, first audio and undecodable frames: **different** statistics per side. `repair.fec_recovered` and the four `video.delay_*` metrics: **ours only**, so never compared | [Row 7](#row-7-measurement) | #509, #510 |
| 8 | Scenario mapping | not audited | | |

## Row 2: encoders

Audited on `main` at 67df95b (2026-10-28), with the seed-1 traces that
`parity-data/traces/` holds for that `main`.

### Opus

Both sides run libopus at the revision M157 builds (#457).

| | ours (`testbench/call.cc`, `testbench/opus_encoder.h`) | libwebrtc (`compare/webrtc/webrtc_call.cc`) |
|---|---|---|
| Encoder | `OpusSendEncoder`: libopus with `RecreateEncoderInstance`'s settings in its order | `AudioEncoderOpusImpl` from `CreateBuiltinAudioEncoderFactory`, format `opus/48000/2` with `minptime=10`, `useinbandfec=1` |
| Channels, application, frame | mono, VOIP, 20 ms, 48 kHz | no `stereo=1`, so mono and VOIP; no `ptime`, so 20 ms (`audio_encoder_opus_config.h` `kDefaultFrameSizeMs`) |
| Bitrate | `OpusBitrateBps(audio_bitrate, 0)` = `audio_bitrate` for the whole call | `target_bitrate_bps = audio_bitrate` at setup (`audio_send_stream.cc:601` `OnReceivedTargetAudioBitrate`); `min_bitrate_bps = max_bitrate_bps = audio_bitrate`, so `OnBitrateUpdated` clamps every allocation to `audio_bitrate` + overhead (`audio_send_stream.cc:513-517`, `:886-902`), and `OnReceivedUplinkAllocation` takes the same overhead off again (`audio_encoder_opus.cc:521-526`). Before the route is known, the allocation is ignored (`:515-519`). So the rate is `audio_bitrate` for the whole call, outages included (a zero allocation is clamped to the minimum) |
| Complexity | 9 | `kDefaultComplexity` 9, and `kDefaultLowRateComplexity` 9 (`audio_encoder_opus_config.cc:17`), so `GetNewComplexity` never changes it |
| Field trials that change the encoder | none | none set by `sm_compare` (only `WebRTC-Video-QualityScaling` with `quality_scaling on`): `WebRTC-AdjustOpusBandwidth` and `WebRTC-Audio-OpusBitrateMultipliers` are off, no audio network adaptor |
| VBR, DTX, FEC, max playback rate | VBR, DTX off, in-band FEC on, 48 kHz | the same, from the format above |
| Input | `sim::SpeechLike(48000, seed + 7)` as 16-bit PCM, `clamp(v) * 32767` | the same signal and conversion, handed to the ADM in 10 ms blocks; no audio processing module is configured (`AudioState::Config` has none), so the encoder gets those samples unchanged |
| Frame timing | frame k leaves the encoder at 20k + 10 ms | the second 10 ms block completes the frame at 20k + 10 ms |
| Loss input | each report block about the audio SSRC goes to `OpusPacketLossRate` (`PacketLossFractionSmoother`, 0.9999 per ms, clamp 0.2, whole percent), set on libopus before the next encode | `ChannelSend::OnReportBlockDataUpdated` -> `OnReceivedUplinkPacketLossFraction` -> the same smoother -> `WebRtcOpus_SetPacketLossRate` (`channel_send.cc:690-695`, `audio_encoder_opus.cc:326-350`, `:487-498`) |

**The traces agree.** Comparing the audio packets on the forward link
(`<scenario>.1.{sm,webrtc}.trace.csv`, wire size and send time per packet):

- On **60 of the 104** scenarios that have both traces, every audio packet of
  the call has the same wire size on both sides, sent at the same
  millisecond (for example `clean_link`: 1500 of 1500 packets). Same payload
  sizes from the same libopus over a 30 s call means the same PCM, settings,
  bitrate, frame timing and loss percentage on both sides.
- On the other 44, the sides agree until the packet after the first time the
  encoder's loss percentage differs. For our side the moment can be read
  from the encoder: in `random_loss_5pct` our percentage first goes to 1 at
  1.591 s and the first different packet is the one sent at 1.61 s; in
  `high_jitter` (no loss on either side, reordering only) it goes to 1 at
  2.090 s and the first different packet is sent at 2.09 s.

**Why the loss input differs: the stack's RTCP, not the harness.** Both
sides hand the encoder exactly what their own stack's report blocks say, by
the same rule. The report blocks themselves are different:

- libwebrtc keeps one `ReceiveStatistics` per receive stream
  (`audio/channel_receive.cc:608`, `video/video_receive_stream2.cc:255`). The
  audio SSRC's report block goes out only in the audio receive channel's own
  RTCP, every 5 s on average (`rtcp_receiver.cc:85`
  `kDefaultAudioReportInterval`).
- Our receiver adds both streams' statistics to one RTCP engine
  (`src/rtp/session.cc:168` and `:205`), so the audio block rides on every
  compound packet: the 1 s video reports and every NACK and PLI. Our sender
  gets 29 audio report blocks on `clean_link`, 278 on `random_loss_5pct` and
  317 on `high_jitter` (seed 1); libwebrtc's interval gives about 6 in 30 s.
  The short intervals give large fractions (0.5 over 19 ms on
  `high_jitter`), which the 10 s smoother turns into a loss percentage
  libwebrtc's encoder does not see.

That is a difference in the stack under test, so it is a stage-1 gap, not a
harness row: #484. On the audio-only scenarios, where our receiver also sends
at the 5 s audio interval, the sides stay identical for longer
(`audio_only_lossy`: 350 packets, until 7.01 s, after both have taken several
lossy reports), and there they part because the link carried different
packets: our side sent 2118 padding packets in that call, libwebrtc 851, so
the loss draws fell on different audio packets (row 5 audits the link).

**What keeps it identical.** `tests/unit/testbench/opus_encoder_test.cc`
pins our side's settings against libwebrtc's constants, and
`tools/check_harness_drift.py` (#490), run last in the `compare-webrtc` job,
compares the two sides' audio packets on the seed-1 traces: on the lossless
traced scenarios (`issue_124_clean_link_v2`, `issue_124_bufferbloat_v2`,
`issue_61_googcc_glue_congestive`, `probe_v2_cross_traffic`) every audio
packet must have the same size on both stacks, and the job fails if one
differs. It covers only those lossless scenarios: the 44 where the sides part
are reported but not required until #484 is fixed.

### Video

Both sides use the same encoder model: per encoded frame, `per_frame = rate /
8 / video_fps`; a keyframe is `4 x per_frame`, a delta frame `per_frame x
Uniform(0.8, 1.2)`; at least 200 bytes; QP and encode time from
`testbench::EncoderModel` (`testbench/encoder_model.h`), which both include.

| | ours (`testbench/call.cc:809-811`) | libwebrtc (`ModelEncoder`, `compare/webrtc/webrtc_call.cc:394-417`) |
|---|---|---|
| Rate | the library's encode decision (`EncodeDecision::target`, after its bitrate adjuster) at the captured frame | the last `SetRates` (`bitrate.get_sum_bps()`) before `Encode`, which `VideoStreamEncoder` sends after its adjuster |
| Random stream | `Random(seed + 13)`, one draw per encoded delta frame | `Random(seed + 13)`, one draw per encoded delta frame |
| Capture times | frame k at `k * 1000 / video_fps` ms | the same |
| Size the QP and encode time see | the size the library asks for | the frame the encoder gets, after `VideoStreamEncoder`'s scaling |
| `keyframe_interval` | the library's `VideoSendController` forces a keyframe when the time since the last keyframe reaches the interval | the model encoder does, on the frame's capture time since the last keyframe (libwebrtc's `VideoStreamEncoder` has no periodic keyframe; a real encoder's GOP does this) |
| `encode_delay on` | the frame reaches the sender after the model's encode time | the encoded callback is posted that much later on the encoder queue |

So for the same rate, frame type and frame size, both sides make frames of
the same size with the same QP. The rates differ only where the stacks do:
`clean_link` seed 1 encodes 179 831 against 180 091 bytes in its first
second, at targets of 1 926 915 against 1 930 000 bps, and both keyframes are
3348 against 3347 bytes, the same 4x of each side's rate.

The size formula is written twice, once on each side. No test fails if one
copy changes; moving it into `EncoderModel` (as #470 does for its new
overshoot and scene-change factor) makes it shared code. That waits for #470,
which edits the same lines.

## Row 5: the link

Audited on `main` at 445302f (2026-10-28), with the seed-1 traces that
`parity-data/traces/` holds (115 scenarios with both sides).

### Shared

| | ours (`testbench/call.cc`) | libwebrtc (`compare/webrtc/webrtc_call.cc`) |
|---|---|---|
| Link model | `sim::NetworkLink<Fwd>` / `<Rev>` (`include/sm/sim/network_link.h`) | the same template, `sim::NetworkLink<Pkt>` |
| Seeds | forward `s.seed`, reverse `s.seed + 1000003` (`:436-438`) | the same (`:687-689`) |
| Scenario changes | `testbench::LinkTimeline` (`call_common.h:156`), applied at the start of each tick (`:709-712`) | the same timeline, applied at the start of each tick and again before every send (`advance_links`, `:691-700`), because a libwebrtc task can run before the tick at the same millisecond (#151); our sends all follow the tick's `Advance`, so both see a change at the same moment |
| Wire size | RTP/RTCP bytes + `kUdpIpOverhead` (the session's `wire_size`) | `data.size() + kUdpIpOverhead` |
| Cross traffic | 1200-byte packets from a byte debt of `cross / 8 / 1000` per ms, sent at the start of the tick before the call's packets (`:832-838`, #390) | the same, before libwebrtc's tasks of that millisecond (`:1037-1043`) |
| Delivery | each packet is handed over with its own arrival time (`ReceiveEach(now, ...)`, `:718`, `:848`) | each packet is posted to the receiver's or sender's queue at its arrival time (`post_at`, `:1050-1118`, #467 / #453) |
| Loss, jitter, duplication | per packet, from the link's one random stream, in send order | the same |

So given the same packets at the same times, both sides lose, delay, reorder
and duplicate them identically. On the traces, every packet in the common
prefix of the two sides' send sequences (the same send time, size and kind:
2575 packets across the 115 scenarios) has the same queue, outcome and
arrival time on both.

The draws are per packet, so a stack that sends a different packet sequence
gets a different realization of the same loss and jitter, as on any
per-packet link model (libwebrtc's `SimulatedNetwork` too). That is the
stack's own traffic, not the harness: for example `audio_only_lossy` loses
different audio packets on the two sides because ours sends 2118 padding
packets and libwebrtc 851 (row 2).

### Different: when the stack is handed a packet and sends its response (#507)

libwebrtc runs on an event clock: a packet is delivered at its arrival time,
and what the stack sends in response (an RTX packet for a NACK, a NACK for a
gap, an encoded frame after `encode_delay`) goes on the link at that same
time (`LinkTransport`, `:767`, `:834`, `rel(sim.Now())`). Ours steps every
1 ms: a packet that arrived in (t-1, t] is handed to the session at tick t,
and the session's response goes on the link at t (`put_on_link(..., now,
...)`, `:845`, `:972`).

| seed 1, 115 scenarios | ours | libwebrtc |
|---|---|---|
| forward-link packets sent off the 1 ms grid | 0 | 37 397, on 70 scenarios |
| of them RTX | 0 | 23 318 of 39 088 RTX packets (60 %): `random_loss_5pct` 230 / 406, `high_jitter` 470 / 747, `burst_loss` 54 / 169 |
| of them video (`encode_delay on`) | 0 | 13 686, on `issue_84_cpu_overuse_slow_encoder` |
| other (RTCP, padding, audio) | 0 | 377, 10 and 6 |

The reverse link has no trace on either side; by the same code our
receiver's NACKs go out at the tick after the packet that revealed the gap,
libwebrtc's at its arrival. So our NACK -> RTX loop is up to 2 ms longer,
and our retransmissions meet the bottleneck at other times than
libwebrtc's. The fix (#507) brings our side up: drive the session at each
packet's arrival time, and add a reverse-link trace on both sides so the
reverse half is checked too. Clock and task timing beyond this is row 6.

## Row 7: measurement

Audited on `main` at 3025aeb (2026-10-28), against the metric means of
`parity-data/compare-webrtc.json` for that `main` (10 seeds).

Every metric in `rep.metrics` falls into one of four kinds:

1. **Shared code.** One function computes it on both sides.
2. **The same formula, written twice.** Both sides apply it to the same kind
   of input.
3. **Each stack's own statistic.** The metric reads the stack under test,
   which is the point. Both sides read the equivalent statistic by the same
   rule.
4. **Different.** The two sides use different rules, different statistics or
   different starting points, or only one side computes the metric.

### Shared code

| metrics | function | fed by |
|---|---|---|
| `cc.target_avg_kbps`, `cc.send_over_target_ms`, `cc.send_over_target_p95_pct`, `cc.target_max_drop_pct`, `cc.goodput_kbps`, `cc.utilization_pct`, `cc.max_settle_ms`, `cc.unsettled` | `AddCcMetrics` (`testbench/call.cc:300`) | the 100 ms samples; the demand is the same formula on both (`video_max` + the audio wire rate) |
| `net.<kind>_kbps`, `net.<kind>_loss_pct`, `_queue_drops`, `_size_*` | `AddLinkKindMetrics` | the shared forward link's per-kind counters, with packets tagged by the same rules (row 5) |
| goodput in the samples | `FirstDelivery` (`call_common.h`) | link arrivals: each media sequence number once, RTX by its original sequence number |
| `audio.concealed_pct`, `audio.interruptions`, `audio.interruption_ms`, `audio.longest_interruption_ms` | `ConcealmentMeter` | each jitter buffer's `totalSamplesReceived`, `concealedSamples` and `concealmentEvents` after every 10 ms pull |
| `sync.av_offset_p50_ms`, `_p95_ms`, `_max_ms` | `AvSyncMeter` | each stack's audio playout timestamp, and the capture time of each rendered frame |
| `video.freeze_count`, `video.freeze_total_ms`, `video.longest_freeze_ms`, `video.pause_*` | `video::FreezeDetector`, the same class on both sides | the render times of each side's frames |
| `cc.queue_p95_ms`, `cc.queue_max_ms`, `cc.queue_over_200ms_pct` | `QueueDelayMs` and `Percentile` | the shared forward link, sampled every 10 ms before the stack's sends of that millisecond (#410) |

### The same formula, written twice

These formulas are the same text on both sides
(`call.cc:1029-1135`, `webrtc_call.cc:1300-1421`), but no test fails if
one copy changes:
- the freeze still in progress at the end of the call, and the pause test;
- `video.render_fps` and `video.render_ratio_pct`;
- `audio.mouth_to_ear_p95_ms`;
- `net.loss_pct` and `cc.rtt_p50_ms`;
- the residual-loss loop, apart from its input (below).

Moving them into `call_common.h` makes them shared code. That belongs with
#509 and #510, which touch the same lines.

### Each stack's own statistic, read by the same rule

| metric | ours | libwebrtc |
|---|---|---|
| target in the samples | the session's `rates().target` | `Call::GetStats().send_bandwidth_bps` |
| `cc.rtt_p50_ms` | the sender's `CallStats` RTT, every 100 ms (#370) | `Call::GetStats().rtt_ms`, every 100 ms |
| `audio.jb_delay_p50_ms`, `_p95_ms` | `jitterBufferDelay / jitterBufferEmittedCount` over each pull | the same, from `AudioReceiveStream::GetStats` |
| `audio.net_delay_p95_ms` | link arrival minus the packet's capture time (RTP time from 0) | link arrival minus `(ts - first ts) / 48` ms |
| `audio.late_discards` | the jitter buffer's `packets_discarded_late` (its `DiscardOldPackets` port) | NetEq's `packetsDiscarded` |
| `video.keyframe_requests` | one per PLI or FIR the sender's RTCP names a media SSRC in | `pli_packets + fir_packets` for the video SSRC |
| `video.e2e_delay_*`, `video.time_to_first_frame_ms`, `video.frames_rendered` | render time of each frame, minus its capture time | the same, from the renderer callback |
| `video.avg_kbps`, `video.qp_mean`, `video.encode_height_mean`, `video.keyframes` | the shared encoder model's output (row 2) | the same |
| `video.encoder_dropped` | captured frames the library chose not to encode | captured minus encoded, which also counts the frames still in the encoder queue when the call ends (at most a frame or two) |

Render times on our side fall on the 1 ms tick, and on libwebrtc's at the
event time: that is #507 and row 6, not a measurement rule.

### Different: repair metrics (#509)

| metric | ours | libwebrtc | example (10-seed means, ours / libwebrtc) |
|---|---|---|---|
| `repair.overhead_pct` | whole RTP packets (header, extensions, payload, the RED redundant block, the RTX sequence number), `call.cc:679-680` | payload only: the RED primary block, video minus the RED byte, RTX minus 2, FEC minus 1, `webrtc_call.cc:790-805` | `high_jitter` 39.9 / 42.3 % at RTX 244 / 241 kbps |
| `repair.nack_recovered` | the library's `NackTracker` counter: any NACKed gap later filled, by a late original too (`src/loss/nack_tracker.cc:32-35`) | RTX packets on the link that fill a sequence number not yet delivered, `webrtc_call.cc:1098-1101` | `high_jitter` 672.5 / 12.1; `issue_127_all_v2_no_stall` 1285 / 83 |
| `video.residual_loss_pct` | received = handed to the depacketizer by our `RtpReceiver`, which includes ULPFEC recoveries (`call.cc:102-110`) | received = link arrivals of media and RTX original sequence numbers, never FEC (`webrtc_call.cc:1097-1101`) | `issue_29_ulpfec_over_red` 0.73 / 4.90 % |
| `repair.fec_recovered` | the ULPFEC receiver's `num_recovered_packets` | not computed, so the parity report skips it | |

### Different: receive statistics and one-sided metrics (#510)

| metric | ours | libwebrtc | example |
|---|---|---|---|
| `audio.stretch_pct` | pulls whose operation was Accelerate or PreemptiveExpand (`call.cc:959`), also when no sample was removed or inserted | pulls in which `removed_samples_for_acceleration` or `inserted_samples_for_deceleration` grew (`webrtc_call.cc:1153-1181`) | `audio_only_lossy` 0.29 / 0.43 %; `clean_link` 0.53 / 0.60 % |
| start of the playout counters (stretch denominator, `ConcealmentMeter`, `jb_delay`) | the first pull after the first audio packet arrives | the first pull with `total_samples_received > 0` | |
| `audio.time_to_first_ms` | arrival of the first packet the jitter buffer accepted (#256) | arrival of the first audio-SSRC packet | equal on the published scenarios, not by construction |
| `video.undecodable_frames` | the keyframe requester's `frames_dropped` only (`call.cc:1098`) | `VideoReceiveStream::Stats::frames_dropped`, which also counts the frame buffer's fast-forwards and the frames cleared for a keyframe; our video trace already uses that sum (`call.cc:992`) | `high_jitter` 0.5 / 0; `issue_127_all_v2_no_stall` 0.6 / 0 |
| `video.delay_encode_ms`, `video.delay_pacer_p95_ms`, `video.delay_network_p95_ms`, `video.delay_jitter_buffer_p95_ms` | `AddVideoDelayMetrics` (`call.cc:384-404`) | not computed, so the parity report skips them | |

The fixes bring the narrower side up and make each rule shared code fed by
the same events. Never drop a metric to make the sides equal.
