---
issue: 441
title: Signed overflow (UB) on Session::Process(+infinity) and Session::SetRtt(+infinity)
severity: sev:3
status: fixed
area: area:cc
regression_tests: testbench/scenarios/issue_441_infinite_rtt.scn, tests/unit/core/core_test.cc, tests/e2e/rtp_session_e2e_test.cc
---

# #441: Signed overflow (UB) on Session::Process(+infinity) and Session::SetRtt(+infinity)

## Issue

A host that hands `rtp::Session` an infinite time or RTT
(`Session::Process(Timestamp::PlusInfinity())`,
`Session::SetRtt(TimeDelta::PlusInfinity())`) hit signed integer overflow,
undefined behaviour that UBSan reports and the ASan build aborts on. Found by
the Tester (coverage area 6, API misuse) on 2ad596b:
https://github.com/aray4702/streaming_media/issues/441. Scenarios never reach
infinity, so no call metric was affected.

## Root cause

Two places on the commit before the fix:

- `src/cc/send_side_bwe.cc:160`, `GccController::OnRttUpdate`, rounded the
  RTT to whole milliseconds with `(rtt.us() + 500) / 1000`, which overflows
  for `PlusInfinity()` (`INT64_MAX + 500`).
- `include/sm/core/units.h:35-41,65-68`: `Timestamp` and `TimeDelta`
  `+`, `-`, `+=`, `-=` and unary `-` added the raw microsecond counts, so an
  infinite operand overflowed (`Session::Process` reaches
  `Timestamp::PlusInfinity() + TimeDelta` on its first call). libwebrtc's
  units saturate instead: `api/units/timestamp.h:78-114` returns
  `PlusInfinity()` / `MinusInfinity()` when an operand is infinite, and
  `RelativeUnit` (`rtc_base/units/unit_base.h`) does the same for
  `TimeDelta`.

## Fix

- `include/sm/core/units.h`: `Timestamp` and `TimeDelta` arithmetic
  saturates on infinite operands as libwebrtc's does (`IsPlusInfinity` /
  `IsMinusInfinity` added; `Timestamp -=` added as libwebrtc has it).
  `TimeDelta::RoundTo` ports `UnitBase::RoundTo`. Finite arithmetic is
  unchanged.
- `src/cc/send_side_bwe.cc`: `GccController::OnRttUpdate` rounds with
  `RoundTo(1 ms)`, as `RtpTransportControllerSend::OnRttUpdate`
  (`call/rtp_transport_controller_send.cc:571`), and drops an infinite RTT
  as it drops a zero one. libwebrtc's `RoundTo` `RTC_DCHECK`s a finite value,
  so an infinite RTT is a precondition violation there too.
- `src/rtp/session.cc`: `Session::Process` ignores a non-finite `now`. With
  saturating units, `RtcpEngine::Process`'s catch-up loop
  (`next_report_ += Interval()` while `next_report_ <= now`) would otherwise
  never end at `+infinity`. libwebrtc's `Clock` never returns infinity.
  Documented in `include/sm/rtp/session.h`.
- Test bench: `host_rtt_inf <time>` hands both sessions an infinite RTT at
  that time, so a call-level scenario covers the issue.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_441_infinite_rtt.scn` (base built with `-DSM_SANITIZE=ON`) | aborts: `send_side_bwe.cc:160:37: runtime error: signed integer overflow: 9223372036854775807 + 500` (main's library with this PR's test-bench directive) | passes, ASan+UBSan and Release: 0 freezes, 0 audio interruptions |
| `tests/unit/core/core_test.cc` `Units.InfinitySaturates` | `Timestamp::MinusInfinity() + 250 ms` is finite (no saturation) | passes |
| `tests/unit/core/core_test.cc` `Units.RoundTo` | does not compile (no `RoundTo`) | passes |
| `tests/e2e/rtp_session_e2e_test.cc` `E2E_RtpSession.InfiniteTimeAndRttDoNotOverflow` | the issue's two UBSan reports (ASan build aborts) | passes under ASan+UBSan |

## Results

The reproduction (`inf_repro.cc` from the issue, both calls) under
ASan+UBSan: before, two signed-overflow reports; after, none (the e2e test
above runs both calls).

The scenario suite against the baseline: every metric of every scenario is
byte-identical to `testbench/baseline.json` (`tools/testbench_compare.py`:
0 regressions, 0 improvements; `--update` leaves the file unchanged), as
expected: no scenario reaches an infinite operand. The new scenario adds its
own entry.
