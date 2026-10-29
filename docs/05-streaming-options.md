# Streaming a video file: the options

How to stream an encoded file such as an MP4, and when it has to be decoded
and re-encoded to follow the available bandwidth.

## Overview

Streaming does not require transcoding. Decoding and re-encoding is only
needed when the bitrate must follow the network continuously and there is
no pre-encoded version to switch to. There are four approaches:

| Approach | Who encodes | Latency | Adapts to bandwidth by |
|---|---|---|---|
| 1. Passthrough | Nobody (file as is) | Any | Pacing, buffering, dropping frames |
| 2. Bitrate ladder (HLS/DASH) | Offline, once | Seconds | Client picks a rendition per segment |
| 3. Live re-encode | Sender, per stream | Sub-second | Encoder target bitrate from congestion control |
| 4. Simulcast / SVC | Sender, once for all receivers | Sub-second | SFU forwards one layer per receiver |

```text
1. Passthrough   MP4 ──► demux ──────────────────────────► packetize ──► send
2. Ladder        MP4 ──► decode ──► encode ×N (offline) ──► segments ──► CDN ──► client picks
3. Re-encode     MP4 ──► decode ──► encode ──────────────► packetize ──► send
                                       ▲ target bitrate
                                       └── bandwidth estimate (GCC) ◄── RTCP feedback
4. Simulcast     MP4 ──► decode ──► encode ×N ──► SFU ──► one layer per receiver
```

## 1. Passthrough (remux)

If the file's codec is one the receiver can decode, the compressed frames
are sent as they are.

- **How.** Demux the MP4 and hand each access unit to the RTP packetizer.
  MP4 stores H.264 in AVCC form: each NAL unit has a length prefix, and the
  SPS and PPS sit once in the `avcC` box. RTP packetizers expect Annex-B
  (start codes), so replace each length prefix with `00 00 00 01` and put
  the SPS and PPS in front of every keyframe.
- **Timing.** Send each frame when its presentation time comes up, using
  the MP4 timestamps converted to the 90 kHz RTP clock. Sending faster than
  real time overruns the receiver's jitter buffer.
- **Cost.** Almost no CPU, and no extra quality loss.
- **Limit.** The bitrate is whatever the file was encoded at. When the
  network gets slower, the sender can only pace, buffer or drop frames. A
  frame can only be dropped if no later frame references it. In a typical
  IPPP stream every P-frame is referenced, so after a drop the receiver has
  to wait for the next keyframe. Files encoded with temporal layers or
  non-reference B-frames give more room to drop.

Use it when bandwidth comfortably exceeds the file's bitrate, or for a
quick prototype.

## 2. Pre-encoded bitrate ladder (HLS / DASH)

This is the video-on-demand approach used by Netflix and YouTube.

- **How.** Transcode the file once, offline, into several renditions (for
  example 1080p at 5 Mbps, 720p at 2.5 Mbps, 480p at 1 Mbps). Cut each into
  segments of 2 to 6 seconds, with keyframes aligned across renditions so
  the player can switch at any segment boundary. Publish a manifest
  (`.m3u8` or `.mpd`) and serve everything over HTTP from a CDN.
- **Adaptation.** The client measures throughput and buffer level and asks
  for the right rendition for each segment (throughput-based ABR, BOLA and
  similar). The server only serves files.
- **Cost.** The encoding happens once, ahead of time. Delivering to many
  viewers costs no CPU, only storage and CDN bandwidth.
- **Limit.** Latency is several segments, typically 6 to 30 seconds. LL-HLS
  and LL-DASH bring it down to 2 to 5 seconds with partial segments.

Use it for video on demand and for large-audience broadcasts where a few
seconds of delay are acceptable.

## 3. Live decode and re-encode

This is the WebRTC approach for one sender and one receiver.

- **How.** Decode the file to raw frames and encode them on the fly. The
  encoder's target bitrate comes from the send-side bandwidth estimate,
  which follows the receiver's RTCP feedback (transport-wide congestion
  control).
- **Adaptation.** The encoder changes bitrate frame by frame. When the
  bitrate gets too low for the current resolution, the sender scales the
  resolution or frame rate down, and back up when there is room again. A
  frame dropper skips frames when the encoder overshoots its target.
- **Cost.** A full decode and encode per outgoing stream, plus one extra
  generation of quality loss. Encoding separately for each viewer does not
  scale beyond a few receivers.

Use it when latency must stay under a second and the bitrate has to track
the network, with one or a few receivers.

## 4. Simulcast and SVC

This is how real-time video scales to many receivers without the server
transcoding.

- **Simulcast.** The **sender** encodes the same raw frames several times
  at different resolutions and bitrates (for example 720p, 360p and 180p),
  each as its own RTP stream with its own SSRC. It uploads all of them to an
  SFU (selective forwarding unit). That costs the sender about 1.3 to 1.5
  times the CPU and uplink of a single stream.
- **SFU.** The server never decodes or encodes. For each receiver it picks
  one stream to match that receiver's bandwidth estimate and forwards its
  packets, rewriting SSRC, sequence numbers and timestamps so the receiver
  sees one continuous stream. It switches streams only at a keyframe, and
  asks the sender for one (PLI or FIR) when it needs to switch.
- **SVC.** The sender produces one bitstream with dependent layers:
  temporal layers (frame rate) and, with VP9 or AV1, spatial layers
  (resolution). The SFU drops the packets of the higher layers instead of
  choosing between separate streams.
- **Receiver.** Each receiver decodes a single stream.

For a file, "the sender" is whatever reads it. A server-side ingest
process decodes the file once and encodes the layers once, shared by every
viewer. Live platforms such as Twitch do the same: the streamer uploads one
high-quality stream, and the platform transcodes it once at ingest into a
ladder for all viewers.

Use it for real-time calls and conferences with many receivers on
different networks.

## Choosing

| Situation | Approach |
|---|---|
| Video on demand, seconds of delay acceptable | 2. Bitrate ladder |
| Real time, one receiver, network varies | 3. Live re-encode |
| Real time, many receivers | 4. Simulcast or SVC, through an SFU |
| Codec supported and bandwidth plentiful | 1. Passthrough |

## In this library

The library covers the real-time path (approaches 1, 3 and parts of 4). It
has no container demuxer and no codec, so the MP4 parsing, decoding and
encoding come from elsewhere (FFmpeg, a platform codec, OpenH264).

- **Passthrough.** The H.264 packetizer
  ([9.5](algorithms/9-rtp/9.5-h264.md)) takes an access unit as an Annex-B
  byte stream, so AVCC samples from an MP4 need the start-code conversion
  above. [`rtp::Session`](design/rtp-session.md) sends it.
- **Re-encode.** The bandwidth estimate
  ([5.2](algorithms/5-cc/5.2-delay-based-bwe.md),
  [5.3](algorithms/5-cc/5.3-loss-based-bwe.md),
  [5.4](algorithms/5-cc/5.4-gcc-controller.md)) sets the target bitrate.
  The pacer ([5.5](algorithms/5-cc/5.5-pacer.md)), frame dropper
  ([5.8](algorithms/5-cc/5.8-frame-dropper.md)), resolution and frame-rate
  adaptation ([5.9](algorithms/5-cc/5.9-video-adaptation.md),
  [7.8](algorithms/7-video/7.8-quality-scaler.md)) and frame scaler
  ([7.6](algorithms/7-video/7.6-frame-scaler.md)) work around the encoder.
- **Layers.** The library sends one layer, as single-layer H.264 in
  libwebrtc M157 (temporal layers were removed in #572). The bitrate
  allocator ([5.7](algorithms/5-cc/5.7-bitrate-allocator.md)) splits the
  estimate across streams. There is no SFU in the library.

See [Call data flow](04-webrtc-call-data-flow.md) for how these pieces fit
into a call.
