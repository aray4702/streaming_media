---
issue: 1095
title: testbench: ICE calls put the negotiated header extension IDs on the wire (found in #1094)
severity: sev:3
status: fixed
area: area:testbench
regression_tests: tests/unit/testbench/ice_overhead_test.cc
---

# #1095: testbench: ICE calls put the negotiated header extension IDs on the wire

## Issue

In `transport ice` calls the bench's sessions sent transport-cc with extension ID 1 while the SDP the endpoints
exchange negotiates ID 3. Both ends were ours, so calls worked, but the wire did not match the SDP.
https://github.com/aray4702/streaming_media/issues/1095, found in #1094 by the Builder.

## Root cause

`MakeSessionConfig` (`testbench/call.cc`) registers transport-cc at a fixed ID 1 for every session, whether or not
the call runs over `transport ice`; the ICE endpoints negotiate their own map (`PeerEndpoint`, transport-cc = 3).

## Fix

After `IceLink::Connect`, `IceLink::NegotiatedExtensions()` reads the extmap of the answer, and the call gives its
sessions transport-cc at that ID (`rtp::Session::SetExtensions`, a setter for the map; nothing was sent yet). Only
transport-cc is mapped, as before: the other negotiated extensions (audio level, abs-send-time, MID) stay off, so
packet sizes and every scenario metric are unchanged. Calls without `transport ice` keep ID 1. The comparison side
(`compare/webrtc`) is not touched.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `tests/unit/testbench/ice_overhead_test.cc` `TestbenchIceExtensions.WireIdsEqualNegotiated` | every media packet carries transport-cc as ID 1, not 3 | all 100 checked packets carry ID 3 |

## Results

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| all | all | | | unchanged |
