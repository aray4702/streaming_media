# Calls on real networks (C12 part 2, #1121)

Calls through the self-hosted signaling service and TURN server (coturn on AWS Lightsail, Oregon; #1054), between
the clients of milestone 4, with the caller-side conversation metrics of milestone 2. `<host>` is the server's name;
it is not written here because `/turn` hands out TURN credentials without authentication.

## How to rerun

```sh
cmake --preset native && cmake --build --preset native -j10
export SM_SIGNALING_URL=https://<host>:8443        # wss://<host>:8443/ws and GET /turn
tools/real_calls.py --cases=a,b,c --out=calls.jsonl            # each case direct and relay only
# case d: sm_call_smoke built for the simulator (cmake --preset ios-simulator, then
#   cmake --build out/ios-simulator --config Release --target sm_call_smoke -- CODE_SIGNING_ALLOWED=NO)
tools/real_calls.py --cases=d --sim=<UDID> --ios-smoke=out/ios-simulator/apps/call/Release-iphonesimulator/sm_call_smoke
tools/real_calls.py --table=calls.jsonl                        # the table below
```

Case b's scripted user is Chrome's microphone with its audio processing on (#1124, `--chrome-mic=apm`, the default):
a getUserMedia track with echoCancellation, noiseSuppression and autoGainControl, fed a WAV of the same turns through
`--use-file-for-fake-audio-capture` (on macOS with `--disable-features=AudioServiceSandbox`, or the audio service
can't read the file). The turns follow a fixed schedule: the barge-in comes 2.3 s after the turn's end, about 1 s
into the reply. A barge-in turn that comes before the reply starts counts as "not in a reply". The reply plays out,
so Chrome's echo canceller has it as its render signal, as our client's AEC3 has its playout. The calls before
#1124 used a WebAudio track with no capture processing (`--chrome-mic=webaudio`).

| case | caller | callee |
|---|---|---|
| a | `sm_call_smoke`, the iOS app's C++ call controller (`apps/call`), on macOS | the AI server |
| b | headless Chrome with the web app (`apps/web/index.html`) | the AI server |
| c | headless Chrome with the web app | `sm_call_smoke` in the same room (the web <-> iOS path), with a synthetic 640x360 camera |
| d | `sm_call_smoke` built for the iOS simulator, run in it | the AI server |

- **Paths.** "direct" lets ICE pick any pair; "relay" sets relay only on the caller (`--relay`, the web app's
  `?relay=1`, the iOS app's "Relay only" toggle, `sm_ai_server --relay`), as RTCConfiguration's
  `iceTransportPolicy: 'relay'`. The table shows the pair ICE selected, as RTCIceCandidateStats reports it. A relay
  candidate whose peer sees it at another address turns peer-reflexive (prflx). This happens when the AI server
  runs on the TURN host and reaches the relay at its private address. That candidate keeps its relay protocol, so
  the row says "relayed".
- **Conversation**, measured the same way by both clients (`apps/call/call_smoke.cc`, the web app's `converse=1`)
  and defined as in the test bench (`conv.response_delay_*`, `conv.barge_in_stop_ms`). The user speaks 1.5 s turns
  of speech-like audio.
  - Response: from the end of the user's speech to the AI's first audio played at the caller. The transport part
    is that time minus the 600 ms of silence the AI's endpointer waits for.
  - Barge-in: every other turn talks over the reply 1 s into it. "Barge-in stop" runs from the barge-in's first
    sample to the reply's audio stopping at the caller. A reply that runs out by itself instead (over 1.5 s) counts
    as "not stopped". "Missed" means no reply came within 6 s.
- **The other metrics** come from `sm_call_smoke`'s library stats (`CallClient::stats`: `Session::GetStats` and
  `pc::GetConnectionStats`) and from Chrome's `getStats()`. They are the receiver's audio concealment and jitter
  buffer delay, packets lost, the pair's RTT, the sender's bitrate target (Chrome: `availableOutgoingBitrate`),
  and video render rate and freezes (Chrome: `framesPerSecond` and `freezeCount`; `sm_call_smoke`: WebRTC's freeze
  definition on its rendered frames).
- Each call is 25 s.

## Results

The AI server was the one deployed on `<host>` (`--ai=auto` found that it answered). The clients ran on a MacBook
(Apple Silicon) on a home connection in California. The AI server runs on the TURN host in Oregon, so "direct"
also crosses the internet (srflx to srflx).

| date | case | client | path | selected pair | connect ms | RTT ms | response ms (transport) | barge-in stop ms | concealed % | jitter buffer ms | lost | target kbps | video fps / freezes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-11-03 08:07 UTC | a | sm_call_smoke | direct | srflx/srflx udp | 581 | 53.6 | 1805 (1205) | 1160, 1 not stopped | 0.7 | 553 | 0 | 66.2 | - |
| 2026-11-03 08:07 UTC | a | sm_call_smoke | relay | prflx/prflx udp, relayed (TURN over udp) | 1444 | 112 | 1410 (810), 1 missed | 870, 1 not stopped | 0.4 | 254 | 0 | 86.3 | - |
| 2026-11-03 08:07 UTC | b | chrome | direct | srflx/srflx udp | 469 | 75 | 1356 (756) | 590 | 1.3 | 268 | 0 | 54.4 | - |
| 2026-11-03 08:07 UTC | b | chrome | relay | prflx/prflx udp, relayed (TURN over udp) | 439 | 48 | 1304 (704) | 540 | 0.6 | 245 | 0 | 54.4 | - |
| 2026-11-03 08:07 UTC | d | sm_call_smoke (iOS simulator) | direct | srflx/srflx udp | 593 | 25.2 | 1210 (610), 1 missed | 730, 1 not stopped | 0.5 | 198 | 0 | 72.7 | - |
| 2026-11-03 08:07 UTC | d | sm_call_smoke (iOS simulator) | relay | prflx/srflx udp, relayed (TURN over udp) | 1432 | 53.5 | 1320 (720), 1 missed | 840, 1 not stopped | 0.4 | 224 | 0 | 82.5 | - |
| 2026-11-03 08:13 UTC | c | chrome | direct | host/host udp | 121 | 1 | - | - | 0 | 41.9 | 0 | 84.4 | 30 / 0 |
| 2026-11-03 08:13 UTC | c | sm_call_smoke | direct | host/host udp | 3695 | 1 | - | - | 0.2 | 48.7 | 0 | 5632 | 13.2 / 1 |
| 2026-11-03 08:13 UTC | c | chrome | relay | relay/relay udp, relayed (TURN over udp) | 1682 | 224 | - | - | 2.4 | 315 | 16 | 84.4 | 30 / 13 |
| 2026-11-03 08:13 UTC | c | sm_call_smoke | relay | relay/relay udp, relayed (TURN over udp) | 4687 | 63.4 | - | - | 2.7 | 292 | 16 | 148 | 12.6 / 4 |

### The iOS app in the simulator

The SmCall app itself (`apps/ios`), launched with `-SMServer https://<host>:8443 -SMAutoCall ai-... -SMAutoHangup 12
-SMVideo NO [-SMRelay YES]`, called the AI server on 2026-11-03 at 08:36 UTC. Its microphone was the simulator's,
so there is no scripted conversation. On hang-up it logs its `call-result` line (`CallClient::stats`):

| path | selected pair | connect ms | RTT ms | audio played s | concealed % | jitter buffer ms |
|---|---|---|---|---|---|---|
| direct | srflx/srflx udp | 478 | 23.1 | 10.6 | 0.83 | 149 |
| relay | prflx/prflx udp, relayed (TURN over udp) | 1359 | 27.9 | 10.1 | 1.23 | 188 |

### What the numbers show

- **Every client pair connected, direct and relay only.** TLS signaling (`wss://`), `/turn` over `https://`,
  DTLS-SRTP (`TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`, `SRTP_AES128_CM_HMAC_SHA1_80`) and TURN over UDP all worked.
  This holds for the native controller on macOS and in the iOS simulator, and for Chrome.
- **Response delay at the caller: 1.2 to 1.8 s.** Less the endpointer's 600 ms, the transport part is 0.6 to
  1.2 s. The test bench's conversation scenarios give 270 to 380 ms for the same quantity
  (`conversation_clean` 290, `conversation_barge_in` 270, `conversation_loss_5pct` 320, `conversation_jitter_50ms`
  380, at 100 ms RTT). The largest difference is the receiver's jitter buffer: 200 to 550 ms here (both our client
  and Chrome receiving from the AI server), against 30 to 90 ms in the test bench. Chrome receiving from
  `sm_call_smoke` (case c, direct) holds 42 ms, but that call ran host to host on the LAN. On the same relayed path,
  every sender builds a buffer of this size (#1126, below): the path's jitter, not the AI server, fills most of it.
  The AI server's send loop added a smaller share, which #1126 removed. The rest of the response comes from the
  RTT, the AI side's own jitter buffer, the reply source's time, the VAD's hangover before the endpointer starts
  counting, and our 10 ms polling of the played-out audio.
- **Barge-in stop: 540 to 1160 ms** (the test bench's `conversation_barge_in`: 190 ms). The same jitter buffers sit
  on the AI-to-user path, on top of up to 300 ms in the turn detector (100 ms onset and 200 ms of speech before it calls a
  barge-in).
- **Our client loses one barge-in and one reply per call; Chrome loses none.** The barge-in isn't stopped and the
  reply is missed. Four extra calls (case a, direct, 30 s each, 2026-11-03 08:10 UTC) isolate the cause:
  - capture chain on (the app's default: high-pass filter, NS, AEC3, AGC): 4 and 5 replies to 6 turns, with 1
    missed reply and 1 barge-in not stopped in each call;
  - `--no-apm` (raw microphone): 6 replies to 6 turns and 3 of 3 barge-ins stopped, in both calls.

  (Superseded by the rerun below, "Barge-in with the capture chain on both sides".) The capture chain removes part of the user's speech while the AI's audio plays, which looks like AEC3
  suppressing double talk, so the AI server's VAD doesn't hear the barge-in. Chrome's synthetic track has no
  capture processing. #1124 measured it against libwebrtc's APM on the same audio
  (`docs/results/apm-webrtc.md`, "barge-in, no echo path"), and libwebrtc does the same. With no echo path, AEC3
  suppresses the user as echo until M157's legacy transparent mode turns on after 6 s of strong render. The barge-ins
  in the first replies of a call fall in that time (4 of 5 talkers detected on each side, worst 1.5 s). A missed
  barge-in likely also costs the next reply: the reply plays on without a gap, so the script sees no new reply start.
- **Video, case c:** Chrome renders `sm_call_smoke`'s 30 fps with no freeze direct, and 13 freezes in 25 s relayed.
  `sm_call_smoke` renders Chrome's camera at 13 fps. Chrome's `availableOutgoingBitrate` toward our endpoint stays
  at 84.4 kbps for the whole call, even host to host, so Chrome sends its video at a very low rate. In case b,
  toward the AI server, it stays at 54.4 kbps. Our sender's target toward Chrome reaches 5.6 Mbps direct. Chrome's
  bandwidth estimate not rising on feedback from our endpoint is a gap to look at.
- **Where Chrome's getStats and ours differ on the same call (case c):**
  - RTT on the relayed pair: Chrome reports 224 ms (`currentRoundTripTime`, from its STUN checks) and our client
    63 ms (the session's RTT from RTCP and transport feedback, falling back to the pair's STUN RTT). These are
    different definitions. Direct, both say 1 ms.
  - Packets lost (16 each way relayed) and concealment (2.4 % and 2.7 %) agree in size.
  - `connect ms` for `sm_call_smoke` in case c counts from its own start, so it includes the 2 s it waited for
    Chrome to join.

## Barge-in with the capture chain on both sides (#1124)

Rerun on 2026-11-03 after the Owner's decision on #1124. Chrome's audio processing is on, so the barge-in scoring
compares APM with APM. The SmCall iOS app now uses VoiceProcessingIO's echo canceller and AGC, with the software
AEC3 and AGC off in its chain, as libwebrtc's iOS client does (`ApplyIosAudioOptions`). `sm_call_smoke` built for
the simulator (case d) runs that chain (high-pass filter and NS). It has no audio device, so nothing takes the place
of AEC3. On macOS (case a) the chain keeps AEC3 and AGC, the library's default. The calls ran 40 s each, with 3 or 4
barge-ins per call, from the same MacBook against the AI server on `<host>`. The "barge-in stop ms" column reads
"stopped of attempted: p50".

| date | case | client | capture | path | selected pair | connect ms | RTT ms | response ms (transport) | barge-in stop ms | concealed % | jitter buffer ms | lost | target kbps | video fps / freezes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-11-03 18:52 UTC | a | sm_call_smoke | hpf, ns, aec3, agc | direct | srflx/srflx udp | 666 | 36.4 | 1400 (800) | 4 of 4: 830 | 1 | 246 | 0 | 114 | - |
| 2026-11-03 18:52 UTC | a | sm_call_smoke | hpf, ns, aec3, agc | relay | prflx/prflx udp, relayed (TURN over udp) | 1473 | 27.7 | 1300 (700) | 4 of 4: 605 | 0.4 | 130 | 4 | 114 | - |
| 2026-11-03 18:52 UTC | b | chrome | APM on | direct | prflx/prflx udp, relayed (TURN over udp) | 455 | 35 | 1581 (981), 1 missed | 2 of 3: 894, 1 not stopped | 1 | 236 | 0 | 100 | - |
| 2026-11-03 18:52 UTC | b | chrome | APM on | relay | prflx/prflx udp, relayed (TURN over udp) | 321 | 48 | 1588 (988) | 3 of 3: 948 | 1.2 | 292 | 0 | 95.6 | - |
| 2026-11-03 18:56 UTC | d | sm_call_smoke (iOS simulator) | hpf, ns | direct | srflx/srflx udp | 609 | 26.9 | 1265 (665) | 4 of 4: 540 | 0.3 | 158 | 8 | 99.6 | - |
| 2026-11-03 18:56 UTC | d | sm_call_smoke (iOS simulator) | hpf, ns | relay | prflx/prflx udp, relayed (TURN over udp) | 1400 | 89.2 | 1410 (810) | 4 of 4: 600 | 0.8 | 166 | 2 | 63.2 | - |
| 2026-11-03 19:57 UTC | a | sm_call_smoke | hpf, ns, aec3, agc | direct | prflx/prflx udp, relayed (TURN over udp) | 1276 | 84.2 | 1235 (635) | 4 of 4: 590 | 0.3 | 100 | 1 | 117 | - |
| 2026-11-03 19:57 UTC | a | sm_call_smoke | hpf, ns, aec3, agc | relay | prflx/prflx udp, relayed (TURN over udp) | 1394 | 21.9 | 1235 (635) | 4 of 4: 575 | 0.3 | 102 | 2 | 117 | - |
| 2026-11-03 19:57 UTC | b | chrome | APM on | direct | prflx/prflx udp, relayed (TURN over udp) | 420 | 28 | 1168 (568) | 3 of 3: 468 | 1.3 | 117 | 20 | 112 | - |
| 2026-11-03 19:57 UTC | b | chrome | APM on | relay | prflx/prflx udp, relayed (TURN over udp) | 328 | 26 | 1188 (588) | 3 of 3: 516 | 0.2 | 127 | 0 | 99.1 | - |

- **AEC3 against Chrome's APM: we are not behind.** Over these rounds, AEC3 (case a) stopped 16 of 16 barge-ins
  and answered every turn. Chrome with APM (case b) stopped 11 of 12 and missed 1 reply. The iOS chain (case d)
  stopped 8 of 8. Barge-in stop p50: 575-830 ms for case a, 468-948 ms for Chrome, 540-600 ms for case d.
- The one-barge-in-per-call loss of the 08:07 rounds (case a and d, AEC3 in both then) did not come back. Those
  calls ran before #1126 and #1131 changed the AI server's audio clock.
- Two more rounds (18:57 and 19:01 UTC) ran while the path was degraded: RTT 244-991 ms, connects of 2-5.5 s, and
  case a direct failed to connect in both rounds. Both clients then lost most barge-ins (case a 0 of 3, Chrome 0 of
  2, with 9 barge-in turns "not in a reply" because the replies came late) and missed 1-3 replies per call. They are
  left out of the table because they measure the path, not the capture chain.
- The on-device check of VoiceProcessingIO's echo canceller (a real speaker and microphone) is the Owner's. The
  simulator has no echo path.

## The jitter buffer on the same path (#1126)

Measured on 2026-11-03 from the same MacBook, audio only, 25 s calls, relayed through the TURN server in Oregon
(Mac -> TURN -> Mac). The AI server ran locally, behind a local signaling service that hands out credentials for
the same TURN server. The caller's `jitter_buffer_ms` is `jitterBufferDelay / jitterBufferEmittedCount`.

- **Every sender builds a similar buffer on this path.** Chrome to Chrome held 132-448 ms across rounds. In each
  call between Chrome and `sm_call_smoke`, the two directions matched within 20 ms (183/190, 202/186, 311/309,
  332/332, 159/182, 362/377). `sm_call_smoke` to `sm_call_smoke` held 105-200 ms. From one call to the next, the
  path moved the buffer more than the endpoints did.
- **The AI server added 35-65 ms on top** (loopback: 51-77 ms against 32-42 ms from `sm_call_smoke`). Its reply
  source ran on the loop thread and stalled the 20 ms send slots at every reply start, and the late slots caught
  up by sending the frames of later slots early. #1126 moved the source to a worker thread and made each slot send
  one frame.

Same rounds, before and after #1126 (jitter buffer ms at the caller):

| sender -> receiver | before | after |
|---|---|---|
| AI server -> `sm_call_smoke` | 173, 229, 181 | 156, 142, 140 |
| `sm_call_smoke` -> `sm_call_smoke` (path reference) | 128, 195, 117 | 107, 133, 126 |
| AI server -> Chrome | 194, 288, 315 | 209, 166, 194 |

At the caller, the response's transport part (p50) stays at 585-720 ms for `sm_call_smoke` and 730-1060 ms for
Chrome, and the barge-in stop at 430-900 ms. Against the test bench's 270-380 ms and 190 ms, the difference is the
path: each leg's jitter buffer holds 100-200 ms here (the test bench's conversation scenarios: 30-90 ms), and both
legs count, the user's audio to the AI and the reply back. The reply source's synthesis (30-90 ms per reply,
`reply_generation_ms` in the AI server's metrics) is in the response now, where a model's time belongs.
