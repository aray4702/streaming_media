# A real group call (C21, #1180)

Six participants in one room on the LiveKit SFU deployed on the Lightsail host (Oregon; LiveKit v1.13.11,
[docs/deploy/livekit.md](../deploy/livekit.md)), measured at the callers by `tools/real_group_call.py`. `<host>` is
the server's name. It is left out here, as in [real-network-calls.md](real-network-calls.md).

## How to rerun

```sh
cmake --preset native && cmake --build --preset native -j10   # sm_call_smoke and sm_ai_server
export SM_SIGNALING_URL=https://<host>:8443                     # LiveKit at wss://<host>:8443/rtc, GET /lk-token
tools/real_group_call.py --seconds=60 --out=group-run --doc=docs/results/real-group-call.md
tools/real_group_call.py --summary=group-run/run.json            # the tables again, from the raw samples
python3 tools/test_real_group_call.py                            # the runner's summaries, offline
```

The tool exits with 77 when `SM_SIGNALING_URL` is unset or its `/lk-token` does not answer.

## The call

| participant | client | sends | receives |
|---|---|---|---|
| page 1-4 | headless Chrome with the web app's group mode (`apps/web/index.html?group=1`, the vendored livekit-client SDK) | Chrome's fake camera, H.264 with simulcast (the SDK's q/h/f layers; adaptive stream and dynacast on). The microphone is a -60 dBFS noise file (`dtx=0`): heard, never the active speaker | every other participant's audio and video, one grid tile each (Chrome window 1280x800, 3 columns for 6) |
| native | `sm_call_smoke --livekit --video` (the iOS app's C++ controller, on macOS) | a synthetic 640x360 30 fps camera (one layer, VideoToolbox H.264) and 1.5 s turns of speech-like audio. It times the AI's replies | every participant, asking for 640x360 video (UpdateTrackSettings) |
| AI | `sm_ai_server --livekit` | its replies (Opus) | every participant's audio. It listens to the active speaker. It also receives video at the server's default layer, though it shows none |

- **Where.** All six clients ran on one MacBook (Apple M4, 10 cores) on a home Wi-Fi connection in California. The
  pages are served from 127.0.0.1 with this checkout's `index.html`. Their `/lk-token` is fetched from the host
  through the tool. LiveKit's signaling and media go to the host directly.
- **Join order.** The four pages join 1.5 s apart, then the AI, then the native client. The window starts once every
  page counts all six in the room and lasts `--seconds`.
- **Page metrics** come from each page's `getStats()`, once a second (the group report, `report=1`). Per remote
  participant they are the received video's size (`frameWidth`/`frameHeight`: the simulcast layer the SFU forwards),
  decoded frames per second, `freezeCount` and `totalFreezesDuration`, video packets lost, audio packets lost and
  received, and the jitter buffer's mean delay (`jitterBufferDelay / jitterBufferEmittedCount`, video and audio).
  The subscriber connection's RTT is the transport's selected pair, `currentRoundTripTime`. Rates, frames, freezes
  and losses are differences over the window. Jitter buffer delays are lifetime means at the window's end.
- **Uplink per sender** comes from each page's `outbound-rtp`, one row per RID: bytes sent (as kbps over the
  window), size, fps, `active` (the share of samples the encoding was active) and `qualityLimitationReason`.
  The native client and the AI report their publisher connection's send rate, congestion controller target, RTT and
  LiveKit signaling RTT (`CallStats`).
- **Native and AI receive metrics** come from their `livekit_group` stats lines (per participant: decoded video fps
  and size, audio packets lost and received). They have no per-participant freeze or jitter buffer figures.
- **Active speakers**: the share of a receiver's samples in which each identity was among LiveKit's active speakers.
  For the native client, the loudest one.
- **Conversation**: the native client's turns and the AI's replies, measured as in
  [real-network-calls.md](real-network-calls.md). The response time runs from the end of the native client's speech
  to the AI's first audio played there.
- **Path probes.** To tell a congested path from a loaded server or client, the tool probes once a second during the
  call:
  - a GET of `/lk-token` on a kept-alive HTTPS connection, which reaches nginx and the signaling service but not
    LiveKit;
  - a TCP handshake to the host's port, which only the host's kernel answers;
  - a TCP handshake to 1.1.1.1:443, a host elsewhere on the internet reached through the same home link;
  - the Mac's 1-minute load average.

## Summary of the runs (2026-11-04, PT)

Three full calls of 60 s with six participants, plus a 6-way shakedown and two smaller calls for comparison. The
full tables of the three full calls follow, newest first. The figures here are taken from them, or from the other
runs' `run.json` with the same tool.

| run (PT) | participants | window | audio lost at the pages (all senders) | video freezes per received stream | page video fps received | native publisher RTT, median | TCP handshake to 1.1.1.1, median / max |
|---|---|---|---|---|---|---|---|
| 23:01 | 6 | 60 s | 18.8% | 20.6 | 10.7-24.6 | 422 ms | 124 / 4733 ms |
| 22:53 | 6 | 60 s | 10.2% | 24.2 | 9.1-25.2 | 666 ms | 506 / 2381 ms |
| 22:51 | 6 | 60 s | 10.3% | 25.5 | 12.4-26.7 | 566 ms | not probed |
| 22:45 (shakedown) | 6 | 34 s | 8.2% | 7.1 | 7.8-15.3 | 1611 ms | not probed |
| 22:49 | 4 (2 pages, native, AI) | 30 s | 1.7% | 6.8 | 11.8-27.7 | 207 ms | not probed |
| 22:56 (Chrome window 800x600) | 4 (2 pages, native, AI) | 30 s | 0.6% | 9.5 | 18.8-28.2 | 75 ms | 36 / 275 ms |

With the link idle, the same probes took 11-16 ms to 1.1.1.1, 32-51 ms to the host's port and 34-102 ms for
`/lk-token`.

## Findings

- **The room works at its cap.** In every run all six participants joined the one room (6 of 6), and every page
  received every other participant's audio and the video of all five video senders. The tool's check passed every
  time. The native client and the AI received all five others as well. Every receiver saw both the native client
  and the AI as active speakers. The pages' quiet microphones were never active speakers, as intended. In each full
  run the AI heard 8-10 of the native client's turns and answered every one it heard. At the AI, its turn end to its
  first reply audio sent took 640-670 ms every time.
- **The quality numbers measure this Mac's home link, not the SFU.** All six clients shared one home Wi-Fi
  connection. During the 6-way calls the queueing on that link reached seconds:
  - a TCP handshake to 1.1.1.1, a host that has nothing to do with the call, took 124-506 ms (median) and up to
    4.7 s, against 11-16 ms with the link idle;
  - handshakes to the host's port rose by about as much;
  - outside any call, one TCP download at about 4.9 Mbps alone pushed the 1.1.1.1 handshake to 700-830 ms
    (macOS `networkQuality` measured 14.5 Mbps down and 27 Mbps up, at 40 round trips per minute under load);
  - the Mac itself was not the limit: its 1-minute load average peaked at 3.6-8.1 (10 cores) in the full runs.

  The four pages alone received 1.6-2.1 Mbps, and the native client and the AI received the same streams again. At
  that load the queue built up, and so did everything that follows from it:
  - publisher RTTs of 0.4-0.7 s median and up to 6 s;
  - 10-19% of the pages' received audio packets lost;
  - about 20-25 video freezes per received stream per minute;
  - audio jitter buffers near 1 s;
  - in the 22:53 run, one signal reconnect and ICE restart on every page, from which they recovered.

  The 4-party calls lost 0.6-1.7% and froze 7-10 times per stream in 30 s. To measure the SFU itself, the clients
  must be spread over better links, for example a cloud VM in the host's region.
- **Received layers.** Every receiver got the pages' video at 320x180 (layer q) nearly all the time. The native
  client's single 640x360 layer went to everyone. The native client (asking for 640x360) and the AI got a page's
  640x360 layer (h) only in the few samples when that page was sending h.
- **Uplink and paused layers.**
  - **Chrome pages.** Each page's camera has three encodings, q 320x180, h 640x360 and f 1280x720 (maxBitrate
    160, 450 and 1700 kbps). The pages sent:
    - q at 25-85 kbps;
    - h on some pages (6-174 kbps) and at 0 kbps on the others;
    - f at 0 kbps (rounded) on every page in every run.

    `qualityLimitationReason` was `bandwidth` in every sample. **No layer was paused by dynacast:** in every report
    of every run, including a 4-party run with 800x600 windows (smaller tiles), every encoding's `active` stayed
    true, both in `outbound-rtp` and in the sender's parameters. So the uplink the pages saved on h and f came from
    Chrome's own bitrate allocation, which gives the upper layers nothing under a low estimate, and not from the SFU
    pausing unneeded layers. A likely reason f is never paused (not measured here): the AI subscribes to video with no
    size (`livekit_receive_width = 0`, the server's default layer), so the server counts the top layer as wanted.
    Subscribing the AI to audio only, or to the lowest layer, would remove that and cut its downlink. In these runs it
    decoded about 10-26 video frames per second of each sender that it never shows.
  - **Native client.** The native client publishes one layer (`call_client.cc`: "One layer until C17"). So the
    library's fix for paused layers (#1173, fixed in #1178: the library stops encoding and sending the simulcast
    layers the SFU pauses) is not exercised by this call. It needs a native simulcast publisher in a room where a
    layer goes unwatched. Its publisher sent 141-142 kbps (audio and video, as `send_kbps` on the pair) while its
    congestion controller's target averaged 111-121 kbps and fell to 89 kbps. Its sending stayed above the target
    for most of the call, which is worth a look on a link that is less congested.
- **Conversation over the congested link.** At the native client, the response to its turns (end of its speech to
  the AI's first audio played) had a median of 3.0-4.9 s in the 6-way calls, against 1.7 s in the 4-party call
  with the calmer link. The AI's own part stayed 640-670 ms, so the rest is the link's queueing and the jitter
  buffers growing to absorb it.

<!-- runs: tools/real_group_call.py inserts each run below, newest first -->

## Run 2026-11-04 23:01 PST

- Room `grp-150b8a66` on `<host>` (LiveKit v1.13.11), 6 of 6 participants in the room at once: 4 headless Chrome pages (web app group mode, Chrome 156.0.8078.80), `sm_call_smoke --livekit --video` (native, talking in turns), `sm_ai_server --livekit` (the AI).
- Window: 60 s, from the moment every page counted all the participants (9.1 s after the first page started).
- The path to the host without LiveKit, once a second: GET /lk-token on a kept-alive HTTPS connection took 197 ms (median), 3705 ms at most; a TCP handshake to the host's port 131 ms (median), 2443 ms at most. This Mac's 1-minute load average peaked at 7.3 (10 cores). A TCP handshake to 1.1.1.1:443 (elsewhere on the internet, through the same home link) took 124 ms (median), 4733 ms at most.
- The native client's publisher connection: RTT 422 ms median, 4925 ms at most; LiveKit signaling (WebSocket ping) RTT 546 ms median, 6220 ms at most; congestion controller target 116 kbps mean, 89.2 kbps lowest.
- The AI's publisher connection: RTT 548 ms median, 3364 ms at most; LiveKit signaling (WebSocket ping) RTT 391 ms median, 3988 ms at most; congestion controller target 4881 kbps mean, 300 kbps lowest.
- The pages' SDK (console log, per page in launch order): signal reconnects 0, 0, 0, 0; ICE restarts 0, 0, 0, 0; full reconnects 0, 0, 0, 0.

### Received, per receiver and sender

| receiver | sender | video received (share of samples) | fps | video kbps | freezes (ms) | video lost | audio lost / received | jitter buffer ms (video / audio) | RTT ms |
|---|---|---|---|---|---|---|---|---|---|
| page 1 (web-9rnk2k) | AI (ai-8b82) | - | - | - | - | - | 548 / 2395 | - / 1082 | 60 |
| page 1 (web-9rnk2k) | native (native-8b82) | 640x360 100% H264 | 17.7 | 87.1 | 28 (29546) | 350 | 548 / 2394 | 356 / 1063 | 60 |
| page 1 (web-9rnk2k) | page 3 (web-1eq8gv) | 320x180 100% H264 | 12.2 | 51.2 | 19 (18875) | 126 | 533 / 2401 | 345 / 1030 | 60 |
| page 1 (web-9rnk2k) | page 4 (web-f7wuz8) | 320x180 100% H264 | 12.1 | 72.6 | 17 (17637) | 114 | 533 / 2401 | 330 / 1062 | 60 |
| page 1 (web-9rnk2k) | page 2 (web-lp36dw) | 320x180 100% H264 | 12.2 | 67.5 | 18 (18963) | 110 | 527 / 2407 | 300 / 1019 | 60 |
| page 2 (web-lp36dw) | AI (ai-8b82) | - | - | - | - | - | 533 / 2354 | - / 1091 | 118 |
| page 2 (web-lp36dw) | native (native-8b82) | 640x360 100% H264 | 21.2 | 92.4 | 29 (21953) | 370 | 530 / 2355 | 428 / 1073 | 118 |
| page 2 (web-lp36dw) | page 3 (web-1eq8gv) | 320x180 100% H264 | 12.2 | 51.2 | 18 (18841) | 129 | 549 / 2383 | 357 / 1011 | 118 |
| page 2 (web-lp36dw) | page 1 (web-9rnk2k) | 320x180 100% H264 | 10.7 | 78.3 | 18 (21953) | 117 | 540 / 2336 | 128 / 1000 | 118 |
| page 2 (web-lp36dw) | page 4 (web-f7wuz8) | 320x180 100% H264 | 12.1 | 72.2 | 19 (18557) | 128 | 531 / 2343 | 297 / 1131 | 118 |
| page 3 (web-1eq8gv) | AI (ai-8b82) | - | - | - | - | - | 570 / 2314 | - / 1109 | 170 |
| page 3 (web-1eq8gv) | native (native-8b82) | 640x360 100% H264 | 24.6 | 98 | 36 (27202) | 389 | 561 / 2322 | 739 / 1067 | 170 |
| page 3 (web-1eq8gv) | page 1 (web-9rnk2k) | 320x180 100% H264 | 11.8 | 80 | 17 (24065) | 122 | 564 / 2325 | 905 / 1003 | 170 |
| page 3 (web-1eq8gv) | page 4 (web-f7wuz8) | 320x180 100% H264 | 13.5 | 72.9 | 16 (20206) | 132 | 553 / 2335 | 1046 / 1021 | 170 |
| page 3 (web-1eq8gv) | page 2 (web-lp36dw) | 320x180 100% H264 | 11.6 | 65.5 | 11 (16841) | 123 | 544 / 2345 | 315 / 1013 | 170 |
| page 4 (web-f7wuz8) | AI (ai-8b82) | - | - | - | - | - | 574 / 2371 | - / 1081 | 77 |
| page 4 (web-f7wuz8) | native (native-8b82) | 640x360 100% H264 | 21.3 | 93 | 29 (22411) | 362 | 575 / 2368 | 416 / 1120 | 77 |
| page 4 (web-f7wuz8) | page 3 (web-1eq8gv) | 320x180 100% H264 | 12.2 | 51.3 | 19 (19023) | 128 | 534 / 2399 | 362 / 1006 | 77 |
| page 4 (web-f7wuz8) | page 1 (web-9rnk2k) | 320x180 100% H264 | 10.8 | 77 | 19 (22172) | 115 | 581 / 2352 | 138 / 1100 | 77 |
| page 4 (web-f7wuz8) | page 2 (web-lp36dw) | 320x180 100% H264 | 12.2 | 67.7 | 17 (18631) | 115 | 539 / 2394 | 311 / 1030 | 77 |
| native (native-8b82) | AI (ai-8b82) | - | - | - | - | - | 527 / 2361 | - / - | - |
| native (native-8b82) | page 3 (web-1eq8gv) | 320x180 100% | 13.8 | - | - | - | 524 / 2420 | - / - | - |
| native (native-8b82) | page 1 (web-9rnk2k) | 320x180 73%, 640x360 27% | 11 | - | - | - | 523 / 2362 | - / - | - |
| native (native-8b82) | page 4 (web-f7wuz8) | 320x180 100% | 13.3 | - | - | - | 514 / 2372 | - / - | - |
| native (native-8b82) | page 2 (web-lp36dw) | 320x180 100% | 12.2 | - | - | - | 536 / 2408 | - / - | - |
| AI (ai-8b82) | native (native-8b82) | 640x360 100% | 21.7 | - | - | - | 552 / 2330 | - / - | - |
| AI (ai-8b82) | page 3 (web-1eq8gv) | 320x180 100% | 12.2 | - | - | - | 550 / 2390 | - / - | - |
| AI (ai-8b82) | page 1 (web-9rnk2k) | 320x180 70%, 640x360 30% | 11 | - | - | - | 539 / 2344 | - / - | - |
| AI (ai-8b82) | page 4 (web-f7wuz8) | 320x180 98%, 640x360 2% | 12.6 | - | - | - | 537 / 2345 | - / - | - |
| AI (ai-8b82) | page 2 (web-lp36dw) | 320x180 100% | 12.2 | - | - | - | 519 / 2364 | - / - | - |

### Sent: uplink per sender and simulcast layer

| sender | layer (RID) | size | fps | kbps | active (share of samples) | quality limitation | encoder |
|---|---|---|---|---|---|---|---|
| page 1 (web-9rnk2k) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264, OpenH264) |
| page 1 (web-9rnk2k) | h | 640x360 100% | 14.1 | 174 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264, OpenH264) |
| page 1 (web-9rnk2k) | q | 320x180 100% | 14.5 | 80.7 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264, OpenH264) |
| page 1 (web-9rnk2k) | audio | - | - | 83.6 | - | - | - |
| page 2 (web-lp36dw) | h | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-lp36dw) | q | 320x180 100% | 13.7 | 69.4 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-lp36dw) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-lp36dw) | audio | - | - | 84 | - | - | - |
| page 3 (web-1eq8gv) | q | 320x180 100% | 14.2 | 52.1 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-1eq8gv) | h | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-1eq8gv) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-1eq8gv) | audio | - | - | 84 | - | - | - |
| page 4 (web-f7wuz8) | h | 640x360 100% | 19.2 | 10.1 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264, OpenH264) |
| page 4 (web-f7wuz8) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264, OpenH264) |
| page 4 (web-f7wuz8) | q | 320x180 100% | 14.4 | 74.1 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264, OpenH264) |
| page 4 (web-f7wuz8) | audio | - | - | 83.8 | - | - | - |
| native (native-8b82) | audio + video (one layer) | - | 29.8 | 141 (target 116, Opus 32) | - | - | library (VideoToolbox H.264) |
| AI (ai-8b82) | audio only | - | - | 31.9 (target 4881, Opus 32) | - | - | - |

### Active speakers

- page 1 (web-9rnk2k): active in the samples: AI (ai-8b82) 40%, native (native-8b82) 17%; seen: AI (ai-8b82), native (native-8b82).
- page 2 (web-lp36dw): active in the samples: AI (ai-8b82) 37%, native (native-8b82) 17%; seen: AI (ai-8b82), native (native-8b82).
- page 3 (web-1eq8gv): active in the samples: AI (ai-8b82) 35%, native (native-8b82) 15%; seen: AI (ai-8b82), native (native-8b82).
- page 4 (web-f7wuz8): active in the samples: AI (ai-8b82) 37%, native (native-8b82) 15%; seen: AI (ai-8b82), native (native-8b82).
- native: loudest active speaker in the samples: AI (ai-8b82) 40%, native (native-8b82) 15%; seen: AI (ai-8b82), native (native-8b82).

### The conversation (native client <-> AI)

- native client: 10 turns, 10 replies (0 missed); response (end of its speech to the AI's first audio played) p50 4045 ms, transport part 3445 ms; all: [1770, 2930, 750, 3860, 1320, 4230, 5040, 5070, 4350, 4390] ms.
- AI: 10 user turns heard, 10 replies, 1 barge-ins; it listened to native (native-8b82); response delay at the AI (its turn end to its first reply audio sent) [661.1, 640.3, 650.2, 660.2, 650.6, 641.9, 640.2, 650.8, 651.7, 640.6] ms.


## Run 2026-11-04 22:53 PST

- Room `grp-73c929bf` on `<host>` (LiveKit v1.13.11), 6 of 6 participants in the room at once: 4 headless Chrome pages (web app group mode, Chrome 156.0.8078.80), `sm_call_smoke --livekit --video` (native, talking in turns), `sm_ai_server --livekit` (the AI).
- Window: 60 s, from the moment every page counted all the participants (12.5 s after the first page started).
- The path to the host without LiveKit, once a second: GET /lk-token on a kept-alive HTTPS connection took 468 ms (median), 3389 ms at most, 2 failed; a TCP handshake to the host's port 614 ms (median), 2839 ms at most. This Mac's 1-minute load average peaked at 8.1 (10 cores). A TCP handshake to 1.1.1.1:443 (elsewhere on the internet, through the same home link) took 506 ms (median), 2381 ms at most.
- The native client's publisher connection: RTT 666 ms median, 1818 ms at most; LiveKit signaling (WebSocket ping) RTT 747 ms median, 1278 ms at most; congestion controller target 121 kbps mean, 89.2 kbps lowest.
- The AI's publisher connection: RTT 580 ms median, 1820 ms at most; LiveKit signaling (WebSocket ping) RTT 462 ms median, 1714 ms at most; congestion controller target 5715 kbps mean, 325 kbps lowest.
- The pages' SDK (console log, per page in launch order): signal reconnects 1, 1, 1, 1; ICE restarts 1, 1, 1, 1; full reconnects 0, 0, 0, 0.

### Received, per receiver and sender

| receiver | sender | video received (share of samples) | fps | video kbps | freezes (ms) | video lost | audio lost / received | jitter buffer ms (video / audio) | RTT ms |
|---|---|---|---|---|---|---|---|---|---|
| page 1 (web-kgljay) | AI (ai-be25) | - | - | - | - | - | 348 / 2466 | - / 1076 | 722 |
| page 1 (web-kgljay) | native (native-be25) | 640x360 100% H264 | 24 | 97.9 | 27 (28081) | 242 | 361 / 2453 | 1135 / 1159 | 722 |
| page 1 (web-kgljay) | page 4 (web-22br5d) | 320x180 100% H264 | 10.5 | 25.8 | 30 (26233) | 135 | 205 / 2431 | 433 / 903 | 722 |
| page 1 (web-kgljay) | page 2 (web-3vx4lk) | 320x180 100% H264 | 10.8 | 22.5 | 27 (27599) | 149 | 214 / 2444 | 496 / 849 | 722 |
| page 1 (web-kgljay) | page 3 (web-k8bduy) | 320x180 100% H264 | 12.6 | 23.5 | 24 (24801) | 139 | 211 / 2425 | 546 / 868 | 722 |
| page 2 (web-3vx4lk) | AI (ai-be25) | - | - | - | - | - | 356 / 2459 | - / 1081 | 1086 |
| page 2 (web-3vx4lk) | native (native-be25) | 640x360 100% H264 | 25.2 | 102 | 22 (24843) | 243 | 374 / 2441 | 1161 / 1145 | 1086 |
| page 2 (web-3vx4lk) | page 4 (web-22br5d) | 320x180 100% H264 | 10.5 | 25.9 | 28 (26381) | 139 | 213 / 2417 | 463 / 898 | 1086 |
| page 2 (web-3vx4lk) | page 3 (web-k8bduy) | 320x180 100% H264 | 12.6 | 23.5 | 25 (24541) | 151 | 220 / 2410 | 596 / 875 | 1086 |
| page 2 (web-3vx4lk) | page 1 (web-kgljay) | 320x180 100% H264 | 11.8 | 23 | 26 (26094) | 155 | 238 / 2419 | 592 / 847 | 1086 |
| page 3 (web-k8bduy) | AI (ai-be25) | - | - | - | - | - | 368 / 2439 | - / 1082 | 1133 |
| page 3 (web-k8bduy) | native (native-be25) | 640x360 100% H264 | 23.5 | 96.1 | 27 (28714) | 249 | 391 / 2417 | 1056 / 1148 | 1133 |
| page 3 (web-k8bduy) | page 4 (web-22br5d) | 320x180 100% H264 | 10.4 | 26 | 29 (25701) | 128 | 212 / 2437 | 371 / 890 | 1133 |
| page 3 (web-k8bduy) | page 2 (web-3vx4lk) | 320x180 100% H264 | 9.9 | 22.1 | 24 (26192) | 153 | 221 / 2417 | 396 / 872 | 1133 |
| page 3 (web-k8bduy) | page 1 (web-kgljay) | 320x180 100% H264 | 9.1 | 21.6 | 18 (29355) | 156 | 236 / 2410 | 602 / 903 | 1133 |
| page 4 (web-22br5d) | AI (ai-be25) | - | - | - | - | - | 361 / 2444 | - / 1095 | 1086 |
| page 4 (web-22br5d) | native (native-be25) | 640x360 100% H264 | 21.5 | 95.1 | 25 (30250) | 241 | 367 / 2439 | 1085 / 1156 | 1086 |
| page 4 (web-22br5d) | page 2 (web-3vx4lk) | 320x180 100% H264 | 10 | 21.9 | 17 (22220) | 152 | 214 / 2421 | 643 / 891 | 1086 |
| page 4 (web-22br5d) | page 3 (web-k8bduy) | 320x180 100% H264 | 12.5 | 23.4 | 17 (19977) | 127 | 208 / 2439 | 724 / 883 | 1086 |
| page 4 (web-22br5d) | page 1 (web-kgljay) | 320x180 100% H264 | 12 | 22.1 | 22 (24659) | 151 | 227 / 2417 | 736 / 900 | 1086 |
| native (native-be25) | AI (ai-be25) | - | - | - | - | - | 341 / 2529 | - / - | - |
| native (native-be25) | page 4 (web-22br5d) | 320x180 100% | 10.3 | - | - | - | 189 / 2446 | - / - | - |
| native (native-be25) | page 2 (web-3vx4lk) | 320x180 100% | 10 | - | - | - | 197 / 2451 | - / - | - |
| native (native-be25) | page 3 (web-k8bduy) | 320x180 100% | 12.8 | - | - | - | 198 / 2449 | - / - | - |
| native (native-be25) | page 1 (web-kgljay) | 320x180 100% | 12.2 | - | - | - | 210 / 2442 | - / - | - |
| AI (ai-be25) | native (native-be25) | 640x360 100% | 25.8 | - | - | - | 313 / 2585 | - / - | - |
| AI (ai-be25) | page 4 (web-22br5d) | 320x180 100% | 10.7 | - | - | - | 160 / 2515 | - / - | - |
| AI (ai-be25) | page 2 (web-3vx4lk) | 320x180 100% | 11.2 | - | - | - | 172 / 2505 | - / - | - |
| AI (ai-be25) | page 3 (web-k8bduy) | 320x180 100% | 12.8 | - | - | - | 168 / 2507 | - / - | - |
| AI (ai-be25) | page 1 (web-kgljay) | 320x180 93%, 640x360 7% | 12 | - | - | - | 173 / 2509 | - / - | - |

### Sent: uplink per sender and simulcast layer

| sender | layer (RID) | size | fps | kbps | active (share of samples) | quality limitation | encoder |
|---|---|---|---|---|---|---|---|
| page 1 (web-kgljay) | q | 320x180 100% | 15.3 | 25.5 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 1 (web-kgljay) | h | 640x360 100% | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 1 (web-kgljay) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 1 (web-kgljay) | audio | - | - | 82.2 | - | - | - |
| page 2 (web-3vx4lk) | q | 320x180 100% | 13.1 | 24.9 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-3vx4lk) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-3vx4lk) | h | 640x360 100% | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-3vx4lk) | audio | - | - | 82.5 | - | - | - |
| page 3 (web-k8bduy) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-k8bduy) | q | 320x180 100% | 15.8 | 25.8 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-k8bduy) | h | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-k8bduy) | audio | - | - | 82.9 | - | - | - |
| page 4 (web-22br5d) | h | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 4 (web-22br5d) | q | 320x180 100% | 13.6 | 27.6 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 4 (web-22br5d) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 4 (web-22br5d) | audio | - | - | 82.7 | - | - | - |
| native (native-be25) | audio + video (one layer) | - | 30 | 142 (target 121, Opus 32) | - | - | library (VideoToolbox H.264) |
| AI (ai-be25) | audio only | - | - | 28.1 (target 5715, Opus 32) | - | - | - |

### Active speakers

- page 1 (web-kgljay): active in the samples: native (native-be25) 27%, AI (ai-be25) 27%; seen: AI (ai-be25), native (native-be25).
- page 2 (web-3vx4lk): active in the samples: native (native-be25) 28%, AI (ai-be25) 28%; seen: AI (ai-be25), native (native-be25).
- page 3 (web-k8bduy): active in the samples: native (native-be25) 23%, AI (ai-be25) 23%; seen: AI (ai-be25), native (native-be25).
- page 4 (web-22br5d): active in the samples: AI (ai-be25) 23%, native (native-be25) 22%; seen: AI (ai-be25), native (native-be25).
- native: loudest active speaker in the samples: AI (ai-be25) 25%, native (native-be25) 19%; seen: AI (ai-be25), native (native-be25).

### The conversation (native client <-> AI)

- native client: 8 turns, 5 replies (2 missed); response (end of its speech to the AI's first audio played) p50 4850 ms, transport part 4250 ms; all: [4740, 4850, 5130, 4450, 5990] ms.
- AI: 8 user turns heard, 8 replies, 0 barge-ins; it listened to native (native-be25); response delay at the AI (its turn end to its first reply audio sent) [650.4, 641.1, 640.5, 640.5, 650.4, 640.5, 640.0, 642.9] ms.


## Run 2026-11-04 22:51 PST

- Room `grp-d53eda72` on `<host>` (LiveKit v1.13.11), 6 of 6 participants in the room at once: 4 headless Chrome pages (web app group mode, Chrome 156.0.8078.80), `sm_call_smoke --livekit --video` (native, talking in turns), `sm_ai_server --livekit` (the AI).
- Window: 60 s, from the moment every page counted all the participants (9.1 s after the first page started).
- The path to the host without LiveKit, once a second: GET /lk-token on a kept-alive HTTPS connection took 580 ms (median), 4663 ms at most; a TCP handshake to the host's port 512 ms (median), 2461 ms at most. This Mac's 1-minute load average peaked at 3.6 (10 cores).
- The native client's publisher connection: RTT 566 ms median, 3526 ms at most; LiveKit signaling (WebSocket ping) RTT 943 ms median, 2021 ms at most; congestion controller target 111 kbps mean, 89.2 kbps lowest.
- The AI's publisher connection: RTT 698 ms median, 3191 ms at most; LiveKit signaling (WebSocket ping) RTT 718 ms median, 3894 ms at most; congestion controller target 6515 kbps mean, 300 kbps lowest.
- The pages' SDK (console log, per page in launch order): signal reconnects 0, 0, 0, 0; ICE restarts 0, 0, 0, 0; full reconnects 0, 0, 0, 0.

### Received, per receiver and sender

| receiver | sender | video received (share of samples) | fps | video kbps | freezes (ms) | video lost | audio lost / received | jitter buffer ms (video / audio) | RTT ms |
|---|---|---|---|---|---|---|---|---|---|
| page 1 (web-pqdfrf) | AI (ai-6abc) | - | - | - | - | - | 292 / 2593 | - / 833 | 1518 |
| page 1 (web-pqdfrf) | native (native-6abc) | 640x360 100% H264 | 25.7 | - | - | - | 334 / 2550 | 890 / 826 | 1518 |
| page 1 (web-pqdfrf) | page 2 (web-2dpikm) | 320x180 100% H264 | 13.1 | 47.6 | 34 (17769) | 89 | 261 / 2602 | 260 / 822 | 1518 |
| page 1 (web-pqdfrf) | page 4 (web-98s9zc) | 320x180 100% H264 | 13.1 | 80.8 | 29 (18825) | 67 | 236 / 2628 | 317 / 882 | 1518 |
| page 1 (web-pqdfrf) | page 3 (web-a7m29k) | 320x180 100% H264 | 12.8 | 63.3 | 23 (15938) | 66 | 266 / 2597 | 278 / 872 | 1518 |
| page 2 (web-2dpikm) | AI (ai-6abc) | - | - | - | - | - | 320 / 2535 | - / 814 | 1791 |
| page 2 (web-2dpikm) | native (native-6abc) | 640x360 100% H264 | 26.7 | 95.9 | 17 (12203) | 244 | 378 / 2476 | 1116 / 828 | 1791 |
| page 2 (web-2dpikm) | page 4 (web-98s9zc) | 320x180 100% H264 | 12.9 | 80.6 | 28 (17752) | 77 | 263 / 2592 | 325 / 867 | 1791 |
| page 2 (web-2dpikm) | page 3 (web-a7m29k) | 320x180 100% H264 | 13.2 | 62.4 | 24 (17977) | 72 | 297 / 2558 | 327 / 839 | 1791 |
| page 2 (web-2dpikm) | page 1 (web-pqdfrf) | 320x180 100% H264 | 12.9 | 75.6 | 32 (18978) | 69 | 298 / 2557 | 251 / 834 | 1791 |
| page 3 (web-a7m29k) | AI (ai-6abc) | - | - | - | - | - | 275 / 2593 | - / 813 | 2030 |
| page 3 (web-a7m29k) | native (native-6abc) | 640x360 100% H264 | 25.5 | 94.8 | 17 (10266) | 198 | 291 / 2577 | 882 / 821 | 2030 |
| page 3 (web-a7m29k) | page 2 (web-2dpikm) | 320x180 100% H264 | 12.7 | 46.2 | 26 (13422) | 81 | 230 / 2639 | 301 / 850 | 2030 |
| page 3 (web-a7m29k) | page 4 (web-98s9zc) | 320x180 100% H264 | 13.1 | 78.8 | 28 (18454) | 69 | 228 / 2640 | 318 / 906 | 2030 |
| page 3 (web-a7m29k) | page 1 (web-pqdfrf) | 320x180 100% H264 | 13.1 | 75.1 | 27 (14664) | 52 | 238 / 2630 | 289 / 861 | 2030 |
| page 4 (web-98s9zc) | AI (ai-6abc) | - | - | - | - | - | 325 / 2530 | - / 816 | 1641 |
| page 4 (web-98s9zc) | native (native-6abc) | 640x360 100% H264 | 25.3 | 93.7 | 16 (10609) | 264 | 401 / 2453 | 858 / 837 | 1641 |
| page 4 (web-98s9zc) | page 2 (web-2dpikm) | 320x180 100% H264 | 13.3 | 45.8 | 30 (19555) | 114 | 316 / 2539 | 363 / 855 | 1641 |
| page 4 (web-98s9zc) | page 3 (web-a7m29k) | 320x180 100% H264 | 12.5 | 61.3 | 25 (16224) | 79 | 325 / 2530 | 315 / 833 | 1641 |
| page 4 (web-98s9zc) | page 1 (web-pqdfrf) | 320x180 100% H264 | 12.4 | 74 | 27 (15580) | 79 | 313 / 2542 | 314 / 851 | 1641 |
| native (native-6abc) | AI (ai-6abc) | - | - | - | - | - | 378 / 2447 | - / - | - |
| native (native-6abc) | page 2 (web-2dpikm) | 320x180 100% | 12.6 | - | - | - | 286 / 2540 | - / - | - |
| native (native-6abc) | page 4 (web-98s9zc) | 320x180 100% | 12.9 | - | - | - | 283 / 2543 | - / - | - |
| native (native-6abc) | page 3 (web-a7m29k) | 320x180 100% | 12.4 | - | - | - | 293 / 2533 | - / - | - |
| native (native-6abc) | page 1 (web-pqdfrf) | 320x180 92%, 640x360 8% | 10.7 | - | - | - | 288 / 2538 | - / - | - |
| AI (ai-6abc) | native (native-6abc) | 640x360 100% | 25.6 | - | - | - | 335 / 2559 | - / - | - |
| AI (ai-6abc) | page 2 (web-2dpikm) | 320x180 97%, 640x360 3% | 13.2 | - | - | - | 244 / 2631 | - / - | - |
| AI (ai-6abc) | page 4 (web-98s9zc) | 320x180 97%, 640x360 3% | 13 | - | - | - | 222 / 2652 | - / - | - |
| AI (ai-6abc) | page 3 (web-a7m29k) | 320x180 95%, 1280x720 5% | 12.5 | - | - | - | 255 / 2619 | - / - | - |
| AI (ai-6abc) | page 1 (web-pqdfrf) | 320x180 92%, 640x360 8% | 12.9 | - | - | - | 252 / 2623 | - / - | - |

### Sent: uplink per sender and simulcast layer

| sender | layer (RID) | size | fps | kbps | active (share of samples) | quality limitation | encoder |
|---|---|---|---|---|---|---|---|
| page 1 (web-pqdfrf) | q | 320x180 100% | 13.4 | 78.5 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 1 (web-pqdfrf) | f | 1280x720 100% | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 1 (web-pqdfrf) | h | 640x360 100% | 7.5 | 36.9 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 1 (web-pqdfrf) | audio | - | - | 83.2 | - | - | - |
| page 2 (web-2dpikm) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-2dpikm) | h | 640x360 100% | 5 | 5.7 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-2dpikm) | q | 320x180 100% | 13.2 | 46.4 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 2 (web-2dpikm) | audio | - | - | 83.2 | - | - | - |
| page 3 (web-a7m29k) | q | 320x180 100% | 13.7 | 62.8 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-a7m29k) | h | 640x360 100% | 9.8 | 50.4 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-a7m29k) | f | 1280x720 100% | 0.8 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 3 (web-a7m29k) | audio | - | - | 83.1 | - | - | - |
| page 4 (web-98s9zc) | h | 640x360 100% | 7.2 | 95 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 4 (web-98s9zc) | f | - | 0 | 0 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 4 (web-98s9zc) | q | 320x180 100% | 14.2 | 84.9 | 100% | bandwidth 100% | SimulcastEncoderAdapter (OpenH264) |
| page 4 (web-98s9zc) | audio | - | - | 83.2 | - | - | - |
| native (native-6abc) | audio + video (one layer) | - | 30 | 142 (target 111, Opus 32) | - | - | library (VideoToolbox H.264) |
| AI (ai-6abc) | audio only | - | - | 28.3 (target 6515, Opus 32) | - | - | - |

### Active speakers

- page 1 (web-pqdfrf): active in the samples: AI (ai-6abc) 28%, native (native-6abc) 23%; seen: AI (ai-6abc), native (native-6abc).
- page 2 (web-2dpikm): active in the samples: AI (ai-6abc) 28%, native (native-6abc) 23%; seen: AI (ai-6abc), native (native-6abc).
- page 3 (web-a7m29k): active in the samples: AI (ai-6abc) 37%, native (native-6abc) 17%; seen: AI (ai-6abc), native (native-6abc).
- page 4 (web-98s9zc): active in the samples: AI (ai-6abc) 32%, native (native-6abc) 25%; seen: AI (ai-6abc), native (native-6abc).
- native: loudest active speaker in the samples: AI (ai-6abc) 33%, native (native-6abc) 17%; seen: AI (ai-6abc), native (native-6abc).

### The conversation (native client <-> AI)

- native client: 10 turns, 9 replies (1 missed); response (end of its speech to the AI's first audio played) p50 3070 ms, transport part 2470 ms; all: [3000, 3070, 3980, 4220, 4600, 650, 2000, 1260, 4840] ms.
- AI: 10 user turns heard, 10 replies, 3 barge-ins; it listened to native (native-6abc); response delay at the AI (its turn end to its first reply audio sent) [660.5, 640.0, 661.2, 640.7, 650.2, 670.0, 650.0, 650.4, 650.3, 640.5] ms.

