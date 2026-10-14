---
issue: 0
title: <what the user experienced, as in the issue title>
severity: sev:2
area: <area labels, comma separated>
regression_tests: testbench/scenarios/issue_0_<slug>.scn, tests/unit/<section>/<file>_test.cc
---
<!-- Copy to docs/fixes/<issue>-<slug>.md. Keep every section; CI checks they exist. -->

# #0: <title>

## Issue

What the user experienced and under which conditions, in two or three
sentences. Link the issue: https://github.com/aray4702/streaming_media/issues/0.
Who found it (Tester run / Owner) and on which commit.

## Root cause

Which module made the wrong decision, and why: the mechanism, not the
symptom. Point to the code (`src/...:line` on the commit before the fix) and
to the algorithm document in `docs/algorithms/` if the design itself was
wrong.

## Fix

What changed and why that addresses the cause. Note trade-offs and anything
else the change affects (other scenarios, platforms, API).

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_0_<slug>.scn` | `video.longest_freeze_ms` 1521 (expect < 1000) | 412 |
| `tests/unit/<section>/<file>_test.cc` `Suite.Case` | assertion that fails before | passes |

## Results

The reproduction scenario across seeds (before -> after):

| seed | metric | before | after |
|---|---|---|---|

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|

Anything that got worse and why that is acceptable.
