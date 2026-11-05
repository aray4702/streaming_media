---
issue: 1171
title: "simulcast: top layer under 3% random loss freezes longer than libwebrtc (312 vs 181 ms)"
severity: sev:3
status: fixed
area: area:video, area:loss
regression_tests: testbench/scenarios/issue_1171_simulcast_random_loss.scn, tests/unit/rtp/packet_router_test.cc
---

# #1171: simulcast: top layer under 3% random loss freezes longer than libwebrtc

## Issue

With simulcast (three layers, C17 / PR #1170) on a 4 Mbps link with 3% random loss and 100 ms RTT, the receiver
viewing the top layer, the C17 work measured on 10 seeds a longest freeze of 312 ms against libwebrtc M157's 181 ms,
and 2.0 freezes against 1.3; single-stream on the same link was at parity. Filed by the Builder's window from C17
(#1162) on `main` a1266a4; https://github.com/aray4702/streaming_media/issues/1171. The lead was that libwebrtc sends
two top-layer keyframes in the first second where we send one.

## Root cause

Two things, measured with `sm_compare` built on macOS arm64 against the prebuilt libwebrtc M157:

1. **The 10-seed gap is mostly the seeds.** On seeds 1-10 the scenario reproduces the issue exactly (312 / 181 ms,
   2.0 / 1.3; `video.longest_freeze_ms` behind by 131 ms against a band of 124, on 4 of 10 seeds). On 200 seeds the
   two stacks are level: longest freeze 227 vs 234 ms, freezes 1.7 vs 1.9, freeze total 416 vs 476 ms; single-stream
   213 vs 217 ms. Of the twenty 10-seed windows of those 200 seeds, only seeds 1-10 put us behind on any freeze
   metric, and seeds 121-130 put us ahead; every 20- and 40-seed window is level. The freezes come from a packet
   whose retransmissions are lost too (seed 9: a top-layer packet NACKed five times, two RTX copies lost), which
   ten seeds do not average out. libwebrtc's seeds 6, 8 and 9 happen to have no freeze.
2. **The keyframe lead is the receiver, as ours.** libwebrtc's second top-layer keyframe in seed 9 follows a PLI on
   the middle layer: that layer's first packet (its keyframe's SPS/PPS) was lost, so `H264SpsPpsTracker` asked for
   a keyframe at 226.9 ms and `OnAssembledFrame` asked again for the delta frame that came first (284.8 ms), and
   `EncoderRtcpFeedback` keys every layer. Our receiver ports both rules (`src/rtp/rtp_receiver.cc:449`,
   `src/loss/keyframe_request.cc:15`); on seeds 1-100, where the same packet is lost in both stacks (seeds 16, 39,
   45, 52, 85, 93, 97) the two send the same PLIs on the same layers within 1 ms. The seeds that differ (ours 13; libwebrtc 9, 25,
   27, 55, 60) are those where the two packet streams have drifted apart and the loss draws hit different packets.
3. **A real difference found on the way: the stream that pads.** libwebrtc's `PacketRouter::SendPacket`
   (packet_router.cc:225-230) makes every module that sends a packet and supports RTX payload padding the
   `last_send_module_`, which `GeneratePadding` asks first (packet_router.cc:261-271), so payload padding resends the
   stream that sends most. Our `SendSideController` (`src/rtp/send_side_controller.cc:351` on a1266a4) moved
   `last_padding_sender_` only when a stream padded, so with simulcast the padding stayed on the layer that was
   added last or padded first, often the top layer's RTX before it had sent anything (empty padding where libwebrtc
   sends payload padding; seed 9 at 214 ms: our four 275-byte padding packets against libwebrtc's 1401-byte RTX
   copies). The case was not ported (`PadsOnLastActiveMediaStream`, tools/catalog.py 5.5) because a one-to-one call
   had one video stream until C17. On 200 seeds this put our padding at 2.2 kbps against 1.7 and the repair overhead
   at 5.2% against 4.9%.

## Fix

`SendSideController::SendPacket` sets `last_padding_sender_` to the packet's stream when it supports RTX payload
padding, after the `CanSendPacket` check, as `PacketRouter::SendPacket` does. With one video stream this is the
stream padding already used, so nothing else changes: on the whole suite only the five simulcast scenarios move.

The scenario `issue_1171_simulcast_random_loss` goes into the suite with `compare_seeds 40`, as the other loss
scenarios with seed-noisy metrics, so `compare-webrtc` measures it on 40 seeds (every 40-seed window of 200 is level
before and after the fix). It is not a regression scenario in the usual sense: there was no behaviour behind
libwebrtc to fail on. Its `expect` lines hold the user-experience bounds both stacks meet on all 200 seeds.

## Regression tests

| Test | Before the fix | After |
|---|---|---|
| `testbench/scenarios/issue_1171_simulcast_random_loss.scn` (sm_compare, 10 seeds, `parity_report.py`) | `video.longest_freeze_ms` 312 vs 181, 1 cell behind | 230 vs 181, 0 behind |
| the same at its `compare_seeds 40` | 250 vs 219, 0 behind | 225 vs 219, 0 behind |
| the same, `sm_testbench` seeds 1-10 | passes (longest freeze at most 469 ms) | passes |
| `tests/unit/rtp/packet_router_test.cc` `PacketRouter.PaddingFollowsTheLastStreamThatSentMedia` | fails: the padding goes on the last-added layer's RTX SSRC (0x120) | passes: the sending layer's RTX SSRC (0x20), payload padding |

## Results

`issue_1171_simulcast_random_loss`, ours / libwebrtc M157 (sm_compare, macOS arm64):

| seeds | metric | before | after |
|---|---|---|---|
| 1-10 | `video.longest_freeze_ms` | 312 / 181 | 230 / 181 |
| 1-10 | `video.freeze_count` | 2.0 / 1.3 | 1.5 / 1.3 |
| 1-10 | `video.freeze_total_ms` | 533 / 308 | 358 / 308 |
| 1-40 | `video.longest_freeze_ms` | 250 / 219 | 225 / 219 |
| 1-40 | `video.freeze_count` | 1.6 / 2.0 | 1.6 / 2.0 |
| 1-200 | `video.longest_freeze_ms` | 227 / 234 | 235 / 234 |
| 1-200 | `video.freeze_count` | 1.7 / 1.9 | 1.7 / 1.9 |
| 1-200 | `video.freeze_total_ms` | 416 / 476 | 434 / 476 |
| 1-200 | `net.padding_kbps` | 2.2 / 1.7 | 1.7 / 1.7 |
| 1-200 | `repair.overhead_pct` | 5.2 / 4.9 | 4.8 / 4.9 |

The improvement on seeds 1-10 is partly the padding and partly which packets the loss draws hit once the packet
stream changes; the 200-seed means are the measure, and they were level before and are level after.

`simulcast_clean_link`, `simulcast_capacity_drop`, `simulcast_capacity_drop_low_layer`, `simulcast_ramp_up` on 10
seeds: 0 gaps before and after. Single-stream (the same link without `simulcast`) is unchanged by construction.

The scenario suite against the baseline (`tools/testbench_compare.py --markdown`, seed 1, macOS arm64; every other
scenario is byte-identical):

| scenario | metric | baseline | result | status |
|---|---|---|---|---|
| issue_1171_simulcast_random_loss | (new scenario) | - | - | new |
| simulcast_capacity_drop | audio.late_discards | 41 | 38 | improved |
| simulcast_capacity_drop | repair.overhead_pct | 5.017 | 3.295 | improved |
| simulcast_capacity_drop_low_layer | audio.late_discards | 41 | 38 | improved |
| simulcast_ramp_up | audio.late_discards | 16 | 25 | REGRESSED |
| simulcast_ramp_up | audio.mouth_to_ear_p95_ms | 163.6 | 186.5 | REGRESSED |
| simulcast_ramp_up | cc.send_over_target_p95_pct | 6.272 | 7.858 | REGRESSED |
| simulcast_ramp_up | video.delay_jitter_buffer_p95_ms | 15.53 | 37.5 | REGRESSED |
| simulcast_ramp_up | video.e2e_delay_p95_ms | 66.22 | 88.95 | REGRESSED |
| simulcast_ramp_up | video.longest_freeze_ms | 32.78 | 11.05 | improved |

The `simulcast_ramp_up` regressions are seed 1's: the padding now resends the layer that last sent, so the packets
on the 1 Mbps link before the rise change and seed 1's delays with them. On its 10 seeds the means move toward
libwebrtc's (ours before -> after, libwebrtc): `video.e2e_delay_p95_ms` 67.7 -> 71.6 (71.7),
`audio.mouth_to_ear_p95_ms` 193.2 -> 191.5 (190.0), `video.longest_freeze_ms` 28.5 -> 27.7 (24.3),
`audio.jb_delay_p95_ms` 127 -> 124 (123). `simulcast_capacity_drop`: `repair.overhead_pct` 5.9 -> 5.6 (5.6),
`audio.mouth_to_ear_p95_ms` 613.9 -> 603.0 (602.9), `video.e2e_delay_p95_ms` 79.7 -> 82.3 (79.6, within its band).
None of the four has a gap on 10 seeds before or after.
