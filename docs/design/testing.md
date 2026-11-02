# Testing, benchmarking and verification

Every algorithm has three kinds of checks, plus generated documentation
of their results.

| Check | Location | What it proves |
|---|---|---|
| Unit tests | `tests/unit/<section>/` | Exact behaviour of the component: wire formats, edge cases (wrap-around, duplicates, overflow), state machines, numerical accuracy |
| End-to-end test | `tests/e2e/<section>_e2e_test.cc` + `<section>_sims.h` | The algorithm doing its job inside a sender -> network -> receiver loop, compared with a baseline (protection off, fixed buffer, no pacing...) |
| Benchmarks | `bench/<section>_bench.cc` | Speed (ns per item or real-time factor) and quality (the e2e outcome as a pass/fail gate) |

## Test framework

`tests/framework/sm_test.h` is a small framework with no dependencies
(no exceptions, no RTTI), so the identical tests run on the desktop,
under sanitizers and in WebAssembly. `SM_TEST(Suite, Name)` registers a
test; `SM_EXPECT_*`/`SM_ASSERT_*` check values; `SM_RECORD(key, value)`
prints a measured value (`[metric] key=value`), which the documentation
generator collects. CTest gets one entry per suite, so every algorithm
reports pass or fail on its own line.

## End-to-end simulation

`include/sm/sim/network_link.h` is a deterministic network emulator:

- bottleneck capacity with a drop-tail queue (so bufferbloat is real);
- Bernoulli or Gilbert-Elliott (burst) loss;
- base delay with Gaussian jitter, with or without reordering;
- duplication;
- outages;
- all of it changeable mid-run (capacity steps, route changes).

`include/sm/sim/network.h` (#985) puts addresses on top of it for the
connectivity work (#944): IPv4 hosts in realms, each with an access link
in each direction (a `NetworkLink`, so every impairment above applies),
datagrams with source and destination addresses, and NATs that rewrite
them. The NAT is libwebrtc's test NAT (`p2p/test/nat_types.cc`,
`nat_server.cc`: open cone, address-restricted, port-restricted and
symmetric mapping and filtering), plus binding timeouts and hairpinning
(RFC 4787), both defaulting to what libwebrtc's test NAT does. NATs nest
(a home NAT behind a carrier-grade NAT). The test bench call does not use
it yet. Its tests are the `Nat` and `SimNetwork` suites.

`include/sm/sim/signals.h` generates license-free test signals: voiced
speech-like audio (gliding pitch, formants, syllables, pauses), noise,
tones and room impulse responses. Simulations step a virtual clock in
1 ms increments, so long scenarios run in milliseconds and give the same
results on every platform.

Typical e2e scenarios: 5% loss at three RTTs for NACK/FEC selection;
capacity steps 2000 -> 800 -> 2500 kbps for GCC; a 600 ms network stall
for time stretching; double talk and an audio route change for the echo
canceller; a call with a mid-call link outage for outage handling.
Each algorithm's document describes its scenario.

### Choosing pass thresholds

Thresholds come from the algorithm's design, not from whatever the first
run happened to produce. Where a first threshold was wrong, the
algorithm document records why. For example, keyframe recovery is bounded
by (repair window + RTT + keyframe transit) per lost frame, and GCC's
utilization after a capacity increase is bounded by its 8%/s
multiplicative ramp. When a test failed, the cause was diagnosed before
anything changed. Of the failures during development, about half were
real defects (listed in each document's verification notes and in
[portability](portability.md)) and half were test expectations that
contradicted the intended design.

## Benchmarks

`bench/framework/sm_bench.h` provides:

- `SM_BENCH(section, Name)`: the body loops `for (auto _ : st)`; the
  harness grows the iteration count until the loop runs for `--min-time`
  and reports ns per item. `st.BudgetNsPerItem(ns)` or
  `st.RequireRealtimeFactor(media_seconds, factor)` set the speed
  requirement.
- `SM_BENCH_QUALITY(section, Name)`: runs once (usually an e2e scenario)
  and gates outcomes with `st.Require(name, value, op, threshold)`.

Speed budgets are set for a desktop-class core and multiplied by
`--budget-scale` / `SM_BENCH_BUDGET_SCALE` on slower targets (the CI uses
4 for hosted runners and 20 under QEMU). Results go to stdout and to JSON
(`--json=path`). Each section's benchmarks are also a CTest entry
(`bench.<section>`), run serially.

## Running everything

```sh
tools/run_all.sh native    # build/, tests, benchmarks -> docs/results/native
tools/run_all.sh wasm      # needs emcc + node        -> docs/results/wasm
tools/run_all.sh all       # native, wasm, asan, noexc, then regenerate docs
```

`tools/run_all.sh` ends by running `tools/gen_docs.py`, which regenerates
[docs/algorithms](../algorithms/README.md) and
[docs/results](../results/README.md) from:

- header comments (design);
- public declarations (API skeleton);
- `ExportMetrics()` and `SM_EVENT()` calls (metrics and events);
- the recorded test output and benchmark JSON (results);
- `tools/catalog.py` (problem mapping, scenarios, verification notes);
- `tools/issues.py` (the issue each algorithm solves).

## Adding an algorithm

1. Header in `include/sm/<section>/` whose leading comment explains the
   design (it becomes the documentation), and the source in
   `src/<section>/` (add it to `src/CMakeLists.txt`).
2. A stats struct, `ExportMetrics()`, and `SM_EVENT()` for decisions.
3. Unit tests (new suite) and an e2e scenario (new `E2E_` suite), and
   register the suites in `tests/<section>.cmake`.
4. Speed and quality benchmarks in `bench/<section>_bench.cc`.
5. An entry in `tools/catalog.py` and its issue description in
   `tools/issues.py`, then run `tools/run_all.sh all`.
