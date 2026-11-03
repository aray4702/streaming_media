# Signaling service and web app (C15, #1102)

`apps/signaling/server.py` is a small signaling service. It uses only the Python standard library
(asyncio, with a short RFC 6455 WebSocket server in the file), so nothing is needed next to coturn
except Python 3.10 or newer. It also serves the web app in `apps/web/`.

## Protocol (shared with the AI server and the iOS app)

WebSocket at `/ws`, JSON text messages:

| Message | Meaning |
|---|---|
| `{"type":"join","room":R,"peer":P}` | Join room R as P. Answer: `{"type":"joined","peers":[...]}`; the others get `{"type":"peer-joined","peer":P}`. A room holds up to 4 peers; a fifth gets `{"type":"error","reason":"room full"}`. A peer id already in the room gets an `error` too. |
| `{"type":"offer"\|"answer","to":P,"sdp":S}` | Relayed to P in the same room, with `"from"` set to the sender. |
| `{"type":"candidate","to":P,"candidate":C,"sdpMid":M,"sdpMLineIndex":N}` | Relayed the same way. |
| `{"type":"bye","to":P}` | Relayed. When a peer's socket closes, the others get `{"type":"peer-left","peer":P}`. |
| `{"type":"register-ai"}` | The AI server announces itself. When the first peer joins a room whose code starts with `ai-`, every registered AI server gets `{"type":"ai-call","room":R}` and joins R as a peer. |

The peer that joins later sends the offer (to each peer already there).

`GET /turn[?peer=ID]` returns `{"urls":[...],"username":"<expiry>:<id>","credential":"<base64 HMAC-SHA1>","ttl":86400}`:
the credential `CreateTurnRestCredentials` makes from coturn's `static-auth-secret`. The secret never leaves the
server. Without `TURN_SECRET` (or `TURN_URLS`), it returns the STUN URLs only.

`POST /report` and `GET /report` are a test hook: the web app posts its call state with `?report=1`.

## Running it

Settings from the environment:

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | 8080 | listening port (put a TLS proxy in front for `https://` and `wss://`; browsers need HTTPS for the camera off localhost) |
| `HOST` | 0.0.0.0 | listening address |
| `TURN_URLS` | none | comma-separated, for example `turn:turn.example.com:3478?transport=udp,turns:turn.example.com:443?transport=tcp` |
| `TURN_SECRET` | none | coturn's `static-auth-secret` (with `use-auth-secret`, see [turn-server.md](turn-server.md)) |
| `STUN_URLS` | Google's STUN | comma-separated |
| `WEB_DIR` | `apps/web` | the web app's files |

```sh
PORT=8080 TURN_URLS=turn:turn.example.com:3478 TURN_SECRET=... python3 apps/signaling/server.py
```

On the server that runs coturn, `apps/signaling/streaming-media-signaling.service` runs it under systemd. Copy `apps/`
to `/opt/streaming_media/apps`, put the variables in `/etc/streaming-media-signaling.env` (mode 600), copy the unit to
`/etc/systemd/system/` and enable it with `systemctl enable --now streaming-media-signaling`.

## Tests

- `unit.SignalingService` (`apps/signaling/test_signaling.py`): join, relay, peer-left, the room limit, the AI-room
  notice, and `/turn` against `CreateTurnRestCredentials`'s output (`sm_turn_credentials`).
- `interop.WebApp.two_tabs`: two headless Chrome processes load the web app and call each other through the service.
- `interop.WebApp.native`: our endpoint (`sm_interop_browser_peer`) answers the web app through the service.

The Chrome tests are skipped, not failed, when Chrome is missing (`SM_CHROME` names it; `SM_BROWSER_REQUIRED=1` fails instead).
