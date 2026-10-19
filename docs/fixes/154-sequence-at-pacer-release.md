---
issue: 154
title: Repair v2: residual video loss after a shallow-queue drop (4.7 % vs libwebrtc 0.1 %)
severity: sev:3
status: fixed
area: area:loss
regression_tests: testbench/scenarios/issue_154_shallow_drop_repair_v2.scn, tests/unit/rtp/send_side_controller_test.cc
---

# #154: Repair v2: residual video loss after a shallow-queue drop

## Issue

With every v2 switch on, `issue_21_shallow_drop_repair` (1.5 Mbps to 500 kbps and back, 20 ms queue) left 4.7 % of the
video packets never recovered where libwebrtc leaves 0.1 %, and `video.residual_loss_pct < 3` failed on 8 of 10 seeds.
Found by the #61 acceptance sweep (Owner, 2026-10-19). https://github.com/aray4702/streaming_media/issues/154

## Root cause

The holes were sequence gaps, as #127 describes. `RtpSender::NewPacket` assigned the RTP sequence number at packetization,
but under `pacer v2` the first packet of a keyframe flushes the video packets still queued
(`PacingController::EnqueuePacket`, `src/cc/pacing_controller.cc:300`). Each flushed packet left a number nobody ever
sent. The receiver NACKed it until it gave up: on seed 1, `pacer.flushed_packets` 78 and `nack/abandon` 78, and 78 of
2155 packets is the 3.6 % residual loss. libwebrtc assigns the number when the pacer releases the packet
(`modules/pacing/packet_router.cc:217` -> `ModuleRtpRtcpImpl2::AssignSequenceNumber`, `rtp_rtcp_impl2.cc:352`), so a
discarded packet leaves no gap.

## Fix

With `rtx v2` and no FEC, `NewPacket` leaves the sequence number unset and `SendSideController::Process` calls the new
`RtpSender::AssignSequenceNumber` for each audio or video packet just before it is serialized and stored in the v2
history. With FEC the number stays assigned at packetization, because FEC packets name the media sequence numbers they
protect when they are made. No v1 behaviour changes. RTX and padding sequence numbers are unchanged.

## Regression tests

| Test | Fails before the fix | Passes after |
|---|---|---|
| `testbench/scenarios/issue_154_shallow_drop_repair_v2.scn` | `video.residual_loss_pct < 1`: 3.80 on seed 1 | 0 |
| `tests/unit/rtp/send_side_controller_test.cc` `SendSideController.FlushedPacketsLeaveNoSequenceGapUnderRtxV2` | released video sequence numbers skip the flushed ones | contiguous |

## Results

`residual_loss_pct`, all v2 on (libwebrtc 0.1 on the issue's scenario):

| seed | before | after |
|---|---|---|
| 1 | 3.80 | 0 |
| 2 | 4.50 | 0 |
| 3 | 5.82 | 0.55 |
| 4 | 4.44 | 0.10 |
| 5 | 2.16 | 0 |
| 6 | 4.43 | 0 |
| 7 | 0 | 0 |
| 8 | 5.73 | 0 |
| 9 | 8.04 | 0.11 |
| 10 | 7.72 | 0.11 |

Seeds 11-20 after the fix: 0 to 0.25.

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_123_v2_allocation_min | video.residual_loss_pct | 20.54 | 1.66 | improved |
| issue_154_shallow_drop_repair_v2 | (new scenario) | - | - | new |

Nothing got worse. v1 scenarios are unchanged.
