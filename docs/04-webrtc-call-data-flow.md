# WebRTC one-on-one call data flow

How audio and video move through a one-on-one WebRTC call, from setup to
playout, and the feedback loop that keeps it real-time.

## Overview

A call has two phases: a one-time setup that connects and secures the peers,
then a continuous media loop in each direction, steered by feedback from the
receiver.

```text
Setup, once per call
  Signaling ──────────► ICE ──────────────────► DTLS handshake
  (SDP offer/answer)    (STUN/TURN path checks)  (derives the SRTP keys)
                                                        │ SRTP keys
Sender (peer A)                                         ▼
  Capture ──► Process ──► Encode ──► Packetize ──► Send
  mic,        AEC, NS,    Opus,      RTP, FEC,     pacer, SRTP
  camera      AGC         VP8, AV1   RTX            │
                            ▲                       │ SRTP over UDP
                            │ target bitrate        │
                  Bandwidth estimate (GCC)          │
                            ▲                       │
                            │ RTCP: TWCC, NACK, PLI │
                            │                       ▼
  Play out ◄── Decode ◄── Jitter buffer ◄── Recover ◄── Receive
  screen,      Opus,      NetEQ,            NACK, RTX,  SRTP,
  speaker      video      frames            PLI         demux
Receiver (peer B)
```

One direction is shown; the same pipeline runs from B to A. The receiver's
RTCP feedback drives the sender's bandwidth estimate, which sets the encoder's
target bitrate. That loop is what keeps the call real-time as the network
changes.

## 1. Setup

Before any media flows, the peers agree on what to send, find a network path,
and derive encryption keys.

1. **Signaling.** The app carries an SDP offer and answer over a channel it
   chooses (WebSocket, HTTP). They fix codecs (Opus, VP8/VP9/H.264/AV1),
   payload types, SSRCs and MIDs, RTP header extensions, feedback types
   (`nack`, `nack pli`, `transport-cc`), ICE credentials and the DTLS
   fingerprint.
2. **ICE.** Each peer gathers host, server-reflexive (STUN) and relay (TURN)
   candidates and trickles them over signaling. STUN connectivity checks run on
   candidate pairs, and one pair is nominated.
3. **DTLS-SRTP.** A DTLS handshake runs over the nominated pair and is checked
   against the SDP fingerprints. The SRTP keys are derived from it.
4. **BUNDLE + rtcp-mux.** Audio, video, RTP and RTCP share one UDP 5-tuple.
   The receiver demultiplexes by first byte: STUN 0–3, DTLS 20–63, RTP/RTCP
   128–191.

## 2. Send side

Each captured frame is cleaned up, encoded to the current target bitrate,
split into RTP packets, paced and encrypted.

1. **Audio processing.** 10 ms capture frames pass through the audio
   processing module: echo cancellation (using far-end playout as reference),
   noise suppression and gain control.
2. **Audio encode.** Opus encodes 20 ms frames, often with in-band FEC and DTX,
   which sends nothing during silence.
3. **Video encode.** Frames are scaled to fit the bitrate and CPU budget, then
   encoded. Keyframes go out at the start and on receiver request; other
   frames are deltas.
4. **RTP packetization.** The codec's payload format splits frames (VP8
   payload descriptor, H.264 FU-A). Each packet carries:
   - an SSRC identifying the stream
   - a sequence number
   - a timestamp (48 kHz clock for audio, 90 kHz for video)
   - header extensions: transport-wide sequence number, abs-send-time, audio
     level, MID
5. **Resilience.** Sent video packets stay in a history buffer for RTX
   retransmission on a separate SSRC. ULPFEC or FlexFEC may add redundancy.
6. **Pacer.** Video bursts are spread out at about 2.5× the target rate so a
   keyframe does not flood the bottleneck queue. Audio jumps ahead of video.
7. **SRTP.** The payload is encrypted, an auth tag appended, and the packet
   sent over UDP.

## 3. Receive side

The receiver decrypts, routes each packet by SSRC or MID, and smooths network
jitter before playout.

- **Demux and decrypt.** STUN, DTLS and RTP are split by first byte. SRTP is
  decrypted and authenticated, then packets are routed to the audio or video
  stream.
- **Audio (NetEQ).** The jitter buffer depth adapts to measured jitter. Packet
  loss concealment fills missing packets. Speech is time-stretched slightly
  (accelerate or expand) so playout stays smooth. Decoded audio is mixed,
  played out, and fed back as the echo canceller's reference.
- **Video.**
  - Packets are reassembled into frames, and each frame is checked for
    decodable dependencies.
  - A gap in sequence numbers triggers a **NACK**; the sender answers with RTX.
  - If recovery fails or the decoder breaks, the receiver sends **PLI** or
    **FIR** to request a keyframe.
  - The frame buffer schedules each frame for render from jitter and
    decode-time estimates.
- **Lip sync.** RTCP Sender Reports map each stream's RTP timestamps to a
  shared NTP clock. The receiver delays whichever of audio or video is ahead.

## 4. Feedback loop

Receiver feedback drives a sender-side bandwidth estimate, and that estimate
sets the encoder, pacer and FEC rates.

- **Transport-wide congestion control (TWCC).** The receiver reports
  per-packet arrival times about every 50–100 ms. The sender's GCC estimator
  compares them with send times: rising queueing delay lowers the bitrate,
  stable delay lets it probe upward. Loss feeds a separate loss-based limit.
- **RTCP reports.** Receiver Reports carry loss fraction, jitter and the fields
  used for RTT. Sender Reports support lip sync and RTT. REMB is the older
  scheme in which the receiver estimates bandwidth itself.
- **Adaptation.** As the target bitrate changes, the video encoder gives up
  quantization quality first, then resolution, then frame rate. Audio is
  protected first.

## 5. Typical numbers

| Measure | Typical value |
|---|---|
| End-to-end latency | ~100–300 ms |
| Audio frame | 20 ms |
| Jitter buffer | 20–200 ms |
| Audio bitrate (Opus) | ~32 kbps |
| Video bitrate | ~300 kbps to 2.5+ Mbps, set by the bandwidth estimate |
| TWCC feedback interval | ~50–100 ms |

These are approximate figures from common deployments, not measurements.
