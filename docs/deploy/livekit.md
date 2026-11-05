# LiveKit SFU for group calls (C19, #1160, #1164)

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
- `subscribe=0` asks for a publish-only grant (`canSubscribe` false). The native client uses it until C18 (below).
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
  with simulcast (the SDK's three layers, q/h/f) and the microphone, and shows each remote participant in a tile. C20
  builds the full grid.
- **Native** (`apps/call`: the iOS app's controller; the AI server can reuse it to join as a participant, C20): `CallClient` with `livekit` set (`call_client.h`) speaks
  LiveKit's signaling itself: protobuf over WebSocket (`livekit_proto.h`, from the vendored
  `apps/call/livekit/protocol/`). It runs two peer connections, as LiveKit's SDKs do. The **publisher** works today:
  AddTrackRequest, our offer, the server's answer, trickle, pings and leave. The **subscriber** waits for C18:
  - LiveKit's first subscriber offer carries only its data channels, and the endpoint needs an audio or video section.
  - Every later participant's tracks come in a renegotiation, which the endpoint refuses (one offer/answer only).
  - Several remote streams need the multi-stream receive of C18.

  Until then the native client joins publish-only (`subscribe=0`, `auto_subscribe=0`). LiveKit then makes the
  publisher connection the primary one and opens no subscriber connection.
  `sm_call_smoke --livekit=wss://<host>:8443 --token=https://<host>:8443/lk-token --room=<code> --video` publishes
  from the command line. `--subscribe` also tries the subscriber path and reports what it received.

## Tests

- `unit.SignalingService`: `/lk-token` (claims, signature, input checks, 503 without keys, publish-only grants) and the
  vendored files' checksums.
- `unit.LiveKitProto`, `unit.LiveKitClient` (`apps/call/livekit_test.cc`): the protobuf codec and the native
  publisher's signaling, without a server.
- `interop.WebApp.group`: two headless Chrome pages in Group mode join one room on a local LiveKit server, publish
  with simulcast and receive each other's audio and video.
- `interop.WebApp.group_native`: `sm_call_smoke` publishes audio and H.264 video, and a Chrome page in the room
  receives them.

The two `interop` tests need Chrome and `livekit-server` (`SM_LIVEKIT_SERVER`, else the `PATH`), and are skipped
without them. LiveKit publishes no macOS binary for v1.13.11, so on a Mac use Homebrew's bottle of the same version:
`brew install livekit`, or fetch the bottle and check it against the sha256 in Homebrew's formula. The bottle is
built from the release's source tag, whose tarball sha256 is in the formula too. The tests start the server
themselves, in its dev-mode shape (`devkey`/`secret`) on free ports.
