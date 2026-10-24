---
issue: 372
title: Test bench reports no audio interruption during a 3 s outage that starts at 5 s (seeds 1-2)
severity: sev:2
status: fixed
area: area:audio, area:testbench
regression_tests: testbench/scenarios/issue_372_outage_interruption_count.scn, tests/unit/testbench/testbench_metrics_test.cc
---

# #372: Test bench reports no audio interruption during a 3 s outage that starts at 5 s

## Issue

A call whose audio is silent for 3 s because the link is down from 5 s to
8 s reported `audio.interruptions` 0, `audio.longest_interruption_ms` 40 and
`audio.concealed_pct` 8.05 on seed 1, so an `expect audio.interruptions == 0`
would pass on a dead call. Seed 2 reported 12 interruptions of 220 ms instead
of one of about 3 s. The library's own `concealed_samples` was right (3.0 s).
Found by the Tester on 55bf3d9:
https://github.com/aray4702/streaming_media/issues/372.

This was a test bench measurement bug (`area:testbench`), not an audio fault.

## Root cause

`testbench/call.cc` (~line 937 on 1e840b7) counted a 10 ms pull as concealed
only when the jitter buffer returned `AudioOperation::kExpand`, and ended the
concealment run on any other operation. NetEq expands by whole Expand
periods: `NetEqImpl::DoExpand` (`compare/webrtc/reference/modules/audio_coding/neteq/neteq_impl.cc`)
loops `Expand::Process` until the sync buffer holds the output plus the
overlap, and our v2 port does the same (`src/timing/audio_jitter_buffer.cc`,
the `NetEqOperation::kExpand` case). On #372's outage each period is 12.56 ms
at 16 kHz, so every fifth pull finds 10 ms of expanded audio already in the
sync buffer, and `NetEqImpl::GetDecision` turns its decision into `kNormal`
("Check if we already have enough samples in the sync_buffer_"). That pull
plays concealed samples, but the bench saw `kNormal` and cut the run, so it
never grew past 40 ms. The audio trace shows the pattern: `concealed_ms`
12.56, 12.56, 12.56, 12.56, 0 repeating from 5.17 s.

`sm_compare`'s libwebrtc side had the same blind spot in another form: it
counted a pull as concealed when the pull's `concealedSamples` delta was at
least half the pull, and the leftover pulls add no concealed samples there
either.

## Fix

Both stacks now measure concealment with one helper,
`testbench::ConcealmentMeter` (`testbench/call_common.h`), from the jitter
buffer's lifetime counters read after every pull: `total_samples_received`,
`concealed_samples` and `concealment_events` (libwebrtc's `GetStats`
`totalSamplesReceived`, `concealedSamples`, `concealmentEvents` on its side).

- A concealment event runs from one `concealment_events` increment to the
  next and lasts the concealed samples added in between. It is an
  interruption at 150 ms or more, as libwebrtc's
  `StatisticsCalculator::EndExpandEvent` counts `interruptionCount`
  (`kInterruptionLenMs = 150`, `>=`;
  `reference/modules/audio_coding/neteq/statistics_calculator.cc`). The old
  rule needed a run of more than 150 ms in 10 ms steps.
- `audio.longest_interruption_ms` is the longest concealment event.
- `audio.concealed_pct` is concealed samples over samples played, not pulls
  labelled `kExpand`; the 100 ms timeseries uses the same samples.

`audio.stretch_pct` is unchanged (ours by operation, libwebrtc's by stretched
samples); #372 is about concealment only.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_372_outage_interruption_count.scn` | `audio.interruptions` 0 (expect == 1), `audio.longest_interruption_ms` 40 (expect > 2900), `audio.concealed_pct` 8.049 (expect > 9.5) | 1, 2977, 10.07 |
| `tests/unit/testbench/testbench_metrics_test.cc` `TestbenchMetrics.ConcealmentEventSpansPullsThatAddNoConcealedSamples` | new helper (one 3000 ms event across pulls that add no samples) | passes |
| `TestbenchMetrics.ShortConcealmentEventsAreNotInterruptions` | new helper (140 ms is not an interruption, 150 ms is) | passes |
| `TestbenchMetrics.NoAudioPlayedIsAllConcealed` | new helper | passes |

## Results

The reproduction scenario across seeds (before on 1e840b7 -> after):

| seed | interruptions | interruption_ms | longest_interruption_ms | concealed_pct |
|---|---|---|---|---|
| 1 | 0 -> 1 | 0 -> 2977 | 40 -> 2977 | 8.049 -> 10.07 |
| 2 | 12 -> 1 | 2640 -> 2979 | 220 -> 2979 | 9.786 -> 10.23 |
| 3 | 1 -> 1 | 2980 -> 2978 | 2980 -> 2978 | 10.19 -> 10.15 |
| 4 | 1 -> 1 | 2980 -> 2980 | 2980 -> 2980 | 10.12 -> 10.11 |
| 5 | 1 -> 1 | 2980 -> 2976 | 2980 -> 2976 | 10.05 -> 10.02 |

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_23_low_bandwidth | audio.interruption_ms | 0 | 151 | REGRESSED |
| issue_331_low_bandwidth_av_offset_seed4 | audio.interruption_ms | 0 | 151 | REGRESSED |
| issue_372_outage_interruption_count | (new scenario) | - | - | new |
| issue_48_prober_low_bandwidth | audio.interruption_ms | 0 | 151 | REGRESSED |
| low_bandwidth | audio.interruption_ms | 0 | 151 | REGRESSED |

The four flagged cells are one concealment event of 151 ms on the
low-bandwidth family (`audio.interruptions` 0 -> 1, longest 150 -> 151).
The old rule saw it as a 150 ms run and needed more than 150 ms; libwebrtc's
`EndExpandEvent` counts 150 ms or more, so it is an interruption under
libwebrtc's definition. `main`'s 10-seed mean on `low_bandwidth` already reads
0.1 interruptions (one seed), against libwebrtc's 0.

Below the tolerance, every audio cell of the suite moved slightly: 78
`audio.longest_interruption_ms` cells now carry the event's concealed samples
instead of a multiple of 10 ms (for example `outage_3s` 2980 -> 2983,
`capacity_drop` 130 -> 127), and 87 `audio.concealed_pct` cells changed by
-0.10 to +0.02 points (mean -0.02). No scenario expectation fails.
