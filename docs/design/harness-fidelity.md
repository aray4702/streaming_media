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

Rows were audited one per Fixer run, in the order 2, 5, 7, 4, 6, 8; rows 1
and 3 had items from the start (#477, #476) and were audited last, after those
items closed. Every row now has a status.

| # | area | status | evidence | item |
|---|---|---|---|---|
| 1 | Captured media | Audio: **identical by construction** since #489 (#477): the same 48 kHz signal and 16-bit conversion, and each 20 ms frame encoded at 20k + 10 ms on both (libwebrtc from two 10 ms ADM blocks, ours directly); on the seed-1 send traces of all 116 audio scenarios every frame has the same RTP timestamp, and every frame up to the first one whose loss input differs (row 2, #484) has the same libopus payload on 115 of them (135 681 frames). **Exception (audit 2026-10-28, #547):** on `issue_533_opus_decoder_frame_older_than_window` the payloads part at frame 240 (4.81 s) with the same loss input (20 %, clamped, on both), ours 1 byte larger, 185 of 682 frames; the same signature appears on `audio_only_lossy` at frame 606 on #543's head, where the RTCP draws now match. `issue_533` is not in `kDefaultTraced`, so the CI drift check does not see it. Video: **identical by construction**: the same capture schedule (`t >= k * 1000 / fps` on the 1 ms tick), capture time, size and frame rate; captured frames per second are the same on all 117 traced video scenarios (audit 2026-10-28). Both are now checked on every traced scenario by `tools/check_harness_drift.py` | [Row 1](#row-1-captured-media) | #477 (closed) |
| 2 | Encoders | Opus: **identical by construction**, packet for packet on 66 of 116 traced scenarios (audit 2026-10-28), checked on the lossless ones by `tools/check_harness_drift.py`; where the sides part, the input that differs is the stack's own RTCP, not the harness. Video: **identical by construction** (the same size formula, seed and QP/encode-time model), the formula is still written twice | [Row 2](#row-2-encoders) | #484 (stack, not harness) |
| 3 | Decoders | Audio: **identical by construction** since #487 (#476): ours decodes with the same libopus as `AudioDecoderOpusImpl` calls it (primary, LBRR, duration estimate), pinned sample for sample by `unit.OpusDecoder`. Video: **identical by construction**: neither side decodes pixels; libwebrtc's `ModelDecoder` returns every frame it is given at once, and ours reports a decode time of 0 for every frame its frame buffer releases, so both stacks' timing sees 0 ms decode time | [Row 3](#row-3-decoders) | #476 (closed) |
| 4 | Packetization and wire | H.264 access units: **shared code** (`rtp::MakeFakeH264Frame`); RTP header extensions (transport-cc, id 1, only), CNAME, RED and RTX formats: **identical by construction**, and on the seed-1 traces every frame of the same size is split into the same packets on both sides (30 157 frame sizes on 137 scenarios, audit 2026-10-28), audio packets match packet for packet (row 2) and RTX is the original + 2 bytes on both. The video packet-size limit: **identical by construction** since #527 (#513): both get libwebrtc's 1460 bytes and ours reserves the RTX and padding-header room as libwebrtc does; the largest delta-frame packet and a full padding packet are the same size on all 9 `VIDEO_REQUIRED` scenarios, checked by `tools/check_harness_drift.py`. On 13 lossy scenarios outside that list the largest delta-frame packet differs by 1 byte (1485 vs 1486) while every shared frame size splits the same way: the largest packet comes from a frame size only one side sent, not from a different limit | [Row 4](#row-4-packetization-and-wire) | #513 (closed), #514 (stack, not harness) |
| 5 | The link | Link model, seeds, timeline, cross traffic and delivery times: **shared code** (`sim::NetworkLink`, `testbench::LinkTimeline`), each packet arrives at the same simulated time on both since #467 / #453. When the stack is handed a packet and sends its response: **identical by construction** since #516 (#507): both at the packet's arrival time (audit 2026-10-28: RTX off the 1 ms grid 22 534 of 46 419 on ours, 23 347 of 46 663 on libwebrtc, all 137 seed-1 traces) | [Row 5](#row-5-the-link) | #507 (closed) |
| 6 | Clock and tasks | Periodic schedules (GoogCC's 25 ms process interval, the receiver's transport-feedback timer, the first RTCP report at half the interval), the order of the capture, feedback, pacer and playout work within a millisecond, and the pacer's 1 ms release grid: **identical by construction**; the transport-feedback arrivals match on all 115 traced scenarios until the traffic differs. The random stream behind each RTCP report interval: **different** (libwebrtc's `webrtc::Random` seeded with the clock start, ours `sm::Random` seeded with the scenario seed): the sender's second RTCP packet goes out at a different time on 115 of 115 traces. Our stack's own timers (video render, timers that follow an off-grid event, ties at the same instant): **the same** since #518: each session runs at its `NextProcessTime`, at the due microsecond as on libwebrtc's `SimTime`, ties in `SimTime`'s order (seed 1: render gaps off the grid in 5/26/17 of 30 seconds on `clean_link`/`high_jitter`/`random_loss_5pct`, libwebrtc 4/23/15; before, 0) | [Row 6](#row-6-clock-and-tasks) | #517, #518 (fixed) |
| 7 | Measurement | Link, congestion-control, goodput, concealment, A/V sync and freeze metrics: **shared code** (`call_common.h`, `AddCcMetrics`, `AddLinkKindMetrics`, `video::FreezeDetector`); the end-of-call summary formulas are **identical by construction** but written twice. Repair metrics (`repair.overhead_pct`, `repair.nack_recovered`, `video.residual_loss_pct`): **shared code** since #521 (#509), one rule in `call_common.h` fed by both calls; `repair.fec_recovered` now reported by both (libwebrtc's `UlpfecReceiver`). Audio stretch, playout start, first audio, undecodable frames and the `video.delay_*` breakdown: **one shared rule** since #523 (#510) | [Row 7](#row-7-measurement) | #509, #510 (closed) |
| 8 | Scenario mapping | One parser (`testbench::ParseScenario`) and one `Scenario` for both sides. Link keys (`at`, `reverse at`): **shared code** (`testbench::LinkTimeline`). Call and media keys (`duration`, `seed`, `audio`, `video`, `red`, `nack`, `fec`, `audio_rate`, `video_fps`, `video_size`, `video_complexity`, `encoder_speed`, `encode_delay`, `keyframe_interval`, `video_max`, `video_min`, `start_rate`, `min_rate`, `max_rate`, `quality_scaling`): **identical by construction**, each mapped to the matching libwebrtc setting. `audio_sample_rate`: **the same on both**, ignored (both run Opus at 48 kHz since #489), so its two scenarios run the default call. `video_max_payload`: **identical by construction** since #527 (#513). `av_sync off`: **identical** since #524: both sides only empty the receiver's sync group, and both senders keep their audio RTCP (`issue_524_av_sync_off`). `host_rtt`, `host_rtt_inf`: **ours only**, a host API libwebrtc has no counterpart for. The stack switches (`timing`, `neteq`, `bitrate_adjuster`, `render_smoothing`, ...): **ours only by design**, they pick the stack under test; libwebrtc runs M157's fixed configuration | [Row 8](#row-8-scenario-mapping) | #524; #513 (row 4); #480 (switches not at libwebrtc's value) |

## Row 1: captured media

Audited on `main` at 5c78f6f (2026-10-28), with the seed-1 traces that
`parity-data/traces/` holds (`<scenario>.1.{sm,webrtc}.{audiosend,video}.csv`).

### Audio

| | ours (`testbench/call.cc`) | libwebrtc (`compare/webrtc/webrtc_call.cc`) |
|---|---|---|
| Signal | `sim::SpeechLike(48000, duration + 1, seed + 7)`, `OpusSendEncoder::ToPcm` (`clamp(v) * 32767`) | the same call and conversion (`:1008`, `:1029-1038`) |
| Chunking | frame k is the samples `[20k, 20k + 20)` ms, encoded at 20k + 10 ms (`:834-841`) | the ADM gets 10 ms blocks at every 10 ms tick (`RecordedDataIsAvailable`, `:1037`); the ACM completes a 20 ms Opus frame on every second block, at 20k + 10 ms |
| Sample rate | 48 kHz on the wire and in NetEq since #489 (`AudioCodecRate`) | 48 kHz (`kAudioRate`, `opus/48000/2`) |
| Audio processing | none | none: `AudioState::Config` has no APM, so the encoder gets the samples unchanged (row 2) |

**The traces agree.** `tools/audio_send.py` on all 123 scenarios with audio
(seed 1): every frame has the same RTP timestamp on both sides (0 differences),
and up to the first frame whose loss input differs (the stack's report blocks,
#484) every frame has the same libopus payload hash: 133 477 of 133 477
frames. libopus is deterministic, so that means the same samples in the same
frames with the same settings. Send times after the encoder differ only by
each stack's pacer (for example 1 frame of 1500 on `issue_124_burst_loss_v2`).

### Video

| | ours | libwebrtc |
|---|---|---|
| Schedule | a frame at the first tick `t >= k * 1000 / video_fps` ms (integer division), `k` counting every captured frame, encoded or dropped (`:858-860`) | the same rule, `k` = `frames_captured` (`:1040-1044`) |
| Capture time | the tick's `now` (`PendingFrame{now, ...}`, `:908`); RTP timestamp `t * 90` (`:905`) | `sim.Now()` at the same tick (`ModelSource::Capture`, `:557-571`); `VideoStreamEncoder` derives the RTP timestamp from it (plus libwebrtc's random offset, row 4 / #514) |
| Size, rate | `OnCapturedFrame({video_width, video_height})`, `vsc_cfg.fps = video_fps` | `ModelSource(video_width, video_height)` (`:852`), `max_framerate = video_fps` |
| Content | none: the encoder model reads no pixels | a black I420 buffer, which the model encoder doesn't read either |
| Adaptation | the library's quality scaling picks the encoded size (`dec.width`, `dec.height`) | `VideoAdapter::AdaptFrameResolution` in the source (`:560`), driven by `VideoStreamEncoder`'s sink wants |

Adaptation is the stack under test (what each one asks for), not the
harness: both start from the same captured frame. **The traces agree:** the
`captured` column of the per-second video trace is the same in every second
on all 125 traced video scenarios; the other 8 are audio only (ours writes
rows of zeros, libwebrtc none).

### What keeps it identical

`tools/check_harness_drift.py` now also checks row 1 on **every** traced
scenario, lossy or not, since capture does not depend on the link: each audio
frame must have the same RTP timestamp and, until the loss inputs part, the
same payload (`audio_send.compare`), and each second must have the same
number of captured video frames. `tools/test_check_harness_drift.py` covers
the check. All 9 scenarios of `kDefaultTraced` pass it on the published
`compare-webrtc.json`.

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

## Row 3: decoders

Audited on `main` at 5c78f6f (2026-10-28).

### Audio

Since #487 (#476) our side decodes with the libopus revision M157 builds,
called as libwebrtc's `AudioDecoderOpusImpl` calls it
(`testbench/opus_decoder.h`, citing
`reference/modules/audio_coding/codecs/opus/audio_decoder_opus.cc` and
`opus_interface.cc`): one mono decoder state at 48 kHz (`opus/48000/2`
without `stereo=1` on both, `webrtc_call.cc:948`), `WebRtcOpus_Decode` with
room for 120 ms, `WebRtcOpus_DecodeFec` for a missing frame's LBRR copy and
`WebRtcOpus_FecDurationEst`. `OpusAudioDecoder` (`call.cc:533`) keeps the
calls in NetEq's order: each frame decoded once, an LBRR copy only for a frame
that hasn't arrived, frames in timestamp order. **Identical by
construction:** `unit.OpusDecoder` (`tests/unit/testbench/opus_decoder_test.cc`)
checks the decoded samples against libwebrtc's call sequence sample for
sample, with in-order frames, LBRR on loss, duplicates and RED copies,
reordering and a late primary. What NetEq then does with those samples is the
stack (#504 owns the receive-side audio cells).

### Video

Neither side decodes pixels. libwebrtc's `ModelDecoder` (`webrtc_call.cc:489-511`)
returns a frame for every `EncodedImage` it is given, inside `Decode()` in
simulated time, so `decode_time_percentile_filter.cc` sees 0 ms. Ours has no
decoder (`video_cfg.deliver_bitstream = false`, `call.cc:618`) and reports
`decode_time = 0` for every frame its frame buffer releases (`call.cc:615`,
#379). **Identical by construction:** both decode every frame they are handed,
instantly; which frames each stack hands over (complete, decodable, in
order) is the stack's frame buffer, measured by `video.undecodable_frames`
(row 7, #510).

## Row 4: packetization and wire

Audited on `main` at b5a82f2 (2026-10-28), with the seed-1 forward-link
traces that `parity-data/traces/` holds for 3025aeb (115 scenarios with both
sides). The reverse link has no trace yet (#507 adds one), so RTCP is checked
on the forward link only.

### Shared or identical

| | ours (`testbench/call.cc`) | libwebrtc (`compare/webrtc/webrtc_call.cc`) |
|---|---|---|
| H.264 access unit for a frame of N bytes | `rtp::MakeFakeH264Frame(key, N)` (`:847`) | the same function, through `ModelAccessUnit` (`model_h264.h`) |
| Packetization mode | mode 1 (`sm/rtp/h264.h`: single NAL, STAP-A, FU-A) | `packetization-mode=1` (`ModelH264Format`, `:510-513`) |
| SPS VUI rewrite of a keyframe | `ParseOutgoingBitstreamAndRewrite` (`src/rtp/session.cc`, #342) | `FrameEncodeMetadataWriter::UpdateBitstream` |
| RTP header extensions | transport-cc, id 1, nothing else (`:541`) | `kTransportSequenceNumberUri`, id 1, nothing else (`:727-728`) |
| SDES CNAME | `sender@testbench` / `receiver@testbench` (`:569`, `:572`) | `sender@testbench` (`kSenderCname`, #409) |
| Audio RED, RTX, ULPFEC over RED | RFC 2198 / 4588 / 5109 from the session | the same, from `AudioSendStream` / `RtpVideoSender` |
| Wire size | RTP or RTCP bytes + 28 (UDP/IP) | the same |
| SSRCs and payload types | 1 / 2 / 3, PT 111 / 63 / 96 / 97 | Chrome's values (`:297`, `:304`) |

SSRCs and payload types differ in value only; both sides write them in the
same fields at the same size, so the packets are the same length.

On the traces:

- **Video.** Grouping each side's video packets by frame (RTP timestamp,
  first transmissions, complete frames), **every frame size that occurs on
  both sides of a scenario is split into the same packets** on both: 28 647
  frame sizes on 115 scenarios (sizes on the wire), for example a
  `clean_link` frame of 6532 bytes goes as 3 x 1306 + 2 x 1307 on both. The exceptions are `issue_257_zero_max_payload` (below) and 18
  frames that are a keyframe on one side and a delta frame of the same total
  on the other. The largest video packet is 1486 bytes on the wire (1458 of
  RTP) on both sides of `clean_link`.
- **Audio.** Packet for packet on 60 of 104 scenarios, RED included (`red`
  is on by default); row 2 explains the other 44.
- **RTX.** Every RTX packet is its original's size + 2 on both sides
  (38 545 and 39 088 packets).
- **RTCP on the forward link.** The same two sizes on both sides, 84 and 108
  bytes (8091 / 6593 packets on ours, 7860 / 7685 on libwebrtc): the same
  compound packets, sent on each stack's own schedule.

### Different: the video packet-size limit (#513)

libwebrtc's `RTPSenderVideo::SendVideo` takes the packet capacity as the
sender's `MaxRtpPacketSize`, less `FecPacketOverhead()` when the frame is
FEC-protected, **less `RtxPacketOverhead()` (2 bytes here) when the frame
may be retransmitted**
(`modules/rtp_rtcp/source/rtp_sender_video.cc:676-687`, `:612-622`;
`rtp_sender.cc:566-595`). Its padding packets are at most
`max_packet_size_ - max_padding_fec_packet_header_`, the header with the
extensions a padding packet carries (`rtp_sender.cc:434-452`).

Our session takes the capacity as `max_payload_size + 12 - FecPacketOverhead()`
(`src/rtp/session.cc:280-287`) with no RTX reservation, and our padding as
`min(max_payload_size, 255)` with no room for the header extension
(`src/rtp/rtp_sender.cc:243-245`).

The harness covers the first of these with its inputs. The scenario default
`video_max_payload 1446` (`testbench/scenario.h:81`) gives our session a
1458-byte packet, the capacity libwebrtc is left with after its RTX
reservation, while libwebrtc keeps its own 1460-byte default and makes the
reservation itself (`webrtc_call.cc:899-905`). So the two sides are handed
different packet-size limits, and they agree only when both of these hold:

- **NACK on**, so libwebrtc reserves the 2 bytes. With NACK off it does not,
  and our packets are the smaller ones: `issue_29_ulpfec_over_red` sends
  video packets up to 1459 bytes on the wire on ours and 1462 on libwebrtc,
  and FEC packets up to 1481 against 1484.
- **The default `video_max_payload`.** A scenario that sets it gives both
  sides `N + 12`, and only libwebrtc takes the 2 bytes off:
  `issue_257_zero_max_payload` (100-byte packets) splits 28 of its 46 shared
  frame sizes differently, with packets of 127-128 bytes on ours against
  124-126 on libwebrtc. There our padding packets are 136 bytes on the wire,
  8 bytes over the limit (the transport-cc extension), against 128 on
  libwebrtc.

The fix (#513) brings our session up to libwebrtc's rule (the RTX
reservation when the frame may be retransmitted, and the padding header), so
that both sides can be given the same packet-size limit, libwebrtc's 1460
bytes by default and `N + 12` for `video_max_payload N`. Done when the
seed-1 traces of `clean_link`, `issue_29_ulpfec_over_red` and
`issue_257_zero_max_payload` split every shared frame size the same and
have the same padding sizes on both sides.

### In the stack, not the harness

These differ in what the stack under test puts on the wire, from the same
input; they are filed as #514 for the Coordinator to place:

- **Padding without RTX.** With NACK off there is no RTX stream, and
  libwebrtc pads on the media SSRC (`rtp_sender.cc:437-449`,
  `can_send_padding_on_media_ssrc`): 383 padding packets on
  `issue_29_ulpfec_over_red` against 4 on ours.
- **Padding in an audio-only call.** `audio_only_lossy` and its copies: ours
  2118 padding packets, 2115 of them 224 bytes; libwebrtc 851, of 38 sizes
  (238, 303 and 104 bytes the most common), so the loss draws fall on other
  audio packets (row 2, row 5).
- **Start values and the first marker bit.** libwebrtc starts each stream's
  sequence number and RTP timestamp at a random value (6666 and 873682028
  for audio on all 105 seed-1 traces with audio) and sets the marker bit on the first audio
  packet (`rtp_sender_audio.cc:88-128`); ours starts both at 0 (video at the
  capture time x 90) and sends no audio marker. No metric reads these.

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

## Row 6: clock and tasks

Audited on `main` at f281dfb (2026-10-28), with the seed-1 forward-link and
GoogCC traces (`<scenario>.1.{sm,webrtc}.{trace,bwe}.csv`) that
`parity-data/traces/` holds for 3025aeb (115 scenarios with both sides).

libwebrtc runs on `SimTime` (`compare/webrtc/sim_time.{h,cc}`): one thread,
a simulated clock, and task queues whose tasks run at the microsecond they
are due, in (due time, posting order), queue by queue in creation order
(`sender`, `receiver`, `network`, then the queues libwebrtc creates itself:
pacer, encoders). `sm_compare`'s 1 ms tick is a task on `network`
(`webrtc_call.cc:1015-1257`). Our test bench steps a 1 ms loop and calls
each session directly (`testbench/call.cc:787-1135`).

### Identical by construction

| | ours (`testbench/call.cc`, the library) | libwebrtc (`compare/webrtc/webrtc_call.cc`, `reference/`) |
|---|---|---|
| Order within a millisecond | link change; RTCP that arrived by `now` to the sender; audio encode; video capture and encode; cross traffic; queue sample; pacer; media that arrived by `now` to the receiver; render; playout pull; the receiver's RTCP | RTCP to the sender at its arrival (sender queue, before the tick); link change (also before every send, `advance_links`); audio block and video capture; cross traffic; queue sample (the tick); then the encoder and pacer queues, created after `network`; the playout pull is posted by the tick, so it runs after the packets that arrived by then |
| GoogCC process interval | `OnProcessInterval` at the first `Process` (t = 0) and every 25 ms after, with the pacer-queue update in the same step (`src/rtp/send_side_controller.cc:341-349`) | `UpdateControllerWithTimeInterval` when the controller is created at call setup, then `controller_task_` and `pacer_queue_update_task_` every 25 ms (`call/rtp_transport_controller_send.cc:772-773`, `:796-815`) |
| Pacer release times | `Process` every 1 ms | the next process at `next_send_time - now` rounded up to 1 ms (`modules/pacing/task_queue_paced_sender.cc:304-312`), so on the grid while the event that scheduled it was |
| Transport feedback timer | the receiver's feedback interval, from t = 0 | `TransportSequenceNumberFeedbackGenerator`'s, from call setup |
| First RTCP report | half the nominal interval, no draw (`src/rtp/rtcp_engine.cc`) | `report_interval_ / 2` in the constructor (`rtcp_sender.cc:171`) |
| NACK processing | every 20 ms from the first `Process` (`src/loss/nack_tracker.cc:7`, `:84`) | `NackPeriodicProcessor`, every 20 ms from registration at setup (`modules/video_coding/nack_requester.cc:55`); not checked on a trace until #507 adds the reverse link |

On the traces:

- **Feedback timing.** The sender's first four transport-feedback arrivals
  (`delay_based` rows of the bwe trace) are at the same times on both sides
  on **all 115** scenarios (`clean_link`: 125, 175, 225, 275 ms), and the
  two sides stay identical for a median of 60 feedback packets, until the
  send rates (and so the feedback interval) part. Both GoogCCs make their
  first update at t = 0.
- **Pacer grid.** libwebrtc's pacer releases video on the 1 ms grid as ours
  does: of its 485 805 video packets, the only ones off the grid are the
  13 686 `encode_delay` frames of `issue_84_cpu_overuse_slow_encoder`
  (#507); audio 6 of 183 250, padding 10 of 5691.

The clock start differs in value only (ours 0, libwebrtc
`100000 + 7919 * ((seed - 1) % 1000)` s, `webrtc_call.cc:639-642`). Both
sides measure relative to it; the one place libwebrtc reads its absolute
value is the RTCP seed below.

### Different: the random stream behind each RTCP report interval (#517)

Both pick an interval uniformly in [m/2, 3m/2] ms by the same rule
(`rtcp_sender.cc:680-693`; `src/rtp/rtcp_engine.cc:25-35`), but from
different random streams:

- libwebrtc: `webrtc::Random` (xorshift64\*) seeded with
  `clock().TimeInMicroseconds()` when each `RTCPSender` is built
  (`rtcp_sender.cc:130`), which in `sm_compare` is the clock start, the same
  for all four of them;
- ours: `sm::Random` (xoshiro256\*\*) seeded with the scenario seed
  (`call.cc:565`), `seed + 1` for the sender's audio RTCP
  (`src/rtp/session.cc:239`).

| seed 1 | sender's 1st RTCP on the forward link | 2nd |
|---|---|---|
| scenarios at the same time on both | 114 of 115 (500 ms, no draw) | **0 of 115** |
| `clean_link` | 500 / 500 ms | 774 / 684 ms |
| `random_loss_5pct` | 500 / 500 ms | 989 / 818 ms |
| `high_jitter` | 500 / 500 ms | 1410 / 1033 ms |

So the report blocks that feed the loss-based estimate, the RTT and the
Opus loss input reach the sender at different times for a reason outside
the stack under test. The fix (#517) ports `webrtc::Random` for the RTCP
draws and seeds every RTCP engine of ours as libwebrtc's are seeded, with
the same per-seed clock value from the harness. `rtc_base/random.{h,cc}` is
not in `compare/webrtc/reference/` yet.

### Fixed: our stack's timers ran on the 1 ms tick (#518)

Fixed by #518's PR: the test bench runs each session at its own due time
inside the tick, on #507's per-event loop (`testbench/call.cc`), and
`Session::NextProcessTime` reports every timer (the video receivers'
frame release, render dequeue, decode time-out and NACK tick in place of
"1 ms away"; at once after a packet). At one instant the order is
`SimTime`'s: the sender's timers, its arriving feedback and finished
frames, then the receiver's timers, then its arriving packets. The NACK
retry ticks keep their 20 ms grid as `RepeatingTask` does
(`repeating_task.cc:87`). Frames render at their release time. On seed 1
the per-second longest render gap is off the grid in 5, 26 and 17 of 30
seconds on `clean_link`, `high_jitter` and `random_loss_5pct` (libwebrtc 4,
23 and 15; before the fix 0), and `issue_12`'s first gaps are 86.6 and
106.9 ms on both stacks. What the audit found before the fix:

`Session::Process` runs once per tick, so each of our timers fires at the
next whole millisecond after it is due; libwebrtc's fire at their due
microsecond. #507 covers the timers that a packet's arrival or an
`encode_delay` frame starts. The rest:

- **Video render.** libwebrtc decodes and renders each frame when its
  decode task is due; ours renders after `receiver.Process(now)` on the
  tick, and `Session::NextProcessTime` is "at most 1 ms away" with a video
  receiver (`include/sm/rtp/session.h:297-299`). The per-second longest
  render gap (video trace) is a whole number of milliseconds in 4065 of
  4065 seconds on ours, and off the grid in **1245 of 3865** on libwebrtc's.
- **Timers after an off-grid event.** libwebrtc schedules from `now`, so
  after one off-grid event the timers that follow are off the grid too:
  2418 of libwebrtc's 18 538 loss-based updates are off the grid (report
  blocks arriving off it, `random_loss_5pct` from 1058.249 ms,
  `high_jitter` from 6232.769 ms); 0 of our 20 104.
- **Ties.** `SimTime` runs a receiver timer posted earlier (transport
  feedback, NACK, RTCP) before a packet that arrives at exactly its due
  time; our tick hands that packet to the receiver before
  `receiver.Process(now)`, so it is in the report on ours and in the next
  one on libwebrtc's.

The fix (#518) drives each session at its due times as `SimTime` does,
with the receiver's frame scheduling reporting its due time to
`NextProcessTime`, on the same loop as #507.

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

Render times on both sides fall at the event time since #518 (row 6).

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

## Row 8: scenario mapping

Audited on `main` at becd6f4 (2026-10-28): every key `testbench/scenario.cc`
accepts, followed into `testbench/call.cc` (ours) and
`compare/webrtc/webrtc_call.cc` (libwebrtc), with the keys' use in the 128
suite scenarios.

`sm_compare` parses each `.scn` once with `testbench::ParseScenario` and hands
the same `Scenario`, with the same seed, to `testbench::RunCall` and
`RunWebrtcCall` (`compare/webrtc/sm_compare_main.cc:241-262`). So the
question is only what each side does with each field.

### Shared code

| keys | both sides |
|---|---|
| `at ...`, `reverse at ...` (every link key: `capacity`, `delay`, `jitter`, `loss`, `burst`, `dup`, `reorder`, `queue`, `cross`, `down`, `up`) | `testbench::LinkTimeline` builds and advances both links (`call.cc:505-509`, `webrtc_call.cc:686-700`); row 5 |
| `duration`, `seed` | the call length and every seed derived from it (link `seed` and `seed + 1000003`, encoder model, speech signal `seed + 7`); row 5. libwebrtc's side also derives its clock start from `seed` (or `compare_clock_start`, which no scenario sets), the input row 6's #517 is about |
| `video_complexity`, `encoder_speed`, `encode_delay` | the shared `testbench::EncoderModel` (`EncoderModel::Qp`, `EncodeTimeUs`) on both (`call.cc:885-901`, `webrtc_call.cc:419-442`) |

### Identical by construction

| key | ours (`testbench/call.cc`) | libwebrtc (`compare/webrtc/webrtc_call.cc`) |
|---|---|---|
| `audio on\|off`, `video on\|off` | no stream configured, no media generated | no send or receive stream created (`:883`, `:898`, `:942`, `:953`), no media generated (`:1028`, `:1039`) |
| `red` | `audio_cfg.red_payload_type` | `send_codec_spec->red_payload_type`, and the RED decoder on the receiver (`:892`, `:948`) |
| `nack`, `fec` | `video_cfg.nack`, `video_cfg.fec` | NACK history and RTX payload types; ULPFEC and RED payload types (`:912-920`, `:962-970`). What each stack then does with FEC and NACK together (#7) is the stack |
| `audio_rate` | Opus at `audio_bitrate` for the whole call | `target_bitrate_bps` = `min` = `max` = `audio_bitrate` (`:891-894`); row 2 |
| `video_fps` | capture every `1000 / fps` ms, `vsc_cfg.fps` | the same capture schedule, `simulcast_layers[0].max_framerate` (`:930`, `:1041`) |
| `video_size` | `OnCapturedFrame({w, h})` | `ModelSource(w, h)` (`:851`) |
| `video_max`, `video_min` | the allocation's maximum and minimum and the adjuster's minimum (`call.cc:547-548`, `:642-644`) | `max_bitrate_bps`, `simulcast_layers[0].min_bitrate_bps` (`:931-932`) |
| `start_rate`, `min_rate`, `max_rate` | GoogCC's constraints (`call.cc:543-545`) | `BitrateConstraints` (`:722-724`) through `int_bps`; ours keeps the `DataRate` (#464 brings ours to libwebrtc's `int` bound) |
| `keyframe_interval` | the library's periodic keyframe: a frame `>= interval` after the last keyframe (`src/rtp/video_send_controller.cc:222-224`) | the model encoder forces a keyframe `>= interval` after the last keyframe it produced (`:404`, `:440`), as an encoder's own key-frame interval does. The rule is written twice; one scenario uses it (`issue_5_bitrate_adjuster`) |
| `quality_scaling on\|off` | `vsc_cfg.quality_scaling` | the QualityScaling field trial, the encoder's `scaling_settings` and `MAINTAIN_FRAMERATE` vs `DISABLED` (`:663-666`, `:469-473`, `:937`) |

### The same on both, and ignored: `audio_sample_rate`

Both sides run Opus at 48 kHz whatever the scenario says: ours since #489
(`AudioCodecRate` returns 48000, `call.cc:494`), libwebrtc's always (the ADM
is fed 48 kHz PCM). So `issue_256_audio_24khz` and
`issue_298_audio_24khz_plays` run the default call on both sides: on our
side, `issue_256_audio_24khz` with and without its `audio_sample_rate 24000`
line gives the same 79 metrics on seed 1. Not a harness difference; the two
scenarios no longer test what their names say (`call.cc:512-516`'s comment
still describes the pre-#489 behaviour).

### Different: `video_max_payload` (#513)

Row 4. The default (1446, from 1458) reaches ours as given and libwebrtc as
its own default; a non-default value is passed to both as `+ 12` clamped to
100-1500 (`call.cc:559-560`, `webrtc_call.cc:909-910`), but ours makes no
RTX reservation.

### Fixed: `av_sync off` (#524)

| | ours | libwebrtc |
|---|---|---|
| `av_sync on` (default) | one sync group; the sender's own audio RTCP engine sends the audio SR (`call.cc`, `sender_cfg.audio_rtcp`; `src/rtp/session.cc:238-245`) | `sync_group = "sm"` on both receive streams (`webrtc_call.cc:957`, `:982`) |
| `av_sync off` | no sync group; the sender's audio RTCP stays on | no sync group; the audio channel still runs compound RTCP (`reference/audio/channel_send.cc:551`) and sends its SR |

Before #524 ours also turned the sender's audio RTCP off with `av_sync off`
(`sender_cfg.audio_rtcp = s.av_sync`), so the key meant two things. Now it
only takes the streams out of the receiver's sync group on both sides, and
`issue_524_av_sync_off` (a `clean_link` copy with `av_sync off`) runs it in
the suite and the comparison. `tests/unit/testbench/av_sync_rtcp_test.cc`
checks that the sender's RTCP on the forward link is the same with sync on
and off.

### Ours only

- **`host_rtt`, `host_rtt_inf`** (`issue_451_host_rtt_near_limit`,
  `issue_441_infinite_rtt`): the host hands our sessions an RTT through
  `Session::SetRtt` (`call.cc:977-985`). libwebrtc has no such host input and
  its side ignores the keys, so those two scenarios compare our call with the
  RTT forced against libwebrtc's normal call. They test our API's robustness
  (no overflow), not parity; on the published comparison `issue_441` has no
  gap.
- **Stack switches** (`timing`, `neteq`, `bitrate_adjuster`, `rate_utilization_tracker`, `control_handler`,
  `send_rate_stats`, `link_headroom`, `sps_pps_tracker`,
  `h264_missing_packets`, `render_smoothing`, `keyframe_flush_padding`):
  they choose the stack under test, so they are ours only by design, and
  libwebrtc's side runs M157's fixed configuration. A cell compares like
  with like only when the switch is at libwebrtc's value. Two are not, by
  default, in every scenario: `render_smoothing` and
  `keyframe_flush_padding` are off on ours while libwebrtc always runs the
  pre-render queue and flushes padding with a keyframe (#437, #480). One
  scenario sets a switch away from libwebrtc's value on purpose:
  `issue_453_sps_pps_tracker_high_jitter` (`h264_missing_packets off`).
  These are stack configuration, owned by #480 (step 3), not the harness.
