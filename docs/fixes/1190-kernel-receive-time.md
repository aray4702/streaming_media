---
issue: 1190
title: "CI: interop.Browser.chrome_offers/chrome_answers miss 1500 kbps on the macOS runner since 2026-11-05 ~1 PM PT"
severity: sev:2
status: fixed
area: area:interop
regression_tests: tests/unit/net/os_sockets_test.cc, tests/interop/browser/run_browser_call.py
---

# #1190: Chrome's estimate toward our endpoint misses 1500 kbps on a loaded CI runner

## Issue

From about 1 PM PT on 2026-11-05, the macOS host job failed
`interop.Browser.chrome_offers` in main run 42113313699 (8ea3960a) and in
PR #1189's run 42116501031: "the estimate did not reach 1500 kbps (last
654 / 595 kbps); Chrome <-> Chrome did on this host". Chrome's estimate
went flat after its first probes and stayed there for 17 s. The commits in
between only add benchmarks, the runner image and Chrome were unchanged,
and the Chrome <-> Chrome self-tests in those runs took 22-30 s instead of
5-10 s: the runner was loaded. Filed by the Builder:
https://github.com/aray4702/streaming_media/issues/1190. It is the same
family as #1168.

## Root cause

Our endpoint's datagram arrival times depended on when its thread got the
CPU, and Chrome measures its bandwidth probes from those times.

Since #1168, `OsSocketServer::Poll` (`apps/net/os_sockets.cc:362` on
3aa63100) stamped each datagram with the caller's clock at the moment
`recvfrom` returned it (`ReadTime()`). That is right only if the thread
reads each datagram as it arrives. On a loaded host the browser peer's
single thread is often not running: it is preempted during its work, or
woken late from poll(2). Datagrams then wait in the socket and are read
together, a few microseconds apart, and a datagram that arrived during the
work got the clock of the *next* `Poll` call. Chrome's probe clusters are
5-6 packets in 10-20 ms, so one such stall per cluster is enough:

- a cluster read in one batch has a receive interval of 0 us, or a receive
  rate 10-20 times the send rate, and `ProbeBitrateEstimator` discards it
  ("invalid send/receive interval", "receive/send ratio too high");
- a cluster with one packet read late measures a low rate (receive interval
  37-75 ms for a 15 ms send interval).

After its initial probes (900 and 1800 kbps), `ProbeController` probes
further only above 0.7 x 1800 kbps, and Chrome's application-limited fake
camera caps the delay-based increase at 1.5 x the acknowledged rate, so the
estimate stays where the probes left it (see #1168).

libwebrtc avoids this: `AsyncUDPSocket::OnReadEvent`
(`compare/webrtc/reference/rtc_base/async_udp_socket.cc:158-163`) uses the
socket's receive timestamp when it has one (its `PhysicalSocket` reads
`SCM_TIMESTAMP`) and only falls back to the clock at the read. Chrome's own
receiver reads on a dedicated network-service I/O thread, so the Chrome <->
Chrome reference is far less exposed to the load.

The CI runner itself is also slow on Chrome's side: in run 42113313699
Chrome's own pacer sent its first 900 kbps probe at 654 kbps (send side,
which our endpoint does not affect), and Chrome takes the lower of the send
and receive rates. The self-test that the check compared against ran
minutes earlier, under a different load.

## Fix

- `OsSocketServer` turns on `SO_TIMESTAMP` for its UDP sockets and reads
  each datagram with `recvmsg`. A datagram's arrival time is the read time
  minus how long it waited in the socket (wall clock now minus the kernel's
  receive time), so it no longer depends on when the thread ran. Where the
  kernel gives no timestamp, the read time is used, as before.
- `Poll(now, wait, earliest)`: a datagram is never stamped before
  `earliest`, the caller's clock at its last call into the endpoint
  (default `now`, the old floor). The browser peer passes the `now` of its
  iteration's work, so a datagram that arrived during that work keeps its
  own time, and the endpoint (whose clock never goes back) receives it as
  is. The AI server and the call client keep the default.
- The times stay in the order the callbacks run, across sockets too (the
  ICE code takes them as its `now`): a datagram on one socket that arrived
  before one already handed out on another gets that later time.
- `run_browser_call.py`: when a call with our endpoint does not reach the
  target but the earlier Chrome <-> Chrome self-test did, it runs the
  self-test again right away and judges the call against that, so both are
  measured under the same load. The rule itself is unchanged: our call fails
  whenever Chrome <-> Chrome ramps on the host at that time, and otherwise
  must end at 80% of Chrome <-> Chrome's estimate. The driver prints
  whether it re-measured, so CI logs show which part was needed.

The library is unchanged (`src/`, `include/`), so the scenario suite and
the libwebrtc comparison are not affected.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/net/os_sockets_test.cc` `OsSockets.DatagramsReadInOneBatchKeepTheirSpacing` | three datagrams sent 6.3 ms apart, read while the callback was busy, stamped 25 us and 6 us apart | spacing within 2 ms of the sender's |
| `tests/unit/net/os_sockets_test.cc` `OsSockets.DatagramArrivingBeforePollKeepsItsTimeDownToEarliest` | a datagram that arrived 30 ms before `Poll` was stamped 36.5 ms after `base`, sent at 8.3 ms | stamped within 2 ms of its send; without `earliest`, at Poll's `now` |
| `interop.Browser.chrome_offers` / `chrome_answers` | see Results | see Results |

The "before" column was measured with the same tests and `SO_TIMESTAMP`
turned off (the read-time stamp of #1168).

## Results

Local Mac (M-series, 4 performance + 6 efficiency cores), the browser peer
built from main (3aa63100) and from this fix, headless Chrome. A call
fails when Chrome's estimate does not reach 1500 kbps in 20 s. "Starved"
reproduces the loaded runner: the peer runs at background QoS
(`taskpolicy -b`, efficiency cores only) next to 10 busy loops (`yes`), with
Chrome at normal priority; the Chrome <-> Chrome self-test runs under the
same load.

| calls | main | this fix |
|---|---|---|
| `chrome_offers`, starved | 5 of 10 fail | 0 of 20 |
| `chrome_answers`, starved | 3 of 10 fail | 0 of 10 |
| `chrome_offers`, kernel time but floor at Poll's `now` (no `earliest`), starved | - | 5 of 10 fail |
| `chrome_offers`, idle | 0 of 3 | 0 of 10 |
| `chrome_answers`, idle | not run | 0 of 10 |
| `chrome_offers`, 12 busy loops (peer at normal priority) | 0 of 10 | 0 of 10 |

Chrome's own probe log, `chrome_offers` starved (10 calls each), every
"Probing successful" update and every discarded probe:

| | updates | recv/send p10 | p50 | p90 | mean \|recv - send interval\| | discarded probes | of them, receive interval 0 us |
|---|---|---|---|---|---|---|---|
| main | 103 | 0.20 | 0.92 | 1.33 | 28.5 ms | 28 | 14 |
| this fix | 107 | 0.87 | 1.16 | 1.35 | 0.8 ms | 1 | 0 |

The failing calls on main end like the CI ones: a first probe measured low
(receive interval 37-75 ms for a 15 ms cluster), the 1800 kbps probe
discarded (receive interval 0 us), and the estimate flat at 300-900 kbps
for the rest of the call. In one of them Chrome <-> Chrome was re-measured
right after (the new check) and reached 3529 kbps, so the call still
failed: the re-measure does not excuse our endpoint. The third row shows
the kernel time alone is not enough under this load: about half of the
datagrams arrived while the peer was outside `Poll`, and the old floor gave
them all the same time.

Not reproduced locally: CI's run 42116501031 shows no result at all for
Chrome's first two clusters (no "Probing successful" or "unsuccessful"
line), so Chrome counted fewer than 4 of their packets. That can also come
from Chrome's side on a starved runner (a pacer that did not send them, as
its 654 kbps send rate in the other run suggests); the re-measured
reference covers that case. The driver prints when it re-measured, so the
next CI failures will tell the two apart.

The unit tests pass 5 of 5 runs (`sm_unit_os_sockets`). The full native
suite passes locally (`SM_BENCH_BUDGET_SCALE=4 ctest -j6`, 619 tests,
benchmarks not built), and so do the 21 tests over the OS sockets (OS
sockets, resolver, browser, web app, real calls, AI server, call client)
after the last change. The library is unchanged, so the scenario suite and
`compare-webrtc` are unaffected.
