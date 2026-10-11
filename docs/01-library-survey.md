# Existing libraries

A survey of libraries for real-time audio/video streaming on embedded systems
and in internet video conferencing over unstable links.

Most real systems should not write the whole stack from scratch. The usual
approach puts a codec, a transport that recovers from packet loss, and a
signaling layer on top of one of the libraries below.

## 1. Full WebRTC stacks

WebRTC is required to talk to browsers for video conferencing. Its tools for
bad links are already well tested: NACK/RTX retransmission, FlexFEC/ULPFEC
forward error correction, congestion control (GCC and transport-cc), and
simulcast/SVC.

| Library | Language | Notes |
|---|---|---|
| **libwebrtc** (Google) | C++ | The reference stack used in Chrome. Most complete: AEC3 echo cancellation, jitter buffer, bandwidth estimation. Large and hard to build, and too heavy for small MCUs. |
| **libdatachannel** | C++17 | Small and easy to embed. Handles ICE, DTLS-SRTP, and RTP/RTCP packetizers with NACK. You add your own codecs and jitter buffer. Good for embedded Linux. |
| **Pion** | Go | Modular and easy to read. Good for gateways, SFUs and tools. Runs on embedded Linux if Go is acceptable there. |
| **webrtc-rs** | Rust | A port of Pion. |
| **Amazon KVS WebRTC C SDK** | C | Built for cameras and IoT. Portable C with a small footprint. Can be used without AWS signaling, with some work. |
| **libpeer** (sepfy) | C | Very small WebRTC for MCUs (ESP32, Pico W). |
| **Espressif esp-webrtc-solution** | C | WebRTC for ESP32-S3/P4 with hardware codec integration. |
| **GStreamer `webrtcbin`** | C | Use this if you are already in a GStreamer pipeline. Good with hardware encoders. |
| **aiortc** | Python | For prototyping and testing, not embedded use. |

**Servers/SFUs** (needed for group calls): Janus (C, light), mediasoup,
LiveKit (Go, built on Pion), Jitsi Videobridge.

## 2. SIP/RTP stacks

These are more traditional and embedded-friendly.

- **PJSIP / PJMEDIA** (C): Mature and very portable, including RTOS targets.
  Has its own jitter buffer, packet-loss concealment (PLC), echo cancellation
  and SRTP. Widely used in embedded VoIP and video phones.
- **baresip + libre/librem** (C): Small and modular, and supports WebRTC as
  well.
- **oRTP**, **uvgRTP**, **JRTPLIB**, **live555**: Bare RTP/RTSP libraries for
  building the rest yourself.

## 3. Transports built for unstable links

These are not browser-native.

- **SRT** (libsrt): Retransmission (ARQ) with a latency budget, plus
  encryption. Widely used for contribution over the public internet.
- **RIST** (librist): Similar goal to SRT, standardized for broadcast.
- **Media over QUIC (MoQ)**: Newer IETF work (moq-rs, moq-js). Lets the sender
  drop old or low-priority data instead of letting it block newer data.
- **KCP**: A UDP protocol tuned for low latency at the cost of extra
  bandwidth. Popular in games and IoT.
- **FEC libraries**: Wirehair, RaptorQ implementations, OpenFEC, for a custom
  FEC layer.

## 4. Codecs

Codec choice matters a lot for loss resilience.

- **Opus**: Built-in FEC, PLC, and DTX (stops sending during silence).
  Opus 1.5 adds deep-learning PLC and DRED, which carries redundant audio
  covering up to about 1 s. The default choice for audio.
- **Video**: libvpx (VP8/VP9, with temporal layers), OpenH264 / x264,
  SVT-AV1 / libaom (AV1 with SVC). On embedded hardware, use the SoC's encoder
  through V4L2 M2M, Rockchip MPP or Jetson NVENC.
- **Audio processing**: The WebRTC APM module (AEC3, noise suppression, AGC)
  can be pulled out on its own. SpeexDSP is lighter.

## 5. General media frameworks

- **GStreamer**: Best for embedded Linux pipelines with hardware acceleration.
- **FFmpeg**: Codecs and muxing; weak at real-time communication.

## Recommendation

- **Browser interop plus embedded targets:** use libdatachannel (embedded
  Linux) or libpeer / KVS C SDK (MCUs) on the devices, Opus for audio, the
  hardware video encoder, and an SFU such as LiveKit or Janus for group calls.
- **Device-to-device or device-to-server only, over very lossy links:**
  consider SRT or a QUIC-based transport, plus a custom FEC layer and an
  adaptive bitrate controller.

Whichever is chosen, these are the parts that decide how well the system
copes with a bad link:

- an adaptive jitter buffer
- retransmission (NACK) combined with FEC
- bandwidth estimation that drives the encoder bitrate
- keyframe-on-demand (PLI/FIR) or SVC, so one lost frame doesn't freeze the
  video
- Opus FEC/PLC for audio
