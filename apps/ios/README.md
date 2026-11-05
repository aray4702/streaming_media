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

## Group calls (LiveKit, C19)

The controller also joins LiveKit rooms (`CallClientConfig::livekit`; see `apps/call/call_client.h` and
[docs/deploy/livekit.md](../../docs/deploy/livekit.md)). It speaks LiveKit's protobuf signaling over WebSocket, takes
its token from the signaling service's `/lk-token`, and publishes the microphone and camera. The app's screen has no
Group button yet; C20 adds it with the grid. Receiving a group waits for C18 (multi-stream receive): until then the
controller joins publish-only. `sm_call_smoke --livekit=...` exercises it from the command line.

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
