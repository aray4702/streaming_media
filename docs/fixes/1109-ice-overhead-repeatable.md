---
issue: 1109
title: "C8: the observability-off build changes ice_two_way_clean's transport bytes (overhead_pct 8.824 vs 8.825)"
severity: sev:1
status: fixed
area: area:testbench
regression_tests: tests/unit/testbench/ice_overhead_test.cc
---

# #1109: C8: the observability-off build changes ice_two_way_clean's transport bytes

## Issue

The observability-off check (C8, #988) failed on `main` (f197bf50, testbench job 125679148938):
`ice_two_way_clean` had `transport.overhead_pct` 8.824 in the default build and 8.825 in the
observability-off build. https://github.com/aray4702/streaming_media/issues/1109, filed by the Builder.

## Root cause

Not the observability switches: the metric changes between two runs of the **same** build. Eight runs of
`out/native/testbench/sm_testbench testbench/scenarios/ice_two_way_clean.scn` (macOS, BoringSSL) gave 8.824
seven times and 8.825 once; the media link traces of every run were identical.

`transport.overhead_pct` (`testbench/call.cc`, `AddIceMetrics`) divided `IceLink::wire_bytes()` by the call's
media bytes, and `wire_bytes()` counts everything the endpoints send from the offer on
(`testbench/ice_transport.cc`, the send observer), so it included the setup's DTLS handshake. That handshake's
size is random by design: the self-signed certificates (`src/tls/boringssl_identity.cc`, a random 64-bit serial
encoded as a DER INTEGER, and an ECDSA P-256 signature), the ServerKeyExchange and the CertificateVerify carry
DER values whose length depends on the random serial and signature values (an ECDSA signature is 70 to 72
bytes). BoringSSL's generator can't be made repeatable from outside the library, so a few bytes moved between
runs, and the rounded metric changed in about one run in eight.

## Fix

`IceLink` records the bytes sent during `Connect()` (`setup_wire_bytes()`), and `transport.overhead_pct` counts
the media phase only: bytes the endpoints sent during the call (SRTP, STUN consent and keepalives, TURN
refreshes, any DTLS) over the call's RTP/RTCP bytes. Numerator and denominator now cover the same interval; the
setup stays measured by the `setup.*` metrics. No library code changes; the check is unchanged and every ICE
scenario stays in it.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/testbench/ice_overhead_test.cc` `TestbenchIceOverhead.RepeatableAcrossRuns` (six runs of a 2 s two-way ICE call must give the same unrounded `transport.overhead_pct`) | 4 of 5 later runs differ from the first | all equal |

## Results

`ice_two_way_clean`, 8 runs of one build: before 8.824 (7) / 8.825 (1); after the media-phase value is
repeatable. `tools/check_observability_off.py` on the `ice_*` scenarios: 0 differences.

The scenario suite against the baseline: 0 regressions; only `transport.overhead_pct` of the ICE scenarios
moved, because the setup is no longer counted (baseline updated):

| scenario | metric | baseline | result |
|---|---|---|---|
| ice_clean_link | transport.overhead_pct | 1.134 | 1.097 |
| ice_mesh_3_home_broadband | transport.overhead_pct | 1.638 | 1.592 |
| ice_network_change | transport.overhead_pct | 1.311 | 1.269 |
| ice_random_loss_5pct | transport.overhead_pct | 1.395 | 1.343 |
| ice_symmetric_nat_turn | transport.overhead_pct | 1.557 | 1.499 |
| ice_two_way_clean | transport.overhead_pct | 8.824 | 8.302 |
