---
issue: 1168
title: "interop: Chrome's estimate toward our endpoint sometimes stalls (957 kbps) while Chrome<->Chrome ramps"
severity: sev:2
status: fixed
area: area:interop, area:cc
regression_tests: tests/unit/net/os_sockets_test.cc, tests/interop/browser/CMakeLists.txt
---

# #1168: Chrome's estimate toward our endpoint sometimes stalls while Chrome <-> Chrome ramps

## Issue

In `interop.Browser.chrome_answers`, Chrome's send-side estimate toward our
endpoint sometimes stopped below the 1500 kbps check and stayed flat for
the rest of the call: 957 kbps for 17 s on PR #1165's CI run 41938668992,
and 1381 kbps on PR #1170's run 41955115421 (2026-11-05, host macOS 15).
In both runs, the Chrome <-> Chrome self-test on the same runner ramped.
It failed CI about one run in three and passed locally. Filed by the
Builder: https://github.com/aray4702/streaming_media/issues/1168.

## Root cause

Our endpoint gave each datagram the wrong arrival time, and Chrome measures
its bandwidth probes from those times.

`OsSocketServer::Poll(now, wait)` (`apps/net/os_sockets.cc:360` on
0cdbc7dd) gave every datagram it read the `now` it was called with:
`OsUdpSocket::OnReadable(now)` set `p.arrival_time = now`. That `now` is
the loop's clock *before* poll(2) waits. The browser peer's loop waits up to
10 ms, and the apps' loops up to 5 ms. So a datagram that arrived 8 ms into
the wait was stamped 8 ms early. All the datagrams drained in one read
batch got the same time. The browser peer also read `now` before the
iteration's work (Process, media and FlushOutgoing), so every stamp was
early by that time as well.

The library reports these times to the sender. The pc endpoint passes
`packet.arrival_time` to the RTP receiver, which puts it in the
transport-wide feedback (#1125). Chrome's `ProbeBitrateEstimator`
(`compare/webrtc/reference/modules/congestion_controller/goog_cc/probe_bitrate_estimator.cc`)
computes each probe cluster's receive rate as bytes over the interval from
the first to the last arrival. The clusters are short: 5 packets, 15-20 ms
at 900 and 1800 kbps. A first packet stamped up to 10 ms early stretches
that interval, so the probe reads too low. A cluster drained in one batch
gets a receive interval near zero, and the ratio check (receive/send > 2)
discards the probe.

After a low probe, Chrome stays low:
- `ProbeController` probes further only when a result is above 0.7 x the
  cluster's target (1260 kbps for the 1800 kbps cluster,
  `probe_controller.cc:360`).
- Chrome's fake camera stream is application-limited: 296 video packets in
  20 s in the failing run.
- So the delay-based increase is capped at 1.5 x the acknowledged rate
  + 10 kbps (`aimd_rate_control.cc:238`). That cap holds the estimate where
  the probes left it: exactly flat at 957 kbps for 17 s.

Chrome's own receiver stamps a datagram when its network thread reads it,
so the Chrome <-> Chrome reference ramps on the same host. libwebrtc's
`AsyncUDPSocket::OnReadEvent` (`rtc_base/async_udp_socket.cc:159`) does
the same when the socket gives no kernel timestamp:
`arrival_time = clock.CurrentTime()` at the read.

The size of the error, measured in the browser peer on main (local Mac
with 16 busy loops, 5 calls; each datagram's stamp compared with the
steady clock right after its `recvfrom`):

| | p50 | p90 | p99 | max | stamps more than 2 ms early |
|---|---|---|---|---|---|
| stamp error | 3.3 ms | 6.5-8.7 ms | 7.3-10.2 ms | 10.5 ms | 65-71% |

## Fix

- `OsSocketServer::Poll` records the steady clock when it is called. Each
  event or datagram gets `now` plus the steady time since that call.
  Datagrams get it right after their `recvfrom`
  (`OsSocketServer::ReadTime`), and TCP events at dispatch. This is
  libwebrtc's clock-at-read stamp. It stays monotonic: never before `now`,
  never after the caller's clock at the read.
- The browser peer, the AI server's `main` and the iOS call bridge now read
  their clock just before `Poll` or `Wait`, not before the iteration's
  work. The comments on `CallClient::Wait`, `AiServer::Wait` and
  `OsSocketServer::Poll` state this contract.
- When the bandwidth check fails, `run_browser_call.py` prints Chrome's
  probe results (`ProbeBitrateEstimator` and `ProbeController` log lines,
  `--enable-logging=stderr --vmodule=...`). A future stall can then be told
  apart from a probe Chrome measured low. Chrome's stderr goes to a file in
  its profile, which replaces a pipe nobody read.

The library is unchanged (`src/`, `include/`), so the scenario suite and
the libwebrtc comparison are not affected.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/net/os_sockets_test.cc` `OsSockets.DatagramArrivingDuringTheWaitIsStampedWhenRead` | stamped 14 us after Poll's `now`, while the datagram was sent 34 795 us after it (`arrival >= sent` fails) | arrival between the send and Poll's return |
| `tests/unit/net/os_sockets_test.cc` `OsSockets.ArrivalSpacingFollowsTheSender` | both datagrams stamped before they were sent (13 us vs 16 404 us, 16 500 us vs 40 577 us) | each between its send and Poll's return |
| `interop.Browser.chrome_answers` / `chrome_offers` (`tests/interop/browser/CMakeLists.txt`) | Chrome's probes toward our endpoint measured from early or bunched stamps (below) | pass, probe statistics like Chrome <-> Chrome's |

## Results

Chrome's probe results, from its own `ProbeBitrateEstimator` log, in
`chrome_answers` calls on a local Mac (M-series, 10 cores). For every
"Probing successful" update, the table gives the ratio of the receive rate
to the send rate, the mean |receive interval - send interval|, and how
many probes Chrome discarded:

| calls | updates | recv/send p10 | p50 | p90 | mean \|recv - send interval\| | discarded probes |
|---|---|---|---|---|---|---|
| main, idle (12 calls) | 112 | 0.85 | 1.05 | 1.46 | 1.9 ms | 9 (receive/send ratio > 2) |
| main, 16 busy loops (5) | 54 | 0.66 | 0.97 | 1.45 | 3.7 ms | 1 |
| this fix, idle (12) | 131 | 0.90 | 1.00 | 1.35 | 0.3 ms | 3 (see below) |
| this fix, 16 busy loops (6) | 79 | 0.92 | 0.97 | 1.32 | 0.2 ms | 0 |
| Chrome <-> Chrome self-test, idle (12) | 351 | 0.90 | 1.05 | 1.27 | 0.3 ms | 0 |

With the fix, Chrome measures its probes toward our endpoint as accurately
as toward itself. On main, the error grows with load: at the 10th
percentile, 34% of the rate is lost. The 1800 kbps cluster has 30% margin
before Chrome stops probing (0.7 x target). That is why the failure needs
a slow CI runner and shows up there about one run in three. The 3
probes discarded with the fix were all early updates of the fourth cluster
(about 7 Mbps), after the estimate had passed 3 Mbps. Two of its packets,
read in one batch, fell in one 250 us feedback tick (receive interval
0 us). That doesn't touch the 1500 kbps check.

All 30 local calls with this fix reached 2 829-4 417 kbps by second 3. So
did the 26 local calls on main (2 373-4 118 kbps): on this Mac, the
probes stay above the threshold.

The full native suite passes (`SM_BENCH_BUDGET_SCALE=4 ctest --preset
native -j4`). The scenario suite is unaffected: no library code changed,
and `sm_testbench` doesn't use the OS sockets.
