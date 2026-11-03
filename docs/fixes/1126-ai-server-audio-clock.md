---
issue: 1126
title: "apps/ai_server: its audio builds a 200-550 ms jitter buffer at the caller (Chrome's: 42 ms); response delay and barge-in stop 3-6x the test bench"
severity: sev:2
status: fixed
area: apps, area:audio
regression_tests: apps/ai_server/ai_server_test.cc
---

# #1126: the AI server's audio builds a large jitter buffer at the caller

## Issue

In the real-network calls of #1121 (PR #1123), the caller's jitter buffer held 200-550 ms of the AI server's audio,
and the response delay (1.2-1.8 s) and barge-in stop (540-1160 ms) were 3-6x the test bench's conversation scenarios.
Chrome receiving from `sm_call_smoke` held 42 ms. Found by the Builder's window,
https://github.com/aray4702/streaming_media/issues/1126.

## Root cause

Two things, measured on 2026-11-03 (details in `docs/results/real-network-calls.md`, "The jitter buffer on the same path").

1. **The 42 ms was a different path.** It came from case c *direct*, which ICE ran host to host on the LAN (RTT
   1 ms). On the same relayed path (Mac -> TURN in Oregon -> Mac), every sender builds a similar buffer. Chrome to
   Chrome held 132-448 ms, `sm_call_smoke` to `sm_call_smoke` 105-200 ms, and in each mixed call both directions
   matched within 20 ms. The test bench's conversation scenarios have no comparable jitter, so most of the gap with
   them is the path.
2. **The AI server's send loop added its own share** (loopback: 51-77 ms from the AI server against 32-42 ms from
   `sm_call_smoke`; relayed: +35-65 ms over `sm_call_smoke` in the same rounds). `AiCall::Media`
   (`apps/ai_server/ai_server.cc` before the fix) called the reply source synchronously on the loop thread at the
   end of the user's turn. `ScriptedReplySource` takes 25-90 ms to synthesize a 3-5 s reply, and a model would take
   longer. The 20 ms send slots stalled behind it. The slots then caught up with `source_.Poll(now)`, which returned
   every reply frame due by the late `now` in one slot and then sent silence in the slots after it. The reply went
   out in a burst with a hole in it, and the RTP timestamps ran ahead of the send clock by the stall, for the rest
   of the call (the debug trace showed -20 ms per reply). The caller's NetEq saw a late burst at every reply start,
   and its buffer held the timestamps' lead.

## Fix

libwebrtc keeps its audio clock free of other work: `ChannelSend::ProcessAndEncodeAudio`
(`compare/webrtc/reference/audio/channel_send.cc`) posts encoding to a task queue so that the capture thread is
never held up, and the RTP timestamp advances by the samples encoded. The AI server now does the same:

- `AiReplySource::OnUserTurn` runs on a worker thread (`std::async`). The loop keeps sending its 20 ms slots and
  starts the reply at the first slot after it is ready. The source's time is now in the response delay, where a
  model's time belongs, instead of in the jitter buffer for the rest of the call. The server never calls the source
  from two threads: a barge-in during generation calls `OnBargeIn` after `OnUserTurn` returns, and a turn that ends
  meanwhile replaces the reply in progress.
- Each slot polls the reply by the slot's time (`Poll(next_send_)`) and sends exactly one frame, so the RTP
  timestamps stay on the send clock. A late loop catches up slot by slot.
- New per-call metrics: `send_late_max_ms`, `rtp_ahead_max_ms` and `reply_generation_ms`.

No library change. App threading: the reply source is the only code off the loop thread.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `apps/ai_server/ai_server_test.cc` `AiServer.SlowReplySourceKeepsTheAudioClock` (a 150 ms reply source) | `send_late_max_ms` 164-167 (expect < 60), `rtp_ahead_max_ms` 160 (expect 0), 3 of 3 runs (old send path with the metrics added) | `send_late_max_ms` 2.3-3.1, `rtp_ahead_max_ms` 0, 3 of 3 runs |

The other two `AiServer` cases now report `rtp_ahead_max_ms` 0 and `send_late_max_ms` 10-12 ms.

## Results

Loopback (`sm_call_smoke --barge --no-apm` to a local `sm_ai_server`, 25 s calls):

| | before | after | `sm_call_smoke` to `sm_call_smoke` |
|---|---|---|---|
| caller's jitter buffer ms | 51, 64, 77 | 42, 47, 56 | 32-42 |

Relayed through the Oregon TURN server, three rounds before and three after. Each round makes back-to-back calls on
the same path:

| caller's view | before | after |
|---|---|---|
| `sm_call_smoke` from the AI server: jitter buffer ms | 173, 229, 181 | 156, 142, 140 |
| `sm_call_smoke` from `sm_call_smoke` (path reference): jitter buffer ms | 128, 195, 117 | 107, 133, 126 |
| Chrome from the AI server: jitter buffer ms | 194, 288, 315 | 209, 166, 194 |
| `sm_call_smoke`: response (transport) ms, p50 | 600, 720, 585 | 625, 690, 625 |
| `sm_call_smoke`: barge-in stop ms | 550-800 | 560-680 |
| Chrome: response (transport) ms, p50 | 738, 1061, 981 | 776, 733, 802 |
| Chrome: barge-in stop ms | 502-1083 | 433-902 |

The test bench is not affected: the change is in `apps/ai_server` only, so `testbench_compare.py` reports no
change.

On loopback, the response now includes the scripted source's synthesis time (30-70 ms on a loaded Mac), which the
burst used to hide behind a larger buffer. On the real path, the smaller buffers offset it.
