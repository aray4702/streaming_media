# Problems WebRTC solves

What WebRTC (in practice, the libwebrtc stack) handles, grouped by area. Each
row gives the problem and the mechanism that solves it. The tables double as a
feature checklist for this library: when building on a lightweight stack such
as libdatachannel, sections 3–8 are mostly ours to implement or borrow.

## 1. Connectivity and session setup

| Problem | WebRTC mechanism |
|---|---|
| Peers behind NAT/firewalls | **ICE** (connectivity checks), **STUN** to discover the public address |
| Symmetric NAT or blocked UDP | **TURN** relay, with fallback over TCP or TLS on port 443 |
| Network change (Wi-Fi ↔ LTE, IP change) | **ICE restart**, continual gathering, candidate pair switching |
| Picking the best path | ICE candidate pair priority, nomination, RTT measurement |
| Keeping NAT bindings alive | STUN consent freshness checks and keepalives |
| Detecting a dead peer or link | ICE consent timeout, then a connection state change to `disconnected` or `failed` |
| Agreeing on codecs, formats and features | **SDP offer/answer** negotiation and RTP header extension negotiation |
| Too many ports to open | **BUNDLE** (all media on one 5-tuple) and **rtcp-mux** |

## 2. Security and privacy

| Problem | Mechanism |
|---|---|
| Eavesdropping on media | **SRTP** encryption, always on |
| Key exchange without a PKI | **DTLS-SRTP**, with the certificate fingerprint carried in SDP |
| Tampering and replay | SRTP authentication tag and replay window |
| Untrusted SFU in group calls | **Insertable Streams / SFrame** for end-to-end encryption |
| Sending traffic to a host that didn't consent | ICE consent freshness |
| IP address leakage | mDNS host candidates (browsers) |

## 3. Packet loss

| Problem | Mechanism |
|---|---|
| Lost video packets | **NACK + RTX**: selective retransmission on a separate SSRC |
| Loss where a retransmit would arrive too late (high RTT) | **FEC**: ULPFEC, FlexFEC; **RED** redundancy for audio |
| Lost audio frames | **Opus in-band FEC** (LBRR), Opus **DRED** (deep redundancy), **PLC** in NetEq |
| Lost or undecodable video frame | **PLI / FIR** keyframe request; **LNTF** (loss notification) |
| A single loss breaking every later frame | **Temporal layers / SVC**: dependent frames can be dropped safely |
| Picking a protection level | Loss-rate–driven trade-off between FEC and NACK, based on RTT |
| Recovering faster after a keyframe loss | Keyframe pacing, intra refresh, LTR (long-term reference) frames in some codecs |

## 4. Jitter, reordering and timing

| Problem | Mechanism |
|---|---|
| Variable packet arrival (audio) | **NetEq** adaptive jitter buffer with time-stretching: accelerate, preemptive expand |
| Variable packet arrival (video) | Video jitter buffer, frame buffer and render-time estimation |
| Out-of-order packets | Sequence-number reordering in the packet buffer |
| Duplicate packets | Dedup by sequence number |
| Clock drift between sender and receiver | NetEq stretching, plus sample-rate drift compensation in the audio device module |
| Measuring network delay | RTCP SR/RR (RTT, jitter), transport-wide feedback, abs-send-time |

## 5. Bandwidth, congestion and unstable links

| Problem | Mechanism |
|---|---|
| Unknown or changing available bandwidth | **GCC** (Google Congestion Control): delay-based and loss-based estimation |
| Fine-grained per-packet feedback | **transport-cc** (Transport-Wide CC) RTCP feedback |
| Receiver-side bandwidth hints | **REMB** (legacy) |
| Bursts filling router queues (bufferbloat) | **Pacer**: smooths packet sends, with priority queues |
| Probing for more bandwidth safely | Bandwidth **probing** (padding / RTX probe clusters) |
| Encoder bitrate overshoot | Encoder rate control, frame dropper |
| Low bandwidth | **Adaptive resolution / framerate degradation** (`degradationPreference`: maintain-framerate, maintain-resolution, balanced) |
| CPU too slow to encode | **CPU overuse detector**, which lowers resolution or fps |
| Heterogeneous receivers in group calls | **Simulcast** and **SVC**; the SFU forwards a matching layer per receiver |
| Sharing bandwidth among streams | Bitrate allocator, with per-stream priority and min/max bitrate |
| Link outage, then recovery | Encoder pause, then keyframe on recovery; ICE restart if needed |
| Protecting audio over video | Audio prioritized in the pacer; video degrades first |

## 6. Audio quality (Audio Processing Module, APM)

| Problem | Mechanism |
|---|---|
| **Echo** (far-end audio coming back through the mic) | **AEC3** acoustic echo canceller; AECM for mobile |
| Delay between the speaker and mic paths | Echo-path delay estimation in AEC3 |
| **Background noise** | **NS** noise suppression (RNNoise-style ML in some builds) |
| Volume too quiet, too loud, or clipping | **AGC2** automatic gain control, limiter, clipping predictor |
| Low-frequency rumble or DC offset | High-pass filter |
| Keyboard typing noise | Transient suppressor |
| **Silence** wasting bandwidth | **VAD** + **DTX** (discontinuous transmission) |
| Silence sounding "dead" at the receiver | **CNG** (comfort noise generation) |
| Detecting who's talking | Audio level header extension (RFC 6464), voice activity |
| Mixing several remote speakers | Audio mixer that picks the loudest N speakers |
| Device sample-rate mismatch | Resampler |
| Audio and mic hardware handling | Audio Device Module (platform I/O, mute, device switching) |

## 7. Video quality and processing

| Problem | Mechanism |
|---|---|
| **Lost frame** (decode impossible) | Keyframe request (PLI/FIR); freeze the last frame until recovery |
| Decoder errors or corrupt bitstream | Error resilience, decoder fallback (hardware → software) |
| Hardware encoder failure | Automatic fallback to a software encoder |
| Rotated camera | Video orientation header extension (CVO) |
| Screen sharing (text must stay sharp) | **Content hint** ("detail" or "text"), screen-content coding tools, variable framerate |
| Talking head vs. motion | Content-type–aware rate control |
| Many codecs and devices | Codec negotiation (VP8, VP9, H.264, AV1, H.265 recently) |
| Frame size and aspect changes | Resolution adaptation and scaling mid-stream |
| Frames arriving too late to render | Render timing and late-frame dropping |
| Region-of-interest quality | Quality scaler and per-codec QP control |

## 8. Synchronization

| Problem | Mechanism |
|---|---|
| **Lip-sync (A/V sync)** | RTCP **Sender Reports** map the RTP timestamp to NTP wallclock; the receiver aligns playout delay between streams |
| Synchronizing multiple streams from one sender | Shared **CNAME**, MediaStream grouping |
| Capture-to-render latency measurement | Absolute capture time extension, timing extensions |
| Identifying streams | SSRC, MID and RID header extensions |

## 9. Data and auxiliary channels

| Problem | Mechanism |
|---|---|
| Sending arbitrary data peer-to-peer | **DataChannel** (SCTP over DTLS) |
| Reliable vs. low-latency data | Ordered/unordered, reliable or partially reliable (maxRetransmits, maxPacketLifeTime) |
| Head-of-line blocking between messages | Multiple SCTP streams |
| Sending large messages | SCTP fragmentation and reassembly, plus a buffered-amount threshold |
| Sending DTMF tones | RFC 4733 telephone-event |

## 10. Observability and control

| Problem | Mechanism |
|---|---|
| Diagnosing call quality | **getStats()**: loss, jitter, RTT, bitrate, freezes, concealment, FPS, QP, and more |
| Detecting a bad network for the UI | Connection state, ICE state, quality limitation reason (bandwidth, CPU) |
| Replaying and debugging problems | RTC event log, AEC dumps |
| Changing settings mid-call without renegotiating | `setParameters` (bitrate, scale, active layers), track replacement |

## What WebRTC does not solve

These must be provided by the application:

- **Signaling**: how peers exchange SDP and ICE candidates (WebSocket, MQTT,
  SIP and so on).
- **Group-call topology**: SFU/MCU servers (WebRTC only defines
  peer-to-peer).
- **Authentication and authorization** of users, plus TURN credentials.
- **Recording, transcoding and broadcasting** to large audiences.
- **Very constrained MCUs**: the full feature set above needs embedded
  Linux-class hardware. Small MCU stacks such as libpeer or KVS implement only
  a subset, often without AEC3, NetEq-class jitter buffering, GCC or FEC.
