# Design: the RTP/RTCP session layer

Status: **proposal for Owner review** (issue #31). Implementation is issue #32.
This document is design only; no code changes come with it.

Product decisions this design applies (program.md 0.1, and the Owner comments
on #31):

- the library owns RTP and RTCP, and everything on the wire is standard;
- one-to-one calls only (BUNDLE demultiplexing stays);
- media uses the codec's standard RTP payload format: **H.264 (RFC 6184)
  first**, Opus (RFC 7587) for audio;
- the test bench's encoder model follows libwebrtc's `FakeH264Encoder`;
- frame boundaries and dependencies follow libwebrtc's H.264 practice.

> **Verification note.** The cloud routine that wrote this document could not
> reach libwebrtc's source (outbound access to `webrtc.googlesource.com` is
> blocked). Every libwebrtc reference below is from the author's knowledge of
> the tree and names the file or class to read, not a line. Statements marked
> **[verify]** are the ones most likely to have changed; the first
> implementation PR must check them against the pinned libwebrtc that
> `compare/` fetches before relying on them.
> The Owner checked the marked items against the pinned libwebrtc (M157,
> `b321f634`) on issue #32 and their results are applied below; the one
> left (SR packet and octet counts) is settled by the interop check in
> `compare/webrtc/rtcp_interop.cc`.

## 1. Where the library stands today

`testbench/call.cc` (929 lines) is the only place a call exists. Of it, roughly
900 lines are session glue:

| Glue in `call.cc` | What it does |
|---|---|
| `Fwd` / `Rev` structs | Media crosses the simulated link as `RtpPacket` objects; NACK, PLI and LNTF cross as structs with a `kind` tag. Only transport-cc, SR and RR cross as bytes. |
| `WriteVideoHeader` / `ReadVideoHeader` | A private 9-byte video payload header (start flag, key flag, 64-bit frame id). It is not a standard format and it carries information (the frame id) that real payload formats do not. |
| Send loop | Frame to packets, `rtx.OnPacketSent`, `fec_enc.AddMediaPacket`, `enqueue` to the pacer with a priority, transport-wide sequence numbers assigned at pacer release, `gcc.OnPacketSent`, padding. |
| Sender RTCP | SR once per second at a fixed phase. |
| Receiver loop | Per-kind dispatch: `RestoreFromRtx`, `fec_dec`, `red_rx`, `video_stats`, `nack.OnReceivedPacket`, `packet_buffer.Insert`. |
| Receiver RTCP | NACK batches every tick, transport-cc every 50 ms, RR every second, PLI/FIR/LNTF from `KeyframeRequester`. |
| Sender feedback dispatch | transport-cc to GCC, NACK to `RtxSender`, PLI to the encoder model, RR to `RttEstimator` and the protection controller. |
| Frame handling | `PopCompleteFrames`, `KeyframeRequester::OnFrame`, jitter estimator, `VideoTiming`, `FrameScheduler`. |

Everything the glue needs already exists as a module: `cc::Pacer`,
`cc::GccController`, `cc::OutageMonitor`, `loss::{RtxSender,FecEncoder,
FecDecoder,RedEncoder,RedReceiver,NackTracker,KeyframeRequester,
ProtectionController}`, `timing::{PacketBuffer,ReceiveStatistics,
RttEstimator,VideoJitterEstimator,VideoTiming,AudioJitterBuffer}`,
`cc::{TransportFeedbackBuilder,ParseTransportFeedback}`, `sync::
{ExtensionMap,ExtensionAccessor,RtpDemuxer}` and, since #28,
`loss/rtcp_feedback.h` (NACK, PLI, FIR, LNTF, compound split/build). What is
missing is the object that connects them, plus the pieces listed in section 9.

Why it matters (from #31): every application would rewrite this glue, #18 was
a bug in exactly this glue, and #20, #21, #23 and #29 all describe behaviour
(RTX rate limiting, protection bitrate inside the target, keyframe budget,
post-outage ramp, ULPFEC) that belongs in the library's sender and receiver.

## 2. Design summary (the recommendations)

1. A new section library **`sm::rtp`** (`include/sm/rtp/`, `src/rtp/`, CMake
   option `SM_WITH_RTP`, depends on core, loss, timing, cc, sync and video, the last for the frame scheduler). It holds
   the session layer and the payload packetizers. Existing modules do not
   change their dependencies.
2. A single facade, **`rtp::Session`**, for one call between two endpoints,
   composed of small classes that tests and advanced users can also use alone:
   `RtpSender` (per stream), `RtpReceiver` (per stream), `RtcpEngine`
   (scheduling, building, parsing, dispatch) and `SendSideController` (pacer
   + GCC + outage monitor + protection). Section 4.
3. **Bytes in, bytes out, pull-based.** The application calls
   `OnPacket(bytes, now)`, `Process(now)`, `Send*Frame(...)` and drains
   `TakeOutgoing()`, `TakeVideoFrames()`, `TakeEvents()`. No callbacks, no
   threads, no clocks. Section 4.2.
4. **Payload formats:** H.264 first (single NAL, STAP-A, FU-A; packetization
   mode 1; Constrained Baseline), Opus for audio, both library-provided.
   VP8, AV1 and VP9 later, each its own issue behind the same
   `VideoPacketizer` / `VideoDepacketizer` interfaces. Section 6.
5. **The test bench encoder model emits real NAL units** (`FakeH264Encoder`
   style: SPS, PPS, IDR for key frames; non-IDR slices for delta frames;
   filler payload), so the real packetizer and depacketizer run in every
   call. Section 6.3.
6. **No dependency descriptor for H.264.** libwebrtc does not use it for
   H.264, and neither will we. Frame boundaries and keyframes come from the
   stream; frame ids and references are derived from RTP sequence numbers
   (section 6.4). LNTF is therefore not sent for H.264 (libwebrtc cannot send
   it without a frame descriptor). Section 6.4.
7. **Migration in seven behaviour-preserving PRs** (section 11), with the one
   unavoidable exception called out: removing the private video header changes
   packet sizes and removes LNTF traffic, which moves test bench numbers
   slightly. Each such change is explained in its PR.

## 3. Mapping to libwebrtc

| libwebrtc | This design | Followed / simplified |
|---|---|---|
| `ModuleRtpRtcpImpl2` (one per send stream; owns RTCPSender, RTCPReceiver, RTPSender) | `rtp::Session` + `RtpSender` + `RtcpEngine` | Split by role, not by stream. One RTCP engine per session (libwebrtc has one per SSRC and relies on `RtcpTransceiver` for reduced-size). One-to-one makes a single engine enough. |
| `RTPSender`, `RtpSenderVideo`, `RTPSenderAudio` | `RtpSender` (+ `VideoPacketizer`, `OpusPayload`) | Header building, sequence numbers, extensions, RED/ULPFEC wrapping, history. |
| `RtpPacketHistory` | `loss::RtxSender` history | Reused as is. Gains the RED-wrapped packet storage rule (section 7.2). |
| `RtpSenderEgress` (assigns transport-wide seq, abs-send-time, feeds `TransportFeedbackObserver`, accounts bytes) | `SendSideController::OnPacketReleased` | The step that runs when the pacer releases a packet. |
| `PacingController` / `RtpPacketPacer` / `PacketRouter` | `cc::Pacer` (existing) | Session owns it; packets enter by priority, as in `PacingController::EnqueuePacket`. |
| `GoogCcNetworkController` | `cc::GccController` (existing) | Session owns it and routes feedback. |
| `RTCPSender` | `RtcpEngine::Build*` | Compound by default, reduced-size (RFC 5506) negotiated by config. Interval and jitter rules in section 8. |
| `RTCPReceiver` | `RtcpEngine::OnRtcp` | Parse and dispatch (section 8.3). |
| `RtpVideoStreamReceiver2` | `RtpReceiver` (video) | RTP parse, RED/ULPFEC (`UlpfecReceiver`), RTX (`RtxReceiveStream`), `NackRequester`, packet buffer, `KeyFrameRequestSender`. |
| `H26xPacketBuffer` (earlier `H264PacketBuffer` + `H264SpsPpsTracker`) | `timing::PacketBuffer` + `rtp::H264Depacketizer` | Section 6.4. |
| `VideoStreamBufferController` / `FrameBuffer` / `VCMTiming` | `VideoJitterEstimator` + `VideoTiming` + `FrameScheduler` (existing) | Composed by the session's video receive stream (section 5.3). |
| `RemoteEstimatorProxy` | `cc::TransportFeedbackBuilder` | Cadence configurable; default 50 ms to keep today's behaviour. |
| `ChannelReceive` (audio) | `RtpReceiver` (audio) + `timing::AudioJitterBuffer` | libwebrtc decodes inside NetEq; here the app supplies a decoder (section 5.2). |
| `RtpDemuxer`, `RtpStreamReceiverController` | `sync::RtpDemuxer` (existing) | BUNDLE by MID, then SSRC, then payload type. |
| `RtcpTransceiver` | not used | Needed only for multiple streams per endpoint. |

Deliberately not mirrored: simulcast and RID handling, `RtpTransportControllerSend`
multi-stream sharing, FlexFEC, and the per-stream `ModuleRtpRtcpImpl2` object
graph. Each is multiparty- or SFU-driven, or has no one-to-one use.

## 4. API

Style rules, as for every module: `Timestamp now` on every call that depends on
time; no threads, clocks, exceptions or RTTI; bounded memory; `MetricsSource`
plus `SM_EVENT`s for each decision; each class compiles out cleanly in the
`embedded` preset (metrics and events become no-ops, and `SM_WITH_RTP=OFF`
removes the section).

### 4.1 Types

```cpp
namespace sm::rtp {

// One 5-tuple-less endpoint of a call. Streams are added once, before traffic.
struct SessionConfig {
  uint32_t local_ssrc_base = 0;       // RTCP sender_ssrc for feedback; also CNAME seed.
  std::string cname;                  // SDES CNAME.
  std::string mid_audio, mid_video;   // BUNDLE MIDs (RFC 8843); empty = no MID extension.
  sync::ExtensionMap extensions;      // Negotiated id <-> URI (SDP stays with the host).
  RtcpConfig rtcp;                    // Intervals, reduced-size, feedback cadence (section 8).
  SendSideConfig send_side;           // Pacer, GCC, outage, protection (section 7).
  uint64_t seed = 1;                  // Seeds RTCP interval randomization and initial seq/ts.
  EventSink* events = nullptr;
};

struct AudioStreamConfig {            // Opus (RFC 7587) first.
  uint32_t ssrc;
  uint8_t payload_type = 111;         // Test bench value; the host sets it from SDP.
  std::optional<uint8_t> red_payload_type;  // RFC 2198 redundancy (audio/red).
  int ptime_ms = 20;
};

struct VideoStreamConfig {            // H.264 (RFC 6184) first.
  uint32_t ssrc;
  uint8_t payload_type = 102;         // H.264 CB, packetization-mode=1; test bench value.
  std::optional<uint32_t> rtx_ssrc;
  std::optional<uint8_t> rtx_payload_type;
  std::optional<uint8_t> red_payload_type;      // 116 in the bench.
  std::optional<uint8_t> ulpfec_payload_type;   // 117 in the bench (issue #29).
  size_t max_payload_size = 1200 - 12;          // Whole packet <= 1200 bytes (libwebrtc's kVideoMtu).
  VideoCodec codec = VideoCodec::kH264;
};

struct EncodedAudioFrame {            // One Opus packet.
  Timestamp capture_time;
  uint32_t rtp_timestamp;             // 48 kHz clock for Opus (RFC 7587 section 4.1), set by the app or derived.
  std::span<const uint8_t> data;
  bool start_of_talkspurt = false;    // RTP marker (DTX resume).
};

struct EncodedVideoFrame {            // Annex-B or length-prefixed NAL units; see 6.2.
  Timestamp capture_time;
  uint32_t rtp_timestamp;             // 90 kHz.
  bool keyframe;                      // Advisory; the receiver re-derives it from NAL types.
  std::span<const uint8_t> data;
};

struct OutgoingPacket {
  std::vector<uint8_t> bytes;         // RTP or RTCP, ready for the socket (SRTP is the host's job).
  PacketKind kind;                    // For the app's accounting only: kAudio, kVideo, kRtx, kFec, kPadding, kRtcp.
};

struct ReceivedVideoFrame {
  Timestamp render_time;              // From the library's timing model.
  uint32_t rtp_timestamp;
  Timestamp capture_time;             // From abs-capture-time if present, else unknown.
  bool keyframe;
  std::vector<uint8_t> data;          // Annex-B bitstream, ready for a decoder.
};

enum class SessionEventType {
  kKeyframeRequested,   // PLI or FIR arrived, or the outage monitor wants one: the encoder should emit an IDR.
  kEncoderPause,        // Outage monitor: stop encoding (and resume below).
  kEncoderResume,
  kTargetRateChanged,   // New total and per-stream targets (section 7.3).
  kRttUpdated,
};
}  // namespace sm::rtp
```

### 4.2 `rtp::Session`

```cpp
class Session : public MetricsSource {
 public:
  explicit Session(SessionConfig cfg);

  // Setup (before the first packet; not thread-safe, like everything here).
  AudioSendId  AddAudioSend(AudioStreamConfig);
  VideoSendId  AddVideoSend(VideoStreamConfig);
  AudioRecvId  AddAudioReceive(AudioStreamConfig, AudioDecoder* decoder);
  VideoRecvId  AddVideoReceive(VideoStreamConfig);

  // Egress.
  void SendAudio(AudioSendId, const EncodedAudioFrame&, Timestamp now);
  void SendVideo(VideoSendId, const EncodedVideoFrame&, Timestamp now);

  // Ingress: one datagram, RTP or RTCP (RFC 5761 demux by payload-type byte),
  // already decrypted by the host.
  void OnPacket(std::span<const uint8_t> bytes, Timestamp now);

  // Drive: releases paced packets, schedules RTCP, NACK retries, decode times.
  // Cheap to call every tick; NextProcessTime tells the host when it must.
  void Process(Timestamp now);
  Timestamp NextProcessTime(Timestamp now) const;

  // Outputs.
  std::vector<OutgoingPacket>      TakeOutgoing();
  std::vector<ReceivedVideoFrame>  TakeVideoFrames(VideoRecvId, Timestamp now);
  std::vector<SessionEvent>        TakeEvents();

  // Encoder and device control: read-only snapshots.
  SendRates      rates() const;       // Target, per-stream allocation, protection overhead.
  const SessionStats& stats() const;
  void ExportMetrics(MetricsWriter&) const override;
};
```

Choices and why:

- **Pull, not callbacks.** Every existing module returns vectors and takes
  `now`; callbacks would force `std::function` (allocation, hidden re-entrancy)
  and make the order of effects depend on the call stack. Pull keeps the whole
  session deterministic and keeps tests as simple loops.
- **The pacer runs inside `Process`**, which is the only place packets reach
  `TakeOutgoing()` for media. RTCP feedback packets that libwebrtc sends
  outside the pacer (NACK, PLI, transport-cc) are queued directly and appear in
  `TakeOutgoing()` after the next `Process`/`OnPacket`, like libwebrtc's RTCP
  path that bypasses the pacer.
- **Audio decoding stays with the application** through a minimal interface
  (virtual functions only, no RTTI):
  ```cpp
  class AudioDecoder { public: virtual ~AudioDecoder();
    virtual std::optional<std::vector<float>> Decode(std::span<const uint8_t> opus, uint32_t rtp_ts) = 0; };
  ```
  because `timing::AudioJitterBuffer` stores decoded PCM (it conceals with PLC
  on PCM), unlike NetEq which decodes after buffering. Whether to change the
  jitter buffer to hold encoded frames is a separate, larger question; it is
  not needed for this layer. The test bench's decoder is a fake that maps the
  RTP timestamp to the right 20 ms of its speech signal, which is what the
  bench does today.
- **Video frames come out as Annex-B bitstream with a render time**, so a real
  decoder needs nothing else. Frame scheduling (`FrameScheduler`) and the
  render decision stay inside the session because they depend on
  jitter/timing state it owns; the application decodes and renders at
  `render_time`. Freeze detection stays in the test bench (it is a
  measurement, not part of the transport).

### 4.3 Components usable alone

`RtpSender`, `RtpReceiver`, `RtcpEngine`, `SendSideController`,
`VideoPacketizer` / `VideoDepacketizer` and `OpusPayload` are public so that
unit tests exercise each in isolation and an application that already owns an
RTCP stack can adopt only the payload formats or only the sender. `Session` is
the supported integration point; the pieces have the same stability promise as
other modules (no ABI promise, source-compatible within a major version).

## 5. Composition

```
 APP                 rtp::Session                                     existing modules
 ---                 ------------                                     ----------------
 encoder (H.264) -> SendVideo -> VideoPacketizer (FU-A/STAP-A) -> RED/ULPFEC (loss::Fec*, Red*)
 Opus encoder    -> SendAudio -> OpusPayload -> (RED)                       |
                                      RtpSender: header, seq, extensions, MID, tcc seq (sync::ExtensionAccessor)
                                      history: loss::RtxSender                  |
                                      SendSideController: cc::Pacer -> release -> cc::GccController::OnPacketSent
 socket  <- TakeOutgoing <- bytes <---------------------------------------------+
 socket  -> OnPacket ->  demux (RFC 5761) -> RTCP: RtcpEngine::OnRtcp -> GCC, RtxSender, events, RttEstimator, ProtectionController
                                           -> RTP: sync::RtpDemuxer (MID/SSRC/PT) -> RtpReceiver
                                                    audio: RED rx -> AudioDecoder -> timing::AudioJitterBuffer
                                                    video: RTX rx, FEC dec, NackTracker, timing::PacketBuffer, depacketizer,
                                                           KeyframeRequester, VideoJitterEstimator, VideoTiming, FrameScheduler
 decoder <- TakeVideoFrames
 audio device <- jitter_buffer(AudioRecvId).GetAudio(...)   (the device pulls 10 ms, as today)
```

### 5.1 What the session owns and where the app plugs in

| Module | Owner | Notes |
|---|---|---|
| `cc::Pacer`, `cc::GccController`, `cc::OutageMonitor` | Session (`SendSideController`) | One per session: congestion control is per transport, not per stream. |
| `loss::RtxSender` | `RtpSender` per video stream | Existing history and throttling. |
| `loss::FecEncoder`, `ProtectionController` | `RtpSender` / `SendSideController` | Protection bitrate counted inside the target (fixes the #21 shape). |
| `loss::RedEncoder` | `RtpSender` | Audio RED today; video RED when #29 lands. |
| `timing::ReceiveStatistics`, `RttEstimator` | `RtpReceiver` / `RtcpEngine` | SR/RR math. |
| `loss::NackTracker`, `KeyframeRequester`, `timing::PacketBuffer`, `RedReceiver`, `FecDecoder` | `RtpReceiver` | |
| `cc::TransportFeedbackBuilder` | Session | One per transport (transport-wide sequence numbers span audio and video). |
| `sync::RtpDemuxer`, `ExtensionMap` | Session | |
| `timing::AudioJitterBuffer`, `VideoJitterEstimator`, `VideoTiming`, `video::FrameScheduler` | `RtpReceiver` | |
| `cc::FrameDropper`, `video::QualityScaler`, adaptation, `bitrate_allocator` | **App** (encoder side) | libwebrtc keeps these in `VideoStreamEncoder`; the session only publishes the rates (`rates()`), as `BitrateAllocator` publishes to encoders. The test bench's encoder model owns a `FrameDropper`, as `call.cc` does now. |
| Encoder, decoder, audio device, socket, SRTP, SDP, ICE | **App / host stack** | |

### 5.2 Where the app's decisions meet the session

- **Encoder.** Each frame the app reads `rates().video_target` (target minus
  audio, minus protection overhead; the `video_rate()` lambda in `call.cc`
  becomes library code), asks its own `FrameDropper` whether to encode, and on
  `kKeyframeRequested` (or periodic policy) emits an IDR. `kEncoderPause` /
  `kEncoderResume` carry the outage monitor's actions.
- **Decoder.** The app feeds `ReceivedVideoFrame::data` to the decoder at
  `render_time` and reports decode failures with
  `Session::OnDecodeFailure(VideoRecvId, now)`, which calls
  `KeyframeRequester::OnDecoderError` (sends PLI).

## 6. Payload formats (decision requested)

### 6.1 Recommendation

| Media | Format | Notes |
|---|---|---|
| Audio | Opus, RFC 7587 | RTP clock is always 48 kHz regardless of the coded sample rate; marker bit on the first packet after DTX silence; optional RED (RFC 2198) as `loss::RedEncoder` does today. libwebrtc default, and no reason found to differ. |
| Video | **H.264, RFC 6184, packetization-mode 1, Constrained Baseline (profile-level-id 42e01f family)** | Owner decision on #31. Single NAL unit, STAP-A aggregation (typically SPS+PPS ahead of the IDR) and FU-A fragmentation. No STAP-B, MTAP, FU-B (those are mode 2, which libwebrtc does not negotiate). |

Why H.264 first (the question asked on #31): iOS (VideoToolbox) and Android
(MediaCodec) hardware encoders produce H.264 and HEVC universally and VP8/VP9/
AV1 on a minority of devices; libwebrtc's default offers on mobile still lead
with VP8 but every hardware path supports Constrained Baseline H.264; and it is
the format with the fewest moving parts to implement and the best fit for a
packet buffer that finds frames from the stream. AV1 comes later (its RTP
format needs the dependency descriptor, section 6.4); VP8 is cheap to add after
(RFC 7741, a small descriptor) and is the natural second codec because
libwebrtc's software default is VP8.

### 6.2 Interfaces

```cpp
class VideoPacketizer {          // frame -> RTP payloads (RtpPacketizer in libwebrtc)
 public:
  virtual std::vector<PayloadChunk> Packetize(std::span<const uint8_t> annexb, size_t max_payload) = 0;
};
class VideoDepacketizer {        // RTP payload -> bitstream + frame info (VideoRtpDepacketizer)
 public:
  virtual std::optional<ParsedPayload> Parse(std::span<const uint8_t> payload) = 0;
};
```

Library-provided for H.264 (`H264Packetizer`, `H264Depacketizer`). A
`VideoCodec` enum selects it. Applications with a different codec can supply
their own implementation of the interfaces; the session needs nothing else
from a codec (it never decodes).

`H264Packetizer` follows `RtpPacketizerH264` (`modules/rtp_rtcp/source/
rtp_format_h264.cc`): parse Annex-B start codes into NAL units; a NAL that fits
goes as a single NAL unit packet; small NALs ahead of the next are aggregated
into a STAP-A when they fit; a NAL larger than the payload limit is split with
FU-A (start/end bits, the original NAL header's type in the FU header, the
marker bit on the last packet of the access unit). Payload limits come from
`PayloadSizeLimits` (first-packet, last-packet and middle limits are the same
here because there is no frame marking extension and no generic descriptor).

`H264Depacketizer` follows `VideoRtpDepacketizerH264`: reconstruct Annex-B (one
start code per NAL, aggregated and reassembled), set `first_packet_in_frame`,
report the NAL types seen in the packet (SPS, PPS, IDR, slice). It enforces the
RFC rules libwebrtc enforces (STAP-A sizes, FU-A start/end consistency, FU-A
fragments that arrive without a start) and counts malformed packets in metrics.

### 6.3 Test bench encoder model

Follow libwebrtc's `FakeH264Encoder` (`test/fake_encoder.cc`):

- a key frame is `SPS + PPS + IDR slice (nal_unit_type 5)`. Checked against
  the pinned libwebrtc (M157, issue #32): `FakeH264Encoder` does not use
  fixed valid parameter sets. It fills the buffer with a counter pattern
  (`data[i] = i`) and writes only start codes and NAL header bytes: SPS
  (`0x67`) is 8 bytes, PPS (`0x68`) 11 bytes, then the IDR (`0x65`) to the end
  of the frame, on every 10th frame (`kIdrFrequency = 10`); every other frame
  is one non-IDR NAL (`0x41`). There is no licence question. We follow it with
  two deliberate differences: (a) the SPS/PPS/IDR frame is emitted when the
  encoder model makes a key frame, not on a fixed 1-in-10 cycle (libwebrtc's
  fake ignores key frame requests here); (b) the multi-slice split below is
  ours, because libwebrtc's fake emits one NAL per frame, so keep it only if
  needed to exercise STAP-A and FU-A (FU-A already occurs with one large NAL);
- a delta frame is one non-IDR slice (type 1);
- every NAL is padded with filler payload bytes so the NAL sizes add up to the
  modelled frame size (keyframe 4x, delta 0.8x to 1.2x of the per-frame
  budget, exactly as `call.cc` computes now);
- NAL payloads avoid emulation-prevention concerns by using a filler pattern
  with no `00 00 0x` sequences, so what the packetizer splits is exactly what
  the depacketizer joins;
- large frames are one NAL unit per slice, not one huge NAL: the model splits a
  frame's bytes into up to N slices (N small, a fixed config) so that STAP-A,
  single NAL and FU-A all occur in real runs. FU-A is exercised by any NAL
  above the payload limit; STAP-A is exercised by SPS+PPS.

Frame identity moves out of the bitstream. The bench keeps an
`rtp_timestamp -> capture_time` map (RTP timestamps are unique per frame at
every supported fps) instead of the 64-bit frame id it hides in the payload now.

### 6.4 Frame boundaries, keyframes and dependencies (Owner question 3)

What libwebrtc does for H.264 today (checked against the pinned tree, M157,
by the Owner on issue #32):

- **Frame boundary:** the RTP marker bit on the last packet of an access unit;
  the first packet of a frame is recognised from the previous frame's marker
  plus consecutive sequence numbers (and FU-A start / non-FU-A in the
  depacketizer). No frame marking or descriptor extension is required.
- **Keyframe:** the depacketizer parses NAL types; a frame containing an IDR
  slice is a key frame. By default an IDR alone is a key frame:
  `RtpVideoStreamReceiver2` builds `H26xPacketBuffer(!sps_pps_idr_is_h264_keyframe_)`,
  so SPS+PPS+IDR is required only when the `sps-pps-idr-in-keyframe` fmtp is
  negotiated or the `WebRTC-SpsPpsIdrIsH264Keyframe` field trial is on. The
  library matches the default and offers the strict mode as an option.
- **Dependencies:** without a descriptor, a delta frame is treated as
  depending on the immediately preceding frame. The packet buffer releases it
  only when all packets before it (back to the last key frame) have been
  received; otherwise it waits for NACK/RTX or a keyframe.
- **Dependency descriptor / generic descriptor:** libwebrtc can generate a
  generic frame id and dependencies for H.264 in `RtpPayloadParams`, and the
  receiver uses them when the generic descriptor or the AV1 dependency
  descriptor header extension is negotiated. Neither is negotiated by default
  for H.264, and the dependency descriptor proper (RTP extension
  `https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension`)
  is used for AV1 and for VP9/AV1 SVC.

Recommendation: **match that. No dependency descriptor for H.264.** The
library derives, from sequence numbers alone:

- `frame_id` = unwrapped RTP sequence number of the frame's **last** packet;
- a delta frame's single reference = `first_seq - 1` (the last packet of the
  frame before it). If a whole frame was lost, that id never arrives, so the
  frame stays undecodable until the lost packets are recovered (RTX, FEC) or a
  keyframe replaces them. When RTX recovers the missing frame its last
  sequence number is exactly the missing id, so references resolve with no
  extra bookkeeping.
- keyframe = IDR seen (strict option: SPS and PPS seen as well).

This replaces the test bench's `frame_id - 1` reference model with one that
works on bytes. `loss::KeyframeRequester` keeps its interface (`FrameInfo{
frame_id, keyframe, references}`); only the producer of the ids changes. If
the Owner later asks for AV1 or SVC, the dependency descriptor extension is a
new `DependencyDescriptor` parser and a producer of the same `FrameInfo`, with
no change to `KeyframeRequester`. It is not needed for one-to-one calls
without SVC, and not for SFU forwarding (out of scope).

**Consequence for LNTF:** libwebrtc's `LossNotificationController` works on
generic-descriptor frame ids and dependencies (`video_header.generic`); without
a descriptor it gets `nullptr` and has nothing to report. LNTF runs when
`goog-lntf` is negotiated (`config_.rtp.lntf.enabled`), not merely when a
descriptor is present, so the rule is: send LNTF only if it is negotiated
**and** frame information is present. With H.264 and no descriptor libwebrtc
sends PLI (and NACK) only. The session therefore disables LNTF sending by
default (`RtcpConfig::send_loss_notification = false`) and enables it only if
a descriptor extension is configured and LNTF is negotiated. It still parses LNTF from the peer
(informational, never causing a keyframe; #26). This changes test bench
reverse-path bytes (fewer LNTF messages), which is called out in migration
PR 6 and moves no media metric except the reverse-link load.

## 7. Sender internals

### 7.1 Packet flow

1. `SendVideo`: packetize; build `RtpPacket` per chunk (sequence numbers, SSRC,
   payload type, timestamp, marker); add header extensions (transport-wide
   sequence number slot, MID on the first packets until the peer acknowledges
   by sending any RTCP for the MID, abs-capture-time on key frames and
   periodically as `RtpSenderVideo` does, video orientation if configured).
2. Optional RED + ULPFEC (section 7.4). Media packets enter `RtxSender`
   history **after** the packet is final (libwebrtc stores in
   `RtpPacketHistory` the packet as `RtpSenderEgress` is about to send it, so
   an RTX resend is bit-identical apart from the RTX header).
3. Enqueue in `cc::Pacer` with the priority of the existing enum
   (`kAudio`, `kRetransmission`, `kVideo`, `kFec`, `kPadding`).
4. When the pacer releases a packet: assign the transport-wide sequence
   number (the two-byte extension value), set abs-send-time if negotiated,
   serialize to bytes, notify `GccController::OnPacketSent`, count bytes for
   the 500 ms send-rate window the GCC process interval reads, push to the
   outgoing queue.
5. Padding (pacer padding and probes): `RtpPacket` today has no padding field;
   padding is an RFC 3550 padding-only RTP packet. The session sends padding on
   the RTX SSRC with the RTX payload type when RTX is configured (as
   `RTPSender::GeneratePadding` does) and on the media SSRC otherwise. This
   needs `RtpPacket` to carry `padding_size` (P bit and trailing count). See
   section 9.

### 7.2 RTX and RED interaction

`RtxSender` currently wraps a media `RtpPacket`. With RED video (needed by
#29), the packet on the wire is a RED packet whose payload type is the RED
type. libwebrtc retransmits the packet as sent (RED-wrapped) and the receiver's
`RtxReceiveStream` restores it before RED/ULPFEC unwrapping (confirmed in `call/rtx_receive_stream.cc`:
the RTX payload type maps to the associated type, which is RED's when RTX is
negotiated for RED, and the packet is marked recovered). The
session stores the post-RED packet in history and restores into the RED
receive path, so RTX works the same with and without RED. This is a pure
ordering rule inside `RtpSender`/`RtpReceiver`; `RtxSender` is unchanged.

### 7.3 Rate publication (the #21 shape)

`SendSideController` owns one function: given the GCC target, audio wire rate,
FEC protection, and a repair-rate limit, compute `SendRates`
(`video_target`, `audio_rate`, `protection_overhead`, `repair_limit`).
`RtpSender` consults the repair limit before enqueueing RTX or FEC (issue #21
asks for exactly this in the library, so the session is where it lives). The
first migration PR copies today's arithmetic bit for bit; #21 changes it
afterwards.

### 7.4 ULPFEC over RED (issue #29, forward-looking)

The session reserves the shape; it does not implement it here. `loss::FecEncoder`
gains a ULPFEC output mode; `RtpSender` wraps each media packet and each FEC
packet in RED (`red_payload_type`), and the FEC packet is a RED block with the
ULPFEC payload type. On receive, `RtpReceiver` unwraps RED and offers the
blocks to `FecDecoder`. The packetizer, history and pacer see only RED packets
either way. #29 can proceed after migration PR 3 without redesigning anything
here.

## 8. RTCP

### 8.1 Scheduling

Follows `RTCPSender`:

- one compound datagram per report (SR or RR first, then SDES CNAME, then any
  feedback that is due), or reduced-size RTCP (RFC 5506) when
  `RtcpConfig::reduced_size` is set and the peer negotiated it. Default
  compound, because compound is what libwebrtc's default offer produces
  without `rtcp-rsize`;
- regular report interval: **1 s for a session with video, 5 s for audio only**
  (libwebrtc's `kDefaultVideoReportInterval` / `kDefaultAudioReportInterval`),
  with libwebrtc's randomization (each interval uniformly 0.5 to 1.5 times the
  nominal; first report at roughly half) drawn from the session's seeded PRNG
  so runs stay deterministic. Full RFC 3550 section 6.2 interval computation
  (5% of session bandwidth, member counts) is **not** used: with two members
  and libwebrtc's fixed intervals it adds nothing, and libwebrtc does not do it
  either;
- early feedback (NACK, PLI/FIR, transport-cc) is sent immediately subject to
  minimum intervals: PLI/FIR are rate limited by `KeyframeRequester`
  (existing), NACK by the tracker's RTT-based throttling (existing),
  transport-cc by its own cadence (default 50 ms, as `call.cc` uses;
  libwebrtc's `TransportSequenceNumberFeedbackGenerator` defaults to 100 ms,
  adaptive 50 to 250 ms, interval = report size / 5% of the estimate; the
  default stays 50 ms until a later PR measures a change).

**Behaviour-preserving mode.** `call.cc` sends SR at `t % 1000 == 500` and RR
at `t % 1000 == 0`, no jitter. `RtcpConfig` therefore has
`randomize_intervals` (default false in the first PRs, to keep the test bench
numbers; switching to libwebrtc's randomization is a separate behaviour-change
PR with its own baseline update) and explicit phase fields `sr_phase`,
`rr_phase` used by the bench config.

### 8.2 Contents

| RTCP | Source | Exists |
|---|---|---|
| SR (200) with report blocks | `timing::ReceiveStatistics` + sender counters (packets/octets sent) | `SerializeSenderReport` exists in `timing/rtcp_stats.h`; counters are pushed in through `RtcpEngine::SetSenderState` and interop-checked against libwebrtc's `SenderReport` |
| RR (201) | `ReceiveStatistics::MakeReportBlock` | exists |
| SDES CNAME (+ MID when BUNDLE) | session config | exists (`rtp/rtcp_sdes_bye.h`, plan PR 1); CNAME sent by `RtcpEngine` |
| Generic NACK (205/1), PLI (206/1), FIR (206/4), LNTF (206/15) | `NackTracker`, `KeyframeRequester` | exist (#28) |
| transport-cc (205/15) | `TransportFeedbackBuilder` | exists |
| REMB (206/15 "REMB") | optional receiver-side BWE feedback | parse path missing; GCC has `OnRemb`. Send side not planned (libwebrtc prefers transport-cc) |
| BYE | session close | **missing**, low priority |
| XR RRTR/DLRR (RTT for receive-only audio) | RTT when a side only receives | **missing**, needed only for audio-only receivers; defer |

### 8.3 Incoming dispatch

`RtcpEngine::OnRtcp` calls `SplitCompoundRtcp` and dispatches by `RtcpType`:

| Type | Goes to |
|---|---|
| SR | `ReceiveStatistics::OnSenderReport`; also `sync::RtpToNtp` for A/V sync |
| RR | `RttEstimator::OnReportBlock` -> RTT event and `GccController::OnRttUpdate`; `ProtectionController` input; loss fraction |
| transport-cc | `ParseTransportFeedback` -> `GccController::OnTransportFeedback`, `OutageMonitor::OnFeedback` |
| REMB | `GccController::OnRemb` |
| NACK | `RtxSender::OnNack` -> enqueue retransmissions at `kRetransmission` priority |
| PLI, FIR | `kKeyframeRequested` event (deduplicated by FIR `seq_nr`) |
| LNTF | counted; informational, never a keyframe (#26) |
| other | counted in `rtcp.ignored` |

### 8.4 RTT

Computed exactly as today (`RttEstimator` on report blocks: `LSR`, `DLSR`),
published to GCC, `NackTracker`, `KeyframeRequester`, `RtxSender` (resend
throttle) and the protection controller. RTT from XR is the later addition.

## 9. Gaps this design adds work for

Listed so #32's first PRs can be planned. Each is a small module with a unit
test, none changes existing behaviour:

1. `BuildCompoundRtcp` does not enforce that only the last packet may carry
   padding (the point raised by the Reviewer on PR #35); fix it in PR 1.
2. `RtpPacket::padding_size` plus P-bit serialize/parse.
3. SDES (CNAME, MID) build/parse, and BYE.
4. RED header and block API tweaks for video if #29 needs them (decided in
   #29, not here).
5. `H264Packetizer`, `H264Depacketizer`, and the fake encoder in the test
   bench.
6. A small `rtp::RtcpEngine` scheduler and a deterministic interval generator.
7. Frame-id derivation in the packet buffer wrapper (section 6.4); the
   existing `timing::PacketBuffer` already reports `first_seq`/`last_seq`
   (unwrapped), which is all that is needed.

## 10. SSRCs, payload types and header extensions

The session takes all of these as configuration (SDP stays with the host).
The values below are test bench configuration (libwebrtc assigns video payload
types dynamically, `PayloadTypePicker` in `webrtc_video_engine.cc`, so there
is no fixed default to match):

| Stream | SSRC (bench) | Payload type |
|---|---|---|
| Audio (Opus) | 1 | 111 |
| Video (H.264 CB, mode 1) | 2 | 102 |
| Video RTX | 3 | 103 (apt=102) |
| Video RED | shares video SSRC | 116 |
| Video ULPFEC | shares video SSRC (inside RED) | 117 |
| Audio RED (optional) | shares audio SSRC | 63 |

(The bench today uses 111, 96, 97. Payload type numbers do not affect any
metric, only the interop check.)

Header extensions the session writes or reads, each through the existing
`ExtensionMap`/`ExtensionAccessor`:

| Extension | Use | Default |
|---|---|---|
| transport-wide-cc seq (draft-holmer) | send-side BWE, mandatory for GCC | on, all media and RTX/FEC/padding |
| MID (RFC 8843, sdes:mid) | BUNDLE demux | on when MIDs configured |
| abs-capture-time | A/V sync, capture-to-render delay | on for video key frames and periodically |
| ssrc-audio-level (RFC 6464) | audio level for the peer | on for audio |
| urn:3gpp:video-orientation | rotation | config |
| abs-send-time | only for REMB | off |
| RID / repaired-rid | simulcast | not used (one-to-one) |
| dependency descriptor | AV1 / SVC | off, a later codec issue |

BUNDLE demux uses `sync::RtpDemuxer`: MID, then SSRC, then payload type; an
unknown SSRC with a known payload type binds on first packet, like libwebrtc's
demuxer.

## 11. Migration plan

Each PR is behaviour-preserving, ships with unit tests, and keeps `main` green.
The test bench keeps running through its current glue until PR 6.

| PR | Content | Behaviour |
|---|---|---|
| 1 | Prerequisites (section 9 items 1 to 3): padding-only RTP packets, SDES/BYE, compound padding rule, plus the empty `sm::rtp` library target and `SM_WITH_RTP` option. | No change. |
| 2 | `RtcpEngine`: scheduling, building SR/RR/SDES/feedback compounds, incoming dispatch to callbacks-free output structs. Tested on its own against hand-built compounds and `compare/`-style libwebrtc parser fixtures. | No change; not used yet. |
| 3 | `RtpSender` (audio + video, no H.264 yet: payloads are opaque chunks like today) with history, RED, FEC, RTX and `SendSideController`. Unit tests plus an e2e over `sim::NetworkLink`. | No change; used by a new e2e test only. |
| 4 | `RtpReceiver` (RTP parse, demux, RTX/RED/FEC unwrap, NACK, packet buffer, keyframe requests, frame timing). | No change; used by e2e only. |
| 5 | `Session` facade composing 2 to 4; `examples/simulated_call.cc` switches to it. | No change to bench metrics. |
| 6 | **Test bench switch**: `call.cc` replaces its glue with `Session`; every packet crosses the link as bytes; `Fwd`/`Rev` become `{bytes, kind}`; delete the replaced glue. The private video header is still in use here (payload chunks stay opaque), so numbers change only through real RTCP/RTP sizes, and LNTF no longer sent. Baseline updated and explained metric by metric. | Declared change: packet sizes. |
| 7 | **H.264**: `H264Packetizer`/`H264Depacketizer`, the fake encoder, derived frame ids; `WriteVideoHeader`/`ReadVideoHeader` removed. Baseline updated; every change explained. | Declared change: real NAL framing overhead and frame-id derivation. |

Status of PR 5 (as built): `rtp::Session` (`include/sm/rtp/session.h`) composes
`RtcpEngine`, `RtpSender`, `SendSideController` and `RtpReceiver`, with one
transport-cc builder and one `sync::RtpDemuxer` per session. Differences from
the sketch in section 4.2, all because payloads are still opaque until PR 7:
`SendVideo` splits a byte span evenly, `SendVideoChunks` takes payloads the app
framed itself, `VideoStreamConfig` carries a `VideoPayloadParser`, and
`TakeVideoFrames` returns the receiver's frame records without a bitstream.
Audio and video senders each get a pacer slot through one shared
`SendSideController`; padding goes on the video stream's RTX SSRC. The video
payload limit leaves 16 bytes for the header extensions so the whole packet
stays within 1200 bytes.

Reason for ordering 7 after 6: the large refactor is isolated from the payload
change, so a regression in either is attributable. If a PR in this series
cannot be made behaviour-preserving, #32 says to stop and report; this plan
names the two places (6 and 7) where a change is expected and bounded.

After PR 7, #21, #23, #20 and #29 become small PRs on `RtpSender`,
`RtpReceiver` and `SendSideController`.

## 12. Test plan

- **Unit tests per component** (`tests/unit/rtp/`): packetizer/depacketizer
  round trips for every H.264 case (single NAL, STAP-A, FU-A with every split
  point, malformed STAP-A/FU-A); Opus payload marker handling; RTCP engine
  scheduling with a fixed seed (counts and spacing over 60 s), early feedback
  throttles, compound validity; dispatch table (each RTCP type reaches its
  module, nothing else does); padding-only packets; extension writing.
- **Fuzz-style tests** for `OnPacket` with random and truncated datagrams (no
  crash, no unbounded growth, counted as malformed): coverage area 6 of
  program.md 7.
- **End-to-end over `sim::NetworkLink`** (`tests/e2e/rtp_session_e2e_test.cc`):
  two `Session` objects, loss/jitter/outage, checking NACK recovery, PLI-driven
  keyframes, RTX/FEC/RED interplay, RTT agreement, transport-cc driven rate
  changes.
- **Test bench** running through the session (PR 6 and 7), compared against
  `testbench/baseline.json` with `tools/testbench_compare.py` as for any
  change.
- **Interop with libwebrtc's parsers.** The program requires a check "against
  libwebrtc's parser" for every wire format. `compare/webrtc` already links
  the prebuilt libwebrtc: add a `compare` target that feeds the session's
  RTP/RTCP bytes to libwebrtc's `RtpPacketReceived::Parse`, `rtcp::
  CommonHeader` / `rtcp::Nack`, `Pli`, `Fir`, `LossNotification`,
  `TransportFeedback`, `Sdes`, `ReportBlock` parsers and the H.264
  depacketizer, and the reverse direction. Packet-level interop tests are
  cheap and are required for PRs 1, 2 and 7.
- **Stretch: a call between this library and libwebrtc** over the simulated
  link in `compare/` (libwebrtc `Call` and this `Session` exchanging bytes
  through `NetworkLink`). This is the end-state proof of parity and of
  interoperability; it is a separate issue after #32.
- **Platforms.** Builds and tests must stay green on native, ASan/UBSan,
  no-exceptions/RTTI, `embedded` (metrics/events compiled out), wasm and the
  mobile cross-builds; `SM_WITH_RTP=OFF` must still build everything else.
  `rtp` adds no `long double`, no exceptions, no dependencies.

## 13. Open questions for the Owner

1. **Audio decoder in the jitter buffer.** This design keeps
   `AudioJitterBuffer` holding PCM and has the app supply a decoder callback
   interface. Alternative: change the buffer to hold encoded frames and decode
   on playout, as NetEq does. Cleaner for a real Opus integration, larger
   change, touches PLC. Recommendation: do not bundle it with #32; open a
   follow-up issue.
2. **LNTF off for H.264 by default** (section 6.4). If the Owner wants LNTF
   exercised, the generic frame descriptor must come with it.
3. **Is `rtp::Session`'s frame timing (jitter estimator, `VideoTiming`,
   `FrameScheduler`) inside the session** (recommended, as above) or exposed
   as a separate class the app wires? Inside gives the "decodable frames out"
   contract the Owner asked for; separate would let an app bring its own frame
   buffer. Recommendation: inside, with the pieces still usable alone.
4. **FlexFEC and REMB sending** are not planned. Say so if either should be.
5. **First codec after H.264:** recommendation is VP8 (RFC 7741), then AV1
   (needs the dependency descriptor).

## 14. Acceptance (issue #31)

- [x] Design document `docs/design/rtp-session.md`, PR labelled `needs-human`,
  no code, auto-merge **not** enabled; the Owner reviews and merges.
- [x] Payload-format question answered (section 6) with the options and
  trade-offs: standard payloads, H.264 first, libwebrtc's fake-encoder and
  descriptor practice.
