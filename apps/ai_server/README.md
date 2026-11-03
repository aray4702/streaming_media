# sm_ai_server (C12 part 1, #1103)

A native process that answers the signaling service's `ai-` rooms with the library, as the AI side of the two-way
call. See `ai_server.h` for the design.

```sh
out/native/apps/ai_server/sm_ai_server --signaling=ws://HOST:8080/ws --metrics=calls.jsonl
#   [--turn=http://HOST:8080/turn|none] [--ip=A.B.C.D]... [--relay] [--reply-seconds=3] [--first-chunk-ms=0] [--quiet]
# or against a TLS host: --signaling=wss://<host>:8443/ws (then /turn is https://<host>:8443/turn)
```

- **Signaling** (#1102's protocol, `apps/signaling/server.py`): it connects to `/ws`, sends `{"type":"register-ai"}`, and for each
  `{"type":"ai-call","room":R}` opens one more WebSocket, joins `R` as `ai-server-<n>` and offers or answers by the
  protocol's rule (the later joiner offers). It reads the sender of relayed messages from a `from` field when it
  needs it. `ws://` or `wss://` (TLS through sm::tls, checked against the system's roots; #1121).
- **STUN/TURN:** `GET /turn` on the signaling host (or `--turn`) at each call; `stun:`, `turn:` (UDP/TCP) and
  `turns:` URLs with the REST username and credential. `--relay` gathers TURN candidates only (iceTransportPolicy
  "relay").
- **Audio:** the caller's Opus through the library, the turn detector (#752) on the played-out audio, and the reply
  from an `AiReplySource` (scripted speech-like audio by default; a model plugs in there), paced by the test bench's
  AI source and sent as Opus (libopus). A barge-in cuts the reply.
- **Metrics:** one JSON line per call (`ai-call-metrics`): connection time, turns, replies, barge-ins,
  `conv.response_delay_ms` and `conv.barge_in_stop_ms` as the server sees them, and the user's speech concealed.
- **Test:** `e2e.AiServer` calls it with a native endpoint through an in-test stub of the signaling protocol and
  through `apps/signaling/server.py` (skipped without python3).
