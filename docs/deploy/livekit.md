# LiveKit SFU for group calls (C19, #1160, #1164; C20, #1172)

Group calls of up to 6 people go through a [LiveKit](https://livekit.io) server (the Owner's decision in #1160). One-to-one calls
stay peer to peer through the signaling service ([signaling.md](signaling.md)). The LiveKit server runs on the same
Lightsail host as coturn ([turn-server.md](turn-server.md)) and the signaling service, behind the same nginx TLS front.

The window set the host up on 2026-11-04 (#1160). This page records how, without any secret. The clients and the
token service are in `apps/` (#1164).

## What runs where

| Piece | Where | Ports |
|---|---|---|
| LiveKit server v1.13.11 (`livekit.service`) | the Lightsail host; its HTTP/WebSocket on loopback | 7880 (loopback only, behind nginx) |
| LiveKit RTC (media) | the same host, public | TCP 7881 (ICE-TCP), UDP 7882 (one UDP port, muxed) |
| TURN for LiveKit's clients | coturn on the same host (LiveKit's embedded TURN is off) | TLS 443, UDP 3478 |
| nginx (TLS) | the host's public name, port 8443 | `/rtc` and `/twirp` go to LiveKit 7880; everything else (`/`, `/ws`, `/turn`, `/lk-token`) goes to the signaling service |
| Signaling service (`apps/signaling/server.py`) | the same host, 8080 behind nginx | serves the web app and issues LiveKit tokens at `GET /lk-token` |

The Lightsail firewall opens TCP 7881 and UDP 7882 next to coturn's ports. Port 7880 stays closed: clients reach
LiveKit's signaling through nginx (`wss://<host>:8443/rtc`), at the web app's own origin. A browser page therefore
needs no second origin or certificate, and the native clients use the same URL.

## LiveKit's configuration (`/etc/livekit.yaml`, mode 0600)

The window's configuration is on the host, not in the repository. This is its shape, with the secrets left out. The key pair is only in this file on the host and in the Owner's
`~/.config/streaming-media-livekit/`.

```yaml
port: 7880
bind_addresses: ["127.0.0.1"]   # nginx is the only way in for signaling
rtc:
  tcp_port: 7881
  udp_port: 7882
  use_external_ip: false
  node_ip: <the static IP>      # Lightsail's 1:1 NAT: advertise the public address
  turn_servers:                 # coturn on this host, not LiveKit's embedded TURN
    - host: <host name>
      port: 443
      protocol: tls
      username: <REST username>
      credential: <REST credential>
    - host: <host name>
      port: 3478
      protocol: udp
      username: <REST username>
      credential: <REST credential>
turn:
  enabled: false
room:
  max_participants: 6           # C19: groups of up to 6
keys:
  <API key>: <API secret>
```

The TURN credential is a coturn REST credential (`use-auth-secret`) valid for one year (#1160). LiveKit hands it to
every client in the JoinResponse's `ice_servers`. LiveKit can make short-lived ones itself instead: give each
`turn_servers` entry coturn's `secret` (or `secret_file`) and a `ttl` in place of `username`/`credential`. That is a
possible hardening step, and the Owner's call.

nginx routes these two paths in the 8443 server block, next to the signaling service's routes. Its configuration is on
the host, not in the repository; the routes have this shape:

```nginx
location /rtc   { proxy_pass http://127.0.0.1:7880; proxy_http_version 1.1;
                  proxy_set_header Upgrade $http_upgrade; proxy_set_header Connection "upgrade";
                  proxy_set_header Host $host; proxy_read_timeout 3600s; }
location /twirp { proxy_pass http://127.0.0.1:7880; proxy_set_header Host $host; }
```

Checks: `GET https://<host>:8443/rtc/validate` returns 401 without a token and 200 with one.

## Tokens: `GET /lk-token` (signaling service)

LiveKit admits a client that holds an access token: an HS256 JWT signed with the API secret. The apps never hold the
secret. They ask the signaling service for a token, the same way they ask `/turn` for TURN credentials:

```
GET /lk-token?room=<code>&identity=<id>[&subscribe=0]
-> 200 {"token": "<JWT>", "url": "<LIVEKIT_URL or empty>", "ttl": 3600}
```

- The token's claims are `iss` (the API key), `sub` (the identity), `nbf`/`exp` (one hour), and the `video` grant:
  `roomJoin`, `room`, `canPublish`, `canSubscribe` and `canPublishData`. The one-hour limit applies to joining:
  LiveKit refreshes a joined client's token itself (`refresh_token`).
- `room` and `identity` must be 1-64 characters of `A-Z a-z 0-9 _ - .`, not starting with `.`; anything else gets
  400. Without the key pair in the environment, the service returns 503.
- `subscribe=0` asks for a publish-only grant (`canSubscribe` false), for a client that only publishes
  (`sm_call_smoke --publish-only`). Every app subscribes by default (C20).
- Like the signaling service's rooms, anyone who can reach the service can get a token for any room code. Room codes
  are the access control, as they are for one-to-one calls. Sign-in is not in C19's scope.

The service's environment (in `/etc/streaming-media-signaling.env`, mode 600, next to `TURN_SECRET`):

| Variable | Meaning |
|---|---|
| `LIVEKIT_API_KEY` | the key in LiveKit's `keys:` |
| `LIVEKIT_API_SECRET` | its secret; it never leaves the server |
| `LIVEKIT_URL` | optional: the LiveKit URL to give clients (for example `wss://<host>:8443`). Empty: clients use the page's origin, where nginx routes `/rtc` |

Restart the service after changing them (`systemctl restart streaming-media-signaling`).

## Clients

- **Web app** (`apps/web/index.html`): the **Group** button, or `?room=<code>&group=1&auto=1`, joins with the official
  `livekit-client` SDK. The SDK is vendored at a pinned, checksum-verified version in `apps/web/vendor/livekit-client/`
  and served from the host, never from a CDN. The page publishes the camera (H.264 by default, `codec=` to change it)
  with simulcast (the SDK's three layers, q/h/f) and the microphone. The grid (C20) has up to 6 tiles: this page's
  camera first, then one per participant in the order they joined. Each tile shows the participant's video (or its
  initials when its camera is off), its name, a muted-microphone badge, an audio level bar, a green frame while it is
  an active speaker (LiveKit's ActiveSpeakersChanged) and its receive stats, refreshed every second. A log under the
  grid records joins and leaves. Mute, Video and Hang up control this page.
- **Native** (`apps/call`: the iOS app's controller, `sm_call_smoke` and the AI server): `CallClient` with `livekit`
  set (`call_client.h`) speaks LiveKit's signaling itself: protobuf over WebSocket (`livekit_proto.h`, from the
  vendored `apps/call/livekit/protocol/`). It runs two peer connections, as LiveKit's SDKs do:
  - the **publisher**: AddTrackRequest, our offer, the server's answer, trickle, pings and leave;
  - the **subscriber** (C20): LiveKit's first offer carries only its data channels; each participant who joins or
    publishes brings a re-offer with its tracks, and one who leaves a re-offer that turns them off. C18's
    renegotiation applies them on one connection, with a recvonly transceiver per track. Each track is mapped to its
    participant by its msid (`PA_<sid>|TR_<sid>`) and the ParticipantUpdates. Each audio track has its own Opus
    decoder and the playout mixes them; each video track has its own VideoToolbox decoder. Video subscriptions ask
    for 640x360 (UpdateTrackSettings), so the server forwards the simulcast layer that fits a tile.
  - Speakers: LiveKit's SpeakersChanged (from the RFC 6464 levels the publishers send) gives each participant's level
    and active state; before the first update a local meter on each participant's audio stands in. Mute and video off
    go out as MuteTrackRequest, so the other apps show them.
  - Built without data channels (the iOS presets), the client rejects the subscriber's data section; LiveKit accepts
    that, since its signaling carries everything the client uses.

  `sm_call_smoke --livekit=wss://<host>:8443 --token=https://<host>:8443/lk-token --room=<code> --video` joins from
  the command line and prints each participant's stats (`livekit_group`); `--publish-only` doesn't subscribe.
- **iOS app** (`apps/ios`): **Group** joins the room code's LiveKit room (`/lk-token`, then `/rtc` with binary
  WebSocket frames) and shows the grid: the local preview and one Metal tile per participant (the #1134 renderer, one
  view each), with the same badges, active-speaker frame and stats as the web app.
- **AI server** (`apps/ai_server`): `sm_ai_server --livekit=wss://<host>:8443 --token=https://<host>:8443/lk-token
  --room=<code>` joins the room as `ai-server` (`--identity=` to change it) and publishes its voice. It listens to the
  active speaker: each participant's audio is decoded and played out on its own, and the turn detector hears the one
  that LiveKit (or the local meter) names the active speaker. It keeps listening to that participant while the AI
  speaks or nobody does, so a barge-in and the end of a turn are heard from the same voice. Replies follow the same
  conversation as a one-to-one call (`AiConversation`). It prints its status (metrics, who it listens to, the
  client's stats) once a second.

## Tests

- `unit.SignalingService`: `/lk-token` (claims, signature, input checks, 503 without keys, publish-only grants) and the
  vendored files' checksums.
- `unit.LiveKitProto`, `unit.LiveKitClient` (`apps/call/livekit_test.cc`): the protobuf codec (SpeakersChanged with
  its float levels, MuteTrackRequest both ways, UpdateTrackSettings), the native publisher's signaling, and the group
  state (participants joining and leaving, the active speaker, mute announced and applied, the data-only subscriber
  offer answered), without a server.
- `interop.WebApp.group`: two headless Chrome pages in Group mode join one room on a local LiveKit server, publish
  with simulcast and receive each other's audio and video.
- `interop.WebApp.group_native`: `sm_call_smoke` publishes audio and H.264 video and a Chrome page in the room
  receives them; the native client receives the page's audio and video.
- `interop.WebApp.group_all` (C20): two pages, `sm_call_smoke` (talking in turns, as the iOS app's controller) and
  `sm_ai_server --livekit` in one room, and a third page that joins and leaves 10 s later. It checks that:
  - every page receives every other participant's audio and video, and the AI's audio;
  - the native client and the AI each receive every other participant;
  - everyone sees the late page join and leave;
  - every participant sees the active speaker move between the native client and the AI;
  - the AI listened to the native client and answered its turns.

  The pages' microphones play quiet noise (-60 dBFS, `dtx=0`), so only the native client and the AI speak.
- `apps/ios/simulator_group.sh <simulator UDID>` (manual, like the other simulator runs): a Chrome page, then
  `sm_call_smoke` built for the simulator, then the SmCall app (`-SMAutoGroup`), all in one room. Each must receive the
  page's audio and video, the app must draw the page's tile, and the page must receive the app's audio.

The `interop` tests need Chrome and `livekit-server` (`SM_LIVEKIT_SERVER`, else the `PATH`), and are skipped
without them. LiveKit publishes no macOS binary for v1.13.11, so on a Mac use Homebrew's bottle of the same version:
`brew install livekit`, or fetch the bottle and check it against the sha256 in Homebrew's formula. The bottle is
built from the release's source tag, whose tarball sha256 is in the formula too. The tests start the server
themselves, in its dev-mode shape (`devkey`/`secret`) on free ports.
