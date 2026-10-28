---
issue: 451
title: Units operators overflow (UB) on infinite and out-of-range operands
severity: sev:3
status: fixed
area: area:cc
regression_tests: testbench/scenarios/issue_451_host_rtt_near_limit.scn, tests/unit/core/core_test.cc
---

# #451: Units operators overflow (UB) on infinite and out-of-range operands

## Issue

After #445 (#441) saturated `Timestamp`/`TimeDelta` `+` and `-` on infinite
operands, the remaining unit operators still overflowed: `TimeDelta * double`
and `DataRate * double` on an infinity (double to int64 out of range),
`DataRate * TimeDelta` (signed overflow), `TimeDelta::MinusInfinity() / -1`
(`INT64_MIN / -1`) and the integer constructors (`TimeDelta::Seconds(INT64_MAX
/ 1e6 + 1)`). Found by the Tester's probe of #445
(https://github.com/aray4702/streaming_media/pull/445#issuecomment-6438582458),
split out by the Coordinator as
https://github.com/aray4702/streaming_media/issues/451. The issue judged them
unreachable from `Session`. Two paths are reachable through the public API,
both only with absurd inputs:
- an RTT from the host (`Session::SetRtt`) within 100 ms of the int64 limit
  overflows `rtt + 100 ms` in `AimdRateControl::GetNearMaxIncreaseRateBpsPerSecond`;
- a start rate of about 19 Tbps or more makes
  `CongestionWindowPushback::UpdateWindow` multiply the target by its 480 ms
  window, and that overflows.

## Root cause

`include/sm/core/units.h` on b998c6b did the arithmetic on the raw int64
values:

- `TimeDelta::operator*(double)` (line 54) and `DataRate::operator*(double)`
  (line 148): `static_cast<int64_t>(value * f)`, which is UB when the product
  is out of range, and an infinity times 2.0 always is.
- `DataRate::operator*(TimeDelta)` (line 157): `bps_ * t.us() / 8000000`.
  Called from `src/cc/congestion_window_pushback.cc:16` with the target rate
  and a 480 ms window, it overflows for a target above
  `INT64_MAX / 480000` bps.
- `TimeDelta::operator/(int64_t)` (line 55): `INT64_MIN / -1`.
- `TimeDelta/Timestamp::Millis/Seconds`, `DataRate::KilobitsPerSec`
  (lines 17-18, 75-76, 138): an unchecked `value * denominator`.
  `SecondsF`/`MillisF`: an unchecked double to int64 cast.
- `DataRate` and `DataSize` `+`, `-` and `+=` had no infinity handling at all.
- `src/cc/congestion_window_pushback.cc:17` averaged the old and new window
  on raw `bytes()`, `(w.bytes() + window_->bytes()) / 2`. Once the product
  above saturates, that sum overflows too. In a Release build without
  sanitizers it wrapped negative, so the window fell to its 3000-byte floor and
  pushback throttled video to about 640 kbps on a 2.5 Mbps link.

libwebrtc's `RelativeUnit` and `UnitBase` (`rtc_base/units/unit_base.h`, in
`reference/` since #456) check these cases with `RTC_DCHECK`s: an infinite
operand of `*` and `/`, a constructor value outside
`(MinusInfinityVal / d, PlusInfinityVal / d)`, and a result that reaches an
infinity. `FromValue(double)` maps +-inf to the infinities. `+` and `-`
saturate on an infinite operand.

## Fix

`include/sm/core/units.h` gets a `units_impl` namespace with libwebrtc's
operations on the raw values: `Add`, `Sub`, `Neg`, `MulInt`, `MulDouble`,
`Div`, `FromFraction`, `FromDouble`, `ToDouble`. Each one cites its
`unit_base.h` counterpart. Where libwebrtc `RTC_DCHECK`s, they saturate,
following #441's convention for `RoundTo`:

- an infinite operand keeps its infinity. A negative scalar flips its sign,
  and a zero scalar (or NaN) gives zero, as libwebrtc's release arithmetic does
  for zero;
- a finite result, or a constructor value, out of range becomes the infinity
  it ran into;
- `operator/(Unit)` divides `ToValue<double>` values, so an infinity reads as
  +-inf (`ToValue<double>`), as libwebrtc.

All four units use them. `DataRate` and `DataSize` gain `MinusInfinity()` and
`IsPlusInfinity()` / `IsMinusInfinity()` as `UnitBase` has them, and `DataSize`
gains `PlusInfinity()` and `IsFinite()`. `DataRate::IsFinite()` is now false
for `MinusInfinity()` too. The integer-scalar overloads (`int64_t`,
`int32_t`, `size_t`), the scalar-on-the-left overloads and
`TimeDelta * DataRate` are added, as `unit_base.h` and `data_rate.h` have them.
`DataRate` gets `-=`, and `DataSize` gets `*`, `/` and `/ DataSize`.

Finite results that are in range keep today's truncation. libwebrtc's
`operator*(double)` rounds with `std::llround`, `DataRate * TimeDelta` rounds
to the nearest byte, and `ms()` rounds to the nearest. Porting the `llround`
alone moved 133 suite metrics (58 worse, 75 better). The Coordinator scoped
this item as metric-neutral hardening, so the rounding is remaining work in
#461.

`src/cc/congestion_window_pushback.cc`: `UpdateWindow` averages with
`(w + *window_) / 2` on `DataSize`, as `GoogCcNetworkController`'s
`UpdateCongestionWindowSize` does (`goog_cc_network_control.cc:399-404`).
For finite windows the result is the same.

Test bench: `host_rtt <time> <rtt>`. From that time on, the host hands both
sessions a finite RTT at every step (`Session::SetRtt`), over what the call
measures. It is the finite counterpart of #441's `host_rtt_inf`, for RTTs the
emulated link cannot produce.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_451_host_rtt_near_limit.scn` (base built with `-DSM_SANITIZE=ON`) | aborts: `units.h:42:12: runtime error: signed integer overflow: 9223372036854700000 + 100000` (main's library with this PR's `host_rtt` directive; main's own test bench does not know the key) | passes on seeds 1-5 (Release x86-64, ASan+UBSan, and aarch64 under QEMU, which gives x86-64's numbers): 2-3 freezes (the 12 s capacity drop gives 2-4 without `host_rtt`), 25.6-26.1 fps, and 0 audio interruptions, except one on seed 3 since libopus (#460). The drop gives that one without `host_rtt` too (170 ms there, 200 ms with it) |
| `tests/unit/core/core_test.cc` `Units.RelativeUnitOperatorsSaturate` | the issue's four cases are UB (UBSan reports), infinities come out finite, and it does not compile (no integer or scalar-first overloads, no `DataSize` infinity) | passes, native and ASan+UBSan |

## Results

The issue's four cases, now unit-test assertions: `TimeDelta::PlusInfinity() *
2.0` and `DataRate::PlusInfinity() * 2.0` are +inf, `DataRate::PlusInfinity()
* TimeDelta::Seconds(1)` is +inf, `TimeDelta::MinusInfinity() / -1` is +inf,
and `TimeDelta::Seconds(INT64_MAX / 1e6 + 1)` is +inf. There are no UBSan
reports under ASan+UBSan.

On seeds 1, 2 and 3, every existing scenario's metrics with this PR's
`sm_testbench` equal main's (b998c6b) exactly. `tools/testbench_compare.py`
against `testbench/baseline.json` reports 0 regressions and 0 improvements.
The baseline is unchanged, apart from the new scenario's entry.

The first version of this PR used the start-rate path, `start_rate
9000000000000mbps` (9 Ebps). It passed on x86-64 but aborted on linux-arm64
and wasm. Every start rate from 1 Tbps up asks `RtpSender::GeneratePadding`
for rate × 5 ms of padding, gigabytes, and throws `std::bad_alloc` or runs for
minutes. That happens on main too, and is reported on #451 for placement. At
9 Ebps x86-64 only escaped it because an out-of-range `llround` returns
INT64_MIN there. The RTT path has no out-of-range conversion: aarch64 gives
x86-64's numbers. The start-rate path also exposed the `UpdateWindow` average fixed
above. Without that fix, a Release build wrapped the window and held video at
about 640 kbps.
