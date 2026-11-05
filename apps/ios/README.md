# SmCall: the iOS calling app (C14, #1116)

A small SwiftUI app on the library. One screen: server URL (default `https://35.165.158.249.sslip.io:8443`), room
code, **Call AI** (a fresh `ai-` room, answered by the AI server) / **Join room** (the web app's room code), video and
mute switches, hang-up, the call's state, the remote video and the live stats (path, RTT, congestion-control target,
rates, audio loss/jitter/concealment, video frames, DTLS and SRTP ciphers, and the rendered and decoded frame rates).

- `SmCallApp.swift`, `ContentView.swift`: the screens; `CallModel` polls the bridge's state and stats 10 times a
  second.
- `RemoteVideoView.h/.mm`: the remote video on the GPU (#1133), as libwebrtc's `RTCMTLVideoView` with its I420
  renderer: an `MTKView` whose display-driven loop takes each new decoded frame, uploads its Y, U and V planes as
  textures and converts to RGB in a Metal shader, drawing at the received frame rate. The call-result JSON line adds
  `app_frames_drawn`, `app_render_fps` and `app_decoded_fps`.
- `CallBridge.h/.mm`: Objective-C++ bridge. GET `/turn` (TURN REST credentials, so the app holds no secret), the
  signaling WebSocket `/ws` through `URLSessionWebSocketTask` (wss:// for an https:// server), the call thread, and the
  devices: `sm::device` Audio Units (microphone, speaker) and the AVFoundation camera.
- `apps/call/call_client.h`: the C++ call controller. It sets up the call with the AI server's wiring
  (`pc::PeerEndpoint` over `apps/net` OS sockets, ICE with STUN/TURN, DTLS-SRTP, the RTP session's congestion control),
  Opus audio through the library's capture chain (HPF, AEC3, NS), and VideoToolbox H.264.

## Group calls (LiveKit, C19, C20)

**Group** joins the room code's LiveKit room ([docs/deploy/livekit.md](../../docs/deploy/livekit.md)). The bridge
GETs `/lk-token` (no secret in the app), opens LiveKit's `/rtc` WebSocket with binary frames, and the controller
(`CallClientConfig::livekit`, `apps/call/call_client.h`) publishes the microphone and camera and receives every
participant. The screen shows a grid of up to 6:
- the local preview first, then one tile per participant in the order they joined. Each tile has its own Metal view
  (`videoViewForParticipant`, the same renderer as the 1:1 view);
- the participant's initials when its camera is off;
- its name, a muted-microphone badge and an audio level bar;
- a green frame while it is an active speaker (LiveKit's speaker updates);
- its receive stats.

Join and leave update the grid, and Mute and Video are announced to the room. The call-result line at hang-up has
each participant's stats (`livekit_group`) and each tile's frames drawn (`app_tiles_drawn`). It also goes to
`Documents/call-results.jsonl`, because the log cuts long lines.

`apps/ios/simulator_group.sh <simulator UDID>` is the group's simulator run. It starts a local LiveKit server
(`SM_LIVEKIT_SERVER`), the signaling service and a Chrome page in Group mode. Then `sm_call_smoke`, built for the
simulator, and the app (`-SMAutoGroup <room>`) join the page's room. Each must receive the page's audio and video,
the app must draw the page's tile, and the page must receive the app's audio.

## Build (unsigned)

```sh
cmake --preset ios-simulator && cmake --build out/ios-simulator --config Release --target SmCall -- CODE_SIGNING_ALLOWED=NO
cmake --preset ios           && cmake --build out/ios           --config Release --target SmCall -- CODE_SIGNING_ALLOWED=NO
```

The app is `out/ios[-simulator]/apps/ios/Release-iphone{simulator,os}/SmCall.app`.

## Simulator smoke run

`apps/ios/simulator_smoke.sh <simulator UDID>` starts `apps/signaling/server.py` and `sm_ai_server` on this Mac. Then
`sm_call_smoke` (the controller as a plain simulator executable, through `xcrun simctl spawn`) and the app (launched
with `-SMServer http://127.0.0.1:8093 -SMAutoCall ai-sim-app -SMAutoHangup 10 -SMVideo NO`) each call an `ai-` room.
The script checks the AI server's metrics line for each call.

`apps/ios/simulator_video.sh <simulator UDID>` checks the remote video's frame rate (#1133): the web app in headless
Chrome (a fake 30 fps camera) and the app join one room on a local signaling service, and the script passes when the
app drew at least 90% of the frames it decoded (its call-result line, from the simulator's log), at 20 fps or more.

## On an iPhone

Find your team ID: Xcode creates an "Apple Development" certificate when you add your Apple ID (Xcode > Settings >
Accounts); its OU is the team ID:

```sh
security find-certificate -c "Apple Development" -p | openssl x509 -noout -subject   # ... OU=<team ID> ...
```

Then configure with it, open the project and run on the connected iPhone:

```sh
cmake --preset ios -DSM_IOS_DEVELOPMENT_TEAM=<team ID>
open out/ios/streaming_media.xcodeproj    # scheme SmCall, destination: your iPhone, then Run
```

Xcode signs automatically with that team (an Apple ID's free Personal Team is enough; the app then expires after 7
days). Without `SM_IOS_DEVELOPMENT_TEAM` the app is built unsigned, as in CI. If the bundle id
`io.github.aray4702.smcall` is taken, change it under Signing & Capabilities. On the iPhone, turn on Developer Mode
(Settings > Privacy & Security) and, at the first launch, trust the developer under Settings > General > VPN & Device
Management.
